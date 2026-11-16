// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "UnderwaterGl.h"

namespace orbitamp::underwater
{

/** THE CABIN'S ART AS THE BINARY CARRIES IT (resources/underwater; CMakeLists.txt, UnderwaterArtData): the
    bathyscaphe's dashboard (3344 x 1882, its window alpha 0) and its sea layer, the passing DC-745's atlas of 32
    headings, each a lossless WebP of the render in resources/underwater/source, decoded by libwebp into straight
    RGBA when the GPU's half asks for it — so nothing of them is held in between — and the sprites' table as its
    text. What an UnderwaterDrive is made with: `UnderwaterDrive (shippedArt())`. No JUCE in here. */
UnderwaterGl::Art shippedArt();

/** A WebP's pixels as straight RGBA, the top row first; an invalid picture when the bytes are not one. */
UnderwaterGl::Picture decodeWebp (const void* data, size_t size);

} // namespace orbitamp::underwater
