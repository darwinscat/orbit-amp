// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace orbitamp::shaders
{

/** THE SHADERS' TEXT. Every GLSL program the faces draw with is in resources/shaders, a folder a world
    (plains, ice, titan, catpit) and what several of them share in common/, and is built into the binary
    (CMakeLists.txt, THE SHADERS). A file is a shader stage, or a piece of one, as the compiler reads it,
    with no #version: the C++ that compiles it puts that in front, with whatever it defines there.

    Two kinds of line are not GLSL. `#include "common/ih.glsl"` stands where that file's text goes,
    itself put together the same way; `#include <world>` where the C++ writes text of its own — the
    numbers a shader is written from (TitanGl.cpp). Either line, newline and all, is replaced by what it
    names, and every other byte is the file's own: a shader reads top to bottom as the compiler will.

    No JUCE: the bytes go in, the source comes out, so a build that carries the files some other way —
    the site's, in WebAssembly — puts the same shaders together the same way. */

/** Text the C++ writes into a shader, under the name the file asks for it by: `#include <name>`. */
struct Written
{
    std::string_view name;
    std::string text;
};

/** Finds a file by its path in resources/shaders ("plains/terrain.vert"); empty when there is none. */
using Find = std::string_view (*) (std::string_view path);

/** A shader's source out of text: the includes replaced, every other byte as it is. A file not found or
    a name not written, and the source is empty — which no driver compiles, so a shader that cannot be
    put together fails where it is built rather than drawing half of itself. */
std::string assemble (std::string_view text, Find find, const std::vector<Written>& written = {});

/** A file as this binary carries it, by its path in resources/shaders; empty when it carries none. */
std::string_view file (std::string_view path) noexcept;

/** A shader's source out of the file this binary carries at that path: assemble() over file(). */
std::string source (std::string_view path, const std::vector<Written>& written = {});

} // namespace orbitamp::shaders
