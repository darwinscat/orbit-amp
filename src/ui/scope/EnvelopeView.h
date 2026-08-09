#pragma once

#include "../Theme.h"
#include "ScopeFrame.h"

#include <array>

namespace orbitamp::scope
{

/** The peak over time, in against out.

    The picked attack stands up on the input and is flattened on the output, with the tail carried
    further. Squashing, drawn. */
struct EnvelopeView
{
    static void paint (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f)
    {
        if (f.isEmpty())
            return;

        constexpr int buckets = 128;
        std::array<float, buckets> de {}, we {};
        const int per = juce::jmax (1, f.size / buckets);

        for (int b = 0; b < buckets; ++b)
            for (int i = b * per; i < (b + 1) * per && i < f.size; ++i)
            {
                de[(size_t) b] = juce::jmax (de[(size_t) b], std::abs (f.dry[i]));
                we[(size_t) b] = juce::jmax (we[(size_t) b], std::abs (f.wet[i]));
            }

        auto fill = [&g, r] (const std::array<float, buckets>& e, juce::Colour c, float alpha)
        {
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int b = 0; b < buckets; ++b)
                p.lineTo (r.getX() + (float) b / (buckets - 1) * r.getWidth(),
                          r.getBottom() - juce::jlimit (0.0f, 1.0f, e[(size_t) b]) * r.getHeight());
            p.lineTo (r.getRight(), r.getBottom());
            p.closeSubPath();

            g.setColour (c.withAlpha (alpha));
            g.fillPath (p);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        };

        fill (de, theme::txFaint, 0.15f);
        fill (we, theme::orange,  0.22f);
    }
};

} // namespace orbitamp::scope
