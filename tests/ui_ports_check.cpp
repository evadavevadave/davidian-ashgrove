// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Checks the GUI layouts against the plugin's port tables: every control is on the
// panel, at the right port number, with the same default and range.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ports.h"
#include "ui/layouts.h"

using namespace ashgrove;

static int failures = 0;

static void check(bool ok, const char* what, const char* uri) {
    if (!ok) {
        printf("FAIL %s: %s\n", uri, what);
        ++failures;
    }
}

static const Widget* find(const Layout& L, int port) {
    for (int i = 0; i < L.nwidgets; ++i)
        if (L.widgets[i].port == port) return &L.widgets[i];
    return nullptr;
}

int main() {
    static_assert(PORT_FIRST_KNOB == PORT_FIRST_KNOB_UI, "first knob port");
    check(N_LAYOUTS == F_COUNT, "one layout per plugin", "all");
    for (int f = 0; f < F_COUNT; ++f) {
        const FamilyDef& d = families()[f];
        const Layout* L = nullptr;
        for (int i = 0; i < N_LAYOUTS; ++i)
            if (!strcmp(LAYOUTS[i].plugin_uri, d.uri)) L = &LAYOUTS[i];
        check(L != nullptr, "has a layout", d.uri);
        if (!L) continue;
        check(L->nports == PORT_FIRST_KNOB + int(d.ports.size()), "port count", d.uri);
        for (size_t k = 0; k < d.ports.size(); ++k) {
            const PortDef& pd = d.ports[k];
            const Widget* w = find(*L, PORT_FIRST_KNOB + int(k));
            char msg[160];
            snprintf(msg, sizeof msg, "port %d (%s) on panel", PORT_FIRST_KNOB + int(k), pd.symbol);
            check(w && !strcmp(w->symbol, pd.symbol), msg, d.uri);
            if (!w) continue;
            snprintf(msg, sizeof msg, "%s default and range", pd.symbol);
            const bool toggle = pd.kind == PK_TOGGLE;
            check(std::fabs(w->def - pd.def) < 1e-6 && w->min == 0 && w->max == (toggle ? 1 : 10) &&
                      (w->kind == W_SWITCH) == toggle,
                  msg, d.uri);
        }
        int nm = 0, ni = 0;
        while (nm < 5 && d.models[nm]) ++nm;
        while (ni < 5 && d.inputs[ni]) ++ni;
        const Widget* model = find(*L, PORT_MODEL);
        check(nm == 1 ? (model == nullptr && L->fixed_model[0]) : (model && model->nopts == nm), "model selector", d.uri);
        const Widget* tube = find(*L, PORT_TUBE);
        check(tube && tube->nopts == d.nTubes, "tube selector size", d.uri);
        for (int t = 0; tube && t < d.nTubes; ++t)
            check(!strcmp(tube->opts[t], pentodeParams(d.tubes[t]).name), "tube names in sheet order", d.uri);
        const Widget* in = find(*L, PORT_INPUT);
        check(in && in->nopts == ni, "input selector size", d.uri);
        check(find(*L, PORT_OVERSAMPLING) && find(*L, PORT_TRIM) && find(*L, PORT_LEVEL), "setup controls", d.uri);
        const Widget* lv = find(*L, PORT_LEVEL);
        check(lv && lv->def == -6 && lv->min == -60 && lv->max == 12, "output level matches TTL", d.uri);
        const Widget* tr = find(*L, PORT_TRIM);
        check(tr && tr->def == 0 && tr->min == -36 && tr->max == 36, "input trim matches TTL", d.uri);
    }
    printf(failures ? "%d FAILED\n" : "GUI layouts match the plugin ports\n", failures);
    return failures ? 1 : 0;
}
