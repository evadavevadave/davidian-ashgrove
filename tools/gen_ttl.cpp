// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Writes manifest.ttl and ashgrove.ttl for the bundle from the port tables.
// Usage: gen_ttl <bundle dir> <library suffix> <ui class>
//   library suffix: .dll, .so or .dylib
//   ui class: WindowsUI, X11UI, CocoaUI, or none (no GUI in this build)
#include <cstdio>
#include <cstring>
#include <string>

#include "ports.h"

using namespace ashgrove;

static void scale(FILE* f, const char* const* labels, int n) {
    fprintf(f, " ;\n        lv2:portProperty lv2:integer , lv2:enumeration ;\n        lv2:scalePoint ");
    for (int i = 0; i < n; ++i)
        fprintf(f, "[ rdfs:label \"%s\" ; rdf:value %d ]%s", labels[i], i, i + 1 < n ? " , " : "");
}

static void control(FILE* f, int& idx, const char* sym, const char* name, double def, double lo, double hi) {
    fprintf(f,
            "    [ a lv2:InputPort , lv2:ControlPort ;\n"
            "        lv2:index %d ; lv2:symbol \"%s\" ; lv2:name \"%s\" ;\n"
            "        lv2:default %.1f ; lv2:minimum %.1f ; lv2:maximum %.1f",
            idx++, sym, name, def, lo, hi);
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string ext = argc > 2 ? argv[2] : ".dll";
    const std::string ui = argc > 3 ? argv[3] : "WindowsUI";
    const bool hasUi = ui != "none";
    FILE* m = fopen((dir + "/manifest.ttl").c_str(), "w");
    FILE* t = fopen((dir + "/ashgrove.ttl").c_str(), "w");
    if (!m || !t) return 1;
    const char* pre =
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
        "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
        "@prefix doap: <http://usefulinc.com/ns/doap#> .\n\n";
    fputs(pre, m);
    fputs(pre, t);
    for (int i = 0; i < F_COUNT; ++i) {
        const FamilyDef& d = families()[i];
        fprintf(m, "<%s> a lv2:Plugin ;\n    lv2:binary <ashgrove%s> ;\n    rdfs:seeAlso <ashgrove.ttl>", d.uri,
                ext.c_str());
        if (hasUi) {
            fprintf(m, " ;\n    <http://lv2plug.in/ns/extensions/ui#ui> <%s#ui> .\n\n", d.uri);
            // The native GUI (src/ui), one UI per plugin, all in one library. A Cocoa
            // UI returns its NSView for the host to place, so it needs no parent.
            const bool cocoa = ui == "CocoaUI";
            fprintf(m,
                    "<%s#ui> a <http://lv2plug.in/ns/extensions/ui#%s> ;\n"
                    "    <http://lv2plug.in/ns/extensions/ui#binary> <ashgrove_ui%s> ;\n"
                    "%s"
                    "    lv2:optionalFeature <http://lv2plug.in/ns/extensions/ui#resize> ,\n"
                    "%s"
                    "        <http://lv2plug.in/ns/extensions/ui#idleInterface> ;\n"
                    "    lv2:extensionData <http://lv2plug.in/ns/extensions/ui#idleInterface> .\n\n",
                    d.uri, ui.c_str(), ext.c_str(),
                    cocoa ? "" : "    lv2:requiredFeature <http://lv2plug.in/ns/extensions/ui#parent> ;\n",
                    cocoa ? "        <http://lv2plug.in/ns/extensions/ui#parent> ,\n" : "");
        } else {
            fputs(" .\n\n", m);
        }
        fprintf(t, "<%s> a lv2:Plugin , lv2:AmplifierPlugin ;\n    doap:name \"%s\" ;\n    lv2:port\n", d.uri, d.name);
        int idx = 0;
        fprintf(t, "    [ a lv2:InputPort , lv2:AudioPort ; lv2:index %d ; lv2:symbol \"in\" ; lv2:name \"In\" ] ,\n", idx++);
        fprintf(t, "    [ a lv2:OutputPort , lv2:AudioPort ; lv2:index %d ; lv2:symbol \"out\" ; lv2:name \"Out\" ] ,\n", idx++);
        fprintf(t,
                "    [ a lv2:OutputPort , lv2:ControlPort ; lv2:index %d ; lv2:symbol \"latency\" ; lv2:name \"Latency\" ;\n"
                "        lv2:portProperty lv2:reportsLatency , lv2:integer ; lv2:minimum 0 ; lv2:maximum 1024 ] ,\n",
                idx++);
        int nm = 0, ni = 0;
        while (nm < 5 && d.models[nm]) ++nm;
        while (ni < 5 && d.inputs[ni]) ++ni;
        control(t, idx, "model", "Model", 0, 0, nm - 1);
        scale(t, d.models, nm);
        fputs(" ] ,\n", t);
        const char* tubes[3];
        for (int k = 0; k < d.nTubes; ++k) tubes[k] = pentodeParams(d.tubes[k]).name;
        control(t, idx, "tube", "Output Tube", 0, 0, d.nTubes - 1);
        scale(t, tubes, d.nTubes);
        fputs(" ] ,\n", t);
        control(t, idx, "input", "Input", 0, 0, ni - 1);
        scale(t, d.inputs, ni);
        fputs(" ] ,\n", t);
        const char* q[2] = {"2x", "4x"};
        control(t, idx, "oversampling", "Oversampling", 0, 0, 1);
        scale(t, q, 2);
        fputs(" ] ,\n", t);
        control(t, idx, "trim", "Input Trim (dB)", 0, -36, 36);
        fputs(" ] ,\n", t);
        control(t, idx, "level", "Output Level (dB)", -6, -60, 12);
        fputs(" ]", t);
        for (const PortDef& p : d.ports) {
            fputs(" ,\n", t);
            if (p.kind == PK_TOGGLE) {
                control(t, idx, p.symbol, p.name, p.def, 0, 1);
                fputs(" ;\n        lv2:portProperty lv2:toggled ]", t);
            } else {
                control(t, idx, p.symbol, p.name, p.def, 0, 10);
                fputs(" ]", t);
            }
        }
        if (idx != cabinetPort(d)) return 2;
        fputs(" ,\n", t);
        control(t, idx, "cabinet", "Cabinet", d.cabDefault, 0, CAB_COUNT - 1);
        scale(t, cabinetNames(), CAB_COUNT);
        fputs(" ] .\n\n", t);
    }
    fclose(m);
    fclose(t);
    return 0;
}
