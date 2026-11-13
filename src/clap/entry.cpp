// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// The exported CLAP entry point. clap-wrapper compiles this file into the CLAP, VST3
// and AU builds, each linked to the same implementation in ashgrove_clap.cpp.
#include <clap/clap.h>

#include "ashgrove_clap.h"

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION, ag_clap_init, ag_clap_deinit, ag_clap_get_factory};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
