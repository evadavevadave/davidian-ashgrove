#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
# Rebuilds, then runs the idle, host and spec checks. Results land in build/.
cd "$(dirname "$0")"
sh build.sh 2>&1 | grep -E ' error '
./build/Release/idle_check.exe > build/idle_results.txt 2>&1
AG_LV2_PATH="$(cygpath -w "$PWD/build/lv2")" ./build/Release/host_check.exe > build/host_results.txt 2>&1
echo "host_check exit=$?"
tail -n 40 build/host_results.txt
./build/Release/spec_test.exe > build/spec_results.txt 2>&1
echo "spec_test exit=$?"
grep -vE '^(CC100|CC50|SC410) ' build/spec_results.txt
./build/Release/ui_ports_check.exe
./build/Release/cab_check.exe | tail -1
./build/Release/plugin_check.exe build/lv2/ashgrove.lv2/ashgrove.dll | tail -1
./build/Release/ui_check.exe "$(cygpath -w build/lv2/ashgrove.lv2)" | tail -2
mkdir -p build/ui && ./build/Release/ui_render.exe build/ui
