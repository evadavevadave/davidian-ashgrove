// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Checks the output-valve models in tubes.h against datasheet figures and fits the
// KT77, which has no published Koren fit, to its datasheet plate curves.
// Not part of the build; run by hand when changing tubes.h:
//   cl /O2 /EHsc /I src tools/fit_valves.cpp   (or g++ -O2 -Isrc tools/fit_valves.cpp)
#include <cmath>
#include <cstdio>

#include "tubes.h"

using namespace ashgrove;

// Datasheet typical operating points (anode and screen at 250 V).
struct Typical {
    Power tube;
    double vg1, ia, ig2, gm;  // V, A, A, A/V
    const char* source;
};

static double screenCurrent(const PentodeParams& p, double vg1, double vg2) {
    const double e2 = vg2 / p.mu + vg1;  // As in circuit.h.
    return e2 > 0 ? 2.0 * e2 * std::sqrt(e2) / p.kg2 : 0.0;
}

static void report(const Typical& t) {
    const PentodeParams& p = pentodeParams(t.tube);
    const double ia = pentodeIa(p, t.vg1, 250, 250);
    const double gm = (pentodeIa(p, t.vg1 + 0.05, 250, 250) - pentodeIa(p, t.vg1 - 0.05, 250, 250)) / 0.1;
    printf("%-5s at %.1f V: Ia %5.1f mA (sheet %5.1f), gm %5.2f mA/V (sheet %5.2f), Ig2 %4.1f mA (sheet %4.1f)  [%s]\n",
           p.name, t.vg1, ia * 1e3, t.ia * 1e3, gm * 1e3, t.gm * 1e3, screenCurrent(p, t.vg1, 250) * 1e3,
           t.ig2 * 1e3, t.source);
}

int main() {
    const Typical typ[] = {
        {Power::EL34, -13.5, 0.100, 0.0149, 0.011, "EL34 data sheet, 250/250 V"},
        {Power::KT77, -15.0, 0.100, 0.010, 0.0105, "KT77 data sheet, 250/250 V"},
        {Power::T6550, -14.0, 0.140, 0.012, 0.011, "6550 data sheet, 250/250 V"},
    };
    for (const Typical& t : typ) report(t);

    // KT77 plate curves, screen 250 V, read at about 400 V anode: grid volts, mA.
    const double pts[][2] = {{-2.5, 248}, {-5, 214}, {-7.5, 180}, {-10, 150}, {-12.5, 121},
                             {-15, 93},   {-17.5, 74}, {-20, 53}, {-22.5, 35}, {-25, 21}};
    const int np = int(sizeof pts / sizeof pts[0]);
    double best = 1e9, bex = 0, bkg1 = 0, bkp = 0;
    for (double ex = 0.8; ex <= 1.6; ex += 0.005)
        for (double kp = 20; kp <= 120; kp += 0.5) {
            // KG1 scales the whole curve, so solve it in closed form (least squares on log).
            PentodeParams p{11.5, ex, 1.0, 4500, kp, 24, 1000, "fit"};
            double s = 0;
            for (int i = 0; i < np; ++i) s += std::log(pentodeIa(p, pts[i][0], 250, 400) * 1e3 / pts[i][1]);
            const double kg1 = std::exp(s / np);
            p.kg1 = kg1;
            double err = 0;
            for (int i = 0; i < np; ++i) {
                const double e = std::log(pentodeIa(p, pts[i][0], 250, 400) * 1e3 / pts[i][1]);
                err += e * e;
            }
            if (err < best) best = err, bex = ex, bkg1 = kg1, bkp = kp;
        }
    // Screen: KG2 that gives the sheet's 10 mA at the typical point.
    const double e2 = 250 / 11.5 - 15.0;
    const double kg2 = 2.0 * e2 * std::sqrt(e2) / 0.010;
    printf("\nKT77 fit (mu 11.5 from the sheet, kvb 24): ex %.3f kg1 %.1f kp %.1f kg2 %.0f, rms error %.1f%%\n", bex,
           bkg1, bkp, kg2, 100 * std::sqrt(best / np));
    PentodeParams f{11.5, bex, bkg1, kg2, bkp, 24, 1000, "KT77"};
    for (int i = 0; i < np; ++i)
        printf("  %6.1f V: %5.1f mA (curve %5.1f)\n", pts[i][0], pentodeIa(f, pts[i][0], 250, 400) * 1e3, pts[i][1]);
    const double gm = (pentodeIa(f, -14.95, 250, 250) - pentodeIa(f, -15.05, 250, 250)) / 0.1;
    printf("  typical point: Ia %.1f mA (sheet 100), gm %.2f mA/V (sheet 10.5)\n", pentodeIa(f, -15, 250, 250) * 1e3,
           gm * 1e3);
    // KT77 with Koren's usual exponent (1.35), so the current scales with screen
    // voltage as the other valves do; mu, KP and KG1 fitted to the curves.
    {
        double b = 1e9, bmu = 0, bkp = 0, bk = 0;
        for (double mu = 7; mu <= 16; mu += 0.05)
            for (double kp = 15; kp <= 120; kp += 0.5) {
                PentodeParams q{mu, 1.35, 1.0, 3500, kp, 24, 1000, "fit"};
                double s = 0;
                for (int i = 0; i < np; ++i) s += std::log(pentodeIa(q, pts[i][0], 250, 400) * 1e3 / pts[i][1]);
                q.kg1 = std::exp(s / np);
                double e2 = 0;
                for (int i = 0; i < np; ++i) {
                    const double e = std::log(pentodeIa(q, pts[i][0], 250, 400) * 1e3 / pts[i][1]);
                    e2 += e * e;
                }
                if (e2 < b) b = e2, bmu = mu, bkp = kp, bk = q.kg1;
            }
        PentodeParams q{bmu, 1.35, bk, 3500, bkp, 24, 1000, "KT77"};
        const double e2s = 250 / bmu - 15.0;
        q.kg2 = 2.0 * e2s * std::sqrt(e2s) / 0.010;
        const double g = (pentodeIa(q, -14.95, 250, 250) - pentodeIa(q, -15.05, 250, 250)) / 0.1;
        printf("\nKT77, ex 1.35: mu %.2f kp %.1f kg1 %.1f kg2 %.0f, curves rms %.1f%%, typical %.1f mA, gm %.2f mA/V\n",
               bmu, bkp, bk, q.kg2, 100 * std::sqrt(b / np), pentodeIa(q, -15, 250, 250) * 1e3, g * 1e3);
        for (int i = 0; i < np; ++i)
            printf("  %6.1f V: %5.1f mA (curve %5.1f)\n", pts[i][0], pentodeIa(q, pts[i][0], 250, 400) * 1e3, pts[i][1]);
        const PentodeParams& el = pentodeParams(Power::EL34);
        printf("  at 468 V screen, 0 V grid, 100 V anode: KT77 %.0f mA, EL34 %.0f mA\n",
               pentodeIa(q, 0, 468, 100) * 1e3, pentodeIa(el, 0, 468, 100) * 1e3);
    }
    // KT77 with the EL34's curve shape (it was designed as an EL34 equivalent):
    // mu from the sheet, KG1 set for the typical point, checked against the curves.
    {
        PentodeParams q{11.5, 1.35, 1.0, 3500, 60, 24, 1000, "KT77"};
        q.kg1 = pentodeIa(q, -15, 250, 250) / 0.100;
        double e2 = 0;
        for (int i = 0; i < np; ++i) {
            const double e = std::log(pentodeIa(q, pts[i][0], 250, 400) * 1e3 / pts[i][1]);
            e2 += e * e;
        }
        const double g = (pentodeIa(q, -14.95, 250, 250) - pentodeIa(q, -15.05, 250, 250)) / 0.1;
        printf("\nKT77, EL34 shape: kg1 %.1f, typical 100 mA, gm %.2f mA/V (sheet 10.5), curves rms %.1f%%\n", q.kg1,
               g * 1e3, 100 * std::sqrt(e2 / np));
        for (int i = 0; i < np; ++i)
            printf("  %6.1f V: %5.1f mA (curve %5.1f)\n", pts[i][0], pentodeIa(q, pts[i][0], 250, 400) * 1e3, pts[i][1]);
    }
    // Koren's later fitted 6550 set (normankoren.com, 2001), with KG2 set for the sheet's screen current.
    {
        const double e = 250 / 8.45 - 14.0;
        const double kg2 = 2.0 * e * std::sqrt(e) / 0.012;
        PentodeParams q{8.45, 1.247, 642.5, kg2, 48.92, 20.9, 1000, "6550"};
        const double g = (pentodeIa(q, -13.95, 250, 250) - pentodeIa(q, -14.05, 250, 250)) / 0.1;
        printf("\n6550 Koren 2001 fit: Ia %.1f mA (sheet 140), gm %.2f mA/V (sheet 11), kg2 for 12 mA screen %.0f\n",
               pentodeIa(q, -14, 250, 250) * 1e3, g * 1e3, kg2);
    }
    {
        const double e = 250 / 11.0 - 13.5;
        printf("EL34 kg2 for the sheet's 14.9 mA screen: %.0f\n", 2.0 * e * std::sqrt(e) / 0.0149);
    }
    return 0;
}
