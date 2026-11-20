// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IoGl.h"

namespace orbitamp::io
{

/** THE ROVER CABIN'S PLATES AND THE WORLD'S FINDS AS THE INSTALLER LAYS THEM OUT, read at run time, never built into the
    binary: dashboard.webp and dashboard_dark.webp (3344 x 1882, the day's and the dark's plates, their window the alpha;
    the face's own day plate is the same file) and finds/ — each <variant>3d folder's CBMESH1 mesh and its table, as
    resources/io/finds/README.txt lists them — each found by face::renders::fileOf as product "IoTideBass" (Faces/IoTideBass in the
    factory folder, or the folder IO_ART_DIR names where it has the file). The plates are read and decoded (by libwebp,
    into straight RGBA) when the GPU's half asks for them, so nothing of them is held in between. A file that is not there
    leaves its part out. What an IoDrive is made with: `IoDrive (shippedArt())`. Only IoArt.cpp knows JUCE. */
IoGl::Art shippedArt();

} // namespace orbitamp::io
