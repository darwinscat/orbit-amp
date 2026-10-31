// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../core/TunerStrobe.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace orbitamp::strobeview
{

/** HOW A STROBE IS DRAWN — the one painter every tuner face draws its strobe with (core::TunerStrobe): the
    OrbitAmp strip, the cockpit's screen and the rovers' strip as bands of stripes, the Titan dial as an old
    strobe tuner's disc. Dumb: the strobe's state and the ink in, pixels out.

    THE INK is the needle's own — green when the string is home, warming to orange off it — so the strobe
    says everything the needle said by its colour, and its stillness says the rest. Under a HUM label a
    face hands it lilac, dimmed as the needle is: a block of dimmed orange is not a line but a wash, and
    rots to brick on the violet panel. A band's contrast is its partial's strength against the strongest; a
    band moving faster than the face's frames can show is drawn as the blur a real strobe's window shows —
    in `wash`, never the ink — not as stripes that alias. Sharp moves right, and the disc clockwise, as the
    needle does. */

/** A band's stripes, as seen this frame: how much of them is stripes (the rest a smear), times its strength. */
struct Look
{
    float stripes = 0.0f, smear = 0.0f;
};

inline Look lookOf (const core::TunerStrobe& s, int k) noexcept
{
    // Stripes per second this band's pattern passes at: a 30 Hz frame shows half a stripe's step at 15,
    // where which way they move can no longer be told. From 8 to 14 the stripes fade into their own average.
    const float rate   = std::abs (s.speed (k)) * (float) core::TunerStrobe::partials[(size_t) k];
    const float sharp  = juce::jlimit (0.0f, 1.0f, 1.0f - (rate - 8.0f) / 6.0f);
    const float weight = juce::jlimit (0.0f, 1.0f, (s.strength (k) - 0.05f) / 0.45f);
    return { weight * sharp, weight * (1.0f - sharp) * 0.5f };
}

/** The bands stacked in `area`, the fundamental at the bottom and two octaves on top — the top of a
    strobe's window is its disc's outer, busiest ring. `period` is the fundamental's stripe, in pixels;
    `well` the band's own dim ground, drawn whether a note is heard or not; `live` whether one is. */
inline void paintBands (juce::Graphics& g, juce::Rectangle<float> area, const core::TunerStrobe& s, bool live,
                        juce::Colour ink, juce::Colour well, juce::Colour wash, float period, float gap = 2.0f)
{
    const int n = core::TunerStrobe::numBands;
    const float h = (area.getHeight() - gap * (float) (n - 1)) / (float) n;

    for (int k = 0; k < n; ++k)
    {
        const auto band = juce::Rectangle<float> (area.getX(), area.getBottom() - (float) (k + 1) * h - (float) k * gap,
                                                  area.getWidth(), h);
        g.setColour (well);
        g.fillRect (band);
        if (! live)
            continue;

        const auto look = lookOf (s, k);
        if (look.smear > 0.0f)
        {
            g.setColour (wash.withMultipliedAlpha (look.smear));
            g.fillRect (band);
        }
        if (look.stripes <= 0.0f)
            continue;

        const float p   = period / (float) core::TunerStrobe::partials[(size_t) k];
        const float off = std::fmod (s.stripes (k) * period, p);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (band.toNearestInt());
        g.setColour (ink.withMultipliedAlpha (look.stripes));
        for (float x = band.getX() + off - p; x < band.getRight(); x += p)
            g.fillRect (juce::Rectangle<float> (x, band.getY(), p * 0.5f, band.getHeight()));
    }
}

/** The disc: three rings about `centre` between radii `inner` and `outer` — the fundamental innermost
    with `segments` segments, its octaves outside it with twice and four times as many — seen through
    the sector `sweep` radians either side of straight up, and turning by the strobe's phase. */
inline void paintDisc (juce::Graphics& g, juce::Point<float> centre, float inner, float outer, float sweep,
                       const core::TunerStrobe& s, bool live, juce::Colour ink, juce::Colour well, juce::Colour wash,
                       int segments = 12, float gap = 2.0f)
{
    const int n = core::TunerStrobe::numBands;
    const float w = (outer - inner - gap * (float) (n - 1)) / (float) n;
    const auto ringArea = [&centre] (float r) { return juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (centre); };

    for (int k = 0; k < n; ++k)
    {
        const float r0 = inner + (float) k * (w + gap), r1 = r0 + w;

        juce::Path window;
        window.addPieSegment (ringArea (r1), -sweep, sweep, r0 / r1);
        g.setColour (well);
        g.fillPath (window);
        if (! live)
            continue;

        const auto look = lookOf (s, k);
        if (look.smear > 0.0f)
        {
            g.setColour (wash.withMultipliedAlpha (look.smear));
            g.fillPath (window);
        }
        if (look.stripes <= 0.0f)
            continue;

        // One stripe of the fundamental's ring is 2 pi / segments; the strobe's phase turns all three
        // rings by the same angle, so the outer rings pass two and four of their own in that time.
        const float pitch = juce::MathConstants<float>::twoPi / (float) (segments * core::TunerStrobe::partials[(size_t) k]);
        const float turn  = std::fmod (s.stripes (k) * juce::MathConstants<float>::twoPi / (float) segments, pitch);

        juce::Path lit;
        for (float a = -sweep - pitch + turn; a < sweep; a += pitch)
            lit.addPieSegment (ringArea (r1), a, a + pitch * 0.5f, r0 / r1);

        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (window);
        g.setColour (ink.withMultipliedAlpha (look.stripes));
        g.fillPath (lit);
    }
}

} // namespace orbitamp::strobeview
