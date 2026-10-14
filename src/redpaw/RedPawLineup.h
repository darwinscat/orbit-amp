// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../channels/Lineup.h"

namespace orbitamp::redpaw
{

/** THE ROVER'S SIX CHANNELS: the Victory V4's six packs, each its own channel with its own memory
    (channels/Lineup.h) — and to the hand, three gears of two. The gear is the pedal — CLEAN the
    Copper, CRUNCH the Sheriff, HI-GAIN the Kraken — and TURBO is that pedal's second channel: the
    Copper TB, the Sheriff's Ch2, the Kraken's Ch2. So the channels go in pairs, gear by gear, TURBO off
    then on, and a Program Change counts them in that order. PARK, the lever's fourth position, is no
    channel: it takes the whole amp out of the sound, and the channel waits for the lever to come back. The engine they are kept by is the
    family's (channels/Engine.h); this file is what makes it the rover's. */
const channels::Lineup& lineup();

inline constexpr int numGears = 3;

/** A channel's gear, 0..2, and whether it is TURBO's. */
inline constexpr int  gearOf (int channel) noexcept  { return channel / 2; }
inline constexpr bool turboOf (int channel) noexcept { return channel % 2 == 1; }

/** The channel a gear and TURBO make. */
inline constexpr int channelOf (int gear, bool turbo) noexcept { return gear * 2 + (turbo ? 1 : 0); }

} // namespace orbitamp::redpaw
