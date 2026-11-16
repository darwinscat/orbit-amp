// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::underwater
{

/** WHAT THE FACE DRAWS WITH, laid out in the factory folder under Faces/DeepGrooveBass (CMakeLists.txt:
    DEEPGROOVEBASS_RENDERS), as the walker's are: the dashboard the world draws (view/UnderwaterArt.h), which the face
    paints itself whenever the world does not — the CPU's landscape, the still, a GPU that will not draw, a picture
    taken; the knobs and the faders' cap, renders of the family's; its mark and its MIDI list. A render that fails to
    decode leaves its image null, and its control draws a stand-in. */
struct FaceArt
{
    juce::Image bg;                 // the dashboard, its window transparent
    juce::Image knobDrive, knobTone;
    juce::Image faderCap;
    juce::Image none;               // a lamp the picture does not paint lit: drawn by code (Console::Places::lampDot)

    /** Decoded the first time the face asks, and kept. */
    static const FaceArt& of();

    /** The cabin's mark, as SVG: the build window wears it. Empty where it has none. */
    static juce::String mark();

    /** What its MIDI window lists (face/Midi.h). */
    static juce::String midiCommands();

    /** Whether its pictures are there at all: no dashboard, no face (ui/FactoryMissing.h). */
    static bool present();
};

} // namespace orbitamp::underwater
