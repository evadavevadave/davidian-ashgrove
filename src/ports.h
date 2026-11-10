// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Port tables shared by the plugin and the Turtle generator, so they cannot drift.
#pragma once
#include <vector>

#include "amp.h"
#include "cabinet.h"

namespace ashgrove {

enum PortKind { PK_KNOB, PK_TOGGLE };

struct PortDef {
    PortKind kind;
    const char* symbol;
    const char* name;
    int param;   // Param index.
    double def;  // Default, in port units.
};

struct FamilyDef {
    Family family;
    const char* uri;
    const char* name;
    const char* models[5];
    Power tubes[3];  // The output tubes this family's spec sheet lists.
    int nTubes;
    const char* inputs[5];
    std::vector<PortDef> ports;
    int cabDefault;  // Cabinet port default (a Cab value).
};

inline const FamilyDef* families() {
    static const FamilyDef f[F_COUNT] = {
        {F_STANDARD, "urn:davidian:ashgrove-classic-lead", "Davidian Ashgrove Classic Lead",
         {"100W", "50W"}, {Power::EL34, Power::KT77, Power::T6550}, 3,
         {"Channel I High", "Channel I Low", "Channel II High", "Channel II Low"},
         {{PK_KNOB, "vol1", "Volume I", P_VOL1, 5}, {PK_KNOB, "vol2", "Volume II", P_VOL2, 5},
          {PK_KNOB, "treble", "Treble", P_TREBLE, 5}, {PK_KNOB, "mid", "Middle", P_MID, 5},
          {PK_KNOB, "bass", "Bass", P_BASS, 5}, {PK_KNOB, "presence", "Presence", P_PRESENCE, 5}}, CAB_4X12_VINTAGE},
        {F_MASTER, "urn:davidian:ashgrove-cascade-lead", "Davidian Ashgrove Cascade Lead",
         {"100W Head", "100W Combo", "50W Head", "50W Combo"}, {Power::EL34, Power::KT77, Power::T6550}, 3,
         {"High", "Low"},
         {{PK_KNOB, "preamp", "Preamp Volume", P_VOL1, 5}, {PK_KNOB, "master", "Master Volume", P_MASTER, 5},
          {PK_KNOB, "treble", "Treble", P_TREBLE, 5}, {PK_KNOB, "mid", "Middle", P_MID, 5},
          {PK_KNOB, "bass", "Bass", P_BASS, 5}, {PK_KNOB, "presence", "Presence", P_PRESENCE, 5}}, CAB_4X12_VINTAGE},
        {F_BASS, "urn:davidian:ashgrove-low-end", "Davidian Ashgrove Low End",
         {"50W", "100W"}, {Power::EL34, Power::KT77, Power::T6550}, 3,
         {"Top", "Bottom"},
         {{PK_KNOB, "volume", "Volume", P_VOL1, 5}, {PK_KNOB, "treble", "Treble", P_TREBLE, 5},
          {PK_KNOB, "mid", "Middle", P_MID, 5}, {PK_KNOB, "sweep", "Mid Sweep", P_SWEEP, 5},
          {PK_KNOB, "bass", "Bass", P_BASS, 5}, {PK_KNOB, "slope", "Slope", P_SLOPE, 0}}, CAB_4X10_BASS},
        {F_COMBO, "urn:davidian:ashgrove-spring-combo", "Davidian Ashgrove Spring Combo",
         {"2x12", "4x10"}, {Power::KT77, Power::T6550}, 2,
         {"Channel I High", "Channel I Low", "Channel II High", "Channel II Low"},
         {{PK_KNOB, "vol1", "I Volume", P_VOL1, 5}, {PK_TOGGLE, "bright", "I Bright", P_BRIGHT, 0},
          {PK_TOGGLE, "boost", "I Boost", P_BOOST, 0}, {PK_KNOB, "treble1", "I Treble", P_TREBLE, 5},
          {PK_KNOB, "mid1", "I Middle", P_MID, 5}, {PK_KNOB, "bass1", "I Bass", P_BASS, 5},
          {PK_KNOB, "reverb", "Reverb", P_REVERB, 3},
          {PK_KNOB, "vol2", "II Volume", P_VOL2, 5}, {PK_KNOB, "treble2", "II Treble", P_TREBLE2, 5},
          {PK_KNOB, "mid2", "II Middle", P_MID2, 5}, {PK_KNOB, "bass2", "II Bass", P_BASS2, 5},
          {PK_KNOB, "master", "Master Volume", P_MASTER, 5}}, CAB_2X12_OPEN},
        {F_DUAL, "urn:davidian:ashgrove-dual-rail", "Davidian Ashgrove Dual Rail",
         {"250W"}, {Power::T6550, Power::KT77}, 2,
         {"Channel I", "Channel II"},
         {{PK_KNOB, "gain1", "I Gain", P_VOL1, 5}, {PK_KNOB, "treble1", "I Treble", P_TREBLE, 5},
          {PK_KNOB, "mid1", "I Middle", P_MID, 5}, {PK_KNOB, "bass1", "I Bass", P_BASS, 5},
          {PK_KNOB, "presence1", "I Presence", P_PRESENCE, 5},
          {PK_KNOB, "gain2", "II Gain", P_VOL2, 5}, {PK_KNOB, "treble2", "II Treble", P_TREBLE2, 5},
          {PK_KNOB, "mid2", "II Middle", P_MID2, 5}, {PK_KNOB, "sweep2", "II Sweep", P_SWEEP, 5},
          {PK_KNOB, "bass2", "II Bass", P_BASS2, 5}, {PK_KNOB, "volume2", "II Volume", P_MASTER, 5}}, CAB_4X12_MODERN},
    };
    return f;
}

// Fixed ports, in order: 0 audio in, 1 audio out, 2 latency, 3 model, 4 tube,
// 5 input, 6 oversampling, 7 input trim, 8 output level, then the family's knobs,
// then the cabinet selector.
constexpr int PORT_FIRST_KNOB = 9;

// The cabinet selector comes after the knobs, so adding it left every earlier port
// number unchanged.
inline int cabinetPort(const FamilyDef& d) { return PORT_FIRST_KNOB + int(d.ports.size()); }

}  // namespace ashgrove
