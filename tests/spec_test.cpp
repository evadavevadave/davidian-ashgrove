// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Measures every model against its spec sheet and the
// DC table in the service manual. Prints a report; "--dc" limits it to operating points.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "amp.h"

using namespace ashgrove;

static const double FS = 48000.0;
static bool gPowerOnly = false;

struct Sine {
    double amp2 = 0.0;
};

// Plays a sine and returns output RMS at the fundamental, plus the combined RMS of
// harmonics 1-8. A Hann window keeps settling transients out of the reading.
static void measure(Amp& a, double freq, double volts, double& fund, double& total, double settle = 0.4,
                    double len = 0.25) {
    const int ns = int(settle * FS), nm = int(len * FS);
    std::vector<double> in(256), out(256), rec;
    rec.reserve(nm + 256);
    double ph = 0.0;
    const double dp = 2.0 * M_PI * freq / FS;
    for (int done = 0; done < ns + nm; done += 256) {
        for (int i = 0; i < 256; ++i, ph += dp) in[i] = volts * std::sin(ph);
        if (gPowerOnly) a.processPowerOnly(in.data(), out.data(), 256);
        else a.process(in.data(), out.data(), 256);
        if (done >= ns) rec.insert(rec.end(), out.begin(), out.end());
    }
    const int N = int(rec.size());
    double harm = 0.0;
    fund = 0.0;
    for (int h = 1; h <= 8; ++h) {
        if (freq * h > FS * 0.45) break;
        double re = 0, im = 0, wsum = 0;
        for (int i = 0; i < N; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / N);
            re += w * rec[i] * std::cos(dp * h * i);
            im += w * rec[i] * std::sin(dp * h * i);
            wsum += w;
        }
        const double rms = 2.0 * std::sqrt(re * re + im * im) / wsum / std::sqrt(2.0);
        if (h == 1) fund = rms;
        else harm += rms * rms;
    }
    total = std::sqrt(fund * fund + harm);
}

static double levelDb(Amp& a, double f, double v) {
    double fu, to;
    measure(a, f, v, fu, to);
    return 20.0 * std::log10(fu + 1e-12);
}

static double swingDb(Amp& a, int param, double f, double v, double rest = 0.5) {
    const double keep = a.param(param);
    a.setParam(param, 1.0);
    a.applyControls();
    const double hi = levelDb(a, f, v);
    a.setParam(param, 0.0);
    a.applyControls();
    const double lo = levelDb(a, f, v);
    a.setParam(param, keep);
    a.applyControls();
    (void)rest;
    return hi - lo;
}

// Lowest input that reaches a target output RMS, by bisection in log volts.
static double sensitivity(Amp& a, double targetRms, double f = 1000.0) {
    double lo = 1e-5, hi = 2.0;
    for (int i = 0; i < 14; ++i) {
        const double m = std::sqrt(lo * hi);
        double fu, to;
        measure(a, f, m, fu, to, 0.12, 0.1);
        (fu > targetRms ? hi : lo) = m;
    }
    return std::sqrt(lo * hi) / std::sqrt(2.0);
}

// Output power and distortion when just reaching a target THD.
static void powerAtThd(Amp& a, double load, double thd, double& watts) {
    // Rises in 1 dB steps and stops at the first crossing; the amp goes through
    // several distortion regimes, so THD is not monotonic beyond that point.
    double best = 0.0;
    for (double m = 1e-2; m < 30.0; m *= 1.122) {
        double fu, to;
        measure(a, 1000.0, m, fu, to, 0.12, 0.1);
        const double h = to > fu ? std::sqrt(to * to - fu * fu) / to : 0.0;
        const double w = fu * fu / load;
        if (h > thd && w > 1.0) break;
        best = std::max(best, w);
    }
    watts = best;
}

struct ModelRow {
    const char* name;
    Family fam;
    int model;
    int input;  // Default input for tests.
    std::vector<Power> tubes;
    double thd;
    double specSens;   // Volts.
    double specPower;  // Watts at the sheet's stated distortion.
};

static const std::vector<ModelRow>& rows() {
    static const std::vector<ModelRow> r = {
        {"CL100", F_STANDARD, 0, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.04, 1e-3, 115},
        {"CL50", F_STANDARD, 1, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.03, 1e-3, 50},
        {"CH100", F_MASTER, 0, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.04, 0.15e-3, 115},
        {"CC100", F_MASTER, 1, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.04, 0.15e-3, 115},
        {"CH50", F_MASTER, 2, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.03, 0.1e-3, 50},
        {"CC50", F_MASTER, 3, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.03, 0.1e-3, 50},
        {"LE50", F_BASS, 0, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.03, 6e-3, 50},
        {"LE100", F_BASS, 1, 0, {Power::EL34, Power::KT77, Power::T6550}, 0.03, 12e-3, 135},
        {"SC212", F_COMBO, 0, 0, {Power::KT77, Power::T6550}, 0.04, 3.5e-3, 120},
        {"SC410", F_COMBO, 1, 0, {Power::KT77, Power::T6550}, 0.04, 3.5e-3, 120},
        {"DR250", F_DUAL, 0, 0, {Power::T6550, Power::KT77}, 0.05, 0.7e-3, 250},
    };
    return r;
}

static void setAll(Amp& a, double v) {
    for (int p = 0; p < P_COUNT; ++p) a.setParam(p, v);
    a.setParam(P_BRIGHT, 0.0);
    a.setParam(P_BOOST, 0.0);
    a.setParam(P_REVERB, 0.0);
    a.applyControls();
}

static void dcTable(Amp& a, Family f, int model, const double* spec) {
    Config c;
    c.family = f;
    c.model = model;
    c.tube = Power::EL34;
    a.configure(c);
    printf("  TP:");
    for (int i = 1; i <= 17; ++i) printf(" %d=%.4g(%.4g)", i, a.debugVolts(i), spec[i - 1]);
    printf(" conv=%d\n", a.dcConverged());
}

int main(int argc, char** argv) {
    const bool dcOnly = argc > 1 && !std::strcmp(argv[1], "--dc");
    auto amp = std::make_unique<Amp>();
    amp->init(FS);
    printf("DC table: model value (manual value)\n");
    static const double s50[17] = {0.8, 135, 1.63, 190, 250, 260, 1, 145, 141, 295, 23, 185, 175, 24, 37.5, 36.5, 11};
    static const double m50[17] = {2.4, 230, 1.6, 192, 256, 264, 0.9, 150, 152, 300, 23, 190, 180, 24, 37.5, 36.5, 11};
    static const double s100[17] = {1, 150, 1.7, 200, 260, 275, 1.2, 160, 162, 320, 24, 205, 195, 25, 37.5, 36.5, 11};
    static const double m100[17] = {2.6, 250, 1.75, 210, 280, 290, 1, 165, 167, 330, 24, 220, 210, 25, 37.5, 36.5, 11};
    printf("Standard 50W\n");  dcTable(*amp, F_STANDARD, 1, s50);
    printf("Master 50W\n");    dcTable(*amp, F_MASTER, 2, m50);
    printf("Standard 100W\n"); dcTable(*amp, F_STANDARD, 0, s100);
    printf("Master 100W\n");   dcTable(*amp, F_MASTER, 0, m100);
    if (dcOnly) return 0;

    // Speed check: one second of audio, input into the most demanding chain.
    {
        Config c;
        c.family = F_DUAL;
        c.tube = Power::T6550;
        amp->configure(c);
        setAll(*amp, 0.7);
        std::vector<double> in(48000), out(48000);
        for (int i = 0; i < 48000; ++i) in[i] = 0.1 * std::sin(2 * M_PI * 220.0 * i / FS);
        auto t0 = std::chrono::steady_clock::now();
        amp->process(in.data(), out.data(), 48000);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("CPU: %.0f%% of one core at 48 kHz (model DR250)\n", sec * 100.0);
    }

    for (const ModelRow& m : rows()) {
        for (Power t : m.tubes) {
            Config c;
            c.family = m.fam;
            c.model = m.model;
            c.tube = t;
            c.input = m.input;
            amp->configure(c);
            // Power: driven at the phase splitter, so only the output stage distorts.
            setAll(*amp, 0.5);
            amp->setParam(P_MASTER, 1.0);
            amp->setParam(P_PRESENCE, 0.0);
            amp->applyControls();
            const double load = amp->power().loadOhms;
            double w = 0, w10 = 0;
            gPowerOnly = true;
            powerAtThd(*amp, load, m.thd, w);
            powerAtThd(*amp, load, 0.10, w10);
            gPowerOnly = false;
            amp->configure(c);
            // Sensitivity: all controls full, the sheet's stated condition.
            setAll(*amp, 1.0);
            const double targetRms = std::sqrt(0.5 * std::min(m.specPower, w) * load);
            const double sens = sensitivity(*amp, targetRms);
            printf("%s %-5s conv=%d bias=%.1fV idle=%.0fmA power=%.0fW at %.0f%% THD (spec %.0f), %.0fW at 10%%, sens=%.3gmV(spec %.3g)\n",
                   m.name, pentodeParams(t).name, amp->dcConverged(), amp->power().biasVolts,
                   amp->power().idleAmps * 1e3, w, m.thd * 100, m.specPower, w10, sens * 1e3, m.specSens * 1e3);
        }
    }

    // Tone ranges, small signal, other controls at 5.
    struct Tone { const char* name; Family f; int model; int input; int p; double hz; double spec; const char* label; };
    static const Tone tones[] = {
        {"CL100", F_STANDARD, 0, 0, P_TREBLE, 10000, 35, "treble"},
        {"CL100", F_STANDARD, 0, 0, P_MID, 600, 9.5, "middle"},
        {"CL100", F_STANDARD, 0, 0, P_BASS, 50, 15, "bass"},
        {"CL100", F_STANDARD, 0, 0, P_PRESENCE, 3000, 6, "presence"},
        {"CL50", F_STANDARD, 1, 0, P_TREBLE, 10000, 26, "treble"},
        {"CL50", F_STANDARD, 1, 0, P_MID, 600, 9.5, "middle"},
        {"CL50", F_STANDARD, 1, 0, P_BASS, 50, 15, "bass"},
        {"CL50", F_STANDARD, 1, 0, P_PRESENCE, 3000, 6, "presence"},
        {"CH100", F_MASTER, 0, 0, P_TREBLE, 10000, 35, "treble"},
        {"CH100", F_MASTER, 0, 0, P_MID, 600, 9.5, "middle"},
        {"CH100", F_MASTER, 0, 0, P_BASS, 50, 15, "bass"},
        {"CH100", F_MASTER, 0, 0, P_PRESENCE, 3000, 6, "presence"},
        {"CH50", F_MASTER, 2, 0, P_TREBLE, 10000, 32, "treble"},
        {"CH50", F_MASTER, 2, 0, P_MID, 500, 9.5, "middle"},
        {"CH50", F_MASTER, 2, 0, P_BASS, 50, 15, "bass"},
        {"CH50", F_MASTER, 2, 0, P_PRESENCE, 3000, 6, "presence"},
        {"LE50", F_BASS, 0, 0, P_TREBLE, 10000, 48, "treble +23/-25"},
        {"LE50", F_BASS, 0, 0, P_MID, 1000, 18, "middle"},
        {"LE50", F_BASS, 0, 0, P_BASS, 50, 34, "bass +/-17"},
        {"LE50", F_BASS, 0, 0, P_SLOPE, 50, 20, "slope"},
        {"LE100", F_BASS, 1, 0, P_TREBLE, 10000, 48, "treble +23/-25"},
        {"LE100", F_BASS, 1, 0, P_MID, 1000, 18, "middle"},
        {"LE100", F_BASS, 1, 0, P_BASS, 50, 34, "bass +/-17"},
        {"LE100", F_BASS, 1, 0, P_SLOPE, 50, 20, "slope"},
        {"SC212 I", F_COMBO, 0, 0, P_TREBLE, 10000, 40, "treble"},
        {"SC212 I", F_COMBO, 0, 0, P_MID, 400, 15, "middle"},
        {"SC212 I", F_COMBO, 0, 0, P_BASS, 40, 34, "bass"},
        {"SC212 II", F_COMBO, 0, 2, P_TREBLE2, 10000, 37, "treble"},
        {"SC212 II", F_COMBO, 0, 2, P_MID2, 450, 17, "middle"},
        {"SC212 II", F_COMBO, 0, 2, P_BASS2, 50, 34, "bass"},
        {"DR250 I", F_DUAL, 0, 0, P_TREBLE, 10000, 28, "treble"},
        {"DR250 I", F_DUAL, 0, 0, P_MID, 600, 12, "middle"},
        {"DR250 I", F_DUAL, 0, 0, P_BASS, 100, 24, "bass"},
        {"DR250 I", F_DUAL, 0, 0, P_PRESENCE, 5000, 13, "presence"},
        {"DR250 II", F_DUAL, 0, 1, P_TREBLE2, 10000, 40, "treble"},
        {"DR250 II", F_DUAL, 0, 1, P_MID2, 600, 12, "middle"},
        {"DR250 II", F_DUAL, 0, 1, P_BASS2, 100, 25, "bass"},
    };
    // Features the sheets state directly.
    {
        auto set = [&](Family f, int input) {
            Config c;
            c.family = f;
            c.tube = Power::T6550;
            c.input = input;
            if (f != F_COMBO && f != F_DUAL) c.tube = Power::EL34;
            amp->configure(c);
            setAll(*amp, 0.5);
            amp->setParam(P_VOL1, 0.3);
            amp->setParam(P_VOL2, 0.3);
            amp->applyControls();
        };
        printf("\nFeatures\n");
        set(F_COMBO, 0);
        printf("  SC212 gain boost    %5.1f dB (sheet +11)\n", swingDb(*amp, P_BOOST, 1000, 0.3e-3));
        printf("  SC212 bright @10k   %5.1f dB (sheet +16)\n", swingDb(*amp, P_BRIGHT, 10000, 0.3e-3));
        {
            set(F_COMBO, 0);
            const double hi = levelDb(*amp, 1000, 0.3e-3);
            set(F_COMBO, 1);
            printf("  SC212 low input     %5.1f dB (sheet -6)\n", levelDb(*amp, 1000, 0.3e-3) - hi);
            set(F_STANDARD, 0);
            const double h2 = levelDb(*amp, 1000, 0.3e-3);
            set(F_STANDARD, 1);
            printf("  CL100 low input     %5.1f dB (sheet -6)\n", levelDb(*amp, 1000, 0.3e-3) - h2);
        }
        set(F_COMBO, 0);
        const double dry = levelDb(*amp, 1000, 0.3e-3);
        amp->setParam(P_REVERB, 1.0);
        amp->applyControls();
        double wet = 0.0;
        {  // Tank alone: tone off, read the tail just after it stops.
            std::vector<double> in(4800), out(4800);
            for (int b = 0; b < 10; ++b) {
                for (int i = 0; i < 4800; ++i) in[i] = 0.3e-3 * std::sin(2 * M_PI * 1000.0 * (b * 4800 + i) / FS);
                amp->process(in.data(), out.data(), 4800);
            }
            std::fill(in.begin(), in.end(), 0.0);
            double t1 = 0, t2 = 0;
            std::vector<double> levels;
            for (int b = 0; b < 40; ++b) {
                amp->process(in.data(), out.data(), 4800);
                double e = 0;
                for (double v : out) e += v * v;
                levels.push_back(10.0 * std::log10(e / 4800 + 1e-20));
            }
            wet = levels[0];
            t1 = levels[2];
            t2 = levels[22];
            printf("  SC212 reverb        %5.1f dB tail vs dry (sheet +17 insertion)\n", wet - dry);
            printf("  SC212 reverb decay  %5.1f s to -60 dB (sheet 3)\n", 60.0 / ((t1 - t2) / 2.0));
        }
        set(F_BASS, 0);
        for (double sw : {0.0, 0.5, 1.0}) {
            amp->setParam(P_SWEEP, sw);
            amp->applyControls();
            double best = 0, bestF = 0;
            for (double f : {250.0, 400.0, 630.0, 1000.0, 1600.0}) {
                const double d = swingDb(*amp, P_MID, f, 0.3e-3);
                if (std::fabs(d) > std::fabs(best)) {
                    best = d;
                    bestF = f;
                }
            }
            printf("  LE50 mid, sweep %.1f: deepest %5.1f dB at %4.0f Hz (sheet 18 dB, 400 Hz-1 kHz)\n", sw * 10, best, bestF);
        }
    }

    printf("\nTone ranges (dB swing, model vs sheet)\n");
    for (const Tone& t : tones) {
        Config c;
        c.family = t.f;
        c.model = t.model;
        c.tube = t.f == F_COMBO || t.f == F_DUAL ? Power::T6550 : Power::EL34;
        c.input = t.input;
        amp->configure(c);
        setAll(*amp, 0.5);
        amp->setParam(P_VOL1, 0.3);
        amp->setParam(P_VOL2, 0.3);
        amp->setParam(P_MASTER, 0.5);
        amp->applyControls();
        const double at5 = swingDb(*amp, t.p, t.hz, 0.5e-3);
        // The DR250 sheet says "measured with mid down", so that is reported as well.
        for (int q : {P_MID, P_MID2})
            if (q != t.p) amp->setParam(q, 0.0);
        amp->applyControls();
        const double mid0 = swingDb(*amp, t.p, t.hz, 0.5e-3);
        for (int q : {P_TREBLE, P_BASS, P_TREBLE2, P_BASS2})
            if (q != t.p) amp->setParam(q, 0.0);
        amp->applyControls();
        const double at0 = swingDb(*amp, t.p, t.hz, 0.5e-3);
        printf("  %-8s %-16s %6.0f Hz  others@5 %6.1f  mid@0 %6.1f  others@0 %6.1f  (spec %.1f)\n", t.name, t.label, t.hz, at5, mid0, at0, t.spec);
    }
    return 0;
}
