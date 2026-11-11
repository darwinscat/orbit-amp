// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Lineup.h"

#include <GuitarDozen.h>   // the generated list of the guitar's TOP 12 (CMakeLists.txt: THE GUITAR'S CABINETS)

#include <vector>

namespace orbitamp::channels
{

/** THE GUITAR'S TOP 12 AS A PRODUCT'S CABINETS (the owner, 08.11): the guitar amps that are toys — the cockpit, the
    rovers — play Orbit Guitar Cab Builder's TOP 12, drawn, as the walker plays the bass's dozen: twelve slots, each one
    of the presets as it stands, CAB stepping through them, no window. Each channel opens on its own slot
    (Lineup::Channel::cabinet), the first unless it says another. */
inline std::vector<Lineup::Cabinet> guitarTopTwelve()
{
    std::vector<Lineup::Cabinet> slots;
    for (const auto* stem : guitarCabinets)
        slots.push_back ({ .preset = stem });
    return slots;
}

/** A lineup with the TOP 12 as its cabinets, drawn. */
inline Lineup withTopTwelve (Lineup line)
{
    line.drawnCabinets = true;
    line.cabinets = guitarTopTwelve();
    return line;
}

} // namespace orbitamp::channels
