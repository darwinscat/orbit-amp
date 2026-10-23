// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cstddef>

/** HOW A FACE'S RENDERS ARE FOUND: by their file names, in the product's own folder of the family's
    factory folder, `Faces/<product>` (device/FactorySet.h) — laid out there by the installer, never built
    into the binary. What a face draws is its renders as they are — each corrected where the picture was
    painted otherwise than the face stands now, its lit lamps cut out of it, its derived sprites made,
    once, by tools/bake-art, which writes them as lossless WebP — and decoded here to exactly the pixels
    that tool made.

    Each face also names an environment variable, `folderVariable`: where it names a folder that has a
    render, that file is read instead, so a new render can be tried by restarting rather than rebuilding
    (tools/bake-art --out bakes into such a folder). */
namespace orbitamp::face::renders
{

/** A product's own pictures, by its name: `Faces/<product>` in the factory folder this build reads. Every
    function here takes the product whose pictures it reads, by its name, first. */
juce::File folder (const juce::String& product);

/** The file a render is read from: the one in the folder `folderVariable` names, where that has it, and
    the product's own in the factory otherwise — which is not there when the factory is not. */
juce::File fileOf (const juce::String& product, const char* name, const char* folderVariable);

/** Whether there is a render of that name to read: a picture only some products carry, or none at all
    with the factory folder missing. */
bool has (const juce::String& product, const char* name, const char* folderVariable);

/** A render by its file name, as it was made — a WebP by decodeWebp, any other format as JUCE decodes
    it. Invalid when it is not there. */
juce::Image decode (const juce::String& product, const char* name, const char* folderVariable);

/** ...and the same, as ARGB, ready to be drawn from. */
juce::Image load (const juce::String& product, const char* name, const char* folderVariable);

/** ...and a file of text the face reads among its pictures — its MIDI sheet, its mark — as UTF-8; empty
    when it is not there. */
juce::String text (const juce::String& product, const char* name, const char* folderVariable);

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
