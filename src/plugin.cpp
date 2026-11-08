// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// LV2 entry points. Selector changes rebuild the circuit behind a short fade.
#include <immintrin.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

#include "amp.h"
#include "lv2_abi.h"
#include "ports.h"

using namespace ashgrove;

namespace {

struct Instance {
    const FamilyDef* def = nullptr;
    double fs = 48000.0;
    std::unique_ptr<Amp> amp;
    float* in = nullptr;
    float* out = nullptr;
    float* latency = nullptr;
    float* model = nullptr;
    float* tube = nullptr;
    float* input = nullptr;
    float* quality = nullptr;
    float* trim = nullptr;
    float* level = nullptr;
    std::vector<float*> knobs;
    int curModel = -1, curTube = -1, curInput = -1, curQuality = 0;
    double gain = 0.0;  // Fade gain around rebuilds.
    double trimG = 1.0, levelG = 0.5;
    std::vector<double> bufIn, bufOut;
};

const int kBlock = 128;
const double kFade = 0.005;

int clampIndex(float v, int n) {
    const int i = int(v + 0.5f);
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

int countOf(const char* const* list, int cap) {
    int n = 0;
    while (n < cap && list[n]) ++n;
    return n;
}

void rebuild(Instance& s, int m, int t, int i, int q) {
    if (q != s.curQuality) s.amp->init(s.fs, q == 0 ? 2 : 4);
    Config c;
    c.family = s.def->family;
    c.model = m;
    c.tube = s.def->tubes[t];
    c.input = i;
    s.amp->configure(c);
    s.curModel = m;
    s.curTube = t;
    s.curInput = i;
    s.curQuality = q;
}

void readKnobs(Instance& s) {
    for (size_t k = 0; k < s.knobs.size(); ++k) {
        if (!s.knobs[k]) continue;
        const PortDef& p = s.def->ports[k];
        const double v = *s.knobs[k];
        s.amp->setParam(p.param, p.kind == PK_TOGGLE ? (v > 0.5 ? 1.0 : 0.0) : v / 10.0);
    }
}

LV2_Handle instantiate(const LV2_Descriptor* d, double rate, const char*, const LV2_Feature* const*) {
    auto s = std::make_unique<Instance>();
    for (int f = 0; f < F_COUNT; ++f)
        if (std::string_view(families()[f].uri) == d->URI) s->def = &families()[f];
    if (!s->def) return nullptr;
    s->fs = rate;
    s->amp = std::make_unique<Amp>();
    s->amp->init(rate, 2);
    s->knobs.assign(s->def->ports.size(), nullptr);
    s->bufIn.resize(kBlock);
    s->bufOut.resize(kBlock);
    return s.release();
}

void connect(LV2_Handle h, uint32_t port, void* data) {
    Instance& s = *static_cast<Instance*>(h);
    float* f = static_cast<float*>(data);
    switch (port) {
        case 0: s.in = f; break;
        case 1: s.out = f; break;
        case 2: s.latency = f; break;
        case 3: s.model = f; break;
        case 4: s.tube = f; break;
        case 5: s.input = f; break;
        case 6: s.quality = f; break;
        case 7: s.trim = f; break;
        case 8: s.level = f; break;
        default:
            if (port - PORT_FIRST_KNOB < s.knobs.size()) s.knobs[port - PORT_FIRST_KNOB] = f;
    }
}

void activate(LV2_Handle h) {
    Instance& s = *static_cast<Instance*>(h);
    s.curModel = -1;
    s.gain = 0.0;
}

void run(LV2_Handle h, uint32_t count) {
    Instance& s = *static_cast<Instance*>(h);
    const unsigned csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040);  // Flush denormals.

    const int nModels = countOf(s.def->models, 5), nInputs = countOf(s.def->inputs, 5);
    const int m = s.model ? clampIndex(*s.model, nModels) : 0;
    const int t = s.tube ? clampIndex(*s.tube, s.def->nTubes) : 0;
    const int i = s.input ? clampIndex(*s.input, nInputs) : 0;
    const int q = s.quality ? clampIndex(*s.quality, 2) : 0;
    if (s.curModel < 0) {  // First run after activate: nothing to fade out yet.
        readKnobs(s);
        rebuild(s, m, t, i, q);
    }
    const bool change = m != s.curModel || t != s.curTube || i != s.curInput || q != s.curQuality;
    const double target = std::pow(10.0, (s.trim ? *s.trim : 0.0f) / 20.0);
    const double targetL = std::pow(10.0, (s.level ? *s.level : 0.0f) / 20.0);
    const double step = 1.0 / (kFade * s.fs);
    // Digital full scale corresponds to the model's rated power into its load.
    const double rated = std::sqrt(2.0 * s.amp->power().ratedWatts * s.amp->power().loadOhms);

    uint32_t done = 0;
    while (done < count) {
        const int n = int(std::min<uint32_t>(count - done, kBlock));
        readKnobs(s);
        s.amp->applyControls();
        for (int k = 0; k < n; ++k) {
            s.trimG += 0.002 * (target - s.trimG);
            s.bufIn[k] = s.in[done + k] * s.trimG;
        }
        s.amp->process(s.bufIn.data(), s.bufOut.data(), n);
        for (int k = 0; k < n; ++k) {
            s.levelG += 0.002 * (targetL - s.levelG);
            s.gain = std::min(1.0, std::max(0.0, s.gain + (change ? -step : step)));
            s.out[done + k] = float(s.bufOut[k] / rated * s.levelG * s.gain);
        }
        done += n;
    }
    // Rebuild once faded out; the fade-in then runs on the next blocks.
    if (change && s.gain <= 0.0) rebuild(s, m, t, i, q);
    if (s.latency) *s.latency = float(s.amp->latency());
    _mm_setcsr(csr);
}

void deactivate(LV2_Handle) {}
void cleanup(LV2_Handle h) { delete static_cast<Instance*>(h); }
const void* extension(const char*) { return nullptr; }

}  // namespace

AG_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    static const LV2_Descriptor d[F_COUNT] = {
        {families()[0].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[1].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[2].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[3].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
        {families()[4].uri, instantiate, connect, activate, run, deactivate, cleanup, extension},
    };
    return index < F_COUNT ? &d[index] : nullptr;
}
