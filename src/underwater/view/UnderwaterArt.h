// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "UnderwaterGl.h"

namespace orbitamp::underwater
{

/** THE CABIN'S ART AS THE INSTALLER LAYS IT OUT, read at run time, never built into the binary: dashboard.webp
    (3344 x 1882, its window alpha 0), sea.webp (its sea layer) and atlas.webp (the passing DC-745's 32 headings),
    made from the renders in resources/underwater/source as resources/underwater/README.txt says, and sprites.txt,
    the atlas's table — each found by face::renders::fileOf as product "DeepGrooveBass" (Faces/DeepGrooveBass in the
    factory folder, or the folder UNDERWATER_ART_DIR names where it has the file). Each picture is read and decoded by
    libwebp into straight RGBA when the GPU's half asks for it, so nothing of them is held in between. A file that is
    not there leaves its part out — no dashboard or sea layer, no cabin (the face paints its own); no atlas or table,
    no passing bathyscaphe. What an UnderwaterDrive is made with: `UnderwaterDrive (shippedArt())`. Only
    UnderwaterArt.cpp knows JUCE; this header and what it hands over do not. */
UnderwaterGl::Art shippedArt();

/** A WebP's pixels as straight RGBA, the top row first; an invalid picture when the bytes are not one. */
UnderwaterGl::Picture decodeWebp (const void* data, size_t size);

} // namespace orbitamp::underwater
