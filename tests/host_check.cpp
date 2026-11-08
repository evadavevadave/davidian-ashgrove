// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Loads the bundle through Ardour's own LV2 library (lilv), checks each plugin
// validates, then instantiates and runs it with every model, tube and input.
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using W = void*;

int main() {
    SetDllDirectoryA("C:\\Program Files\\Ardour9\\bin");
    HMODULE L = LoadLibraryA("lilv-0.dll");
    if (!L) {
        printf("cannot load lilv-0.dll\n");
        return 2;
    }
#define F(ret, name, ...)                                  \
    auto name = (ret(*)(__VA_ARGS__))GetProcAddress(L, #name); \
    if (!name) {                                           \
        printf("missing %s\n", #name);                     \
        return 2;                                          \
    }
    F(W, lilv_world_new)
    F(W, lilv_new_string, W, const char*)
    F(W, lilv_new_file_uri, W, const char*, const char*)
    F(void, lilv_world_load_bundle, W, W)
    F(unsigned, lilv_plugins_size, W)
    F(void, lilv_world_set_option, W, const char*, W)
    F(void, lilv_world_load_all, W)
    F(W, lilv_world_get_all_plugins, W)
    F(W, lilv_plugins_begin, W)
    F(W, lilv_plugins_next, W, W)
    F(bool, lilv_plugins_is_end, W, W)
    F(W, lilv_plugins_get, W, W)
    F(bool, lilv_plugin_verify, W)
    F(W, lilv_plugin_get_uri, W)
    F(const char*, lilv_node_as_uri, W)
    F(W, lilv_plugin_get_name, W)
    F(const char*, lilv_node_as_string, W)
    F(unsigned, lilv_plugin_get_num_ports, W)
    F(void, lilv_plugin_get_port_ranges_float, W, float*, float*, float*)
    F(W, lilv_plugin_instantiate, W, double, const void* const*)
    F(W, lilv_plugin_get_bundle_uri, W)
    F(W, lilv_plugin_get_uis, W)
    F(unsigned, lilv_uis_size, W)
    F(W, lilv_uis_begin, W)
    F(W, lilv_uis_get, W, W)
    F(W, lilv_ui_get_binary_uri, W)
#undef F
    W world = lilv_world_new();
    if (const char* path = getenv("AG_LV2_PATH"))
        lilv_world_set_option(world, "http://drobilla.net/ns/lilv#lv2-path", lilv_new_string(world, path));
    lilv_world_load_all(world);
    if (const char* b = getenv("AG_BUNDLE")) lilv_world_load_bundle(world, lilv_new_file_uri(world, nullptr, b));
    W plugins = lilv_world_get_all_plugins(world);
    printf("plugins visible to lilv: %u\n", lilv_plugins_size(plugins));
    int found = 0, bad = 0;
    for (W it = lilv_plugins_begin(plugins); !lilv_plugins_is_end(plugins, it); it = lilv_plugins_next(plugins, it)) {
        W p = lilv_plugins_get(plugins, it);
        std::string uri = lilv_node_as_uri(lilv_plugin_get_uri(p));
        if (uri.rfind("urn:davidian:ashgrove-", 0) != 0) {
            printf("other plugin: %s\n", uri.c_str());
            continue;
        }
        ++found;
        const unsigned np = lilv_plugin_get_num_ports(p);
        std::vector<float> lo(np), hi(np), def(np);
        lilv_plugin_get_port_ranges_float(p, lo.data(), hi.data(), def.data());
        const int verified = lilv_plugin_verify(p) ? 1 : 0;
        printf("%s: verify=%d ports=%u name=%s\n", uri.c_str(), verified, np,
               lilv_node_as_string(lilv_plugin_get_name(p)));
        if (!verified) ++bad;
        printf("  bundle: %s\n", lilv_node_as_uri(lilv_plugin_get_bundle_uri(p)));
        {
            W uis = lilv_plugin_get_uis(p);
            const unsigned n = uis ? lilv_uis_size(uis) : 0;
            printf("  GUIs: %u%s%s\n", n, n ? ", binary " : "",
                   n ? lilv_node_as_uri(lilv_ui_get_binary_uri(lilv_uis_get(uis, lilv_uis_begin(uis)))) : "");
            if (n != 1) ++bad;
        }
        // A LilvInstance starts with the LV2_Descriptor pointer and the handle.
        struct Inst {
            const void* desc;
            void* handle;
        };
        struct Desc {
            const char* uri;
            void* inst;
            void (*connect)(void*, uint32_t, void*);
            void (*activate)(void*);
            void (*run)(void*, uint32_t);
            void (*deact)(void*);
            void (*cleanup)(void*);
            const void* (*ext)(const char*);
        };
        Inst* in = (Inst*)lilv_plugin_instantiate(p, 48000.0, nullptr);
        if (!in) {
            printf("  instantiate failed\n");
            ++bad;
            continue;
        }
        const Desc* d = (const Desc*)in->desc;
        std::vector<float> ctl(np, 0.0f), a(4800), b(4800);
        for (unsigned k = 0; k < np; ++k) ctl[k] = std::isnan(def[k]) ? 0.0f : def[k];
        d->connect(in->handle, 0, a.data());
        d->connect(in->handle, 1, b.data());
        for (unsigned k = 2; k < np; ++k) d->connect(in->handle, k, &ctl[k]);
        d->activate(in->handle);
        for (unsigned i = 0; i < a.size(); ++i) a[i] = 0.05f * std::sin(2 * 3.14159f * 440.0f * i / 48000.0f);
        double worst = 0;
        bool nan = false;
        int combos = 0;
        for (float m = lo[3]; m <= hi[3]; m += 1)
            for (float t = lo[4]; t <= hi[4]; t += 1)
                for (float s = lo[5]; s <= hi[5]; s += 1) {
                    ctl[3] = m;
                    ctl[4] = t;
                    ctl[5] = s;
                    double peak = 0;
                    for (int rep = 0; rep < 8; ++rep) {
                        d->run(in->handle, 4800);
                        if (rep >= 4)
                            for (float v : b) {
                                if (std::isnan(v) || std::isinf(v)) nan = true;
                                peak = std::fmax(peak, std::fabs(v));
                            }
                    }
                    if (peak < 1e-4) {
                        printf("  silent: model %g tube %g input %g\n", m, t, s);
                        ++bad;
                    }
                    worst = std::fmax(worst, peak);
                    ++combos;
                }
        // Stress: hard input with knobs and selectors moving, including 4x oversampling.
        srand(1234);
        for (unsigned i = 0; i < a.size(); ++i) a[i] = 1.0f * std::sin(2 * 3.14159f * 110.0f * i / 48000.0f);
        double stressPeak = 0;
        for (int rep = 0; rep < 60; ++rep) {
            for (unsigned k = 3; k < np; ++k) {
                if (k >= 3 && k <= 6 && rep % 12 != 0) continue;
                ctl[k] = lo[k] + (hi[k] - lo[k]) * float(rand()) / RAND_MAX;
                if (k <= 6) ctl[k] = std::round(ctl[k]);
            }
            d->run(in->handle, 1200);
            for (unsigned i = 0; i < 1200; ++i) {
                if (std::isnan(b[i]) || std::isinf(b[i])) nan = true;
                stressPeak = std::fmax(stressPeak, std::fabs(b[i]));
            }
        }
        printf("  stress: peak %.2f\n", stressPeak);
        if (stressPeak > 100.0) {
            printf("  stress output out of bounds\n");
            ++bad;
        }
        printf("  ran %d model/tube/input combinations, peak %.2f, latency %g, bad values: %s\n", combos, worst,
               ctl[2], nan ? "YES" : "none");
        if (nan) ++bad;
        d->cleanup(in->handle);
    }
    printf("plugins found: %d, problems: %d\n", found, bad);
    return (found == 5 && bad == 0) ? 0 : 1;
}
