// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>
#include <synth/State.h>

#include <vector>

namespace orbitamp::device
{

/** THE BASS'S CABINETS AS A PLAYER PICKS THEM, by name: the dozen the build carries (THE BASS'S DOZEN, CMakeLists.txt) —
    twelve of cab-ir-generator's presets, fixed — and the player's own, kept as the same presets' files in `Bass Cabinets`
    in the family's folder (OrbitBass's SAVE AS…), beside `Bass IRs`, where the cabinets' own app can read and write them
    too. A cabinet's name says it as a person remembers it: its number in the dozen first — "7: 1x15 THUMP" (the owner,
    06.11) — and a player's own as the player named it. */
struct BassCabinets
{
    struct Cabinet
    {
        juce::String name;      // as shown: "7: 1x15 THUMP", or the player's own name
        cabir::State state;
        juce::File file;        // a player's own: its file; the dozen's: none
    };

    /** The dozen, in the build's order. */
    static const std::vector<Cabinet>& dozen();

    /** The player's own, by name — read from the folder each time asked. */
    static std::vector<Cabinet> own();

    /** Where the player's own are kept: `Bass Cabinets` in the family's folder, made on first look. */
    static juce::File folder();

    /** The dozen's slot that `state` is, or -1. */
    static int dozenOf (const cabir::State& state);

    /** The name of the cabinet `state` is, the dozen's or the player's own; empty for neither. */
    static juce::String nameOf (const cabir::State& state);

    /** The playing cabinet kept as the player's own under `name` — a cabinet of that name already there is replaced.
        Its file, or none where it could not be written. */
    static juce::File save (const juce::String& name, const cabir::State& state);

    /** A player's own rewritten as `state`, its name and style kept: EDIT… on one keeps every change in its file. */
    static bool keep (const juce::File& file, const cabir::State& state);

    /** A player's own renamed, or moved to the Trash. */
    static bool rename (const juce::File& file, const juce::String& name);
    static bool remove (const juce::File& file);

    /** A name as a file keeps it: no quotes, no backslashes, trimmed. */
    static juce::String cleaned (const juce::String& name);
};

} // namespace orbitamp::device
