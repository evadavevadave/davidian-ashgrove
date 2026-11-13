// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// The amplifier models. Circuits are transcribed from the service manual
// schematics; each builder names the drawing it comes from.
#pragma once
#include "circuit.h"
#include "oversampler.h"
#include "spring.h"

namespace ashgrove {

enum Family { F_STANDARD, F_MASTER, F_BASS, F_COMBO, F_DUAL, F_COUNT };

enum Param {
    P_VOL1, P_VOL2, P_TREBLE, P_MID, P_BASS, P_PRESENCE, P_MASTER, P_SWEEP, P_SLOPE,
    P_BRIGHT, P_BOOST, P_REVERB, P_TREBLE2, P_MID2, P_BASS2, P_COUNT
};

struct Config {
    Family family = F_MASTER;
    int model = 0;     // Index into the family's model list.
    Power tube = Power::EL34;
    int input = 0;     // Family-specific jack or channel choice.
};

struct PowerInfo {
    double ratedWatts, loadOhms, htVolts, biasVolts, idleAmps;
};

class Amp {
public:
    void init(double sampleRate, int oversample = 2);
    void configure(const Config& c);
    void setParam(int p, double pos01);
    double param(int p) const { return params_[p]; }

    // Runs one block. Input is volts at the jack; output is speaker terminal volts.
    void process(const double* in, double* out, int n);
    // Test use: drives the phase splitter directly, bypassing the preamp sections.
    void processPowerOnly(const double* in, double* out, int n);
    // Applies pending control changes.
    void applyControls();

    int latency() const { return os_.latency(); }
    const PowerInfo& power() const { return pinfo_; }
    double debugVolts(int testPoint) const;
    bool dcConverged() const { return dcOk_; }
    double internalRate() const { return fsInt_; }
    int segCount() const { return nSeg_; }
    Circuit& segment(int i) { return seg_[i]; }
    double segSignal(int i) const { return seg_[i].output(wire_[i].oSig); }
    void setReverbEnabled(bool on) { reverbOn_ = on; }

private:
    struct Wire {
        int uSig = -1, uRail = -1, uLoad = -1, uAux = -1;
        int oSig = -1, oRail = -1, oFeed = -1, oAux = -1;
        double feedR = 1.0;
    };
    struct PowerCfg {
        double ht, rs, cres, rdrop, cdrop, cscreen, bleed;
        double zpp, rload, rscreen, rgrid, count, bias;
        double rated;
    };
    struct PiCfg {
        double rga, rgb, rtail, r16, rbot, cg, cin, cout, cp, rfb, cn17;
        bool presence, tandem;
    };

    void build();
    void buildStandard();
    void buildMaster();
    void buildBass();
    void buildCombo();
    void build2000();
    void buildPower(Circuit& c, Wire& w, const PowerCfg& p, const PiCfg& pi);
    PowerCfg powerCfg(double ht, double rs, double cres, double rdrop, double cdrop,
                      double cscreen, double bleed, double rated, double rload, double rscreen,
                      double rgrid, double count, double markedBias);

    int rail(Circuit& c, Wire& w, double r, double cap);
    int source(Circuit& c, Wire& w);
    void inputNet(Circuit& c, int in, int g, bool high);
    int leadStack(Circuit& c, int src, double rs, double ct, double cb, double cm, int pt, int pb,
                  int pm, bool capToWiper);
    int standardMix(Circuit& c, int plate2, int plate1);
    void cathodeFollowerStack(Circuit& c, int rn);
    void tp(int number, int seg, int node);
    void solveOperatingPoint();

    static constexpr int MAX_SEG = 4;
    Circuit seg_[MAX_SEG];
    Wire wire_[MAX_SEG];
    int nSeg_ = 0;
    int oSpk_ = -1;
    Config cfg_;
    PowerInfo pinfo_{};
    double params_[P_COUNT] = {};
    bool paramChanged_[P_COUNT] = {};
    double railV_[MAX_SEG] = {}, feedI_[MAX_SEG] = {}, sigV_[MAX_SEG] = {}, auxV_[MAX_SEG] = {};
    int tpSeg_[20] = {}, tpIdx_[20] = {};
    bool dcOk_ = false;

    Oversampler os_;
    SpringTank spring_;
    double fs_ = 48000.0, fsInt_ = 192000.0, T_ = 1.0 / 192000.0;
    bool reverbOn_ = true;
    double revOut_ = 0.0, revPrev_ = 0.0, revSend_ = 0.0;
    int revPhase_ = 0;
};

}  // namespace ashgrove
