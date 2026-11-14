# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Davidian
#
# Builds the plugins on Windows (MSVC), Linux (GCC or Clang) and macOS (Apple Clang):
# the LV2 bundle in <build>/lv2/ashgrove.lv2, and CLAP, VST3 and (macOS) AU plugins
# in <build>/plugins. LV2 GUI:
#   Windows: plugin + Win32 GUI (ui:WindowsUI)
#   Linux:   plugin + X11 GUI (ui:X11UI)
#   macOS:   plugin + Cocoa GUI (ui:CocoaUI)
cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest macOS the bundle runs on")
endif()
project(ashgrove VERSION 1.3.0 LANGUAGES C CXX)
# clap-wrapper builds Objective-C for the macOS formats; the languages must be enabled here.
if(APPLE)
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Everything ends up in a plugin library, including clap-wrapper's helper libraries.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
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
  set(UI_CLASS "CocoaUI")
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

# The GUI: plain C, drawn by its own software renderer into a native window. On macOS
# the window layer is Objective-C (manual retain/release) in the same file.
if(APPLE)
  set_source_files_properties(src/ui/ui_window.c PROPERTIES LANGUAGE OBJC COMPILE_OPTIONS "-fno-objc-arc")
endif()
if(WIN32)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c src/ui/ui_window.c)
  target_link_libraries(ashgrove_ui user32 gdi32)
elseif(APPLE)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c src/ui/ui_window.c)
  target_link_libraries(ashgrove_ui "-framework Cocoa")
else()
  find_package(X11 REQUIRED)
  add_library(ashgrove_ui MODULE src/ui/ui_lv2.c src/ui/ui_window.c)
  target_include_directories(ashgrove_ui PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(ashgrove_ui ${X11_LIBRARIES} m)
endif()
if(TARGET ashgrove_ui)
  set_target_properties(ashgrove_ui PROPERTIES C_STANDARD 11)
  bundle_library(ashgrove_ui)
  add_dependencies(ashgrove ashgrove_ui)
endif()

# Tests. host_check and ui_check use Windows APIs and ui_mac_check Cocoa; the rest build
# everywhere.
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
elseif(APPLE)
  add_executable(ui_mac_check tests/ui_mac_check.m)
  target_compile_options(ui_mac_check PRIVATE -fno-objc-arc -Wno-unused-function)
  target_link_libraries(ui_mac_check "-framework Cocoa")
endif()
# Loads the built plugin library directly and runs every plugin and cabinet.
add_executable(plugin_check tests/plugin_check.cpp)
target_link_libraries(plugin_check ampcore ${CMAKE_DL_LIBS})

# ---- CLAP, VST3 and AU ----
# One CLAP implementation (src/clap) is built as CLAP, VST3 and, on macOS, AUv2 by
# clap-wrapper. Configuring downloads clap-wrapper and the CLAP, VST3 and AudioUnit
# SDKs from GitHub at the pinned versions; turn this off to build LV2 only.
option(AG_PLUGIN_FORMATS "Build CLAP, VST3 and AU plugins (downloads their SDKs)" ON)
if(AG_PLUGIN_FORMATS)
  set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES ON CACHE BOOL "" FORCE)
  include(FetchContent)
  FetchContent_Declare(clap-wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
    GIT_TAG 806bba0a5f792e3966b99c1561ab9952d3316d7c)  # 0.17.0
  FetchContent_MakeAvailable(clap-wrapper)

  add_library(ashgrove_clap_impl STATIC src/clap/ashgrove_clap.cpp src/ui/ui_window.c)
  set_target_properties(ashgrove_clap_impl PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(ashgrove_clap_impl PUBLIC ampcore clap clap-wrapper-extensions)
  if(WIN32)
    target_link_libraries(ashgrove_clap_impl PUBLIC user32 gdi32)
  elseif(APPLE)
    target_link_libraries(ashgrove_clap_impl PUBLIC "-framework Cocoa")
  else()
    target_include_directories(ashgrove_clap_impl PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(ashgrove_clap_impl PUBLIC ${X11_LIBRARIES} m)
  endif()

  make_clapfirst_plugins(
    TARGET_NAME ashgrove_formats
    IMPL_TARGET ashgrove_clap_impl
    OUTPUT_NAME "Davidian Ashgrove"
    ENTRY_SOURCE src/clap/entry.cpp
    BUNDLE_IDENTIFIER "com.davidian.ashgrove"
    BUNDLE_VERSION ${PROJECT_VERSION}
    COPY_AFTER_BUILD FALSE
    WINDOWS_FOLDER_VST3 TRUE
    PLUGIN_FORMATS CLAP VST3 AUV2
    ASSET_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)

  # Loads the built .clap and runs every plugin through the CLAP interface.
  add_executable(clap_check tests/clap_check.cpp)
  target_link_libraries(clap_check ampcore clap ${CMAKE_DL_LIBS})
endif()
