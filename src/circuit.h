// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Real-time nonlinear circuit solver. Each schematic section is entered as a
// netlist, reduced to a state-space form (the discrete K method), and stepped per
// sample with Newton iteration on the tube voltages only. Everything lives in
// fixed arrays, so rebuilding for a control change does not allocate.
#pragma once
#include <cmath>
#include <cstring>

#include "tubes.h"

namespace ashgrove {

constexpr int GND = -1;

class Circuit {
public:
    static constexpr int MAX_N = 64, MAX_X = 44, MAX_K = 14, MAX_U = 8, MAX_O = 6;
    static constexpr int MAX_R = 80, MAX_POT = 16;

    enum Taper { LIN, LOG };

    void clear() {
        nNodes_ = nR_ = nX_ = nK_ = nU_ = nO_ = nV_ = nI_ = nPot_ = nDev_ = nDbg_ = 0;
        hasCT_ = hasVCVS_ = false;
        std::memset(xs_, 0, sizeof(xs_));
        std::memset(v_, 0, sizeof(v_));
        std::memset(vLast_, 0, sizeof(vLast_));
        std::memset(u_, 0, sizeof(u_));
        std::memset(o_, 0, sizeof(o_));
        dirty_ = true;
    }

    int node() { return nNodes_++; }
    int input(double value = 0.0) {
        u_[nU_] = value;
        return nU_++;
    }

    int R(int a, int b, double value) {
        res_[nR_] = {a, b, value};
        return nR_++;
    }
    void C(int a, int b, double farads) { addReactive(a, b, farads, true); }
    void L(int a, int b, double henries) { addReactive(a, b, henries, false); }

    // Node voltage forced to input u.
    void V(int a, int u) {
        vsrc_[nV_++] = {a, u};
    }
    // Current drawn out of node a, set by input u.
    void I(int a, int u) {
        isrc_[nI_++] = {a, u};
    }

    // Three-terminal pot. Position 1 puts the wiper at terminal a.
    void pot(int a, int w, int b, double total, Taper t, int param, double pos = 0.5) {
        const int r1 = R(a, w, total), r2 = R(w, b, total);
        pots_[nPot_++] = {r1, r2, total, t, param, false};
        applyPot(pots_[nPot_ - 1], pos);
    }
    // Variable resistance from a to b, maximum at position 1 (or 0 if inverted).
    void rheostat(int a, int b, double total, Taper t, int param, bool invert = false,
                  double pos = 0.5) {
        const int r1 = R(a, b, total);
        pots_[nPot_++] = {r1, -1, total, t, param, invert};
        applyPot(pots_[nPot_ - 1], pos);
    }
    void setParam(int param, double pos) {
        for (int i = 0; i < nPot_; ++i)
            if (pots_[i].param == param) {
                applyPot(pots_[i], pos);
                dirty_ = true;
            }
    }
    bool usesParam(int param) const {
        for (int i = 0; i < nPot_; ++i)
            if (pots_[i].param == param) return true;
        return false;
    }

    void triode(int g, int p, int k) {
        devs_[nDev_++] = {true, nK_, nullptr, 1.0};
        port(g, k);
        port(p, k);
        addCap(g, p, 1.7e-12);
    }
    void pentode(int g1, int g2, int a, int k, Power type, double count) {
        devs_[nDev_++] = {false, nK_, &PentodeTable::get(type), count};
        port(g1, k);
        port(g2, k);
        port(a, k);
    }
    // Ideal centre-tapped winding: (pa - ct) = -(pb - ct), with equal currents.
    void centreTap(int pa, int pb, int ct) {
        ctA_ = pa;
        ctB_ = pb;
        ctC_ = ct;
        hasCT_ = true;
    }
    // Node s follows gain * (a - b), with no loading of a or b.
    void vcvs(int s, int a, int b, double gain) {
        vcS_ = s;
        vcA_ = a;
        vcB_ = b;
        vcG_ = gain;
        hasVCVS_ = true;
    }
    int probe(int a, int b = GND) {
        probes_[nO_] = {a, b};
        return nO_++;
    }

    // A node whose DC voltage is recorded by solveDC, for checking the build.
    int debugNode(int a) {
        dbgNode_[nDbg_] = a;
        return nDbg_++;
    }
    double debugValue(int i) const { return dbgVal_[i]; }

    void setInput(int u, double v) { u_[u] = v; }
    double output(int o) const { return o_[o]; }
    bool dirty() const { return dirty_; }

    // Operating point: caps open, inductors shorted. Sets the reactive state.
    bool solveDC(double T) {
        compile(T, true);
        std::memset(xs_, 0, sizeof(xs_));
        bool ok = false;
        ok = solvePorts(400, 40.0, 1e-6, 0);
        finish();  // Fills o_ using the DC matrices.
        std::memcpy(vLast_, v_, sizeof(vLast_));
        double z[MAX_N];
        nodeVoltages(z);
        for (int i = 0; i < nDbg_; ++i) dbgVal_[i] = at(z, dbgNode_[i]);
        compile(T, false);
        for (int i = 0; i < nX_; ++i) {
            const Reactive& e = reac_[i];
            const double dv = at(z, e.a) - at(z, e.b);
            if (e.isCap)
                xs_[i] = geq_[i] * dv;
            else
                xs_[i] = dv / kDcShort - geq_[i] * dv;
        }
        std::memset(iNl_, 0, sizeof(iNl_));
        return ok;
    }

    // Rebuild matrices after a control change. Reactive state is preserved.
    void update(double T) {
        if (dirty_) compile(T, false);
    }

    inline void step() {
        double p[MAX_K], good[MAX_K];
        for (int k = 0; k < nK_; ++k) {
            double s = 0.0;
            for (int j = 0; j < nU_; ++j) s += Pu_[k][j] * u_[j];
            for (int j = 0; j < nX_; ++j) s += Px_[k][j] * xs_[j];
            p[k] = s;
        }
        std::memcpy(p_, p, sizeof(double) * nK_);
        // Predict from the last two samples, then iterate. The Jacobian factors are
        // reused while they stay good enough, which saves most of the cost.
        for (int k = 0; k < nK_; ++k) {
            const double now = v_[k];
            good[k] = now;
            v_[k] = 2.0 * now - vLast_[k];
            vLast_[k] = now;
        }
        if (!solvePorts(8, 60.0, kTol, 2)) {
            // Fall back to a careful solve from the last good point.
            std::memcpy(v_, good, sizeof(double) * nK_);
            if (!solvePorts(60, 25.0, kTol * 50.0, 0)) ++fails_;
        }
        finish();
    }

    long fails() const { return fails_; }
    void clearFails() { fails_ = 0; }
    double portVolts(int k) const { return v_[k]; }
    int nodes() const { return nNodes_; }
    int states() const { return nX_; }
    int ports() const { return nK_; }

    // DC-time helper: recompute p from inputs (reactive state is zero).
    void prepareDC() {
        for (int k = 0; k < nK_; ++k) {
            double s = 0.0;
            for (int j = 0; j < nU_; ++j) s += Pu_[k][j] * u_[j];
            p_[k] = s;
        }
    }

private:
    static constexpr double kDcShort = 1e-3;
    static constexpr double kTol = 2e-7;  // Volts of port residual.
    double G_[MAX_K][MAX_K];
    struct Res { int a, b; double v; };
    struct Reactive { int a, b; double val; bool isCap; };
    struct Src { int a, u; };
    struct Port { int a, b; };
    struct Pot { int r1, r2; double total; Taper t; int param; bool invert; };
    struct Dev { bool triode; int base; const PentodeTable* pt; double count; };

    static double taper(Taper t, double x) {
        x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
        return t == LOG ? (std::pow(100.0, x) - 1.0) / 99.0 : x;
    }
    void applyPot(const Pot& p, double pos) {
        double f = taper(p.t, p.invert ? 1.0 - pos : pos);
        if (p.r2 < 0) {
            res_[p.r1].v = std::max(p.total * f, 1.0);
        } else {
            res_[p.r1].v = std::max(p.total * (1.0 - f), 1.0);
            res_[p.r2].v = std::max(p.total * f, 1.0);
        }
    }
    void addReactive(int a, int b, double val, bool cap) {
        reac_[nX_++] = {a, b, val, cap};
    }
    void addCap(int a, int b, double f) { addReactive(a, b, f, true); }
    void port(int a, int b) {
        ports_[nK_++] = {a, b};
    }
    static double at(const double* z, int n) { return n < 0 ? 0.0 : z[n]; }

    // Builds A z = Bu u + Bx x + Bi i, then reduces it onto the tube ports.
    void compile(double T, bool dc) {
        const int base = nNodes_;
        int n = base + nV_ + (hasCT_ ? 1 : 0) + (hasVCVS_ ? 1 : 0);
        nZ_ = n;
        static thread_local double A[MAX_N][MAX_N];
        for (int i = 0; i < n; ++i) std::memset(A[i], 0, sizeof(double) * n);

        auto stamp = [&](int a, int b, double g) {
            if (a >= 0) A[a][a] += g;
            if (b >= 0) A[b][b] += g;
            if (a >= 0 && b >= 0) {
                A[a][b] -= g;
                A[b][a] -= g;
            }
        };
        for (int i = 0; i < nR_; ++i) stamp(res_[i].a, res_[i].b, 1.0 / res_[i].v);
        for (int i = 0; i < nNodes_; ++i) A[i][i] += 1e-10;  // Keeps floating nodes defined.
        for (int i = 0; i < nX_; ++i) {
            const Reactive& e = reac_[i];
            double g;
            if (e.isCap)
                g = dc ? 0.0 : 2.0 * e.val / T;
            else
                g = dc ? 1.0 / kDcShort : T / (2.0 * e.val);
            geq_[i] = g;
            stamp(e.a, e.b, g);
        }
        int row = base;
        for (int i = 0; i < nV_; ++i, ++row) {
            A[vsrc_[i].a][row] += 1.0;
            A[row][vsrc_[i].a] += 1.0;
        }
        if (hasCT_) {
            if (ctA_ >= 0) { A[ctA_][row] += 1.0; A[row][ctA_] += 1.0; }
            if (ctB_ >= 0) { A[ctB_][row] += 1.0; A[row][ctB_] += 1.0; }
            if (ctC_ >= 0) { A[ctC_][row] -= 2.0; A[row][ctC_] -= 2.0; }
            ++row;
        }
        if (hasVCVS_) {
            A[vcS_][row] += 1.0;
            A[row][vcS_] += 1.0;
            if (vcA_ >= 0) A[row][vcA_] -= vcG_;
            if (vcB_ >= 0) A[row][vcB_] += vcG_;
            ++row;
        }

        // LU with partial pivoting.
        int piv[MAX_N];
        for (int c = 0; c < n; ++c) {
            int best = c;
            for (int r = c + 1; r < n; ++r)
                if (std::fabs(A[r][c]) > std::fabs(A[best][c])) best = r;
            piv[c] = best;
            if (best != c)
                for (int j = 0; j < n; ++j) std::swap(A[c][j], A[best][j]);
            const double d = 1.0 / A[c][c];
            for (int r = c + 1; r < n; ++r) {
                const double f = A[r][c] * d;
                A[r][c] = f;
                if (f != 0.0)
                    for (int j = c + 1; j < n; ++j) A[r][j] -= f * A[c][j];
            }
        }
        auto solve = [&](double* b) {
            for (int c = 0; c < n; ++c) {
                if (piv[c] != c) std::swap(b[c], b[piv[c]]);
            }
            for (int c = 0; c < n; ++c) {
                for (int r = c + 1; r < n; ++r) b[r] -= A[r][c] * b[c];
            }
            for (int c = n - 1; c >= 0; --c) {
                double s = b[c];
                for (int j = c + 1; j < n; ++j) s -= A[c][j] * b[j];
                b[c] = s / A[c][c];
            }
        };

        // Response of every unknown to each input, state and port current.
        double b[MAX_N];
        for (int j = 0; j < nU_; ++j) {
            std::memset(b, 0, sizeof(double) * n);
            for (int i = 0; i < nV_; ++i)
                if (vsrc_[i].u == j) b[base + i] += 1.0;
            for (int i = 0; i < nI_; ++i)
                if (isrc_[i].u == j && isrc_[i].a >= 0) b[isrc_[i].a] -= 1.0;
            solve(b);
            for (int i = 0; i < n; ++i) Zu_[i][j] = b[i];
        }
        for (int j = 0; j < nX_; ++j) {
            std::memset(b, 0, sizeof(double) * n);
            const Reactive& e = reac_[j];
            const double s = e.isCap ? 1.0 : -1.0;
            if (e.a >= 0) b[e.a] += s;
            if (e.b >= 0) b[e.b] -= s;
            solve(b);
            for (int i = 0; i < n; ++i) Zx_[i][j] = b[i];
        }
        for (int j = 0; j < nK_; ++j) {
            std::memset(b, 0, sizeof(double) * n);
            if (ports_[j].a >= 0) b[ports_[j].a] -= 1.0;
            if (ports_[j].b >= 0) b[ports_[j].b] += 1.0;
            solve(b);
            for (int i = 0; i < n; ++i) Zi_[i][j] = b[i];
        }

        auto diff = [&](double (*Z)[MAX_N], int a, int b2, int j) {
            return (a < 0 ? 0.0 : Z[a][j]) - (b2 < 0 ? 0.0 : Z[b2][j]);
        };
        for (int k = 0; k < nK_; ++k) {
            const Port& q = ports_[k];
            for (int j = 0; j < nU_; ++j) Pu_[k][j] = diff(Zu_, q.a, q.b, j);
            for (int j = 0; j < nX_; ++j) Px_[k][j] = diff(Zx_, q.a, q.b, j);
            for (int j = 0; j < nK_; ++j) Ki_[k][j] = diff(Zi_, q.a, q.b, j);
        }
        for (int o = 0; o < nO_; ++o) {
            const Port& q = probes_[o];
            for (int j = 0; j < nU_; ++j) Ou_[o][j] = diff(Zu_, q.a, q.b, j);
            for (int j = 0; j < nX_; ++j) Ox_[o][j] = diff(Zx_, q.a, q.b, j);
            for (int j = 0; j < nK_; ++j) Oi_[o][j] = diff(Zi_, q.a, q.b, j);
        }
        for (int x = 0; x < nX_; ++x) {
            const Reactive& e = reac_[x];
            const double g2 = 2.0 * geq_[x];
            for (int j = 0; j < nU_; ++j) Fu_[x][j] = g2 * diff(Zu_, e.a, e.b, j);
            for (int j = 0; j < nX_; ++j) Fx_[x][j] = g2 * diff(Zx_, e.a, e.b, j);
            for (int j = 0; j < nK_; ++j) Fi_[x][j] = g2 * diff(Zi_, e.a, e.b, j);
            Fx_[x][x] += e.isCap ? -1.0 : 1.0;
        }
        dirty_ = false;
        luValid_ = false;
        prepareDC();
    }

    inline void evalDevices(const double* v, double* i, double (*G)[MAX_K]) const {
        for (int d = 0; d < nDev_; ++d) {
            const Dev& D = devs_[d];
            const int b = D.base;
            if (D.triode) {
                double dg, dp, gg;
                i[b] = gridCurrent(v[b], 2000.0, gg);
                i[b + 1] = TriodeTable::get().eval(v[b], v[b + 1], dg, dp);
                G[b][b] = gg;
                G[b][b + 1] = 0.0;
                G[b + 1][b] = dg;
                G[b + 1][b + 1] = dp;
            } else {
                const PentodeTable& t = *D.pt;
                const PentodeParams& p = t.params();
                const double vg1 = v[b], vg2 = std::max(v[b + 1], 1.0), va = std::max(v[b + 2], 0.0);
                double gg;
                i[b] = D.count * gridCurrent(vg1, p.rgi, gg);
                // Ia = A(vg2) F(x) T(va), with x = kp (1/mu + vg1/vg2).
                const double x = p.kp * (1.0 / p.mu + vg1 / vg2);
                double dF, dT, dAv;
                const double F = t.F(x, dF);
                const double A = t.A(vg2, dAv);
                const double Tv = t.T(va, dT);
                i[b + 2] = D.count * A * F * Tv;
                const double e2 = vg2 / p.mu + vg1;
                double ig2 = 0.0, dg2 = 0.0;
                if (e2 > 0.0) {
                    const double s = std::sqrt(e2);
                    ig2 = 2.0 * e2 * s / p.kg2;
                    dg2 = 3.0 * s / p.kg2;
                }
                i[b + 1] = D.count * ig2;
                for (int r = 0; r < 3; ++r) G[b + r][b] = G[b + r][b + 1] = G[b + r][b + 2] = 0.0;
                G[b][b] = D.count * gg;
                G[b + 1][b] = D.count * dg2;
                G[b + 1][b + 1] = D.count * dg2 / p.mu + 1e-9;
                G[b + 2][b] = D.count * A * dF * (p.kp / vg2) * Tv;
                G[b + 2][b + 1] = D.count * (dAv * F - A * dF * p.kp * vg1 / (vg2 * vg2)) * Tv;
                G[b + 2][b + 2] = D.count * A * F * dT;
            }
        }
    }

    // Residual of v = p + K i(v) at v; also leaves currents in iNl_ and slopes in G_.
    inline double residual(const double* v, double* r) {
        evalDevices(v, iNl_, G_);
        double worst = 0.0;
        for (int a = 0; a < nK_; ++a) {
            double s = v[a] - p_[a];
            for (int b = 0; b < nK_; ++b) s -= Ki_[a][b] * iNl_[b];
            r[a] = -s;
            worst = std::max(worst, std::fabs(s));
        }
        return worst;
    }

    inline void factor() {
        const int K = nK_;
        double (*J)[MAX_K] = Jlu_;
        for (int a = 0; a < K; ++a)
            for (int b = 0; b < K; ++b) J[a][b] = (a == b ? 1.0 : 0.0);
        // J = I - K G, using G's block sparsity.
        for (int d = 0; d < nDev_; ++d) {
            const int b0 = devs_[d].base, m = devs_[d].triode ? 2 : 3;
            for (int a = 0; a < K; ++a)
                for (int c = 0; c < m; ++c) {
                    double s = 0.0;
                    for (int e = 0; e < m; ++e) s += Ki_[a][b0 + e] * G_[b0 + e][b0 + c];
                    J[a][b0 + c] -= s;
                }
        }
        for (int c = 0; c < K; ++c) {
            int best = c;
            for (int a = c + 1; a < K; ++a)
                if (std::fabs(J[a][c]) > std::fabs(J[best][c])) best = a;
            pivK_[c] = best;
            if (best != c)
                for (int j = 0; j < K; ++j) std::swap(J[c][j], J[best][j]);
            const double d = 1.0 / J[c][c];
            for (int a = c + 1; a < K; ++a) {
                const double f = J[a][c] * d;
                J[a][c] = f;
                if (f != 0.0)
                    for (int j = c + 1; j < K; ++j) J[a][j] -= f * J[c][j];
            }
        }
        luValid_ = true;
    }

    // Damped Newton with a backtracking line search, so a bad step cannot run away.
    // staleIters is how many iterations may reuse the previous Jacobian factors.
    inline bool solvePorts(int maxIter, double maxStep, double tol, int staleIters) {
        const int K = nK_;
        if (K == 0) return true;
        double r[MAX_K], trial[MAX_K], rt[MAX_K];
        double worst = residual(v_, r);
        for (int it = 0; it < maxIter; ++it) {
            if (worst < tol) return true;
            if (it >= staleIters || !luValid_) factor();
            const double (*J)[MAX_K] = Jlu_;
            double dv[MAX_K];
            std::memcpy(dv, r, sizeof(double) * K);
            for (int c = 0; c < K; ++c)
                if (pivK_[c] != c) std::swap(dv[c], dv[pivK_[c]]);
            for (int c = 0; c < K; ++c)
                for (int a = c + 1; a < K; ++a) dv[a] -= J[a][c] * dv[c];
            for (int c = K - 1; c >= 0; --c) {
                double s = dv[c];
                for (int j = c + 1; j < K; ++j) s -= J[c][j] * dv[j];
                dv[c] = s / J[c][c];
            }
            double big = 0.0;
            for (int a = 0; a < K; ++a) big = std::max(big, std::fabs(dv[a]));
            double alpha = big > maxStep ? maxStep / big : 1.0;
            double wt = 0.0;
            for (int ls = 0; ls < 8; ++ls, alpha *= 0.5) {
                for (int a = 0; a < K; ++a) trial[a] = v_[a] + alpha * dv[a];
                wt = residual(trial, rt);
                if (wt < worst) break;
            }
            std::memcpy(v_, trial, sizeof(double) * K);
            std::memcpy(r, rt, sizeof(double) * K);
            worst = wt;
        }
        return worst < tol;
    }

    inline void finish() {
        const double* i = iNl_;  // Left by the last Newton evaluation.
        for (int o = 0; o < nO_; ++o) {
            double s = 0.0;
            for (int j = 0; j < nU_; ++j) s += Ou_[o][j] * u_[j];
            for (int j = 0; j < nX_; ++j) s += Ox_[o][j] * xs_[j];
            for (int j = 0; j < nK_; ++j) s += Oi_[o][j] * i[j];
            o_[o] = s;
        }
        double nx[MAX_X];
        for (int x = 0; x < nX_; ++x) {
            double s = 0.0;
            for (int j = 0; j < nU_; ++j) s += Fu_[x][j] * u_[j];
            for (int j = 0; j < nX_; ++j) s += Fx_[x][j] * xs_[j];
            for (int j = 0; j < nK_; ++j) s += Fi_[x][j] * i[j];
            nx[x] = s;
        }
        std::memcpy(xs_, nx, sizeof(double) * nX_);
    }

    void nodeVoltages(double* z) const {
        for (int n = 0; n < nZ_; ++n) {
            double s = 0.0;
            for (int j = 0; j < nU_; ++j) s += Zu_[n][j] * u_[j];
            for (int j = 0; j < nK_; ++j) s += Zi_[n][j] * iNl_[j];
            z[n] = s;
        }
    }

    int nNodes_ = 0, nR_ = 0, nX_ = 0, nK_ = 0, nU_ = 0, nO_ = 0, nV_ = 0, nI_ = 0, nPot_ = 0,
        nDev_ = 0, nZ_ = 0;
    bool hasCT_ = false, hasVCVS_ = false, dirty_ = true;
    int ctA_ = 0, ctB_ = 0, ctC_ = 0, vcS_ = 0, vcA_ = 0, vcB_ = 0;
    double vcG_ = 0.0;
    Res res_[MAX_R];
    Reactive reac_[MAX_X];
    Src vsrc_[MAX_U], isrc_[MAX_U];
    Port ports_[MAX_K], probes_[MAX_O];
    Pot pots_[MAX_POT];
    Dev devs_[MAX_K];

    double Jlu_[MAX_K][MAX_K], vLast_[MAX_K];
    int pivK_[MAX_K];
    bool luValid_ = false;
    long fails_ = 0;
    int nDbg_ = 0, dbgNode_[24];
    double dbgVal_[24];
    double geq_[MAX_X];
    double xs_[MAX_X], v_[MAX_K], p_[MAX_K], iNl_[MAX_K], u_[MAX_U], o_[MAX_O];
    double Pu_[MAX_K][MAX_U], Px_[MAX_K][MAX_X], Ki_[MAX_K][MAX_K];
    double Ou_[MAX_O][MAX_U], Ox_[MAX_O][MAX_X], Oi_[MAX_O][MAX_K];
    double Fu_[MAX_X][MAX_U], Fx_[MAX_X][MAX_X], Fi_[MAX_X][MAX_K];
    double Zu_[MAX_N][MAX_N], Zx_[MAX_N][MAX_N], Zi_[MAX_N][MAX_N];
};

}  // namespace ashgrove
