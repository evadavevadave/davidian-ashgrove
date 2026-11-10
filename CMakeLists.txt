# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Builds the ashgrove.lv2 bundle on Windows (MSVC), Linux (GCC or Clang) and macOS
# (Apple Clang). The bundle lands in <build>/lv2/ashgrove.lv2.
#   Windows: plugin + Win32 GUI (ui:WindowsUI)
#   Linux:   plugin + X11 GUI (ui:X11UI)
#   macOS:   plugin only; hosts show their own controls
cmake_minimum_required(VERSION 3.20)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS the bundle runs on")
endif()
project(ashgrove_lv2 VERSION 1.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/O2 /fp:fast /W3 /utf-8)
  add_compile_definitions(_USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # A static runtime means the plugin needs no Visual C++ redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
  # Fast maths, but NaN and infinity checks must keep working.
  add_compile_options(-O3 -ffast-math -fno-finite-math-only -Wall -fvisibility=hidden)
endif()

if(WIN32)
  set(LIB_SUFFIX ".dll")
  set(UI_CLASS "WindowsUI")
elseif(APPLE)
  set(LIB_SUFFIX ".dylib")
  set(UI_CLASS "none")
else()
  set(LIB_SUFFIX ".so")
  set(UI_CLASS "X11UI")
endif()

set(BUNDLE ${CMAKE_BINARY_DIR}/lv2/ashgrove.lv2)

# Puts a library straight into the bundle, for single- and multi-config generators.
function(bundle_library target)
  set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ${LIB_SUFFIX})
  foreach(cfg "" _RELEASE _DEBUG _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY${cfg} ${BUNDLE}
      LIBRARY_OUTPUT_DIRECTORY${cfg} ${BUNDLE})
  endforeach()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Hosts may be built against an older C++ runtime than the build machine's.
    target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc -Wl,--no-undefined)
  endif()
endfunction()

add_library(ampcore STATIC src/amp.cpp)
target_include_directories(ampcore PUBLIC src)
set_target_properties(ampcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
  target_link_libraries(ampcore PUBLIC m)
endif()

add_library(ashgrove MODULE src/plugin.cpp)
target_link_libraries(ashgrove ampcore)
bundle_library(ashgrove)

add_executable(gen_ttl tools/gen_ttl.cpp)
target_link_libraries(gen_ttl ampcore)
add_custom_command(TARGET ashgrove POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BUNDLE}
  COMMAND gen_ttl ${BUNDLE} ${LIB_SUFFIX} ${UI_CLASS})
add_dependencies(ashgrove gen_ttl)

# The GUI: plain C, drawn by its own software renderer into a native window.
if(WIN32)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c)
  target_link_libraries(ashgrove_ui user32 gdi32)
elseif(NOT APPLE)
  find_package(X11 REQUIRED)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c)
  target_include_directories(ashgrove_ui PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(ashgrove_ui ${X11_LIBRARIES} m)
endif()
if(TARGET ashgrove_ui)
  set_target_properties(ashgrove_ui PROPERTIES C_STANDARD 11)
  bundle_library(ashgrove_ui)
  add_dependencies(ashgrove ashgrove_ui)
endif()

# Tests. host_check and ui_check use Windows APIs; the rest build everywhere.
add_executable(spec_test tests/spec_test.cpp)
target_link_libraries(spec_test ampcore)
add_executable(idle_check tests/idle_check.cpp)
target_link_libraries(idle_check ampcore)
add_executable(cab_check tests/cab_check.cpp)
target_link_libraries(cab_check ampcore)
add_executable(ui_render tests/ui_render.c)
set_target_properties(ui_render PROPERTIES C_STANDARD 11)
if(NOT WIN32)
  target_link_libraries(ui_render m)
endif()
add_executable(ui_ports_check tests/ui_ports_check.cpp)
target_link_libraries(ui_ports_check ampcore)
if(WIN32)
  add_executable(host_check tests/host_check.cpp)
  add_executable(ui_check tests/ui_check.c)
  set_target_properties(ui_check PROPERTIES C_STANDARD 11)
  target_link_libraries(ui_check user32 gdi32)
endif()
# Loads the built plugin library directly and runs every plugin and cabinet.
add_executable(plugin_check tests/plugin_check.cpp)
target_link_libraries(plugin_check ampcore ${CMAKE_DL_LIBS})
