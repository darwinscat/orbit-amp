// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cstddef>

/** HOW A FACE'S RENDERS ARE FOUND: by their file names. Each face keeps its renders in a binary-data
    namespace of its own; this is what it does with them, said once. What a face draws is its renders as
    they are — each corrected where the picture was painted otherwise than the face stands now, its lit
    lamps cut out of it, its derived sprites made, once, by tools/bake-art, which writes them as lossless
    WebP — and decoded here to exactly the pixels that tool made. */
namespace orbitamp::face::renders
{

/** Where a product's renders are built into its binary — the three things juce_add_binary_data makes. */
struct Binary
{
    const char* const* names;               // namedResourceList
    int count;                              // namedResourceListSize
    const char* (*originalName) (const char*);              // getNamedResourceOriginalFilename
    const char* (*data) (const char*, int& size);           // getNamedResource
};

/** A render by its file name, as it was made — a WebP by decodeWebp, any other format as JUCE decodes it:
    the copy built into the binary — or, when the environment variable `folderVariable` names a folder
    that has one, the file from there, so a new render can be tried by restarting rather than rebuilding
    (tools/bake-art --out bakes into such a folder). */
juce::Image decode (const char* name, const Binary&, const char* folderVariable);

/** ...and the same, as ARGB, ready to be drawn from. */
juce::Image load (const char* name, const Binary&, const char* folderVariable);

/** HOW A STRAIGHT COLOUR IS PREMULTIPLIED HERE: times its alpha over 255, rounded to the nearest — so that
    every premultiplied value up to its alpha is some straight value's, and a picture tools/bake-art took
    from what a face drew comes back as exactly what it drew. JUCE's own (PixelARGB::premultiply) never
    reaches the alpha itself from an alpha of 128 up, and a white glow lives there. */
inline juce::uint8 premultiplied (juce::uint8 value, juce::uint8 alpha) noexcept
{
    return alpha == 255 ? value : (juce::uint8) (((unsigned) value * alpha + 127u) / 255u);
}

/** WebP bytes as an ARGB juce::Image, premultiplied as above; invalid when they are not one picture. */
juce::Image decodeWebp (const void* data, size_t size);

} // namespace orbitamp::face::renders
