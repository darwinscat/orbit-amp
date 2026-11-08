// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Cabinets.h"

namespace orbitamp::device
{

/** THE BASS'S CABINETS AS A PLAYER PICKS THEM: the bass's Cabinets (Cabinets.h) — its dozen (THE BASS'S DOZEN,
    CMakeLists.txt), all of Orbit Bass Cab Builder's presets, and the player's own in `Bass Cabinets`, beside `Bass IRs` —
    as the bass amps have always asked for them. */
struct BassCabinets
{
    using Cabinet = Cabinets::Cabinet;

    static const Cabinets& bass() { return Cabinets::of (CabKind::bass); }

    static const std::vector<Cabinet>& dozen()                       { return bass().dozen(); }
    static const std::vector<Cabinet>& all()                         { return bass().all(); }
    static int allOf (const cabir::State& state)                     { return bass().allOf (state); }
    static std::vector<Cabinet> own()                                { return bass().own(); }
    static juce::File folder()                                       { return bass().folder(); }
    static int dozenOf (const cabir::State& state)                   { return bass().dozenOf (state); }
    static juce::String nameOf (const cabir::State& state)           { return bass().nameOf (state); }
    static juce::File save (const juce::String& name, const cabir::State& state) { return bass().save (name, state); }
    static bool keep (const juce::File& file, const cabir::State& state)         { return bass().keep (file, state); }
    static bool rename (const juce::File& file, const juce::String& name)        { return bass().rename (file, name); }
    static bool remove (const juce::File& file)                      { return bass().remove (file); }
    static juce::String cleaned (const juce::String& name)           { return Cabinets::cleaned (name); }
};

} // namespace orbitamp::device
