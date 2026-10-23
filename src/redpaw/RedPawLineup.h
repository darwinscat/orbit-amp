// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../channels/Lineup.h"

namespace orbitamp::redpaw
{

/** THE ROVER'S CHANNELS: each its own pack (channels/Lineup.h) — and to the hand, gears of two. The gear
    is the pedal and TURBO is that pedal's second channel, so the channels go in pairs, gear by gear,
    TURBO off then on, and a Program Change counts them in that order. A pair is ONE memory: TURBO is an
    option of the gear's, as its tone is — thrown, only the pack changes, and a gear pulled comes back
    with TURBO as it was left (Lineup::pairs). The lever's places are the gears and nothing else. The
    engine they are kept by is the family's (channels/Engine.h); this file is what makes it the rover's.

    WHICH CHANNELS, AND SO HOW MANY GEARS, IS THE PRODUCT'S: each rover says its own in a file of its
    own, in its skin (RedPawSkin.h) — RedPawAmp's the IR-X's in redpaw/RedPawSkin.cpp, FrostPawAmp's the
    ReVolt's in frostpaw/FrostPawSkin.cpp, AmberPawAmp's the V4's in amberpaw/AmberPawSkin.cpp, each a pack
    to a gear and TURBO its Boost — and whatever counts gears counts its pairs: the
    lever's notches, the keys, the programs, the nav screen's names, the window. */

/** A rover's lineup over the channels it plays: the names its memory is kept under, its CHANNEL, INPUT's
    zero and the cabinet a channel opens on, are every rover's. */
channels::Lineup roverLineup (std::vector<channels::Lineup::Channel>);

/** As many gears as the product has pairs of channels — at most as many as the lever's slot has
    notches. */
inline constexpr int maxGears = 3;
int numGears (const channels::Lineup&) noexcept;


/** A channel's gear, 0..numGears (lineup) - 1, and whether it is TURBO's. */
inline constexpr int  gearOf (int channel) noexcept  { return channel / 2; }
inline constexpr bool turboOf (int channel) noexcept { return channel % 2 == 1; }

/** The channel a gear and TURBO make. */
inline constexpr int channelOf (int gear, bool turbo) noexcept { return gear * 2 + (turbo ? 1 : 0); }

} // namespace orbitamp::redpaw
