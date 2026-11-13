// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Davidian
// Entry functions of the CLAP implementation (ashgrove_clap.cpp), exported by
// entry.cpp in each plugin format clap-wrapper builds.
#pragma once

bool ag_clap_init(const char* plugin_path);
void ag_clap_deinit();
const void* ag_clap_get_factory(const char* factory_id);
