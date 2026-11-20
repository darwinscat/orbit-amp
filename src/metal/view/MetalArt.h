// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MetalGl.h"

namespace orbitamp::metal
{

/** THE METAL WORLD'S ART AS THE INSTALLER LAYS IT OUT, read at run time, never built into the binary: the cabin's two
    plates (dashboard.webp, dashboard_flash.webp, made from catbay's renders as resources/metal/README.txt says, the
    face's own files) and world/ — the owner's meshes with their credits, the groves' guitars, the burning letters, the
    banners, the dragon (resources/metal/world/README.txt) — each found under the product's folder (Faces/MetalAmp in
    the factory folder, or the folder METAL_ART_DIR names). A picture is read and decoded by libwebp into straight RGBA
    when the GPU's half asks for it; a file that is not there leaves its part out. The word CENSORED, which the page
    draws on a canvas, is drawn here by JUCE. What a MetalDrive is made with: `MetalDrive (shippedArt())`. Only
    MetalArt.cpp knows JUCE; this header and what it hands over do not. */
MetalGl::Art shippedArt();

} // namespace orbitamp::metal
