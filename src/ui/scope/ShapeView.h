#pragma once

#include "../Theme.h"
#include "ScopeFrame.h"

namespace orbitamp::scope
{

/** One cycle, big.

    A sine going square says "hard"; a lopsided one says the two halves are treated differently,
    which is the warm-versus-angry difference without a word of explanation. */
struct ShapeView
{
    static void paint (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f)
    {
        if (f.isEmpty())
            return;

        // A rising zero crossing near the end, and a window back from it — so the drawing sits still
        // instead of sliding across the well.
        int end = f.size - 1;
        for (int i = f.size - 2; i > f.size / 2; --i)
            if (f.dry[i] <= 0.0f && f.dry[i + 1] > 0.0f) { end = i; break; }

        const int n = juce::jmin (600, end);
        const int from = end - n;

        // Normalised to the window's own peak — ONE factor for both traces, so the wet-to-dry
        // difference survives. Form is scale-invariant; the level lives on the rails and in
        // WAVE. The floor keeps silence flat instead of blowing its noise up to full height.
        float peak = 0.02f;
        for (int i = from; i < from + n; ++i)
            peak = juce::jmax (peak, juce::jmax (std::abs (f.dry[i]), std::abs (f.wet[i])));

        const float norm = 0.92f / peak;

        trace (g, r, f.dry + from, n, norm, theme::lilac.withAlpha (0.75f), 1.0f);
        trace (g, r, f.wet + from, n, norm, theme::orange, 1.6f);
    }

private:
    static void trace (juce::Graphics& g, juce::Rectangle<float> r, const float* data, int n,
                       float norm, juce::Colour colour, float thickness)
    {
        if (n < 2)
            return;

        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + (float) i / (float) (n - 1) * r.getWidth();
            const float y = r.getCentreY()
                          - juce::jlimit (-1.0f, 1.0f, data[i] * norm) * r.getHeight() * 0.5f;

            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness));
    }
};

} // namespace orbitamp::scope
