#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Configures and builds with the Visual Studio toolchain from Git Bash.
set -e
VS="/c/Program Files/Microsoft Visual Studio/18/Community"
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cd "$(dirname "$0")"
[ -d build ] || "$CMAKE" -S . -B build -A x64 >/dev/null
"$CMAKE" --build build --config Release -- -v:m -nologo "$@"
