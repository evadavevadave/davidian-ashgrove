// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// With no input, a healthy amp is silent. Any output here means the model is
// oscillating, so this reports its level and approximate frequency.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "amp.h"

using namespace ashgrove;

static void report(Amp& a, const char* name, double pos) {
    for (int p = 0; p < P_COUNT; ++p) a.setParam(p, pos);
    a.setParam(P_BRIGHT, 0.0);
    a.setParam(P_BOOST, 0.0);
    a.setParam(P_REVERB, 0.0);
    a.applyControls();
    std::vector<double> in(48000, 0.0), out(48000);
    a.process(in.data(), out.data(), 48000);  // Settle.
    // A brief kick, then see whether it dies away.
    in[0] = 1e-3;
    a.process(in.data(), out.data(), 48000);
    in[0] = 0.0;
    a.process(in.data(), out.data(), 48000);
    double e = 0, peak = 0;
    int zc = 0;
    for (int i = 24000; i < 48000; ++i) {
        e += out[i] * out[i];
        peak = std::max(peak, std::fabs(out[i]));
        if (i > 24000 && (out[i] > 0) != (out[i - 1] > 0)) ++zc;
    }
    printf("%-14s controls %.0f%%: rms %.3g V, peak %.3g V, about %d Hz\n", name, pos * 100, std::sqrt(e / 24000), peak, zc);
}

int main() {
    auto a = std::make_unique<Amp>();
    a->init(48000.0);
    struct R { const char* n; Family f; int m; int in; Power t; };
    const R rows[] = {
        {"CL100 I", F_STANDARD, 0, 0, Power::EL34}, {"CL100 II", F_STANDARD, 0, 2, Power::EL34},
        {"CL50 I", F_STANDARD, 1, 0, Power::EL34}, {"CH100", F_MASTER, 0, 0, Power::EL34},
        {"CH50", F_MASTER, 2, 0, Power::EL34},     {"LE50", F_BASS, 0, 0, Power::EL34},
        {"LE100", F_BASS, 1, 0, Power::EL34},       {"SC212 I", F_COMBO, 0, 0, Power::KT77},
        {"SC212 II", F_COMBO, 0, 2, Power::KT77},   {"DR250 I", F_DUAL, 0, 0, Power::T6550},
        {"DR250 II", F_DUAL, 0, 1, Power::T6550},
    };
    for (const R& r : rows) {
        Config c;
        c.family = r.f;
        c.model = r.m;
        c.input = r.in;
        c.tube = r.t;
        a->configure(c);
        report(*a, r.n, 0.5);
        report(*a, r.n, 1.0);
    }
    return 0;
}
