// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitChannels.h"

namespace orbitamp::catpit
{

/** THE COCKPIT'S PRESETS. A preset is the whole amp — all four channels as each was left, the one that
    was playing, the reverb, the delay, the cabinet, OUTPUT — in a file of the cockpit's own, in a folder
    of its own: its chain is fixed, and a preset from OrbitAmp's looser one does not lie in it.

    INPUT is not in a preset. It is set to the guitar and the interface, not to a sound, and a preset
    that moved it would be undoing a setting made once for the room.

    Loading goes through the processor's history, so it is one undoable step and lands in the register
    that is playing. Which preset the sound came from, and a number that stands for that sound, are
    kept on the channels' node — so the name follows the sound through undo, the registers and a saved
    session, and "changed since" is a comparison of sounds, not a count of edits: turn a knob and turn
    it back, and the preset is whole again.

    No widgets in here. Message thread only. */
class Presets
{
public:
    static constexpr const char* extension = ".catpitamp";

    Presets (AmpProcessor&, Channels&);

    static juce::File directory();
    static juce::StringArray names();
    static bool exists (const juce::String& name);

    /** The preset this sound was loaded from or last saved as; empty when it is nobody's. */
    juce::String current() const;

    /** Whether the sound has moved away from that preset. False when there is none. */
    bool changed() const;

    bool load (const juce::String& name);
    bool save (const juce::String& name);
    bool rename (const juce::String& to);
    bool removeCurrent();

    /** Back to what a fresh cockpit is: every control at its default, every channel's memory gone. */
    void init();

private:
    AmpProcessor& amp;
    Channels& channels;

    static juce::File fileFor (const juce::String& name);
    juce::String sound() const;
    void mark (const juce::String& name);
};

} // namespace orbitamp::catpit
