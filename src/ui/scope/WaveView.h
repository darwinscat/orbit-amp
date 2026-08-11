#pragma once

#include "../../core/WaveRibbon.h"
#include "../Theme.h"

#include <array>
#include <cmath>

namespace orbitamp::scope
{

/** Both waveforms over a few seconds, one on top of the other, matched for level.

    Orange is the input, filled. Violet is the output — the deeper accent rather than the lilac tint,
    because the line spends most of its length ON the orange and has to hold against it — drawn over
    the fill as an OUTLINE. The whole picture is where that line runs against the orange body: inside
    it at the pick attack, where the peaks were cut, and outside it through the sustain, where the
    tail was lifted.

    An outline rather than a second filled shape, because a solid one hid the difference exactly where
    there was one to see, which is most of a note. */
class WaveView
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<float> r, core::WaveRibbon& ribbon,
                bool halfWave = false)
    {
        const int pixels = juce::jmax (2, (int) r.getWidth());
        ribbon.setResolution (pixels);

        int count = 0;
        float phase = 0.0f;
        if (! ribbon.read (columns, count, phase) || count < 2)
            return;

        // Half-wave: the magnitude silhouette off the floor — an audio editor's overview.
        // The lo pick becomes the flat baseline and the hi pick the envelope's extreme.
        const float mid  = halfWave ? r.getBottom() : r.getCentreY();
        const float half = halfWave ? r.getHeight() : r.getHeight() * 0.5f;

        // One column per pixel, so no grouping happens here at all — grouping is what shimmered.
        auto at = [count, pixels] (auto pick, int x)
        {
            return pick (juce::jlimit (0, count - 1, x * count / juce::jmax (1, pixels)));
        };

        // ONE filled path, not a rectangle per pixel: a rectangle exactly one unit wide lands on
        // fractional device pixels once the editor is scaled, and the antialiased seams between them
        // read as a picket fence. The gaps were the rasteriser, not the audio.
        auto band = [&] (auto lo, auto hi, juce::Colour c, bool filled)
        {
            juce::Path p;

            // Started explicitly: a path whose first instruction is lineTo begins at the origin, and
            // every frame drew a diagonal from the well's top-left corner that had nothing to do with
            // the audio. One pixel past the right edge, because the whole path slides left by up to a
            // pixel and without it a sliver of well blinks there every column.
            p.startNewSubPath (r.getX() - phase, mid - shape (at (hi, 0)) * half);

            for (int x = 1; x <= pixels; ++x)
                p.lineTo (r.getX() + (float) x - phase, mid - shape (at (hi, x)) * half);

            for (int x = pixels + 1; --x >= 0;)
                p.lineTo (r.getX() + (float) x - phase, mid - shape (at (lo, x)) * half);

            p.closeSubPath();

            g.setColour (c);

            if (filled)
                g.fillPath (p);

            // Stroked either way: the path collapses to a line where the signal is silent, and a line
            // with no thickness disappears. The approach to a note is worth seeing.
            g.strokePath (p, juce::PathStrokeType (filled ? 1.0f : 1.4f));
        };

        if (halfWave)
        {
            // One magnitude per column — the larger excursion of the pair — and zero for the lo
            // pick, which pins the band's return path to the baseline.
            band ([] (int) { return 0.0f; },
                  [this] (int i) { const auto& c = columns[(size_t) i];
                                   return juce::jmax (c.dryHi, -c.dryLo); },
                  theme::orange.withAlpha (0.5f), true);

            band ([] (int) { return 0.0f; },
                  [this] (int i) { const auto& c = columns[(size_t) i];
                                   return juce::jmax (c.wetHi, -c.wetLo); },
                  theme::violet, false);
            return;
        }

        band ([this] (int i) { return columns[(size_t) i].dryLo; },
              [this] (int i) { return columns[(size_t) i].dryHi; },
              theme::orange.withAlpha (0.5f), true);

        band ([this] (int i) { return columns[(size_t) i].wetLo; },
              [this] (int i) { return columns[(size_t) i].wetHi; }, theme::violet, false);
    }

private:
    /** Amplitude, bent so quiet is visible.

        Linear, a picked note spends most of its life in the bottom tenth of the well and the picture
        is a flat line with a spike on it. This lifts the small end without a scale break at zero: at
        full deflection it is unchanged, at a tenth it is a fifth, at a hundredth a twentieth. The
        picture is not a meter, and legibility beats a proportion nobody is going to measure. */
    static float shape (float a)
    {
        return juce::jlimit (-1.0f, 1.0f,
                             (a < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (a), 0.7f));
    }

    std::array<core::WaveRibbon::Column, core::WaveRibbon::buckets> columns {};
};

} // namespace orbitamp::scope
