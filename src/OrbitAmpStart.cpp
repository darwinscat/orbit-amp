// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginProcessor.h"

namespace orbitamp
{

/** OrbitAmp's first start in a process: the player's IRs it kept in its own folder are gathered into the
    family's (gatherFormerIrs). Apart from the face, in a file of its own, so that a gate built on the face
    stands in one that does nothing and never reaches the machine's real folders. */
void firstStart()
{
    gatherFormerIrs();
}

} // namespace orbitamp
