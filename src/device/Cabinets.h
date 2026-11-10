// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CabTables.h"

#include <juce_core/juce_core.h>
#include <synth/State.h>

#include <vector>

namespace orbitamp::device
{

/** AN INSTRUMENT'S CABINETS AS A PLAYER PICKS THEM, by name: the twelve the build carries — the bass's dozen, the
    guitar's TOP 12 (CMakeLists.txt: THE BASS'S DOZEN, THE GUITAR'S CABINETS), cab-ir-generator's presets, fixed — all
    of its builder's presets by style and number, the typical ones last, and the player's own, kept as the same presets'
    files in the family's folder (`Bass Cabinets`, `Guitar Cabinets`), where the builder's own app can read and write them
    too. A cabinet's name says it as a person remembers it: its number among the twelve first — "7: 1x15 THUMP" (the
    owner, 06.11) — and a player's own as the player named it. */
class Cabinets
{
public:
    struct Cabinet
    {
        juce::String name;      // as shown: "7: 1x15 THUMP", or the player's own name
        cabir::State state;
        juce::File file;        // a player's own: its file; the build's: none
        juce::String style;     // all's: the preset's style, "Rock", "Typical"
    };

    /** The bass's, the guitar's. */
    static const Cabinets& of (CabKind);

    /** The twelve, in the build's order. */
    const std::vector<Cabinet>& dozen() const;

    /** ALL OF THEM (the owner, 06.11): every factory preset of its builder's and its typical cabinets, by style and
        number, the typical ones last — each named as its preset is, "ROCK · 8x10 CLASSIC", "TOP 12: 4x12 SELFISH GENE". */
    const std::vector<Cabinet>& all() const;
    int allOf (const cabir::State& state) const;

    /** The player's own, by name — read from the folder each time asked. */
    std::vector<Cabinet> own() const;

    /** Where the player's own are kept, in the family's folder, made on first look. */
    juce::File folder() const;

    /** The twelve's slot that `state` is, or -1. */
    int dozenOf (const cabir::State& state) const;

    /** The name of the cabinet `state` is — the twelve's, else one of all, else the player's own; empty for none. */
    juce::String nameOf (const cabir::State& state) const;

    /** The playing cabinet kept as the player's own under `name` — a cabinet of that name already there is replaced.
        Its file, or none where it could not be written. */
    juce::File save (const juce::String& name, const cabir::State& state) const;

    /** A player's own rewritten as `state`, its name and style kept: EDIT… on one keeps every change in its file. */
    bool keep (const juce::File& file, const cabir::State& state) const;

    /** A player's own renamed, or moved to the Trash. */
    bool rename (const juce::File& file, const juce::String& name) const;
    bool remove (const juce::File& file) const;

    /** A name as a file keeps it: no quotes, no backslashes, trimmed. */
    static juce::String cleaned (const juce::String& name);

private:
    explicit Cabinets (CabKind k) : kind (k) {}
    const CabKind kind;
};

} // namespace orbitamp::device
