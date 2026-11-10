// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
/* Panel layouts for the five plugins. Port numbers follow src/ports.h: fixed ports
 * 3-8 (model, tube, input, oversampling, input trim, output level), then each
 * plugin's knobs from 9 in table order, then the cabinet selector. tests/ui_ports_check.cpp checks that these
 * tables agree with ports.h, so the two cannot drift apart.
 *
 * Each panel has its own original name (the "Ashgrove" series) and none of the
 * original amplifiers' branding. Only the control grouping follows the circuits. */
#ifndef UI_LAYOUTS_H
#define UI_LAYOUTS_H

enum { W_KNOB, W_TRIM, W_SWITCH, W_SELECT };

enum {
    PORT_MODEL = 3,
    PORT_TUBE = 4,
    PORT_INPUT = 5,
    PORT_OVERSAMPLING = 6,
    PORT_TRIM = 7,
    PORT_LEVEL = 8,
    PORT_FIRST_KNOB_UI = 9,
    UI_MAX_PORTS = 24
};

typedef struct {
    int kind, port;
    float x, y, r;
    float min, max, def;
    const char* label;  /* on the panel */
    const char* name;   /* in the display */
    const char* lo;     /* scale end labels */
    const char* hi;
    const char* opts[6];
    int nopts;
    const char* symbol; /* the port's LV2 symbol, checked against ports.h */
    float colw;         /* selectors: when > 0, the options wrap into two columns this far apart */
} Widget;

typedef struct {
    float x0, y0, x1, y1;
    const char* title;
} Section;

typedef struct {
    const char* plugin_uri;
    const char* ui_uri;
    const char* title;
    const char* subtitle;
    const char* idle; /* readout text when nothing is selected */
    int w, h;
    const Section* sections;
    int nsections;
    const Widget* widgets;
    int nwidgets;
    int nports;
    const char* fixed_model[2]; /* shown instead of a model selector, or NULL */
} Layout;

#define KNOB(port, sym, x, y, def, label, name) \
    { W_KNOB, port, x, y, 30, 0, 10, def, label, name, "0", "10", { 0 }, 0, sym }
#define TOGGLE(port, sym, x, y, label, name) \
    { W_SWITCH, port, x, y, 0, 0, 1, 0, label, name, 0, 0, { "OFF", "ON" }, 2, sym }
#define TRIM_IN(x, y) \
    { W_TRIM, PORT_TRIM, x, y, 17, -36, 36, 0, "INPUT", "INPUT TRIM", 0, 0, { 0 }, 0, "trim" }
#define TRIM_OUT(x, y) \
    { W_TRIM, PORT_LEVEL, x, y, 17, -60, 12, -6, "OUTPUT", "OUTPUT LEVEL", 0, 0, { 0 }, 0, "level" }
#define SELECT2(port, sym, x, y, name, a, b) \
    { W_SELECT, port, x, y, 0, 0, 1, 0, "", name, 0, 0, { a, b }, 2, sym }
#define SELECT3(port, sym, x, y, name, a, b, c) \
    { W_SELECT, port, x, y, 0, 0, 2, 0, "", name, 0, 0, { a, b, c }, 3, sym }
#define SELECT4(port, sym, x, y, name, a, b, c, d) \
    { W_SELECT, port, x, y, 0, 0, 3, 0, "", name, 0, 0, { a, b, c, d }, 4, sym }

/* The bottom row is the same on every panel; Y0 is its top edge. */
#define SETUP_SECTIONS(Y0, INPUT_TITLE)                                                    \
    { 16, Y0, 146, Y0 + 130, "MODEL" }, { 156, Y0, 281, Y0 + 130, "OUTPUT VALVES" },       \
    { 291, Y0, 411, Y0 + 130, INPUT_TITLE }, { 421, Y0, 541, Y0 + 130, "OVERSAMPLING" },   \
    { 551, Y0, 761, Y0 + 130, "CABINET" }, { 771, Y0, 944, Y0 + 130, "LEVELS" }
#define TUBES3(Y0) SELECT3(PORT_TUBE, "tube", 176, Y0 + 34, "OUTPUT VALVES", "EL34", "KT77", "6550")
#define OVERSAMPLE(Y0) SELECT2(PORT_OVERSAMPLING, "oversampling", 451, Y0 + 34, "OVERSAMPLING", "2X", "4X")
#define LEVELS(Y0) TRIM_IN(816, Y0 + 72), TRIM_OUT(900, Y0 + 72)
/* Cabinet choices in Cab order (src/cabinet.h), two columns of three. The port comes
 * after the plugin's knobs; def is the plugin's default cabinet. */
#define CABINET(port, Y0, def)                                                           \
    { W_SELECT, port, 571, Y0 + 34, 0, 0, 5, def, "", "CABINET", 0, 0,                     \
      { "OFF", "4X12 VINTAGE", "4X12 MODERN", "2X12 OPEN", "4X10 BASS", "1X15 BASS" }, 6, \
      "cabinet", 106 }

/* ---- Classic Lead: CL100, CL50 ---- */

static const Section STD_SECTIONS[] = {
    { 16, 76, 286, 236, "PREAMP" },
    { 296, 76, 944, 236, "TONE" },
    SETUP_SECTIONS(256, "INPUT"),
};
static const Widget STD_WIDGETS[] = {
    KNOB(9, "vol1", 86, 148, 5, "VOLUME I", "VOLUME I"),
    KNOB(10, "vol2", 216, 148, 5, "VOLUME II", "VOLUME II"),
    KNOB(11, "treble", 377, 148, 5, "TREBLE", "TREBLE"),
    KNOB(12, "mid", 539, 148, 5, "MIDDLE", "MIDDLE"),
    KNOB(13, "bass", 701, 148, 5, "BASS", "BASS"),
    KNOB(14, "presence", 863, 148, 5, "PRESENCE", "PRESENCE"),
    SELECT2(PORT_MODEL, "model", 36, 290, "MODEL", "100 WATT", "50 WATT"),
    TUBES3(256),
    SELECT4(PORT_INPUT, "input", 311, 290, "INPUT", "I HIGH", "I LOW", "II HIGH", "II LOW"),
    OVERSAMPLE(256),
    LEVELS(256),
    CABINET(15, 256, 1),
};

/* ---- Cascade Lead: CH100, CC100, CH50, CC50 ---- */

static const Section MV_SECTIONS[] = {
    { 16, 76, 286, 236, "VOLUME" },
    { 296, 76, 944, 236, "TONE" },
    SETUP_SECTIONS(256, "INPUT"),
};
static const Widget MV_WIDGETS[] = {
    KNOB(9, "preamp", 86, 148, 5, "PREAMP", "PREAMP VOLUME"),
    KNOB(10, "master", 216, 148, 5, "MASTER", "MASTER VOLUME"),
    KNOB(11, "treble", 377, 148, 5, "TREBLE", "TREBLE"),
    KNOB(12, "mid", 539, 148, 5, "MIDDLE", "MIDDLE"),
    KNOB(13, "bass", 701, 148, 5, "BASS", "BASS"),
    KNOB(14, "presence", 863, 148, 5, "PRESENCE", "PRESENCE"),
    SELECT4(PORT_MODEL, "model", 36, 290, "MODEL", "100W HEAD", "100W COMBO", "50W HEAD", "50W COMBO"),
    TUBES3(256),
    SELECT2(PORT_INPUT, "input", 311, 290, "INPUT", "HIGH", "LOW"),
    OVERSAMPLE(256),
    LEVELS(256),
    CABINET(15, 256, 1),
};

/* ---- Low End: LE50, LE100 ---- */

static const Section BASS_SECTIONS[] = {
    { 16, 76, 166, 236, "VOLUME" },
    { 176, 76, 944, 236, "TONE" },
    SETUP_SECTIONS(256, "INPUT"),
};
static const Widget BASS_WIDGETS[] = {
    KNOB(9, "volume", 91, 148, 5, "VOLUME", "VOLUME"),
    KNOB(10, "treble", 253, 148, 5, "TREBLE", "TREBLE"),
    KNOB(11, "mid", 406, 148, 5, "MIDDLE", "MIDDLE"),
    KNOB(12, "sweep", 560, 148, 5, "SWEEP", "MID SWEEP"),
    KNOB(13, "bass", 714, 148, 5, "BASS", "BASS"),
    KNOB(14, "slope", 867, 148, 0, "SLOPE", "SLOPE"),
    SELECT2(PORT_MODEL, "model", 36, 290, "MODEL", "50 WATT", "100 WATT"),
    TUBES3(256),
    SELECT2(PORT_INPUT, "input", 311, 290, "INPUT", "TOP", "BOTTOM"),
    OVERSAMPLE(256),
    LEVELS(256),
    CABINET(15, 256, 4),
};

/* ---- Spring Combo: SC212, SC410 ---- */

static const Section COMBO_SECTIONS[] = {
    { 16, 76, 716, 236, "CHANNEL I" },
    { 726, 76, 944, 236, "REVERB" },
    { 16, 252, 716, 412, "CHANNEL II" },
    { 726, 252, 944, 412, "MASTER" },
    SETUP_SECTIONS(432, "INPUT"),
};
static const Widget COMBO_WIDGETS[] = {
    KNOB(9, "vol1", 86, 148, 5, "VOLUME", "CHANNEL I VOLUME"),
    TOGGLE(10, "bright", 176, 152, "BRIGHT", "CHANNEL I BRIGHT"),
    TOGGLE(11, "boost", 256, 152, "BOOST", "CHANNEL I BOOST"),
    KNOB(12, "treble1", 390, 148, 5, "TREBLE", "CHANNEL I TREBLE"),
    KNOB(13, "mid1", 520, 148, 5, "MIDDLE", "CHANNEL I MIDDLE"),
    KNOB(14, "bass1", 650, 148, 5, "BASS", "CHANNEL I BASS"),
    KNOB(15, "reverb", 835, 148, 3, "DEPTH", "REVERB DEPTH"),
    KNOB(16, "vol2", 86, 324, 5, "VOLUME", "CHANNEL II VOLUME"),
    KNOB(17, "treble2", 390, 324, 5, "TREBLE", "CHANNEL II TREBLE"),
    KNOB(18, "mid2", 520, 324, 5, "MIDDLE", "CHANNEL II MIDDLE"),
    KNOB(19, "bass2", 650, 324, 5, "BASS", "CHANNEL II BASS"),
    KNOB(20, "master", 835, 324, 5, "MASTER", "MASTER VOLUME"),
    SELECT2(PORT_MODEL, "model", 36, 466, "MODEL", "2 X 12", "4 X 10"),
    SELECT2(PORT_TUBE, "tube", 176, 466, "OUTPUT VALVES", "KT77", "6550"),
    SELECT4(PORT_INPUT, "input", 311, 466, "INPUT", "I HIGH", "I LOW", "II HIGH", "II LOW"),
    OVERSAMPLE(432),
    LEVELS(432),
    CABINET(21, 432, 3),
};

/* ---- Dual Rail: DR250 ---- */

static const Section DUAL_SECTIONS[] = {
    { 16, 76, 944, 236, "CHANNEL I" },
    { 16, 252, 944, 412, "CHANNEL II" },
    SETUP_SECTIONS(432, "CHANNEL"),
};
static const Widget DUAL_WIDGETS[] = {
    KNOB(9, "gain1", 93, 148, 5, "GAIN", "CHANNEL I GAIN"),
    KNOB(10, "treble1", 248, 148, 5, "TREBLE", "CHANNEL I TREBLE"),
    KNOB(11, "mid1", 403, 148, 5, "MIDDLE", "CHANNEL I MIDDLE"),
    KNOB(12, "bass1", 712, 148, 5, "BASS", "CHANNEL I BASS"),
    KNOB(13, "presence1", 867, 148, 5, "PRESENCE", "CHANNEL I PRESENCE"),
    KNOB(14, "gain2", 93, 324, 5, "GAIN", "CHANNEL II GAIN"),
    KNOB(15, "treble2", 248, 324, 5, "TREBLE", "CHANNEL II TREBLE"),
    KNOB(16, "mid2", 403, 324, 5, "MIDDLE", "CHANNEL II MIDDLE"),
    KNOB(17, "sweep2", 557, 324, 5, "SWEEP", "CHANNEL II MID SWEEP"),
    KNOB(18, "bass2", 712, 324, 5, "BASS", "CHANNEL II BASS"),
    KNOB(19, "volume2", 867, 324, 5, "VOLUME", "CHANNEL II VOLUME"),
    SELECT2(PORT_TUBE, "tube", 176, 466, "OUTPUT VALVES", "6550", "KT77"),
    SELECT2(PORT_INPUT, "input", 311, 466, "CHANNEL", "CHANNEL I", "CHANNEL II"),
    OVERSAMPLE(432),
    LEVELS(432),
    CABINET(20, 432, 2),
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const Layout LAYOUTS[] = {
    { "urn:davidian:ashgrove-classic-lead", "urn:davidian:ashgrove-classic-lead#ui", "DAVIDIAN ASHGROVE CLASSIC LEAD",
      "TWO-CHANNEL VALVE LEAD HEAD, 50 OR 100 WATT", "CLASSIC LEAD", 960, 402, STD_SECTIONS,
      COUNT(STD_SECTIONS), STD_WIDGETS, COUNT(STD_WIDGETS), 16, { 0, 0 } },
    { "urn:davidian:ashgrove-cascade-lead", "urn:davidian:ashgrove-cascade-lead#ui", "DAVIDIAN ASHGROVE CASCADE LEAD",
      "MASTER-VOLUME VALVE LEAD AMP, 50 OR 100 WATT", "CASCADE LEAD", 960, 402, MV_SECTIONS,
      COUNT(MV_SECTIONS), MV_WIDGETS, COUNT(MV_WIDGETS), 16, { 0, 0 } },
    { "urn:davidian:ashgrove-low-end", "urn:davidian:ashgrove-low-end#ui", "DAVIDIAN ASHGROVE LOW END",
      "VALVE BASS HEAD WITH SWEPT MIDDLE AND SLOPE", "LOW END", 960, 402, BASS_SECTIONS,
      COUNT(BASS_SECTIONS), BASS_WIDGETS, COUNT(BASS_WIDGETS), 16, { 0, 0 } },
    { "urn:davidian:ashgrove-spring-combo", "urn:davidian:ashgrove-spring-combo#ui", "DAVIDIAN ASHGROVE SPRING COMBO",
      "TWO-CHANNEL 100 WATT VALVE COMBO WITH SPRING REVERB", "SPRING COMBO", 960, 578, COMBO_SECTIONS,
      COUNT(COMBO_SECTIONS), COMBO_WIDGETS, COUNT(COMBO_WIDGETS), 22, { 0, 0 } },
    { "urn:davidian:ashgrove-dual-rail", "urn:davidian:ashgrove-dual-rail#ui", "DAVIDIAN ASHGROVE DUAL RAIL",
      "RELAY-SWITCHED TWO-CHANNEL VALVE HEAD, 250 WATT", "DUAL RAIL", 960, 578, DUAL_SECTIONS,
      COUNT(DUAL_SECTIONS), DUAL_WIDGETS, COUNT(DUAL_WIDGETS), 21, { "SIX-VALVE", "250 WATT" } },
};
#define N_LAYOUTS COUNT(LAYOUTS)

#endif
