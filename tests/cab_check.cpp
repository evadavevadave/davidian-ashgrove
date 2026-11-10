// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Checks the speaker cabinets at common sample rates: "Off" passes audio unchanged,
// every cabinet is stable and level-matched, and each has the expected overall
// shape (low cut, top-end roll-off). Prints each cabinet's response.
#include <cmath>
#include <cstdio>
#include <vector>

#include "cabinet.h"

using namespace ashgrove;

static int failures = 0;

static void check(bool ok, const char* what, int cab, double fs) {
    if (!ok) {
        printf("FAIL %s: %s at %.0f Hz\n", cabinetNames()[cab], what, fs);
        ++failures;
    }
}

static double db(double g) { return 20.0 * std::log10(g); }

// Gain at f measured by running a sine through the filter.
static double measured(Cabinet& c, double fs, double f) {
    c.set(c.type());
    const int n = int(fs);  // One second; the last half is measured.
    double e = 0.0;
    for (int i = 0; i < n; ++i) {
        const double y = c.process(std::sin(2 * 3.14159265358979323846 * f * i / fs));
        if (i >= n / 2) e += y * y;
    }
    return std::sqrt(e / (n / 2) * 2.0);
}

int main() {
    const double rates[] = {44100, 48000, 88200, 96000, 192000};
    const double freqs[] = {40, 80, 120, 250, 500, 1000, 2000, 3000, 5000, 8000, 12000};
    Cabinet c;
    for (double fs : rates) {
        c.init(fs);
        for (int t = 0; t < CAB_COUNT; ++t) {
            c.set(t);
            // Impulse response: finite, and gone after half a second.
            std::vector<double> h(static_cast<size_t>(fs));
            for (size_t i = 0; i < h.size(); ++i) h[i] = c.process(i == 0 ? 1.0 : 0.0);
            double tail = 0.0;
            bool finite = true;
            for (size_t i = 0; i < h.size(); ++i) {
                if (!std::isfinite(h[i])) finite = false;
                if (i > h.size() / 2) tail = std::fmax(tail, std::fabs(h[i]));
            }
            check(finite, "impulse response is finite", t, fs);
            check(tail < 1e-9, "impulse response dies away", t, fs);
            if (t == CAB_OFF) {
                check(h[0] == 1.0 && tail == 0.0, "Off passes audio unchanged", t, fs);
                continue;
            }
            // The analytic response agrees with a sine run through the filter.
            const double m = measured(c, fs, 1000.0);
            check(std::fabs(db(m) - db(c.response(1000.0))) < 0.1, "response matches measurement", t, fs);
            // Level match: power average over 100 Hz to 5 kHz is 0 dB.
            double sum = 0.0;
            for (int k = 0; k < 64; ++k) {
                const double g = c.response(100.0 * std::pow(50.0, k / 63.0));
                sum += g * g;
            }
            check(std::fabs(db(std::sqrt(sum / 64))) < 0.05, "level matched", t, fs);
            // Shape: the lows below the speaker resonance and the highs above the
            // cone's range are both well down on the midrange.
            const bool bass = t == CAB_4X10_BASS || t == CAB_1X15_BASS;
            const double mid = c.response(1000.0);
            check(db(c.response(bass ? 20.0 : 40.0) / mid) < -6.0, "low cut", t, fs);
            check(db(c.response(10000.0) / mid) < -18.0, "top-end roll-off", t, fs);
            if (fs == 48000) {
                printf("%-13s", cabinetNames()[t]);
                for (double f : freqs) printf(" %6.1f", db(c.response(f)));
                printf("\n");
            }
        }
        if (fs == 48000) {
            printf("%-13s", "dB at Hz:");
            for (double f : freqs) printf(" %6.0f", f);
            printf("\n");
        }
    }
    // The bass cabinets reach lower than the guitar ones.
    c.init(48000);
    c.set(CAB_1X15_BASS);
    const double b = c.response(50.0);
    c.set(CAB_4X12_VINTAGE);
    check(b > c.response(50.0) * 2.0, "1x15 reaches lower than a guitar 4x12", CAB_1X15_BASS, 48000);
    printf(failures ? "%d FAILED\n" : "All cabinet checks passed\n", failures);
    return failures ? 1 : 0;
}
