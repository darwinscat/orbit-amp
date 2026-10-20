// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "RedPawLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::redpaw
{

/** The rover's renders, decoded once from the product's own folder in the family's factory folder
    (Faces/<product>, laid out from resources/redpaw for RedPawAmp), each by its file name — so a render
    can be tried in place of the laid-out one by naming its folder in the skin's variable (REDPAW_ART_DIR
    for RedPawAmp) and restarting (face/Renders.h). A render that fails to decode leaves its image null, and its control
    draws a stand-in.

    Each is as tools/bake-art made it, drawn as it is. The dashboard is the picture with the windscreen
    already cut out, corrected to the face as it stands now — its rims, its notches, its corner plates —
    and its lit lamps cut out of it; every control is a render of its own, drawn at its place: the knobs
    without a pointer (the pointer is drawn), the lever's T grip seen from above with its collar, the
    slider and fader caps, the roof's toggle up and down, the square keys and TURBO each up and pressed. */
struct Skin;

struct Art
{

    juce::Image bg;                    // the dashboard, its windscreen transparent, its lamps out
    juce::Image knobDrive, knobTone;
    juce::Image lever;
    juce::Image leverGlow;             // a soft light the shape of the lever, white, `leverGlowPad` wider all round
    static constexpr int leverGlowPad = 26;    // in the lever render's own pixels, as bake-art made it
    juce::Image sliderCap, faderCap;
    juce::Image faderCapLight;         // the fader cap's metal lifted, for the small corner faders
    juce::Image toggleUp, toggleDown;
    juce::Image keyUp, keyDown;
    juce::Image turboUp, turboDown;
    juce::Image turboLid;              // TURBO's hatch shut, over layout::turboLid, where the rover has one; else it stays open
    juce::Image emblem;               // the maker's mark raised in the rover's own metal, where it has one; else it is engraved

    /** The lamps (layout::lamps), each as the dashboard painted it lit, over the box it is laid back
        into; what is not the lamp or its glow is transparent. In `bg` they are out. None where the
        picture paints no lamp lit: the binary carries none. */
    std::array<juce::Image, layout::numLamps> lampLit;

    /** A rover's, by its skin: decoded the first time that rover asks, and kept for each rover. */
    static const Art& of (const Skin&);

    /** THE STILL the rover ships: one frame of its world, chosen and kept, that its windscreen shows in
        STILL instead of rendering one. Decoded when asked, never held here; invalid where it has none. */
    static juce::Image still (const Skin&);

    /** THE ROVER'S MARK, as SVG: its vehicle seen from the front, as its icon draws it — the build window
        wears it. Empty where the product has none. */
    static juce::String mark (const Skin&);

};

} // namespace orbitamp::redpaw
