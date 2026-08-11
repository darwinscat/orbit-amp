#pragma once

#include "../Theme.h"
#include "ScopeFrame.h"

namespace orbitamp::scope
{

/** Output against input.

    A clean path is a diagonal; soft clipping bends it into an S; hard clipping flattens its ends. A
    device that reacts to frequency draws a loop instead of a line, and the loop's width IS that
    reaction. */
struct TransferView
{
    static void paint (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f)
    {
        if (f.isEmpty())
            return;

        const float side = juce::jmin (r.getWidth(), r.getHeight());
        const auto box = r.withSizeKeepingCentre (side, side);

        g.setColour (theme::hair);
        g.drawLine (box.getX(), box.getBottom(), box.getRight(), box.getY(), 1.0f);   // unity

        // Normalised to the window's own peak — ONE factor for BOTH axes, so the slope and the
        // unity diagonal keep telling the truth; the curve just stretches along it. (A dB axis
        // is impossible here: a log cannot hold the negative half, and the S lives in linear.)
        float peak = 0.02f;
        for (int i = f.size / 2; i < f.size; ++i)
            peak = juce::jmax (peak, juce::jmax (std::abs (f.dry[i]), std::abs (f.wet[i])));

        const float norm = 0.92f / peak;

        juce::Path p;
        bool started = false;

        for (int i = f.size / 2; i < f.size; ++i)
        {
            const float x = box.getCentreX() + juce::jlimit (-1.0f, 1.0f, f.dry[i] * norm) * side * 0.5f;
            const float y = box.getCentreY() - juce::jlimit (-1.0f, 1.0f, f.wet[i] * norm) * side * 0.5f;

            if (! started) { p.startNewSubPath (x, y); started = true; }
            else           p.lineTo (x, y);
        }

        g.setColour (theme::orange.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
};

} // namespace orbitamp::scope
