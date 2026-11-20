// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::metal
{

/** WHAT THE FACE DRAWS WITH, laid out in the factory folder under Faces/MetalAmp (CMakeLists.txt: METALAMP_RENDERS): the
    cabin's night plate, which the face paints whenever the world does not draw it (until the metal world comes, always);
    the knobs and the faders' cap, renders of the family's; its mark and its MIDI list. The flash plate ships beside the
    night one for the world's renderer, which crossfades the two with the lightning: the face never reads it. A render
    that fails to decode leaves its image null, and its control draws a stand-in. */
struct FaceArt
{
    juce::Image bg;                 // the night plate, its window transparent
    juce::Image knobDrive, knobTone;
    juce::Image faderCap;
    juce::Image none;               // a lamp the plate does not paint lit: drawn by code (Console::Places::lampDot)

    /** Decoded the first time the face asks, and kept. */
    static const FaceArt& of();

    /** The cabin's mark, as SVG: the build window wears it. Empty where it has none. */
    static juce::String mark();

    /** What its MIDI window lists (face/Midi.h). */
    static juce::String midiCommands();

    /** Whether its pictures are there at all: no plate, no face (ui/FactoryMissing.h). */
    static bool present();
};

} // namespace orbitamp::metal
