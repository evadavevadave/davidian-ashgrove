// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// The plugin engine shared by every format (LV2, CLAP, and VST3 and AU through CLAP).
// It runs one amp family with its cabinet. Controls arrive as an array indexed by
// LV2 port number, so every format uses the same numbering. Selector changes
// (model, valves, input, oversampling, cabinet) switch behind a short fade.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "amp.h"
#include "ports.h"

#if defined(_M_X64) || defined(__x86_64__) || defined(__SSE__)
#include <xmmintrin.h>
#define AG_FTZ_X86 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define AG_FTZ_ARM64 1
#endif

namespace ashgrove {

// Flushes denormals to zero while the amp runs, so decaying filter states don't
// slow the CPU down. Restores the host's setting afterwards.
class DenormalGuard {
public:
    DenormalGuard() {
#if defined(AG_FTZ_X86)
        saved_ = _mm_getcsr();
        _mm_setcsr(unsigned(saved_) | 0x8040u);  // FTZ and DAZ.
#elif defined(AG_FTZ_ARM64)
        unsigned long long r;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(r));
        saved_ = r;
        r |= 1ull << 24;  // FZ.
        __asm__ __volatile__("msr fpcr, %0" : : "r"(r));
#endif
    }
    ~DenormalGuard() {
#if defined(AG_FTZ_X86)
        _mm_setcsr(unsigned(saved_));
#elif defined(AG_FTZ_ARM64)
        unsigned long long r = saved_;
        __asm__ __volatile__("msr fpcr, %0" : : "r"(r));
#endif
    }

private:
    unsigned long long saved_ = 0;
};

// Fixed port numbers (see ports.h for the knobs and the cabinet).
enum FixedPort { PORT_IN = 0, PORT_OUT, PORT_LATENCY, PORT_MODEL, PORT_TUBE, PORT_INPUT, PORT_QUALITY, PORT_TRIM, PORT_LEVEL };

// What a control port is, for formats that describe parameters themselves (CLAP).
struct PortMeta {
    enum Kind { ENUM, TOGGLE, KNOB, DB } kind;
    const char* symbol;
    const char* name;
    double min, max, def;
    const char* const* labels;  // ENUM only: max - min + 1 labels.
};

inline int countOf(const char* const* list, int cap) {
    int n = 0;
    while (n < cap && list[n]) ++n;
    return n;
}

// Number of ports including audio and latency; control ports are 3 .. count-1.
inline int portCount(const FamilyDef& d) { return cabinetPort(d) + 1; }

inline PortMeta portMeta(const FamilyDef& d, int port) {
    static const char* const quality[] = {"2x", "4x"};
    // Valve names in this family's sheet order.
    static const char* tubes[F_COUNT][3];
    const int fi = int(&d - families());
    for (int k = 0; k < d.nTubes; ++k) tubes[fi][k] = pentodeParams(d.tubes[k]).name;
    switch (port) {
        case PORT_MODEL: return {PortMeta::ENUM, "model", "Model", 0, double(countOf(d.models, 5) - 1), 0, d.models};
        case PORT_TUBE: return {PortMeta::ENUM, "tube", "Output Tube", 0, double(d.nTubes - 1), 0, tubes[fi]};
        case PORT_INPUT: return {PortMeta::ENUM, "input", "Input", 0, double(countOf(d.inputs, 5) - 1), 0, d.inputs};
        case PORT_QUALITY: return {PortMeta::ENUM, "oversampling", "Oversampling", 0, 1, 0, quality};
        case PORT_TRIM: return {PortMeta::DB, "trim", "Input Trim", -36, 36, 0, nullptr};
        case PORT_LEVEL: return {PortMeta::DB, "level", "Output Level", -60, 12, -6, nullptr};
        default: break;
    }
    if (port == cabinetPort(d))
        return {PortMeta::ENUM, "cabinet", "Cabinet", 0, double(CAB_COUNT - 1), double(d.cabDefault), cabinetNames()};
    const PortDef& p = d.ports[size_t(port - PORT_FIRST_KNOB)];
    if (p.kind == PK_TOGGLE) {
        static const char* const offOn[] = {"Off", "On"};
        return {PortMeta::TOGGLE, p.symbol, p.name, 0, 1, p.def, offOn};
    }
    return {PortMeta::KNOB, p.symbol, p.name, 0, 10, p.def, nullptr};
}

class Engine {
public:
    bool init(const FamilyDef* def, double fs) {
        def_ = def;
        fs_ = fs;
        amp_ = std::make_unique<Amp>();
        amp_->init(fs, 2);
        cab_.init(fs);
        curQuality_ = 0;
        bufIn_.resize(kBlock);
        bufOut_.resize(kBlock);
        reset();
        return true;
    }

    // Starts afresh: the next run rebuilds the circuit and fades in.
    void reset() {
        curModel_ = -1;
        curCab_ = -1;
        gain_ = 0.0;
    }

    const FamilyDef& family() const { return *def_; }
    int latency() const { return amp_->latency(); }

    // Fills ctl (portCount() long) with every port's default.
    static void defaults(const FamilyDef& d, float* ctl) {
        for (int p = 0; p < portCount(d); ++p) ctl[p] = p < PORT_MODEL ? 0.0f : float(portMeta(d, p).def);
    }

    // Runs n samples. ctl holds the control values, indexed by port number.
    void run(const float* in, float* out, uint32_t count, const float* ctl) {
        DenormalGuard ftz;
        const FamilyDef& d = *def_;
        const int m = clampIndex(ctl[PORT_MODEL], countOf(d.models, 5));
        const int t = clampIndex(ctl[PORT_TUBE], d.nTubes);
        const int i = clampIndex(ctl[PORT_INPUT], countOf(d.inputs, 5));
        const int q = clampIndex(ctl[PORT_QUALITY], 2);
        const int cb = clampIndex(ctl[cabinetPort(d)], CAB_COUNT);
        if (curModel_ < 0) {  // First run after a reset: nothing to fade out yet.
            readKnobs(ctl);
            rebuild(m, t, i, q);
            cab_.set(cb);
            curCab_ = cb;
        }
        const bool ampChange = m != curModel_ || t != curTube_ || i != curInput_ || q != curQuality_;
        const bool change = ampChange || cb != curCab_;
        const double target = std::pow(10.0, ctl[PORT_TRIM] / 20.0);
        const double targetL = std::pow(10.0, ctl[PORT_LEVEL] / 20.0);
        const double step = 1.0 / (kFade * fs_);
        // Digital full scale corresponds to the model's rated power into its load.
        const double rated = std::sqrt(2.0 * amp_->power().ratedWatts * amp_->power().loadOhms);

        uint32_t done = 0;
        while (done < count) {
            const int n = int(std::min<uint32_t>(count - done, kBlock));
            readKnobs(ctl);
            amp_->applyControls();
            for (int k = 0; k < n; ++k) {
                trimG_ += 0.002 * (target - trimG_);
                bufIn_[size_t(k)] = in[done + uint32_t(k)] * trimG_;
            }
            amp_->process(bufIn_.data(), bufOut_.data(), n);
            for (int k = 0; k < n; ++k) {
                levelG_ += 0.002 * (targetL - levelG_);
                gain_ = std::min(1.0, std::max(0.0, gain_ + (change ? -step : step)));
                out[done + uint32_t(k)] = float(cab_.process(bufOut_[size_t(k)] / rated) * levelG_ * gain_);
            }
            done += uint32_t(n);
        }
        // Rebuild once faded out; the fade-in then runs on the next blocks.
        if (change && gain_ <= 0.0) {
            if (ampChange) rebuild(m, t, i, q);
            if (cb != curCab_) {
                cab_.set(cb);
                curCab_ = cb;
            }
        }
    }

private:
    static const int kBlock = 128;
    static constexpr double kFade = 0.005;

    static int clampIndex(float v, int n) {
        const int i = int(v + 0.5f);
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    }

    void rebuild(int m, int t, int i, int q) {
        if (q != curQuality_) amp_->init(fs_, q == 0 ? 2 : 4);
        Config c;
        c.family = def_->family;
        c.model = m;
        c.tube = def_->tubes[t];
        c.input = i;
        amp_->configure(c);
        curModel_ = m;
        curTube_ = t;
        curInput_ = i;
        curQuality_ = q;
    }

    void readKnobs(const float* ctl) {
        for (size_t k = 0; k < def_->ports.size(); ++k) {
            const PortDef& p = def_->ports[k];
            const double v = ctl[PORT_FIRST_KNOB + int(k)];
            amp_->setParam(p.param, p.kind == PK_TOGGLE ? (v > 0.5 ? 1.0 : 0.0) : v / 10.0);
        }
    }

    const FamilyDef* def_ = nullptr;
    double fs_ = 48000.0;
    std::unique_ptr<Amp> amp_;
    Cabinet cab_;
    int curModel_ = -1, curTube_ = -1, curInput_ = -1, curQuality_ = 0, curCab_ = -1;
    double gain_ = 0.0;  // Fade gain around rebuilds.
    double trimG_ = 1.0, levelG_ = 0.5;
    std::vector<double> bufIn_, bufOut_;
};

}  // namespace ashgrove
