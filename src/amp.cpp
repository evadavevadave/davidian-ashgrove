// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
#include "amp.h"

#include <algorithm>

namespace ashgrove {

namespace {
const double k = 1e3, M = 1e6, u = 1e-6, n = 1e-9, p = 1e-12;
const double kPi = 3.14159265358979323846;
}  // namespace

void Amp::init(double sr, int oversample) {
    fs_ = sr;
    // At high host rates the factor drops, keeping about the same internal rate.
    const int factor = std::max(1, sr < 60000.0 ? oversample : (sr < 120000.0 ? oversample / 2 : 1));
    os_.init(factor);
    fsInt_ = sr * factor;
    T_ = 1.0 / fsInt_;
    spring_.init(sr);
    for (double& v : params_) v = 0.5;
    params_[P_BRIGHT] = params_[P_BOOST] = 0.0;
    params_[P_REVERB] = 0.0;
    configure(cfg_);
}

void Amp::configure(const Config& c) {
    cfg_ = c;
    build();
    for (int s = 0; s < nSeg_; ++s)
        for (int q = 0; q < P_COUNT; ++q) seg_[s].setParam(q, params_[q]);
    solveOperatingPoint();
}

void Amp::setParam(int q, double pos) {
    if (params_[q] == pos) return;
    params_[q] = pos;
    paramChanged_[q] = true;
}

void Amp::applyControls() {
    for (int q = 0; q < P_COUNT; ++q) {
        if (!paramChanged_[q]) continue;
        paramChanged_[q] = false;
        for (int s = 0; s < nSeg_; ++s) seg_[s].setParam(q, params_[q]);
    }
    for (int s = 0; s < nSeg_; ++s) seg_[s].update(T_);
}

void Amp::tp(int number, int s, int node) {
    tpSeg_[number] = s;
    tpIdx_[number] = seg_[s].debugNode(node);
}

double Amp::debugVolts(int number) const {
    return seg_[tpSeg_[number]].debugValue(tpIdx_[number]);
}

// Supply node fed from the next section's rail through r, decoupled by cap.
int Amp::rail(Circuit& c, Wire& w, double r, double cap) {
    const int in = c.node(), nd = c.node();
    w.uRail = c.input(300.0);
    c.V(in, w.uRail);
    c.R(in, nd, r);
    if (cap > 0.0) c.C(nd, GND, cap);
    w.uLoad = c.input(0.0);
    c.I(nd, w.uLoad);
    w.oRail = c.probe(nd);
    w.oFeed = c.probe(in, nd);
    w.feedR = r;
    return nd;
}

int Amp::source(Circuit& c, Wire& w) {
    const int s = c.node();
    w.uSig = c.input(0.0);
    c.V(s, w.uSig);
    return s;
}

// Jack wiring. High: both 68K in parallel. Low: 68K series, 68K to ground (-6 dB).
void Amp::inputNet(Circuit& c, int in, int g, bool high) {
    c.R(in, g, 68 * k);
    c.R(high ? in : GND, g, 68 * k);
}

// Three-band stack. Returns the treble wiper. capToWiper selects the lead wiring
// (lead schematic); the combos tie the mid wiper to its top instead (combo schematic).
int Amp::leadStack(Circuit& c, int src, double rs, double ct, double cb, double cm, int pt, int pb,
                   int pm, bool capToWiper) {
    const int s = c.node(), top = c.node(), wip = c.node(), j = c.node(), mb = c.node();
    c.R(src, s, rs);
    c.C(src, top, ct);
    c.pot(top, wip, j, 250 * k, Circuit::LIN, pt);
    c.C(s, j, cb);
    c.rheostat(j, mb, 1 * M, Circuit::LOG, pb);
    if (capToWiper) {
        const int mw = c.node();
        c.pot(mb, mw, GND, 25 * k, Circuit::LIN, pm);
        c.C(s, mw, cm);
    } else {
        c.rheostat(mb, GND, 25 * k, Circuit::LIN, pm);
        c.C(s, mb, cm);
    }
    return wip;
}

// Lead schematic, standard preamp: volumes, the Ch I bright network, and the 470K mixer.
int Amp::standardMix(Circuit& c, int plate2, int plate1) {
    const int t2 = c.node(), w2 = c.node(), t1 = c.node(), w1 = c.node(), m = c.node();
    c.C(plate2, t2, 22 * n);
    c.pot(t2, w2, GND, 1 * M, Circuit::LOG, P_VOL2);
    c.R(w2, m, 470 * k);
    c.C(plate1, t1, 22 * n);
    c.pot(t1, w1, GND, 1 * M, Circuit::LOG, P_VOL1);
    c.C(t1, w1, 5 * n);
    c.R(w1, m, 470 * k);
    c.C(w1, m, 470 * p);
    return m;
}

// Marked EL34 bias is used as drawn; KT77 is set to the same idle current, and
// 6550 to about 60% of its 35 W rating, since the manual gives no bias for them.
Amp::PowerCfg Amp::powerCfg(double ht, double rs, double cres, double rdrop, double cdrop,
                            double cscreen, double bleed, double rated, double rload,
                            double rscreen, double rgrid, double count, double markedEl34Bias) {
    PowerCfg c{};
    const double headroom = 60.0;
    c.ht = ht; c.rs = rs; c.cres = cres; c.rdrop = rdrop; c.cdrop = cdrop;
    c.cscreen = cscreen; c.bleed = bleed; c.rated = rated; c.rload = rload;
    c.rscreen = rscreen; c.rgrid = rgrid; c.count = count;
    c.zpp = 2.0 * (ht - headroom) * (ht - headroom) / rated;
    const PentodeParams& el34 = pentodeParams(Power::EL34);
    const PentodeParams& tube = pentodeParams(cfg_.tube);
    double idle;
    if (cfg_.tube == Power::EL34) {
        c.bias = markedEl34Bias;
        idle = pentodeIa(el34, c.bias, ht, ht);
        // A marked voltage that would exceed the 25 W plate rating on this tube
        // fit is backed off to 70% of the rating (only the LE50's "approx" -26 V).
        if (idle * ht > 25.0) {
            idle = 17.5 / ht;
            c.bias = solveBias(el34, ht, ht, idle);
        }
    } else if (cfg_.tube == Power::KT77) {
        // The DR250 is drawn for 6550 only, so its -75 V does not apply to KT77.
        idle = cfg_.family == F_DUAL ? 19.0 / ht : pentodeIa(el34, markedEl34Bias, ht, ht);
        c.bias = solveBias(tube, ht, ht, idle);
    } else {
        idle = 21.0 / ht;
        c.bias = solveBias(tube, ht, ht, idle);
    }
    pinfo_ = {rated, rload, ht, c.bias, idle};
    return c;
}

// Phase splitter ("all models" drawing) plus output stage (power-section schematics).
void Amp::buildPower(Circuit& c, Wire& w, const PowerCfg& g, const PiCfg& pi) {
    const int seg = nSeg_ - 1;
    const double totalIdle = pinfo_.idleAmps * 2.0 * g.count;
    const int src = c.node(), res = c.node(), cho = c.node(), mid = c.node(), a = c.node();
    w.uRail = -1;
    c.V(src, c.input(g.ht + totalIdle * g.rs));
    c.R(src, res, g.rs);
    c.C(res, GND, g.cres);
    c.R(res, mid, 90.0);  // Choke resistance and inductance are estimates.
    c.L(mid, cho, 8.0);
    if (g.cscreen > 0.0) c.C(cho, GND, g.cscreen);
    if (g.bleed > 0.0) c.R(cho, GND, g.bleed);
    c.R(cho, a, g.rdrop);
    c.C(a, GND, g.cdrop);
    w.uLoad = c.input(0.0);
    c.I(a, w.uLoad);
    w.oRail = c.probe(a);

    // Phase splitter.
    const int in = source(c, w), ga = c.node(), gb = c.node(), pa = c.node(), pb = c.node();
    const int tail = c.node(), n16 = c.node(), n17 = c.node();
    c.C(in, ga, pi.cin);
    c.R(ga, n16, pi.rga);
    c.R(gb, n16, pi.rgb);
    c.R(a, pa, 82 * k);
    c.R(a, pb, 100 * k);
    c.C(pa, pb, pi.cp);
    c.triode(ga, pa, tail);
    c.triode(gb, pb, tail);
    c.R(tail, n16, pi.rtail);
    c.R(n16, n17, pi.r16);
    c.R(n17, GND, pi.rbot);
    c.C(gb, n17, pi.cg);
    if (pi.cn17 > 0.0) c.C(n17, GND, pi.cn17);
    if (pi.presence) {
        const int pr = c.node();
        c.C(n17, pr, 100 * n);
        c.rheostat(pr, GND, 22 * k, Circuit::LIN, P_PRESENCE, true);
    }
    tp(12, seg, pa);
    tp(13, seg, pb);
    tp(11, seg, ga);
    tp(14, seg, gb);
    tp(15, seg, tail);
    tp(16, seg, n16);
    tp(17, seg, n17);
    tp(10, seg, a);

    // Drive to the output grids, with the optional post-splitter master (combo schematic).
    int da = c.node(), db = c.node();
    c.C(pa, da, pi.cout);
    c.C(pb, db, pi.cout);
    if (pi.tandem) {
        const int wa = c.node(), wb = c.node(), ca = c.node(), cb = c.node();
        c.pot(da, wa, GND, 1 * M, Circuit::LOG, P_MASTER);
        c.pot(db, wb, GND, 1 * M, Circuit::LOG, P_MASTER);
        c.C(wa, ca, 22 * n);
        c.C(wb, cb, 22 * n);
        da = ca;
        db = cb;
    }
    const int bias = c.node();
    c.V(bias, c.input(g.bias));
    const double rb = cfg_.tube == Power::T6550 ? 150 * k : 220 * k;  // "USA" value on the lead power schematic.
    c.R(da, bias, rb);
    c.R(db, bias, rb);
    const int g1a = c.node(), g1b = c.node(), g2a = c.node(), g2b = c.node(), va = c.node(),
              vb = c.node();
    c.R(da, g1a, std::max(g.rgrid / g.count, 1.0));
    c.R(db, g1b, std::max(g.rgrid / g.count, 1.0));
    c.R(cho, g2a, std::max(g.rscreen / g.count, 1.0));
    c.R(cho, g2b, std::max(g.rscreen / g.count, 1.0));
    c.pentode(g1a, g2a, va, GND, cfg_.tube, g.count);
    c.pentode(g1b, g2b, vb, GND, cfg_.tube, g.count);

    // Output transformer: values are estimates, set from the rated power.
    const double lm = g.zpp / (2.0 * kPi * 18.0);
    const double lk = g.zpp / (2.0 * kPi * 16000.0);
    const int x = c.node(), spk = c.node();
    c.centreTap(va, vb, res);
    c.L(va, vb, lm);
    c.R(va, vb, g.zpp * 20.0);  // Core loss; also damps numerical ringing.
    c.L(va, x, lk);
    c.R(x, vb, g.zpp);
    c.vcvs(spk, x, vb, 1.0 / std::sqrt(g.zpp / g.rload));
    c.R(spk, n17, pi.rfb);
    oSpk_ = c.probe(spk);
    w.oSig = oSpk_;
}

void Amp::build() {
    for (int i = 0; i < MAX_SEG; ++i) {
        seg_[i].clear();
        wire_[i] = Wire{};
    }
    for (int& v : tpSeg_) v = 0;
    switch (cfg_.family) {
        case F_STANDARD: buildStandard(); break;
        case F_MASTER: buildMaster(); break;
        case F_BASS: buildBass(); break;
        case F_COMBO: buildCombo(); break;
        default: build2000(); break;
    }
}

// Lead and lead power schematics. Models: CL100 (100 W), CL50 (50 W). Input: 0 I High, 1 I Low, 2 II High, 3 II Low.
void Amp::buildStandard() {
    const bool is100 = cfg_.model == 0;
    nSeg_ = 3;
    {  // V1: both input triodes.
        Circuit& c = seg_[0];
        Wire& w = wire_[0];
        const int r5 = rail(c, w, 10 * k, 50 * u);
        const int in = source(c, w);
        const int g1 = c.node(), g2 = c.node(), k1 = c.node(), k2 = c.node(), p1 = c.node(),
                  p2 = c.node();
        // Channel I is V1b, channel II is V1a. The unused jack pair sits at ground.
        const bool chI = cfg_.input < 2;
        const bool high = cfg_.input % 2 == 0;
        inputNet(c, chI ? in : GND, g1, high);
        inputNet(c, chI ? GND : in, g2, high);
        c.R(k1, GND, 2.7 * k);
        c.C(k1, GND, 680 * n);
        c.R(k2, GND, 820.0);
        c.C(k2, GND, 330 * u);
        c.R(r5, p1, 100 * k);
        c.R(r5, p2, 100 * k);
        c.triode(g1, p1, k1);
        c.triode(g2, p2, k2);
        const int m = standardMix(c, p2, p1);
        (void)m;
        w.oSig = c.probe(p1);
        w.oAux = c.probe(p2);
        tp(1, 0, k2); tp(2, 0, p2); tp(3, 0, k1); tp(4, 0, p1); tp(5, 0, r5);
    }
    {  // V2a gain stage, V2b cathode follower, tone stack.
        Circuit& c = seg_[1];
        Wire& w = wire_[1];
        const int r6 = rail(c, w, 10 * k, 50 * u);
        const int p1 = source(c, w), p2 = c.node();
        w.uAux = c.input(0.0);
        c.V(p2, w.uAux);
        const int m = standardMix(c, p2, p1);
        const int kk = c.node(), pl = c.node(), kf = c.node();
        c.R(r6, pl, 100 * k);
        c.R(kk, GND, 1 * k);
        c.triode(m, pl, kk);
        c.triode(pl, r6, kf);
        c.R(kf, GND, 100 * k);
        const int t = leadStack(c, kf, 33 * k, 470 * p, 22 * n, 22 * n, P_TREBLE, P_BASS, P_MID, true);
        const int l = c.node();
        c.C(t, l, 22 * n);
        c.R(l, GND, 1 * M);
        w.oSig = c.probe(t);
        tp(6, 1, r6); tp(7, 1, kk); tp(8, 1, pl); tp(9, 1, kf);
    }
    nSeg_ = 3;
    PiCfg pi{1 * M, 1 * M, 470.0, 10 * k, 4.7 * k, 100 * n, 22 * n, 22 * n, 47 * p, 100 * k, 0.0, true, false};
    PowerCfg g = is100
        ? powerCfg(468, 70, 50 * u, 20 * k, 100 * u, 50 * u, 112 * k, 115, 16, 1.5 * k, 5.6 * k, 2, -42)
        : powerCfg(365, 120, 100 * u, 10 * k, 50 * u, 50 * u, 0, 55, 16,
                   cfg_.tube == Power::T6550 ? 1.5 * k : 0.0, 0.0, 1, -30);
    buildPower(seg_[2], wire_[2], g, pi);
}

// Lead and lead power schematics. Models: CH100, CC100 (100 W); CH50, CC50 (50 W). Input: 0 High, 1 Low.
void Amp::buildMaster() {
    const bool is100 = cfg_.model < 2;
    const bool high = cfg_.input == 0;
    nSeg_ = 3;
    {
        Circuit& c = seg_[0];
        Wire& w = wire_[0];
        const int r5 = rail(c, w, 10 * k, 50 * u);
        const int in = source(c, w);
        const int g1 = c.node(), k1 = c.node(), p1 = c.node();
        c.R(high ? in : GND, g1, 68 * k);
        c.R(k1, GND, 2.7 * k);
        c.C(k1, GND, 680 * n);
        c.R(r5, p1, 100 * k);
        c.triode(g1, p1, k1);
        const int x = c.node();
        if (high) c.C(p1, x, 22 * n);
        else c.R(in, x, 1.0);  // The Low jack feeds the volume network directly.
        const int top = c.node(), wip = c.node();
        c.R(x, top, 470 * k);
        c.C(x, top, 470 * p);
        c.pot(top, wip, GND, 1 * M, Circuit::LOG, P_VOL1);
        c.C(top, wip, 1 * n);
        const int k2 = c.node(), p2 = c.node(), y = c.node(), g3 = c.node();
        c.R(k2, GND, 10 * k);
        c.R(r5, p2, 100 * k);
        c.triode(wip, p2, k2);
        c.C(p2, y, 22 * n);
        c.R(y, g3, 470 * k);
        c.C(y, g3, 470 * p);
        c.R(g3, GND, 470 * k);
        w.oSig = c.probe(p2);
        tp(1, 0, k2); tp(2, 0, p2); tp(3, 0, k1); tp(4, 0, p1); tp(5, 0, r5);
    }
    {
        Circuit& c = seg_[1];
        Wire& w = wire_[1];
        const int r6 = rail(c, w, 10 * k, 50 * u);
        const int in = source(c, w);
        const int y = c.node(), g3 = c.node(), kk = c.node(), pl = c.node(), kf = c.node();
        c.C(in, y, 22 * n);
        c.R(y, g3, 470 * k);
        c.C(y, g3, 470 * p);
        c.R(g3, GND, 470 * k);
        c.R(kk, GND, 820.0);
        c.R(r6, pl, 100 * k);
        c.triode(g3, pl, kk);
        c.triode(pl, r6, kf);
        c.R(kf, GND, 100 * k);
        const int t = leadStack(c, kf, 33 * k, 470 * p, 22 * n, 22 * n, P_TREBLE, P_BASS, P_MID, true);
        const int mw = c.node(), l = c.node();
        c.pot(t, mw, GND, 1 * M, Circuit::LOG, P_MASTER);
        c.C(mw, l, 22 * n);
        c.R(l, GND, 1 * M);
        w.oSig = c.probe(mw);
        tp(6, 1, r6); tp(7, 1, kk); tp(8, 1, pl); tp(9, 1, kf);
    }
    PiCfg pi{1 * M, 1 * M, 470.0, 10 * k, 4.7 * k, 100 * n, 22 * n, 22 * n, 47 * p, 100 * k, 0.0, true, false};
    PowerCfg g = is100
        ? powerCfg(468, 70, 50 * u, 20 * k, 100 * u, 50 * u, 112 * k, 115, 16, 1.5 * k, 5.6 * k, 2, -42)
        : powerCfg(365, 120, 100 * u, 10 * k, 50 * u, 50 * u, 0, 55, 16,
                   cfg_.tube == Power::T6550 ? 1.5 * k : 0.0, 0.0, 1, -30);
    buildPower(seg_[2], wire_[2], g, pi);
}

// Bass schematics (one per power rating). Models: 0 LE50, 1 LE100. Input: 0 Top, 1 Bottom.
void Amp::buildBass() {
    const bool isLE100 = cfg_.model == 1;
    const bool high = cfg_.input == 0;
    nSeg_ = 3;
    {
        Circuit& c = seg_[0];
        Wire& w = wire_[0];
        const int r = rail(c, w, 1.0, 0.0);
        const int in = source(c, w);
        const int g1 = c.node(), k1 = c.node(), p1 = c.node();
        inputNet(c, in, g1, high);
        c.R(k1, GND, 820.0);
        c.R(r, p1, 100 * k);
        c.triode(g1, p1, k1);
        const int a = c.node(), q = c.node(), mw = c.node(), v = c.node(), cc = c.node(), vw = c.node();
        c.C(p1, a, 100 * n);
        c.C(a, v, 100 * p);
        c.R(a, q, 220 * k);
        c.rheostat(q, mw, 1 * M, Circuit::LIN, P_SWEEP);
        c.R(mw, v, 470 * k);
        c.rheostat(mw, cc, 1 * M, Circuit::LIN, P_MID);
        c.C(cc, GND, 2 * n);
        c.pot(v, vw, GND, 1 * M, Circuit::LOG, P_VOL1);
        const int k2 = c.node(), p2 = c.node(), l = c.node();
        c.R(k2, GND, 10 * k);
        c.R(r, p2, 100 * k);
        c.triode(vw, p2, k2);
        c.C(p2, l, 22 * n);
        c.R(l, GND, 1 * M);
        w.oSig = c.probe(p2);
    }
    {
        Circuit& c = seg_[1];
        Wire& w = wire_[1];
        const int r = rail(c, w, 1.0, 0.0);
        const int in = source(c, w);
        const int g = c.node(), kf = c.node(), nb = c.node();
        c.C(in, g, 22 * n);
        c.R(g, nb, 1 * M);
        c.triode(g, r, kf);
        c.R(kf, nb, 1 * k);
        c.R(nb, GND, 100 * k);
        // Active tone stack around V2b.
        const int t0 = c.node(), a = c.node(), m = c.node(), b = c.node(), d = c.node(), tw = c.node(),
                  nn = c.node(), gv = c.node(), pl = c.node(), kv = c.node();
        c.C(kf, t0, 100 * n);
        c.R(t0, a, 33 * k);
        c.pot(a, m, b, 1 * M, Circuit::LIN, P_BASS);
        c.C(a, m, 2 * n);
        c.C(b, m, 2 * n);
        c.R(b, d, 33 * k);
        c.R(m, nn, 47 * k);
        c.C(nn, gv, 220 * n);
        c.R(gv, GND, 1 * M);
        c.C(nn, tw, 2 * n);
        c.pot(t0, tw, d, 250 * k, Circuit::LIN, P_TREBLE);
        c.C(d, pl, 100 * n);
        c.R(r, pl, 100 * k);
        c.R(kv, GND, 1.5 * k);
        c.C(kv, GND, 25 * u);
        c.triode(gv, pl, kv);
        // Slope: three high-pass sections sharing one variable resistance.
        const int s = c.node(), n1 = c.node(), n2 = c.node(), n3 = c.node(), o = c.node(), l = c.node();
        c.C(pl, n1, 22 * n);
        c.R(n1, s, 100 * k);
        c.C(n1, n2, 22 * n);
        c.R(n2, s, 100 * k);
        c.C(n2, n3, 22 * n);
        c.R(n3, s, 100 * k);
        c.C(n3, o, 22 * n);
        c.rheostat(s, GND, 1 * M, Circuit::LIN, P_SLOPE, true);
        c.C(o, l, 22 * n);
        c.R(l, GND, 1 * M);
        c.R(o, GND, 1 * M);
        w.oSig = c.probe(o);
    }
    PiCfg pi{1 * M, 1 * M, 470.0, 10 * k, 4.7 * k, 100 * n, 1e-3, isLE100 ? 22 * n : 100 * n, 220 * p,
             isLE100 ? 27 * k : 100 * k, 0.0, false, false};
    PowerCfg g = isLE100
        ? powerCfg(468, 70, 50 * u, 10 * k, 100 * u, 50 * u, 112 * k, 135, 16, 1.5 * k, 1.5 * k, 2, -42)
        : powerCfg(355, 120, 100 * u, 10 * k, 50 * u, 50 * u, 0, 55, 16,
                   cfg_.tube == Power::T6550 ? 1.5 * k : 0.0, 0.0, 1, -26);
    buildPower(seg_[2], wire_[2], g, pi);
}

// Combo schematic. Models: SC212, SC410 (same circuit). Input: 0 I High, 1 I Low, 2 II High, 3 II Low.
void Amp::buildCombo() {
    const bool chI = cfg_.input < 2;
    const bool high = cfg_.input % 2 == 0;
    nSeg_ = 3;
    {
        Circuit& c = seg_[0];
        Wire& w = wire_[0];
        const int r = rail(c, w, 10 * k, 50 * u);
        const int in = source(c, w);
        const int g1 = c.node(), k1 = c.node(), p1 = c.node(), t = c.node(), vw = c.node();
        inputNet(c, in, g1, high);
        c.R(k1, GND, 4.7 * k);
        c.R(r, p1, 100 * k);
        c.triode(g1, p1, k1);
        c.C(p1, t, 22 * n);
        c.pot(t, vw, GND, 1 * M, Circuit::LOG, chI ? P_VOL1 : P_VOL2);
        const int k2 = c.node(), p2 = c.node();
        c.R(r, p2, 100 * k);
        c.triode(vw, p2, k2);
        if (chI) {
            const int y = c.node(), z = c.node();
            c.C(k1, y, 470 * n);
            c.R(y, GND, 100 * k);
            c.rheostat(y, GND, 1e7, Circuit::LIN, P_BOOST, true);
            c.C(t, z, 250 * p);
            c.rheostat(z, vw, 1e7, Circuit::LIN, P_BRIGHT, true);
            c.R(k2, GND, 1.5 * k);
            c.C(k2, GND, 25 * u);
            const int tw = leadStack(c, p2, 82 * k, 250 * p, 47 * n, 47 * n, P_TREBLE, P_BASS, P_MID, false);
            const int g3 = c.node();
            c.R(tw, g3, 680 * k);
            c.C(tw, g3, 100 * p);
            c.R(g3, GND, 220 * k);
        } else {
            c.R(k2, GND, 2.7 * k);
            const int tw = leadStack(c, p2, 82 * k, 250 * p, 47 * n, 47 * n, P_TREBLE2, P_BASS2, P_MID2, false);
            c.R(tw, GND, 1 * M);
        }
        w.oSig = c.probe(p2);
    }
    {
        Circuit& c = seg_[1];
        Wire& w = wire_[1];
        const int r = rail(c, w, 10 * k, 50 * u);
        const int src = source(c, w);
        // Channel I path into V3a, with the dry and reverb mix at V3b.
        const int t1 = leadStack(c, chI ? src : GND, 82 * k, 250 * p, 47 * n, 47 * n, P_TREBLE, P_BASS, P_MID, false);
        const int t2 = leadStack(c, chI ? GND : src, 82 * k, 250 * p, 47 * n, 47 * n, P_TREBLE2, P_BASS2, P_MID2, false);
        const int g3 = c.node(), k3 = c.node(), p3 = c.node(), y = c.node(), mix = c.node();
        c.R(t1, g3, 680 * k);
        c.C(t1, g3, 100 * p);
        c.R(g3, GND, 220 * k);
        c.R(k3, GND, 2.7 * k);
        c.R(r, p3, 100 * k);
        c.triode(g3, p3, k3);
        c.C(p3, y, 47 * n);
        c.R(y, GND, 1 * M);
        c.R(y, mix, 6.8 * M);
        c.R(t2, mix, 1 * M);
        c.R(mix, GND, 470 * k);
        // Reverb return through C14 and the reverb pot.
        const int ret = c.node(), rt = c.node(), rw = c.node();
        w.uAux = c.input(0.0);
        c.V(ret, w.uAux);
        c.C(ret, rt, 3 * n);
        c.pot(rt, rw, GND, 250 * k, Circuit::LIN, P_REVERB);
        c.R(rw, mix, 470 * k);
        const int k4 = c.node(), p4 = c.node(), l = c.node();
        c.R(k4, GND, 4.7 * k);
        c.R(r, p4, 100 * k);
        c.triode(mix, p4, k4);
        c.C(p4, l, 10 * n);
        c.R(l, GND, 270 * k);
        w.oSig = c.probe(p4);
        w.oAux = c.probe(y);
    }
    PiCfg pi{270 * k, 1 * M, 470.0, 10 * k, 22 * k, 100 * n, 10 * n, 22 * n, 1 * p, 100 * k, 0.0, false, true};
    PowerCfg g = powerCfg(465, 70, 50 * u, 10 * k, 50 * u, 50 * u, 112 * k, 120, 8, 1.5 * k, 1.5 * k, 2, -42);
    buildPower(seg_[2], wire_[2], g, pi);
}

// 250 W head schematics. Input: 0 Channel I, 1 Channel II (relay switched).
void Amp::build2000() {
    const bool chI = cfg_.input == 0;
    nSeg_ = 4;
    {
        Circuit& c = seg_[0];
        Wire& w = wire_[0];
        const int r = rail(c, w, 10 * k, 50 * u);
        const int in = source(c, w);
        const int g1 = c.node(), k1 = c.node(), p1 = c.node(), t = c.node(), vw = c.node();
        c.R(in, g1, 68 * k);
        c.R(k1, GND, 2.7 * k);
        if (!chI) c.C(k1, GND, 680 * n);
        c.R(r, p1, 100 * k);
        c.triode(g1, p1, k1);
        c.C(p1, t, 22 * n);
        c.pot(t, vw, GND, 1 * M, Circuit::LOG, chI ? P_VOL1 : P_VOL2);
        const int k2 = c.node(), p2 = c.node(), y = c.node(), g3 = c.node();
        c.R(k2, GND, 2.7 * k);
        c.C(k2, GND, 680 * n);
        c.R(r, p2, 100 * k);
        c.triode(vw, p2, k2);
        c.C(p2, y, 22 * n);
        c.R(y, g3, 680 * k);
        c.C(y, g3, 5 * n);
        c.R(g3, GND, 100 * k);
        if (!chI) c.C(r, y, 220 * p);
        w.oSig = c.probe(p2);
    }
    {
        Circuit& c = seg_[1];
        Wire& w = wire_[1];
        const int r = rail(c, w, 10 * k, 50 * u);
        const int src = source(c, w);
        const int y = c.node(), g3 = c.node(), k3 = c.node(), pl = c.node();
        c.C(src, y, 22 * n);
        c.R(y, g3, 680 * k);
        c.C(y, g3, 5 * n);
        c.R(g3, GND, 100 * k);
        c.R(k3, GND, chI ? 3.3 * k : 1 * k);
        c.R(r, pl, 100 * k);
        c.triode(g3, pl, k3);
        int tw;
        if (chI) {
            tw = leadStack(c, pl, 33 * k, 470 * p, 22 * n, 22 * n, P_TREBLE, P_BASS, P_MID, true);
        } else {
            const int s = c.node(), q = c.node(), top = c.node(), j = c.node(), mb = c.node(), mw = c.node();
            tw = c.node();
            c.R(pl, q, 10 * k);
            c.rheostat(q, s, 100 * k, Circuit::LIN, P_SWEEP);
            c.C(pl, top, 470 * p);
            c.pot(top, tw, j, 250 * k, Circuit::LIN, P_TREBLE2);
            c.C(s, j, 22 * n);
            c.rheostat(j, mb, 1 * M, Circuit::LOG, P_BASS2);
            c.pot(mb, mw, GND, 25 * k, Circuit::LIN, P_MID2);
            c.C(s, mw, 22 * n);
        }
        const int g4 = c.node(), nb = c.node(), kf = c.node(), y2 = c.node();
        c.C(tw, g4, 22 * n);
        c.R(g4, nb, 1 * M);
        c.triode(g4, r, kf);
        c.R(kf, nb, 1 * k);
        c.R(nb, GND, 100 * k);
        c.C(kf, y2, 100 * n);
        int out;
        if (chI) {
            const int m1 = c.node(), pw = c.node(), pb = c.node(), cc = c.node();
            out = c.node();
            c.R(y2, m1, 68 * k);
            c.pot(y2, pw, pb, 25 * k, Circuit::LIN, P_PRESENCE);
            c.R(pb, GND, 4.7 * k);
            c.C(pw, cc, 10 * n);
            c.R(cc, m1, 1.0);
            c.R(m1, out, 10 * k);
            c.R(out, GND, 15 * k);
        } else {
            const int top = c.node();
            out = c.node();
            c.R(y2, top, 22 * k);
            // Schematic reads "1K LOG"; 1M assumed, matching the other volume pots.
            c.pot(top, out, GND, 1 * M, Circuit::LOG, P_MASTER);
        }
        c.R(out, GND, 18 * k);  // Load of the mix resistors in the next section.
        w.oSig = c.probe(out);
    }
    {  // V3 mix stage with its own feedback.
        Circuit& c = seg_[2];
        Wire& w = wire_[2];
        const int r = rail(c, w, 10 * k, 50 * u);
        const int src = source(c, w);
        const int mx = c.node(), nn = c.node(), g = c.node(), kv = c.node(), pl = c.node(), kf = c.node(),
                  j = c.node(), l = c.node();
        c.R(src, mx, 10 * k);
        c.R(mx, GND, 10 * k);
        c.R(mx, nn, 47 * k);
        c.C(nn, g, 220 * n);
        c.R(g, GND, 1 * M);
        c.R(kv, GND, 1.5 * k);
        c.C(kv, GND, 470 * n);
        c.R(r, pl, 100 * k);
        c.triode(g, pl, kv);
        c.triode(pl, r, kf);
        c.R(kf, GND, 100 * k);
        c.C(kf, j, 100 * n);
        c.R(j, nn, 220 * k);
        c.C(j, nn, 220 * p);
        c.C(j, l, 22 * n);
        c.R(l, GND, 1 * M);
        w.oSig = c.probe(j);
    }
    PiCfg pi{1 * M, 1 * M, 470.0, 10 * k, 4.7 * k, 100 * n, 22 * n, 100 * n, 1 * p, 100 * k, 100 * n, false, false};
    PowerCfg g = powerCfg(640, 60, 25 * u, 22 * k, 50 * u, 25 * u, 112 * k, 250, 8, 1 * k, 1.5 * k, 3, -75);
    buildPower(seg_[3], wire_[3], g, pi);
}

// Finds the DC operating point across all sections, passing rail and signal
// voltages between them until they agree.
void Amp::solveOperatingPoint() {
    double rail[MAX_SEG], load[MAX_SEG], sig[MAX_SEG], aux[MAX_SEG];
    for (int i = 0; i < MAX_SEG; ++i) {
        rail[i] = pinfo_.htVolts * 0.7;
        load[i] = sig[i] = aux[i] = 0.0;
    }
    bool ok = true;
    for (int pass = 0; pass < 6; ++pass) {
        ok = true;
        for (int i = nSeg_ - 1; i >= 0; --i) {
            Circuit& c = seg_[i];
            const Wire& w = wire_[i];
            if (w.uRail >= 0 && i + 1 < nSeg_) c.setInput(w.uRail, rail[i + 1]);
            if (w.uLoad >= 0) c.setInput(w.uLoad, i > 0 ? load[i - 1] : 0.0);
            if (w.uSig >= 0) c.setInput(w.uSig, i > 0 ? sig[i - 1] : 0.0);
            if (w.uAux >= 0 && i > 0 && cfg_.family == F_STANDARD) c.setInput(w.uAux, aux[i - 1]);
            ok = c.solveDC(T_) && ok;
            rail[i] = c.output(w.oRail);
            load[i] = w.oFeed >= 0 ? c.output(w.oFeed) / w.feedR : 0.0;
            sig[i] = c.output(w.oSig);
            aux[i] = w.oAux >= 0 ? c.output(w.oAux) : 0.0;
        }
    }
    for (int i = 0; i < MAX_SEG; ++i) {
        railV_[i] = rail[i];
        feedI_[i] = load[i];
        sigV_[i] = sig[i];
        auxV_[i] = aux[i];
    }
    dcOk_ = ok;
    revOut_ = revPrev_ = revSend_ = 0.0;
    revPhase_ = 0;
}

void Amp::process(const double* in, double* out, int count) {
    const int os = os_.factor();
    const bool combo = cfg_.family == F_COMBO;
    // Tank return scale, calibrated to the +17 dB insertion gain on the SC212 sheet.
    const double revGain = 5.0;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < os; ++j) {
            const double x = os == 1 ? in[i] : os_.up(j == 0 ? in[i] : 0.0);
            double prev = x;
            for (int s = 0; s < nSeg_; ++s) {
                Circuit& c = seg_[s];
                const Wire& w = wire_[s];
                c.setInput(w.uSig, prev);
                if (w.uRail >= 0) c.setInput(w.uRail, railV_[s + 1]);
                if (w.uLoad >= 0) c.setInput(w.uLoad, s > 0 ? feedI_[s - 1] : 0.0);
                if (w.uAux >= 0) {
                    if (combo) {
                        const double t = double(revPhase_) / os;
                        c.setInput(w.uAux, (revPrev_ + (revOut_ - revPrev_) * t) * revGain);
                    } else {
                        c.setInput(w.uAux, auxV_[s - 1]);
                    }
                }
                c.step();
                prev = c.output(w.oSig);
                railV_[s] = c.output(w.oRail);
                if (w.oFeed >= 0) feedI_[s] = c.output(w.oFeed) / w.feedR;
                if (w.oAux >= 0) auxV_[s] = c.output(w.oAux);
            }
            if (combo) {
                revSend_ += wire_[1].oAux >= 0 ? seg_[1].output(wire_[1].oAux) : 0.0;
                if (++revPhase_ >= os) {
                    revPhase_ = 0;
                    revPrev_ = revOut_;
                    revOut_ = spring_.process(reverbOn_ ? revSend_ / os : 0.0);
                    revSend_ = 0.0;
                }
            }
            if (os == 1) out[i] = prev;
            else os_.pushDown(prev);
        }
        if (os != 1) out[i] = os_.readDown();
    }
}

}  // namespace ashgrove

namespace ashgrove {

void Amp::processPowerOnly(const double* in, double* out, int count) {
    const int os = os_.factor();
    Circuit& c = seg_[nSeg_ - 1];
    const Wire& w = wire_[nSeg_ - 1];
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < os; ++j) {
            const double x = os == 1 ? in[i] : os_.up(j == 0 ? in[i] : 0.0);
            c.setInput(w.uSig, sigV_[nSeg_ - 2] + x);
            c.step();
            if (os == 1) out[i] = c.output(w.oSig);
            else os_.pushDown(c.output(w.oSig));
        }
        if (os != 1) out[i] = os_.readDown();
    }
}

}  // namespace ashgrove
