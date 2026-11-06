// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::redgroove
{

/** THE WALKER'S RENDERS, decoded once from its own folder in the family's factory folder (Faces/RedGrooveBass,
    laid out from resources/redgroove — and, where a render of its own is not drawn yet, the helicopter's from
    resources/amberpaw: CMakeLists.txt, REDGROOVEBASS_RENDERS), each by its file name; a folder named in
    REDGROOVE_ART_DIR is read instead (face/Renders.h). A render that fails to decode leaves its image null,
    and its control draws a stand-in.
    The dashboard is the picture with the dome transparent and every socket, slot and hole empty; every
    control is drawn over it — the knobs from renders of their own, without a pointer (the pointer is drawn). */
struct Art
{
    juce::Image bg;                 // the dashboard, its dome transparent
    juce::Image knobDrive, knobTone;
    juce::Image faderCap;           // the columns' faders' cap
    juce::Image none;               // a lamp the picture does not paint lit: drawn by code (Console::Places::lampDot)

    /** Decoded the first time the walker asks, and kept. */
    static const Art& of();

    /** THE STILL the walker ships: one frame of its world, at twice the dome's size, that the dome shows.
        Decoded when asked, never held here; invalid where it has none. */
    static juce::Image still();

    /** THE WALKER'S MARK, as SVG — the build window wears it. Empty where it has none. */
    static juce::String mark();

    /** What its MIDI window lists (face/Midi.h). */
    static juce::String midiCommands();

    /** Whether its pictures are there at all: no dashboard, no face (ui/FactoryMissing.h). */
    static bool present();
};

} // namespace orbitamp::redgroove
