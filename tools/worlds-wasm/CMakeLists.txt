# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
#
# THE WORLDS FOR THE SITE: the windscreens' GPU views, the app's own C++ built to WebAssembly, so the site
# draws what the plugin draws and a change to one is a change to both. A project of its own, built with
# emscripten and nothing else — no JUCE, no fetching:
#
#     source ~/emsdk/emsdk_env.sh
#     emcmake cmake -S tools/worlds-wasm -B build-wasm -G Ninja
#     cmake --build build-wasm            # -> build-wasm/plains-wasm.mjs + .wasm, ice-wasm.*, titan-wasm.*, sky-wasm.*, walker-wasm.*,
#                                         #    underwater-wasm.*, io-wasm.*
#
# Each world is an ES module the page imports — plains-wasm.mjs exports createPlainsView (plains-view.js),
# ice-wasm.mjs createIceView (ice-view.js), titan-wasm.mjs createTitanView (titan-view.js), sky-wasm.mjs
# CatpitAmp's createCatpitSky (sky-view.js), walker-wasm.mjs RedGrooveBass's createWalkerView (walker-view.js),
# underwater-wasm.mjs the bass amp's createUnderwaterView (underwater-view.js), io-wasm.mjs the SansAmp RBI bass's
# createIoView (io-view.js), each behind the one view of view.js —
# and the .wasm beside it is fetched by it. The renderers and the worlds are the app's (src/redpaw/view,
# src/frostpaw/view, src/amberpaw/view, src/catpit, src/redgroove/view, src/underwater/view, src/io/view) over WebGL 2
# (src/shaders/Gpu.h); the shaders are
# resources/shaders, carried into the .wasm the way the app's build carries them (CMakeLists.txt, THE SHADERS)
# and put together by the same src/shaders/Shaders.cpp.

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
# A WORLD: its own file of calls (plains.cpp, ...) over the app's world and renderer, built to <name>-wasm.mjs
# + .wasm; view.js at the end of the module, `JS` after it (files the build writes for a world), the world's
# <name>-view.js last. Every world exports the calls view.h names; `EXTRA` are a world's own beyond them.
# ----------------------------------------------------------------------------
set(ORBITAMP_VIEW_CALLS
    _view_make _view_build _view_context _view_create _view_error _view_gear _view_turbo _view_settle
    _view_frame _view_draw _view_test _view_read _view_read_width _view_read_height _view_destroy)

function(orbitamp_world name factory)
    cmake_parse_arguments(W "" "" "SOURCES;EXTRA;JS" ${ARGN})
    add_executable(${name}_wasm ${name}.cpp ${W_SOURCES} "${ORBITAMP_ROOT}/src/shaders/Shaders.cpp")
    target_include_directories(${name}_wasm PRIVATE "${ORBITAMP_ROOT}/src" "${ORBITAMP_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(${name}_wasm PRIVATE -O3 -Wall -Wextra)
    set(calls ${ORBITAMP_VIEW_CALLS} ${W_EXTRA})
    list(JOIN calls "," calls)
    set(view "${CMAKE_CURRENT_SOURCE_DIR}/view.js")
    set(world_view "${CMAKE_CURRENT_SOURCE_DIR}/${name}-view.js")
    set(world_js "")
    foreach(js IN LISTS W_JS)
        list(APPEND world_js "--extern-post-js=${js}")
    endforeach()
    target_link_options(${name}_wasm PRIVATE
        -O3
        -sENVIRONMENT=web,worker
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=${factory}
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB
        -sFILESYSTEM=0
        "-sEXPORTED_FUNCTIONS=${calls}"
        "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString"
        "--extern-post-js=${view}"
        ${world_js}
        "--extern-post-js=${world_view}")
    set_property(TARGET ${name}_wasm APPEND PROPERTY LINK_DEPENDS "${view}" ${W_JS} "${world_view}")
    set_target_properties(${name}_wasm PROPERTIES OUTPUT_NAME "${name}-wasm" SUFFIX ".mjs")
endfunction()

# THE PLAINS: RedPawAmp's windscreen.
orbitamp_world(plains createPlainsModule SOURCES
    "${ORBITAMP_ROOT}/src/redpaw/view/PlainsGl.cpp" "${ORBITAMP_ROOT}/src/redpaw/view/PlainsWorld.cpp")

# THE ICE: FrostPawAmp's windscreen.
orbitamp_world(ice createIceModule SOURCES
    "${ORBITAMP_ROOT}/src/frostpaw/view/IceGl.cpp" "${ORBITAMP_ROOT}/src/frostpaw/view/IceWorld.cpp")

# TITAN: AmberPawAmp's canopy — with the wipers' keys and the searchlights' switches of its own.
orbitamp_world(titan createTitanModule SOURCES
    "${ORBITAMP_ROOT}/src/amberpaw/view/TitanGl.cpp" "${ORBITAMP_ROOT}/src/amberpaw/view/TitanWorld.cpp"
    EXTRA _view_wipers _view_lights)

# THE SKY: CatpitAmp's space flight — with the lever's travel and the glass's place of its own.
orbitamp_world(sky createSkyModule SOURCES
    "${ORBITAMP_ROOT}/src/catpit/CatpitSkyGl.cpp"
    EXTRA _view_throttle _view_place)

# THE WALKER: RedGrooveBass's dome over the glowing forest — with the bass it hears of its own. Its world is matched to
# its prototype to the last bit, compiled without contraction as the app's build compiles it (a fused multiply-add
# rounds once where the JS rounds twice; WebAssembly has none to fuse into, and this says so where a compiler might one
# day find one).
orbitamp_world(walker createWalkerModule SOURCES
    "${ORBITAMP_ROOT}/src/redgroove/view/WalkerGl.cpp" "${ORBITAMP_ROOT}/src/redgroove/view/WalkerWorld.cpp"
    EXTRA _view_hear)
set_source_files_properties("${ORBITAMP_ROOT}/src/redgroove/view/WalkerWorld.cpp" PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# THE SEA FLOOR: the bass amp's bathyscaphe — with the bass it hears, PARK, the cabin and the art the page hands it of
# its own. Its world and its matrices are matched to their prototype number for number with the walker's JS built-ins
# (WalkerWorld.cpp), compiled without contraction as the app's build compiles them.
orbitamp_world(underwater createUnderwaterModule SOURCES
    "${ORBITAMP_ROOT}/src/underwater/view/UnderwaterGl.cpp" "${ORBITAMP_ROOT}/src/underwater/view/UnderwaterWorld.cpp"
    "${ORBITAMP_ROOT}/src/redgroove/view/WalkerWorld.cpp"
    EXTRA _view_hear _view_park _view_cabin _view_picture _view_sprites _view_art)
set_source_files_properties("${ORBITAMP_ROOT}/src/underwater/view/UnderwaterGl.cpp" "${ORBITAMP_ROOT}/src/underwater/view/UnderwaterWorld.cpp"
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# IO: the SansAmp RBI bass's rover cabin — with the bass it hears, PARK, the amp's three knobs as the world's inputs, the
# cabin's dark said back, the cabin, the dosimeter's needle and the files the page hands it of its own. Its files are
# read at run time, never built in, as in the app: the page serves resources/io as it is shipped, and the module knows
# its list (io-files.js, written here from that folder: each file's path and size, the README.txt files left out). Its
# world and its matrices are matched to their prototype number for number with the walker's JS built-ins, compiled
# without contraction as the app's build compiles them; the cabin's Glass is the sea's.
set(ORBITAMP_IO_FILES_DIR "${ORBITAMP_ROOT}/resources/io")
file(GLOB_RECURSE ORBITAMP_IO_FILES CONFIGURE_DEPENDS RELATIVE "${ORBITAMP_IO_FILES_DIR}" "${ORBITAMP_IO_FILES_DIR}/*")
list(FILTER ORBITAMP_IO_FILES EXCLUDE REGEX "(^|/)README\\.txt$")
list(SORT ORBITAMP_IO_FILES)
set(_io_files "")
foreach(file IN LISTS ORBITAMP_IO_FILES)
    file(SIZE "${ORBITAMP_IO_FILES_DIR}/${file}" size)
    string(APPEND _io_files "    ['${file}', ${size}],\n")
endforeach()
file(CONFIGURE OUTPUT "${ORBITAMP_GENERATED_DIR}/io-files.js" @ONLY CONTENT
"// Written by tools/worlds-wasm/CMakeLists.txt (IO): the Io world's files as the app ships them (resources/io), each
// path under that folder with its size in bytes.
const IO_FILES = [
@_io_files@];
")
orbitamp_world(io createIoModule SOURCES
    "${ORBITAMP_ROOT}/src/io/view/IoGl.cpp" "${ORBITAMP_ROOT}/src/io/view/IoWorld.cpp" "${ORBITAMP_ROOT}/src/io/view/IoFrame.cpp"
    "${ORBITAMP_ROOT}/src/underwater/view/UnderwaterWorld.cpp" "${ORBITAMP_ROOT}/src/redgroove/view/WalkerWorld.cpp"
    JS "${ORBITAMP_GENERATED_DIR}/io-files.js"
    EXTRA _view_hear _view_park _view_cabin _view_needle _view_knob _view_dark _view_name _view_picture _view_file _view_art)
set_source_files_properties("${ORBITAMP_ROOT}/src/io/view/IoGl.cpp" "${ORBITAMP_ROOT}/src/io/view/IoWorld.cpp" "${ORBITAMP_ROOT}/src/io/view/IoFrame.cpp"
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
