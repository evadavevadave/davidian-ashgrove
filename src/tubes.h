// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Vacuum tube models (Koren equations). The service manual has no tube curves,
// so these parameters are estimates; ECC83 is calibrated to the manual's DC table.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace ashgrove {

enum class Power { EL34, KT77, T6550 };

struct PentodeParams {
    double mu, ex, kg1, kg2, kp, kvb, rgi;
    const char* name;
};

inline const PentodeParams& pentodeParams(Power t) {
    // EL34 and 6550: published Koren fits. KT77: estimate, no published fit.
    static const PentodeParams el34{11.0, 1.35, 650.0, 4200.0, 60.0, 24.0, 1000.0, "EL34"};
    static const PentodeParams kt77{10.5, 1.35, 700.0, 4500.0, 52.0, 20.0, 1000.0, "KT77"};
    static const PentodeParams t6550{7.9, 1.35, 890.0, 4800.0, 60.0, 24.0, 1000.0, "6550"};
    switch (t) {
        case Power::EL34: return el34;
        case Power::KT77: return kt77;
        default: return t6550;
    }
}

// Anode current in amps, grid voltages relative to the cathode.
inline double pentodeIa(const PentodeParams& p, double vg1, double vg2, double va) {
    if (vg2 < 1.0) vg2 = 1.0;
    const double x = p.kp * (1.0 / p.mu + vg1 / vg2);
    const double l = x > 30.0 ? x : std::log1p(std::exp(x));
    const double e1 = vg2 / p.kp * l;
    return 2.0 * std::pow(e1, p.ex) / p.kg1 * std::atan(std::max(va, 0.0) / p.kvb);
}

// Bias voltage for a target idle anode current, by bisection.
inline double solveBias(const PentodeParams& p, double vscreen, double vplate, double idle) {
    double lo = -200.0, hi = 0.0;
    for (int i = 0; i < 60; ++i) {
        const double m = 0.5 * (lo + hi);
        (pentodeIa(p, m, vscreen, vplate) > idle ? hi : lo) = m;
    }
    return 0.5 * (lo + hi);
}

// Pentode functions on 1-D tables: Ia = A(vg2) F(x) T(va).
class PentodeTable {
public:
    static const PentodeTable& get(Power t) {
        static const PentodeTable el34(Power::EL34), kt77(Power::KT77), t6550(Power::T6550);
        return t == Power::EL34 ? el34 : (t == Power::KT77 ? kt77 : t6550);
    }
    const PentodeParams& params() const { return *p_; }
    inline double F(double x, double& d) const { return lookup(f_, (x - kXMin) / kXStep, kXStep, d); }
    inline double A(double v, double& d) const { return lookup(a_, v / kVStep, kVStep, d); }
    inline double T(double v, double& d) const { return lookup(t_, v / kVStep, kVStep, d); }

private:
    static constexpr double kXMin = -80.0, kXStep = 0.02, kVStep = 1.0;
    static constexpr int kNx = 8001, kNv = 1501;
    const PentodeParams* p_;
    std::vector<double> f_, a_, t_;
    static inline double lookup(const std::vector<double>& t, double f, double step, double& d) {
        const double lim = double(t.size()) - 1.001;
        f = f < 0.0 ? 0.0 : (f > lim ? lim : f);
        const int i = int(f);
        const double a = t[i], b = t[i + 1];
        d = (b - a) / step;
        return a + (b - a) * (f - i);
    }
    explicit PentodeTable(Power type) : p_(&pentodeParams(type)), f_(kNx), a_(kNv), t_(kNv) {
        const PentodeParams& p = *p_;
        for (int i = 0; i < kNx; ++i) {
            const double x = kXMin + i * kXStep;
            f_[i] = std::pow(x > 30.0 ? x : std::log1p(std::exp(x)), p.ex);
        }
        for (int i = 0; i < kNv; ++i) {
            a_[i] = 2.0 / p.kg1 * std::pow(i * kVStep / p.kp, p.ex);
            t_[i] = std::atan(i * kVStep / p.kvb);
        }
    }
};

// ECC83/12AX7 plate current on a lookup table, so it is cheap per sample.
class TriodeTable {
public:
    static const TriodeTable& get() {
        static const TriodeTable t;
        return t;
    }
    // Returns plate current; dg and dp receive the partial derivatives.
    inline double eval(double vg, double vp, double& dg, double& dp) const {
        if (vp <= 0.0) vp = 0.0;
        double fg = (vg - kVgMin) / kVgStep;
        double fp = vp / kVpStep;
        fg = std::min(std::max(fg, 0.0), double(kNg - 1.001));
        fp = std::min(fp, double(kNp - 1.001));
        const int ig = int(fg), ip = int(fp);
        const double tg = fg - ig, tp = fp - ip;
        const double* row = &data_[size_t(ig) * kNp + ip];
        const double a = row[0], b = row[1], c = row[kNp], d = row[kNp + 1];
        dg = ((c - a) * (1 - tp) + (d - b) * tp) / kVgStep;
        dp = ((b - a) * (1 - tg) + (d - c) * tg) / kVpStep;
        return (a * (1 - tg) + c * tg) * (1 - tp) + (b * (1 - tg) + d * tg) * tp;
    }
    static double direct(double vg, double vp) {
        if (vp <= 0.0) return 0.0;
        const double x = kKp * (1.0 / kMu + vg / std::sqrt(kKvb + vp * vp));
        const double l = x > 30.0 ? x : std::log1p(std::exp(x));
        return 2.0 * std::pow(vp / kKp * l, kEx) / kKg1;
    }

private:
    static constexpr double kMu = 100.0, kEx = 1.4, kKp = 600.0, kKvb = 300.0;
    static constexpr double kKg1 = 760.0;  // Published fit is 1060; trimmed to the manual's DC table.
    static constexpr double kVgMin = -60.0, kVgStep = 0.1, kVpStep = 2.0;
    static constexpr int kNg = 700, kNp = 351;  // Vg -60..+10 V, Vp 0..700 V.
    std::vector<double> data_;
    TriodeTable() : data_(size_t(kNg) * kNp) {
        for (int g = 0; g < kNg; ++g)
            for (int p = 0; p < kNp; ++p)
                data_[size_t(g) * kNp + p] = direct(kVgMin + g * kVgStep, p * kVpStep);
    }
};

// Grid conduction: i = v^1.5 / r above zero volts.
inline double gridCurrent(double v, double r, double& d) {
    if (v <= 0.0) {
        d = 1e-12;
        return v * 1e-12;
    }
    const double s = std::sqrt(v);
    d = 1.5 * s / r;
    return v * s / r;
}

}  // namespace ashgrove
