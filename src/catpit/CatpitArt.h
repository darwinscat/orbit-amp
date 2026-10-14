// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "CatpitLayout.h"

namespace orbitamp::catpit
{

/** The cockpit's renders, decoded once at startup from the copies built into the binary
    (resources/catpit), already at the size the window can need at its largest on a retina screen.
    A render that fails to decode leaves its image null, and its control draws a stand-in. */
struct Art
{
    juce::Image bg;            // the cockpit, its windshield cut out to transparent
    juce::Image knobDrive;     // the big knurled knob, no pointer — the pointer is drawn
    juce::Image knobTone;      // the ringed knob, no pointer
    juce::Image leverGrip;     // the throttle's T-grip, rebuilt with the bar reaching further left of its stem
    float leverAxis = 0.5f;    // where that stem stands across the grip, 0 = its left end
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

/** Where a render's opaque part sits — its alpha bounding box, in image pixels. The renders carry
    uneven transparent margins, so anything that is centred or scaled goes by this, not the canvas. */
juce::Rectangle<float> opaqueBounds (const juce::Image&);

/** Draws `img`'s opaque part fitted into `area` (aspect kept, centred), at `alpha`. */
void drawFitted (juce::Graphics&, const juce::Image& img, juce::Rectangle<float> area, float alpha = 1.0f);

} // namespace orbitamp::catpit
