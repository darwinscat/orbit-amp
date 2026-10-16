// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Prefs.h"

#include <array>

namespace orbitamp::prefs
{

/** HUM IN A MENU — a switch, then the mode. Every menu that offers the hum offers it in these four
    lines, in this order: "On", ticked unless HUM is OFF, which flips OFF and back as the H key does;
    then 50 Hz, 60 Hz and AUTO, the tick on the mode in force — or, under OFF, on the mode OFF
    bypasses, which is the one the stage still runs. OFF is not a mode in the list: the switch is how
    it is reached. Picking a mode under OFF switches the hum on in it. Each surface sets the lines in
    its own case and adds them with its own hands; what they say and do is here, once. */
struct HumLine
{
    juce::String text;
    bool ticked   = false;
    bool isSwitch = false;      // the "On" line
    Hum  mode     = Hum::off;   // what a mode line chooses
};

/** The four lines, `heard` as for humLabel (AmpProcessor::humHeard). */
inline std::array<HumLine, 4> humMenu (int heard)
{
    const Hum now  = hum();
    const Hum runs = now == Hum::off ? humLastOn() : now;
    return { { { "On", now != Hum::off, true, Hum::off },
               { humLabel (Hum::hz50, heard),      runs == Hum::hz50,      false, Hum::hz50 },
               { humLabel (Hum::hz60, heard),      runs == Hum::hz60,      false, Hum::hz60 },
               { humLabel (Hum::automatic, heard), runs == Hum::automatic, false, Hum::automatic } } };
}

/** What a picked line does: the switch flips, a mode is chosen — and with it the hum is on. */
inline void humPick (const HumLine& line) { setHum (line.isSwitch ? humFlip() : line.mode); }

/** Where the hum stands, in a menu's title: OFF, or the mode in force as its line says it. */
inline juce::String humState (int heard) { return hum() == Hum::off ? juce::String ("Off") : humLabel (hum(), heard); }

} // namespace orbitamp::prefs
