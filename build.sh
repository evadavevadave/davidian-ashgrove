#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Configures and builds with the Visual Studio toolchain from Git Bash.
set -e
VS="/c/Program Files/Microsoft Visual Studio/18/Community"
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cd "$(dirname "$0")"
# LV2 only by default. AG_PLUGIN_FORMATS=ON also builds CLAP and VST3, which downloads
# clap-wrapper and the CLAP and VST3 SDKs from GitHub.
"$CMAKE" -S . -B build -A x64 -DAG_PLUGIN_FORMATS=${AG_PLUGIN_FORMATS:-OFF} >/dev/null
"$CMAKE" --build build --config Release -- -v:m -nologo "$@"
