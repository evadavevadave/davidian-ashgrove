// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Minimal declarations of the stable LV2 core C ABI, so the plugin and GUI build
// with no external dependency. Layout matches the LV2 specification's LV2_Descriptor.
// Usable from C (the GUI) and C++ (the plugin).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
#define AG_EXTERN_C extern "C"
#else
#define AG_EXTERN_C
#endif
#ifdef _WIN32
#define AG_EXPORT AG_EXTERN_C __declspec(dllexport)
#else
#define AG_EXPORT AG_EXTERN_C __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif
typedef void* LV2_Handle;

typedef struct LV2_Feature {
    const char* URI;
    void* data;
} LV2_Feature;

typedef struct LV2_Descriptor {
    const char* URI;
    LV2_Handle (*instantiate)(const struct LV2_Descriptor* descriptor, double sample_rate,
                              const char* bundle_path, const LV2_Feature* const* features);
    void (*connect_port)(LV2_Handle instance, uint32_t port, void* data_location);
    void (*activate)(LV2_Handle instance);
    void (*run)(LV2_Handle instance, uint32_t sample_count);
    void (*deactivate)(LV2_Handle instance);
    void (*cleanup)(LV2_Handle instance);
    const void* (*extension_data)(const char* uri);
} LV2_Descriptor;
#ifdef __cplusplus
}
#endif
