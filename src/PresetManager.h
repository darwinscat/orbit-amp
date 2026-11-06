// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PresetStore.h"
#include "Product.h"

namespace orbitamp
{

/** The workstation's user presets on disk — OrbitAmp's and OrbitBass's, each in its own folder:
    `.orbitamp` files in `Darwin's Cat/<Product>/Presets`, each the whole state as presetForSaving writes
    it, kept the way every amp of the family keeps them (PresetStore.h). The menu that loads, saves,
    deletes and resets them is the toolbar's (ui/Chrome.cpp).

    Factory presets will live alongside these as embedded resources; there are none yet because
    there is nothing curated to ship. */
class PresetManager
{
public:
    static PresetStore store (const Product& p) { return { p.name, ".orbitamp" }; }

    static juce::StringArray names (const Product& p)                                           { return store (p).names(); }
    static juce::ValueTree read (const Product& p, const juce::String& name)                     { return store (p).read (name); }
    static bool write (const Product& p, const juce::String& name, const juce::ValueTree& state) { return store (p).write (name, state); }
    static bool remove (const Product& p, const juce::String& name)                              { return store (p).remove (name); }
};

} // namespace orbitamp
