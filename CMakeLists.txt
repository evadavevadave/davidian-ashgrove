# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
cmake_minimum_required(VERSION 3.20)
project(ashgrove_lv2 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/O2 /fp:fast /arch:AVX2 /W3 /utf-8)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # A static runtime means the plugin needs no Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
  add_compile_options(-O3 -ffast-math -Wall -fvisibility=hidden)
endif()

add_library(ampcore STATIC src/amp.cpp)
target_include_directories(ampcore PUBLIC src)
set_target_properties(ampcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(BUNDLE ${CMAKE_BINARY_DIR}/lv2/ashgrove.lv2)
add_library(ashgrove MODULE src/plugin.cpp)
target_link_libraries(ashgrove ampcore)
set_target_properties(ashgrove PROPERTIES
  PREFIX ""
  RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BUNDLE}
  LIBRARY_OUTPUT_DIRECTORY_RELEASE ${BUNDLE})

add_executable(gen_ttl tools/gen_ttl.cpp)
target_link_libraries(gen_ttl ampcore)
add_custom_command(TARGET ashgrove POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE}
  COMMAND gen_ttl ${BUNDLE})
add_dependencies(ashgrove gen_ttl)

add_executable(spec_test tests/spec_test.cpp)
target_link_libraries(spec_test ampcore)
if(WIN32)
  add_executable(host_check tests/host_check.cpp)
endif()

add_executable(idle_check tests/idle_check.cpp)
target_link_libraries(idle_check ampcore)

# The GUI: plain C, drawn by its own software renderer into a Win32 window.
if(WIN32)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c)
  set_target_properties(ashgrove_ui PROPERTIES
    PREFIX ""
    C_STANDARD 11
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${BUNDLE}
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${BUNDLE})
  target_link_libraries(ashgrove_ui user32 gdi32)
  add_dependencies(ashgrove ashgrove_ui)

  add_executable(ui_check tests/ui_check.c)
  set_target_properties(ui_check PROPERTIES C_STANDARD 11)
  target_link_libraries(ui_check user32 gdi32)
endif()
add_executable(ui_render tests/ui_render.c)
set_target_properties(ui_render PROPERTIES C_STANDARD 11)
add_executable(ui_ports_check tests/ui_ports_check.cpp)
target_link_libraries(ui_ports_check ampcore)

