// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "RedPawLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::redpaw
{

/** The rover's renders, decoded once from the copies the product built into its binary (RedPawSkin.h —
    resources/redpaw for RedPawAmp), each by its file name — so a render can be tried in place of the
    built-in one by naming its folder in the skin's variable (REDPAW_ART_DIR for RedPawAmp) and
    restarting (face/Renders.h). A render that fails to decode leaves its image null, and its control
    draws a stand-in.

    The dashboard is the picture with the windscreen already cut out; every control is a render of its
    own, drawn at its place: the knobs without a pointer (the pointer is drawn), the lever's T grip seen
    from above with its collar, the slider and fader caps, the roof's toggle up and down, the square keys
    and TURBO each up and pressed. */
struct Art
{
    juce::Image bg;                    // the dashboard, its windscreen transparent, its lamps out
    juce::Image knobDrive, knobTone;
    juce::Image lever;
    juce::Image leverGlow;             // a soft light the shape of the lever, white, `leverGlowPad` wider all round
    static constexpr int leverGlowPad = 26;    // in the lever render's own pixels
    juce::Image sliderCap, faderCap;
    juce::Image faderCapLight;         // the fader cap's metal lifted, for the small corner faders
    juce::Image toggleUp, toggleDown;
    juce::Image keyUp, keyDown;
    juce::Image turboUp, turboDown;
    juce::Image emblem;                // the maker's mark raised in the rover's own metal, where it has one; else it is engraved

    /** The lamps (layout::lamps), each as the dashboard painted it lit, over the box it is laid back
        into; what is not the lamp or its glow is transparent. In `bg` they are out. */
    std::array<juce::Image, layout::numLamps> lampLit;

    static const Art& get();
};

} // namespace orbitamp::redpaw
