# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
#
# THE WORLDS FOR THE SITE: the windscreens' GPU views, the app's own C++ built to WebAssembly, so the site
# draws what the plugin draws and a change to one is a change to both. A project of its own, built with
# emscripten and nothing else — no JUCE, no fetching:
#
#     source ~/emsdk/emsdk_env.sh
#     emcmake cmake -S tools/worlds-wasm -B build-wasm -G Ninja
#     cmake --build build-wasm            # -> build-wasm/plains-wasm.mjs + plains-wasm.wasm
#
# plains-wasm.mjs is the ES module the page imports, createPlainsView (plains-view.js); the .wasm beside it
# is fetched by it. The renderer and the world are src/redpaw/view's PlainsGl and PlainsWorld over WebGL 2
# (src/shaders/Gpu.h); the shaders are resources/shaders, carried into the .wasm the way the app's build
# carries them (CMakeLists.txt, THE SHADERS) and put together by the same src/shaders/Shaders.cpp.

cmake_minimum_required(VERSION 3.22)
project(OrbitAmpWorldsWasm LANGUAGES CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "tools/worlds-wasm is built with emscripten: emcmake cmake -S tools/worlds-wasm -B build-wasm")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(ORBITAMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ORBITAMP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# ----------------------------------------------------------------------------
# THE SHADERS, as the app's build has them: every file in resources/shaders, its path written beside it in
# the same order (ShaderPaths.h), and the bytes under the names src/shaders/Shaders.cpp reads binary data by
# (ShaderData.h — the app's is juce_add_binary_data's). An edited file is carried from the next build on.
# ----------------------------------------------------------------------------
set(ORBITAMP_SHADER_DIR "${ORBITAMP_ROOT}/resources/shaders")
file(GLOB_RECURSE ORBITAMP_SHADER_FILES CONFIGURE_DEPENDS
    "${ORBITAMP_SHADER_DIR}/*.glsl" "${ORBITAMP_SHADER_DIR}/*.vert" "${ORBITAMP_SHADER_DIR}/*.frag")
list(SORT ORBITAMP_SHADER_FILES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ORBITAMP_SHADER_FILES})

set(_paths "")
set(_bytes "")
set(_names "")
set(_files "")
set(_sizes "")
set(_data "")
set(_index 0)
foreach(file IN LISTS ORBITAMP_SHADER_FILES)
    file(RELATIVE_PATH path "${ORBITAMP_SHADER_DIR}" "${file}")
    get_filename_component(name "${file}" NAME)
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" length)
    math(EXPR size "${length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(APPEND _paths "    \"${path}\",\n")
    string(APPEND _bytes "    inline constexpr unsigned char file${_index}[] = { ${hex}0 };\n")
    string(APPEND _names "        \"file${_index}\",\n")
    string(APPEND _files "        \"${name}\",\n")
    string(APPEND _sizes "        ${size},\n")
    string(APPEND _data "        (const char*) bytes::file${_index},\n")
    math(EXPR _index "${_index} + 1")
endforeach()
list(LENGTH ORBITAMP_SHADER_FILES _count)

file(CONFIGURE OUTPUT "${ORBITAMP_GENERATED_DIR}/ShaderPaths.h" @ONLY CONTENT
"// Written by tools/worlds-wasm/CMakeLists.txt (THE SHADERS): the paths in resources/shaders of the files ShaderData carries, in its order.
#pragma once
namespace orbitamp::shaders
{
inline constexpr const char* embeddedPaths[] =
{
@_paths@};
}
")
file(CONFIGURE OUTPUT "${ORBITAMP_GENERATED_DIR}/ShaderData.h" @ONLY CONTENT
"// Written by tools/worlds-wasm/CMakeLists.txt (THE SHADERS): the files' bytes, by the names binary data has.
#pragma once
#include <cstring>
namespace ShaderData
{
namespace bytes
{
@_bytes@}
inline constexpr int namedResourceListSize = @_count@;
inline const char* namedResourceList[] =
{
@_names@};
inline const char* originalFilenames[] =
{
@_files@};
inline const int sizes[] =
{
@_sizes@};
inline const char* const data[] =
{
@_data@};
inline int indexOf (const char* name)
{
    for (int i = 0; i < namedResourceListSize; ++i)
        if (std::strcmp (namedResourceList[i], name) == 0)
            return i;
    return -1;
}
inline const char* getNamedResource (const char* name, int& size)
{
    const int i = indexOf (name);
    size = i < 0 ? 0 : sizes[i];
    return i < 0 ? nullptr : data[i];
}
inline const char* getNamedResourceOriginalFilename (const char* name)
{
    const int i = indexOf (name);
    return i < 0 ? nullptr : originalFilenames[i];
}
}
")

# ----------------------------------------------------------------------------
# THE PLAINS: RedPawAmp's windscreen.
# ----------------------------------------------------------------------------
add_executable(plains_wasm
    plains.cpp
    "${ORBITAMP_ROOT}/src/redpaw/view/PlainsGl.cpp"
    "${ORBITAMP_ROOT}/src/redpaw/view/PlainsWorld.cpp"
    "${ORBITAMP_ROOT}/src/shaders/Shaders.cpp")
target_include_directories(plains_wasm PRIVATE "${ORBITAMP_ROOT}/src" "${ORBITAMP_GENERATED_DIR}")
target_compile_options(plains_wasm PRIVATE -O3 -Wall -Wextra)
set(_view_js "${CMAKE_CURRENT_SOURCE_DIR}/plains-view.js")
target_link_options(plains_wasm PRIVATE
    -O3
    -sENVIRONMENT=web,worker
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPlainsModule
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB
    -sFILESYSTEM=0
    "-sEXPORTED_FUNCTIONS=_plains_make,_plains_build,_plains_context,_plains_create,_plains_error,_plains_gear,_plains_turbo,_plains_park,_plains_settle,_plains_frame,_plains_draw,_plains_test,_plains_read,_plains_read_width,_plains_read_height,_plains_destroy"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString"
    "--extern-post-js=${_view_js}")
set_property(TARGET plains_wasm APPEND PROPERTY LINK_DEPENDS "${_view_js}")
set_target_properties(plains_wasm PROPERTIES OUTPUT_NAME "plains-wasm" SUFFIX ".mjs")
