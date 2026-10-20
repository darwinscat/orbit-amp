// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Channels.h"
#include "../PresetStore.h"

namespace orbitamp::channels
{

/** A PRODUCT'S PRESETS. A preset is the whole amp — every channel as it was left, the one that was
    playing, the reverb, the delay, the cabinet, OUTPUT — in a file of the product's own, in a folder of
    its own: its chain is fixed, and a preset from OrbitAmp's looser one, or another product's, does
    not lie in it. The folder is the product's (`Darwin's Cat/<product>/Presets`), and so is the file's
    extension: its name in lower case, `.catpitamp` for the cockpit. The files are kept the way every amp
    of the family keeps them (PresetStore.h).

    INPUT is not in a preset. It is set to the guitar and the interface, not to a sound, and a preset
    that moved it would be undoing a setting made once for the room. Nor is PARK, where the product has
    it: whether the amp is in the sound at all is the moment's, not the preset's.

    Loading goes through the processor's history, so it is one undoable step and lands in the register
    that is playing. Which preset the sound came from, and a number that stands for that sound, are
    kept on the channels' node — so the name follows the sound through undo, the registers and a saved
    session, and "changed since" is a comparison of sounds, not a count of edits: turn a knob and turn
    it back, and the preset is whole again.

    No widgets in here. Message thread only. */
class Presets
{
public:
    Presets (AmpProcessor&, Channels&);

    /** The product's folder and its files: `Darwin's Cat/<product>/Presets`, `.<product in lower case>`. */
    PresetStore store() const;
    juce::StringArray names() const;
    bool exists (const juce::String& name) const;

    /** The preset this sound was loaded from or last saved as; empty when it is nobody's. */
    juce::String current() const;

    /** Whether the sound has moved away from that preset. False when there is none. */
    bool changed() const;

    bool load (const juce::String& name);
    bool save (const juce::String& name);
    bool rename (const juce::String& to);
    bool removeCurrent();

    /** Back to what a fresh product is: every control at its default, every channel's memory gone. */
    void init();

private:
    AmpProcessor& amp;
    Channels& channels;

    juce::String sound() const;
    void mark (const juce::String& name);
};

} // namespace orbitamp::channels
