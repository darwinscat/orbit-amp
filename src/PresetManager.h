// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PresetStore.h"

namespace orbitamp
{

/** OrbitAmp's user presets on disk: `.orbitamp` files in `Darwin's Cat/OrbitAmp/Presets`, each the whole
    state as presetForSaving writes it, kept the way every amp of the family keeps them (PresetStore.h).
    The menu that loads, saves, deletes and resets them is the toolbar's (ui/Chrome.cpp).

    Factory presets will live alongside these as embedded resources; there are none yet because
    there is nothing curated to ship. */
class PresetManager
{
public:
    static PresetStore store() { return { "OrbitAmp", ".orbitamp" }; }

    static juce::StringArray names()                                           { return store().names(); }
    static juce::ValueTree read (const juce::String& name)                     { return store().read (name); }
    static bool write (const juce::String& name, const juce::ValueTree& state) { return store().write (name, state); }
    static bool remove (const juce::String& name)                              { return store().remove (name); }
};

} // namespace orbitamp
