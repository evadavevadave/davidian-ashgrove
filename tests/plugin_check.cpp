// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Loads the built plugin library the way an LV2 host does (by its lv2_descriptor
// entry point) and runs every plugin with every cabinet at two sample rates.
// Needs no LV2 host or library, so it runs on Windows, Linux and macOS alike.
// Usage: plugin_check <path to ashgrove.dll / .so / .dylib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "lv2_abi.h"
#include "ports.h"

#ifdef _WIN32
#include <windows.h>
static void* open_lib(const char* p) { return (void*)LoadLibraryA(p); }
static void* symbol(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void* open_lib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* symbol(void* lib, const char* name) { return dlsym(lib, name); }
#endif

using namespace ashgrove;

typedef const LV2_Descriptor* (*DescFn)(uint32_t);

static int failures = 0;

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: plugin_check <plugin library>\n");
        return 2;
    }
    void* lib = open_lib(argv[1]);
    if (!lib) {
        printf("FAIL cannot load %s\n", argv[1]);
        return 1;
    }
    DescFn get = (DescFn)symbol(lib, "lv2_descriptor");
    if (!get) {
        printf("FAIL no lv2_descriptor entry point\n");
        return 1;
    }
    int n = 0;
    while (get(uint32_t(n))) ++n;
    if (n != F_COUNT) {
        printf("FAIL expected %d plugins, found %d\n", int(F_COUNT), n);
        ++failures;
    }
    const double rates[] = {44100.0, 96000.0};
    for (int f = 0; f < n && f < F_COUNT; ++f) {
        const LV2_Descriptor* d = get(uint32_t(f));
        const FamilyDef* def = nullptr;
        for (int k = 0; k < F_COUNT; ++k)
            if (!strcmp(families()[k].uri, d->URI)) def = &families()[k];
        if (!def) {
            printf("FAIL unknown plugin %s\n", d->URI);
            ++failures;
            continue;
        }
        const int nports = cabinetPort(*def) + 1;
        for (double fs : rates) {
            for (int cab = 0; cab < CAB_COUNT; ++cab) {
                // Controls at their defaults, apart from the cabinet.
                std::vector<float> ctl(size_t(nports), 0.0f);
                ctl[8] = -6.0f;
                for (size_t k = 0; k < def->ports.size(); ++k) ctl[PORT_FIRST_KNOB + k] = float(def->ports[k].def);
                ctl[size_t(cabinetPort(*def))] = float(cab);
                LV2_Handle h = d->instantiate(d, fs, "", nullptr);
                if (!h) {
                    printf("FAIL %s: instantiate at %.0f Hz\n", d->URI, fs);
                    ++failures;
                    continue;
                }
                const int block = 256, blocks = int(fs * 0.5) / block;
                std::vector<float> in(block), out(block);
                float latency = 0;
                d->connect_port(h, 0, in.data());
                d->connect_port(h, 1, out.data());
                d->connect_port(h, 2, &latency);
                for (int p = 3; p < nports; ++p) d->connect_port(h, uint32_t(p), &ctl[size_t(p)]);
                if (d->activate) d->activate(h);
                double peak = 0.0;
                bool finite = true;
                long t = 0;
                for (int b = 0; b < blocks; ++b) {
                    for (int i = 0; i < block; ++i, ++t)  // A 110 Hz note at 100 mV.
                        in[size_t(i)] = float(0.1 * std::sin(2 * 3.14159265358979323846 * 110.0 * double(t) / fs));
                    d->run(h, uint32_t(block));
                    for (float v : out) {
                        if (!std::isfinite(v)) finite = false;
                        if (b > blocks / 2) peak = std::fmax(peak, std::fabs(double(v)));
                    }
                }
                if (d->deactivate) d->deactivate(h);
                d->cleanup(h);
                const bool ok = finite && peak > 1e-3 && peak < 4.0 && latency >= 0 && latency <= 1024;
                if (!ok) ++failures;
                printf("%s %-36s %6.0f Hz  %-13s peak %.3f, latency %.0f\n", ok ? "ok  " : "FAIL", d->URI, fs,
                       cabinetNames()[cab], peak, latency);
            }
        }
    }
    printf(failures ? "%d FAILED\n" : "All plugin checks passed\n", failures);
    return failures ? 1 : 0;
}
