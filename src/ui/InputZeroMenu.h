// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Prefs.h"

#include <vector>

namespace orbitamp::prefs
{

/** THE INPUT'S ZERO AS EVERY AMP OFFERS IT (prefs::inputZero): one section of its settings, under one name,
    the same values in the same order — the highest first, the one in force ticked — whatever the face
    draws it with: OrbitAmp's SETUP window as a row (ui/SetupPanel.cpp), the cockpit's and the rovers'
    settings menu and their INPUT column's menu as items (face/Console.cpp). Picking one is the machine's
    setting, not an edit: the hand's control stays where it is, the engine plays it over the new zero
    from its next block (AmpProcessor::inputTrimDb), and there is no step to undo. */
inline const juce::String inputZeroTitle { "INPUT'S ZERO" };

/** A zero as the menus write it: "+12 dB", "0 dB", "-9 dB". */
inline juce::String inputZeroWords (int db) { return (db > 0 ? "+" : "") + juce::String (db) + " dB"; }

struct InputZeroLine
{
    int          db;
    juce::String text;
    bool         ticked;
};

/** The section's lines, the highest zero first, the one in force ticked. */
inline std::vector<InputZeroLine> inputZeroMenu (const Product& p)
{
    std::vector<InputZeroLine> lines;
    const int now = inputZero (p);
    for (int db = inputZeroHighest; db >= inputZeroLowest; db -= inputZeroStep)
        lines.push_back ({ db, inputZeroWords (db), db == now });
    return lines;
}

inline void inputZeroPick (const Product& p, const InputZeroLine& line) { setInputZero (p, line.db); }

/** The title with the zero in force beside it, where the section is a submenu: "INPUT'S ZERO  -9 dB". */
inline juce::String inputZeroState (const Product& p) { return inputZeroTitle + "  " + inputZeroWords (inputZero (p)); }

} // namespace orbitamp::prefs
