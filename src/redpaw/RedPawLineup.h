// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../channels/Lineup.h"

namespace orbitamp::redpaw
{

/** THE ROVER'S CHANNELS: each its own pack with its own memory (channels/Lineup.h) — and to the hand,
    gears of two. The gear is the pedal and TURBO is that pedal's second channel, so the channels go in
    pairs, gear by gear, TURBO off then on, and a Program Change counts them in that order. PARK, the
    lever's top end, is no channel: it takes the whole amp out of the sound, and the channel waits for
    the lever to come back. The engine they are kept by is the family's (channels/Engine.h); this file
    is what makes it the rover's.

    WHICH CHANNELS, AND SO HOW MANY GEARS, IS THE PRODUCT'S: each rover lists its own in a file of its
    own — RedPawAmp's in redpaw/RedPawSkin.cpp, FrostPawAmp's in frostpaw/FrostPawSkin.cpp — and
    whatever counts gears counts its pairs: the lever's notches, the keys, the programs, the nav screen's
    names, the window. */
const channels::Lineup& lineup();

/** A rover's lineup over the channels it plays: the names its memory is kept under, its CHANNEL, INPUT's
    zero, the cabinet a channel opens on and PARK, are every rover's. */
channels::Lineup roverLineup (std::vector<channels::Lineup::Channel>);

/** As many gears as the product has pairs of channels — at most as many as the lever's slot has
    notches. */
inline constexpr int maxGears = 3;
int numGears() noexcept;

/** A channel's gear, 0..numGears() - 1, and whether it is TURBO's. */
inline constexpr int  gearOf (int channel) noexcept  { return channel / 2; }
inline constexpr bool turboOf (int channel) noexcept { return channel % 2 == 1; }

/** The channel a gear and TURBO make. */
inline constexpr int channelOf (int gear, bool turbo) noexcept { return gear * 2 + (turbo ? 1 : 0); }

} // namespace orbitamp::redpaw
