// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CatpitLayout.h"

namespace orbitamp::catpit
{

/** The environment variable that names a folder to try new renders from (face/Renders.h). */
inline constexpr const char* artFolderVariable = "CATPIT_ART_DIR";

/** The cockpit's renders, decoded once at startup from its own folder in the family's factory folder
    (Faces/CatpitAmp, laid out from resources/catpit), as tools/bake-art made them: the panel corrected
    and its lamps cut out, the grip rebuilt, each already at the size the window can need at its largest
    on a retina screen. A render that fails to decode leaves its image null, and its control draws a
    stand-in. */
struct Art
{
    juce::Image bg;            // the cockpit, its windshield cut out to transparent
    juce::Image knobDrive;     // the big knurled knob, no pointer — the pointer is drawn
    juce::Image knobTone;      // the ringed knob, no pointer
    juce::Image socket;        // a tone knob's socket and ring: drawn as many times as the pack has tones
    juce::Image leverGrip;     // the throttle's T-grip, rebuilt with the bar reaching further left of its stem (layout::gripAxis)
    juce::Image guardClosed, guardOpen, guardOn;
    juce::Image sliderCap;

    /** The lamps (layout::lamps), each as the render painted it lit, over the box it is laid back into;
        what is not the lamp or its glow is transparent. In `bg` they are out. */
    std::array<juce::Image, layout::numLamps> lampLit;

    static const Art& get();
};

/** THE SKY, STANDING STILL: the GPU sky at a channel's speed, taken the way a long exposure takes it —
    a single frame of that sky holds few stars one can see, most of them smaller than a pixel and seen
    only because they move, so these carry more stars, none thinner than a pixel, on trails long enough
    to say how fast the channel flies. The glass's whole rectangle (layout::glass) at twice the render's
    size. Decoded when asked for rather than at startup — only one of the four is ever on the glass, and
    only when the stars are switched off. */
juce::Image stillSky (int channel);

} // namespace orbitamp::catpit
