// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::io
{

/** WHAT THE FACE DRAWS WITH, laid out in the factory folder under Faces/IoBass (CMakeLists.txt: IOBASS_RENDERS), as
    the bathyscaphe's are: the day's dashboard, which the face paints itself whenever the world does not draw the cabin —
    until the Io world's port, always (the dark plate beside it is the world's, crossfaded by the Sun; the face never
    reads it); the knobs and the faders' cap, renders of the family's; its MIDI list. No mark of its own: the maker's
    plate wears the family's cat. A render that fails to decode leaves its image null, and its control draws a
    stand-in. */
struct FaceArt
{
    juce::Image bg;                 // the day's dashboard, its windscreen transparent
    juce::Image knobDrive, knobTone;
    juce::Image faderCap;
    juce::Image none;               // a lamp the picture does not paint lit: drawn by code (Console::Places::lampDot)

    /** Decoded the first time the face asks, and kept. */
    static const FaceArt& of();

    /** The cabin's mark, as SVG: the build window wears it. Empty where it has none — Io's, for now. */
    static juce::String mark();

    /** What its MIDI window lists (face/Midi.h). */
    static juce::String midiCommands();

    /** Whether its pictures are there at all: no dashboard, no face (ui/FactoryMissing.h). */
    static bool present();
};

} // namespace orbitamp::io
