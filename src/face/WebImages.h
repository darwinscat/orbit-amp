// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <vector>

/** THE PICTURES OF A FACE'S KIT FOR THE SITE (Console::exportForWeb): how every one of them is written —
    the cabin, the posters, the stills, every sprite — so that what the app writes is the kit the site
    reads, with no step between them.

    Each is a WebP, encoded with libwebp's own encoder as `cwebp -q 70 -m 6` encodes it: the colour plane
    lossy at `quality`, the slowest and smallest method. A picture with a hole in it — any pixel not wholly
    opaque: a sprite, the cabin with its window, a poster with its window empty — keeps its alpha, and the
    alpha plane lossless (`-alpha_q 100`), so a cut edge stays as it was drawn; a picture with none is
    written with no alpha plane at all (`-noalpha`). JUCE keeps its pictures premultiplied and WebP speaks
    straight colour: each pixel is unpremultiplied first, exactly as JUCE's own PNG writer did it.

    The pictures a page shows whole — the cabin and the posters — are written at THREE SIZES: as drawn (twice
    the stage, 3344 x 1882), at the stage's own (@1x, half of that) and at half the stage's (@half, a
    quarter), each scaled down with JUCE's high resampling quality. Sprites live in their own folder,
    `sprites/`, each a WebP under the name places.json's spriteBounds knows it by. */
namespace orbitamp::face::web
{

/** The colour plane's quality, every picture's but the half-size cabin's. */
inline constexpr float quality = 70.0f;

/** ...and the half-size cabin's, a little higher, as the kits packed by hand had it. */
inline constexpr float halfCabinQuality = 80.0f;

/** Whether a picture has a hole: a pixel anywhere in it not wholly opaque. */
bool hasHoles (const juce::Image&);

/** The picture as WebP bytes, as said above. Empty for an invalid picture, one larger than WebP holds, or
    an encoder's refusal. */
juce::MemoryBlock encode (const juce::Image&, float colourQuality = quality);

/** ...written to `file`, which it replaces, its folder made where it is not there yet. False when nothing
    was written. */
bool write (const juce::File& file, const juce::Image&, float colourQuality = quality);

/** The picture scaled to `width` x `height` with JUCE's high resampling quality — by halves, as far as
    halving goes, and the rest in one last step, so that no step shrinks by more than half: a renderer
    filters a step that size well (CoreGraphics' high interpolation, the software renderer's bilinear
    taps), and past it starts dropping pixels instead of averaging them. */
juce::Image scaled (const juce::Image&, int width, int height);

/** The names of a picture shown whole at the three sizes — `<stem>.webp`, `<stem>@1x.webp` and
    `<stem>@half.webp` — under the keys `<key>`, `<key>1x` and `<key>Half` (poster / poster1x / posterHalf,
    cabin / cabin1x / cabinHalf), as places.json carries them. */
juce::var threeNames (const juce::String& stem, const juce::String& key);

/** ...and the picture written at those three sizes into `dir`: as it is, at half its size and at a quarter
    of it — the half-size one at `halfQuality` — its names as threeNames gives them. */
juce::var writeThreeSizes (const juce::File& dir, const juce::String& stem, const juce::Image& full,
                           const juce::String& key, float halfQuality = quality);

/** Where a sprite of that name lies in the kit, as places.json names it: `sprites/<stem>.webp`. */
juce::String spriteFile (const juce::String& stem);

/** ...and a sprite written there, the name places.json gives it returned — or null, for places.json to carry
    instead of a name, where there is no picture to write (a render the skin has not) or it could not be
    written: the site's reader fails on a file named that is not there. */
juce::var writeSprite (const juce::File& dir, const juce::String& stem, const juce::Image&);

/** A frame read back from the GPU — RGBA, its rows bottom-up as GL keeps them — as an opaque picture, the
    right way up: what a live view's first frame goes under a poster's glass as. Invalid for no pixels. */
inline juce::Image fromGlRows (const std::vector<std::uint8_t>& rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < (size_t) width * (size_t) height * 4u)
        return {};
    juce::Image img (juce::Image::RGB, width, height, false);
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
    {
        const auto* row = rgba.data() + (size_t) (height - 1 - y) * (size_t) width * 4u;
        for (int x = 0; x < width; ++x)
            bd.setPixelColour (x, y, juce::Colour (row[x * 4], row[x * 4 + 1], row[x * 4 + 2]));
    }
    return img;
}

} // namespace orbitamp::face::web
