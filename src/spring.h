// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Spring tank for the SC212/SC410. The manual gives only the unit (a 4-spring tank,
// by its type code) and a 3 second decay, so delay times and tone are estimates.
#pragma once
#include <cmath>
#include <vector>

namespace ashgrove {

class SpringTank {
public:
    void init(double fs) {
        fs_ = fs;
        static const double delays[kLines] = {0.0331, 0.0413, 0.0372, 0.0447};
        for (int i = 0; i < kLines; ++i) {
            lines_[i].assign(size_t(delays[i] * fs) + 2, 0.0);
            lens_[i] = int(delays[i] * fs);
            pos_[i] = 0;
            // Feedback that reaches -60 dB after the manual's 3 second decay.
            gains_[i] = std::pow(10.0, -3.0 * delays[i] / 3.45);  // Trimmed; filters add loss.
            lp_[i] = 0.0;
            for (int a = 0; a < kAllpass; ++a) ap_[i][a] = 0.0;
        }
        lpCoef_ = std::exp(-2.0 * 3.14159265358979 * 4200.0 / fs);
        hp_ = 0.0;
        hpCoef_ = std::exp(-2.0 * 3.14159265358979 * 180.0 / fs);
    }
    inline double process(double x) {
        hp_ += (1.0 - hpCoef_) * (x - hp_);
        const double in = x - hp_;
        double out = 0.0;
        for (int i = 0; i < kLines; ++i) {
            double y = lines_[i][pos_[i]];
            lp_[i] = (1.0 - lpCoef_) * y + lpCoef_ * lp_[i];
            y = lp_[i];
            // Dispersion: the "drip" of a real spring.
            for (int a = 0; a < kAllpass; ++a) {
                const double s = ap_[i][a];
                const double v = y - 0.62 * s;
                y = 0.62 * v + s;
                ap_[i][a] = v;
            }
            out += y;
            lines_[i][pos_[i]] = 0.25 * in + gains_[i] * y;
            if (++pos_[i] >= lens_[i]) pos_[i] = 0;
        }
        return out * 0.25;
    }

private:
    static constexpr int kLines = 4, kAllpass = 12;
    double fs_ = 48000.0, lpCoef_ = 0.0, hpCoef_ = 0.0, hp_ = 0.0;
    std::vector<double> lines_[kLines];
    int lens_[kLines] = {}, pos_[kLines] = {};
    double gains_[kLines] = {}, lp_[kLines] = {}, ap_[kLines][kAllpass] = {};
};

}  // namespace ashgrove
