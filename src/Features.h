// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHAT THE AMPS SHIP WITH, switched at compile time — one switch a feature, said here and nowhere else.

    HUM — the stage that takes mains hum off the input (felitronics::dehum, the processor's removeHum),
    its LEARN, its menus and its key — is OFF in every amp for now: it is still too raw to put in a
    player's hands. Its code stays, whole, so that switching it back on is this one line. While it is
    off, nothing of it reaches a player: no HUM in the input column's menus or the settings menu, no
    LEARN HUM key, no H key, no HUM tag, no HUM in a web kit; and the engine never prepares the stage,
    whatever the machine's settings or a saved session say — a session that played with HUM on plays
    without it. The machine's settings keep their keys (prefs::humMains and the rest) and are not
    touched, so a choice made before comes back with the switch.

    The hum gate builds the processor with the switch thrown (ORBITAMP_FEATURE_HUM=1, CMakeLists.txt:
    orbitamp_hum_test): it tests the stage, which has to stay ready for the day it comes back. Built as
    the products are, the same gate holds the switch itself (hum-off, in the gates binary). */
#ifndef ORBITAMP_FEATURE_HUM
 #define ORBITAMP_FEATURE_HUM 0
#endif

namespace orbitamp::features
{
inline constexpr bool hum = ORBITAMP_FEATURE_HUM != 0;
}
