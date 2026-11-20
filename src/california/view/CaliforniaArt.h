// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CaliforniaGl.h"

namespace orbitamp::california
{

/** THE VAN CABIN'S ART AND THE WORLD'S FILES AS THE INSTALLER LAYS THEM OUT, read at run time, never built into the
    binary: dashboard.webp (3344 x 1882, the windshield alpha 0), dashboard_pendant.webp and its .txt (the peace
    pendant, its box and pivot), label.webp (the CENSORED word), and finds/ — the sprites' and the signs', the flags'
    and the chalk's atlases with their tables, the meshes (CBMESH1) with their tables and textures — as
    resources/california/README.txt lists them, each found by face::renders::fileOf as product "PeacePhaseAmp"
    (Faces/PeacePhaseAmp in the factory folder, or the folder CALIFORNIA_ART_DIR names where it has the file). The
    prototype names its pictures .png; they are read from the .webp of the same name. Each is read and decoded (by
    libwebp, into straight RGBA) when the GPU's half asks for it, so nothing of them is held in between. A file that
    is not there leaves its part out. What a CaliforniaDrive is made with: `CaliforniaDrive (shippedArt())`; `wink`
    the optional chalks (on in the app). Only CaliforniaArt.cpp knows JUCE. */
CaliforniaGl::Art shippedArt (bool wink = true);

} // namespace orbitamp::california
