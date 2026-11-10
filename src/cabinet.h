// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Speaker cabinet simulation. Each cabinet is a short cascade of filters shaped like
// the measured response of that kind of cabinet with a close microphone: the
// speaker's low-end resonance, the box's mid character, the cone's presence peaks
// and the steep top-end roll-off. The shapes are estimates, not measurements of any
// particular product. It runs at the host rate, after the amp's oversampling.
#pragma once
#include <cmath>

namespace ashgrove {

enum Cab { CAB_OFF, CAB_4X12_VINTAGE, CAB_4X12_MODERN, CAB_2X12_OPEN, CAB_4X10_BASS, CAB_1X15_BASS, CAB_COUNT };

inline const char* const* cabinetNames() {
    static const char* const n[CAB_COUNT] = {"Off", "4x12 Vintage", "4x12 Modern", "2x12 Open", "4x10 Bass", "1x15 Bass"};
    return n;
}

class Cabinet {
public:
    enum Kind { HP, LP, PEAK };
    struct Stage {
        Kind kind;
        double f, q, db;
    };

    void init(double fs) {
        fs_ = fs;
        set(type_);
    }

    // Picks a cabinet and resets the filters; the caller fades around this.
    void set(int type) {
        type_ = type < 0 || type >= CAB_COUNT ? CAB_OFF : type;
        n_ = 0;
        int count = 0;
        const Stage* st = stages(type_, count);
        for (int i = 0; i < count && n_ < kMax; ++i) design(st[i]);
        for (int i = 0; i < n_; ++i) bq_[i].z1 = bq_[i].z2 = 0.0;
        // Level match: unity power gain averaged over 100 Hz to 5 kHz (log spaced),
        // so switching cabinets changes the tone more than the loudness.
        norm_ = 1.0;
        if (n_ > 0) {
            double sum = 0.0;
            const int pts = 64;
            for (int k = 0; k < pts; ++k) {
                const double f = 100.0 * std::pow(50.0, k / double(pts - 1));
                const double m = magnitude(f);
                sum += m * m;
            }
            norm_ = 1.0 / std::sqrt(sum / pts);
        }
    }

    int type() const { return type_; }

    double process(double x) {
        if (n_ == 0) return x;
        x *= norm_;
        for (int i = 0; i < n_; ++i) {
            Bq& b = bq_[i];
            const double y = b.b0 * x + b.z1;
            b.z1 = b.b1 * x - b.a1 * y + b.z2;
            b.z2 = b.b2 * x - b.a2 * y;
            x = y;
        }
        return x;
    }

    // Gain at frequency f (Hz), including the level match. Used by the tests.
    double response(double f) const { return n_ == 0 ? 1.0 : norm_ * magnitude(f); }

    static const Stage* stages(int type, int& count) {
        // Closed-back 4x12 with vintage 25 W speakers: firm low thump, a soft
        // upper-mid hump and an early top roll-off.
        static const Stage vintage[] = {
            {HP, 75, 1.25, 0},   {PEAK, 110, 1.0, 3},  {PEAK, 450, 0.9, -3}, {PEAK, 1800, 1.4, 3},
            {PEAK, 3200, 2.0, 4}, {LP, 4600, 0.54, 0},  {LP, 4600, 1.31, 0},
        };
        // Closed-back 4x12 with modern high-power speakers: deeper, scooped mids,
        // more top end.
        static const Stage modern[] = {
            {HP, 78, 1.0, 0},    {PEAK, 105, 0.9, 4},  {PEAK, 700, 0.8, -4}, {PEAK, 2500, 1.2, 4},
            {PEAK, 4200, 2.0, 2}, {LP, 5000, 0.54, 0},  {LP, 5000, 1.31, 0},
        };
        // Open-back 2x12 combo: less low end, airy and forward mids.
        static const Stage open[] = {
            {HP, 95, 0.8, 0},   {PEAK, 200, 1.0, 1.5}, {PEAK, 1200, 1.0, 2}, {PEAK, 2800, 1.6, 3.5},
            {LP, 5200, 0.54, 0}, {LP, 5200, 1.31, 0},
        };
        // Closed 4x10 bass cabinet: tight lows, a punchy upper range.
        static const Stage b410[] = {
            {HP, 52, 1.0, 0},    {PEAK, 90, 1.0, 2},  {PEAK, 400, 1.0, -2}, {PEAK, 2000, 1.2, 2},
            {LP, 5000, 0.54, 0}, {LP, 5000, 1.31, 0},
        };
        // Single 15 inch bass cabinet: deep and round, little top end.
        static const Stage b115[] = {
            {HP, 40, 1.2, 0},    {PEAK, 70, 1.0, 3},  {PEAK, 600, 0.9, -2}, {PEAK, 1200, 1.4, 1.5},
            {LP, 2800, 0.54, 0}, {LP, 2800, 1.31, 0},
        };
        switch (type) {
            case CAB_4X12_VINTAGE: count = int(sizeof vintage / sizeof vintage[0]); return vintage;
            case CAB_4X12_MODERN: count = int(sizeof modern / sizeof modern[0]); return modern;
            case CAB_2X12_OPEN: count = int(sizeof open / sizeof open[0]); return open;
            case CAB_4X10_BASS: count = int(sizeof b410 / sizeof b410[0]); return b410;
            case CAB_1X15_BASS: count = int(sizeof b115 / sizeof b115[0]); return b115;
            default: count = 0; return nullptr;
        }
    }

private:
    static const int kMax = 8;
    struct Bq {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    // Audio EQ Cookbook (R. Bristow-Johnson) biquads, normalised so a0 = 1.
    void design(const Stage& s) {
        const double pi = 3.14159265358979323846;
        // Keep corners below Nyquist at low sample rates.
        const double f = std::fmin(s.f, 0.45 * fs_);
        const double w = 2.0 * pi * f / fs_, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / (2.0 * s.q);
        double b0, b1, b2, a0, a1, a2;
        if (s.kind == PEAK) {
            const double A = std::pow(10.0, s.db / 40.0);
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        } else if (s.kind == HP) {
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        } else {
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        }
        Bq& b = bq_[n_++];
        b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
    }

    double magnitude(double f) const {
        const double pi = 3.14159265358979323846;
        const double w = 2.0 * pi * f / fs_;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        double m = 1.0;
        for (int i = 0; i < n_; ++i) {
            const Bq& b = bq_[i];
            const double nr = b.b0 + b.b1 * c1 + b.b2 * c2, ni = -(b.b1 * s1 + b.b2 * s2);
            const double dr = 1.0 + b.a1 * c1 + b.a2 * c2, di = -(b.a1 * s1 + b.a2 * s2);
            m *= std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
        }
        return m;
    }

    double fs_ = 48000.0, norm_ = 1.0;
    int type_ = CAB_OFF, n_ = 0;
    Bq bq_[kMax];
};

}  // namespace ashgrove
