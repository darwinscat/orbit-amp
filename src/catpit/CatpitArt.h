// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::catpit
{

/** The cockpit's renders, read from disk once at startup. A PROTOTYPE's art: the files live in a
    private folder (CATPIT_ART_DIR), not in this repository, so swapping one and restarting is the
    whole loop. A file that is missing leaves its image null, and its control draws a stand-in. */
struct Art
{
    juce::Image bg;            // the cockpit, its windshield cut out to transparent
    juce::Image knobDrive;     // the big knurled knob, no pointer — the pointer is drawn
    juce::Image knobTone;      // the ringed knob, no pointer
    juce::Image leverGrip;     // the throttle's T-grip, rebuilt with the bar reaching further left of its stem
    float leverAxis = 0.5f;    // where that stem stands across the grip, 0 = its left end
    juce::Image guardClosed, guardOpen, guardOn;
    juce::Image sliderCap;

    static const Art& get();
};

/** Where a render's opaque part sits — its alpha bounding box, in image pixels. The renders carry
    uneven transparent margins, so anything that is centred or scaled goes by this, not the canvas. */
juce::Rectangle<float> opaqueBounds (const juce::Image&);

/** Draws `img`'s opaque part fitted into `area` (aspect kept, centred), at `alpha`. */
void drawFitted (juce::Graphics&, const juce::Image& img, juce::Rectangle<float> area, float alpha = 1.0f);

} // namespace orbitamp::catpit
