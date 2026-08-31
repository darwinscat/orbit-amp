// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Theme.h"
#include "ScopeFrame.h"

#include <array>

namespace orbitamp::scope
{

/** The peak over time, in against out — on a DECIBEL ruler, because squashing IS a dB story:
    the distance between the two curves is the compression, readable as height.

    The picked attack stands up on the input and is flattened on the output, with the tail carried
    further. Squashing, drawn. */
struct EnvelopeView
{
    static constexpr float floorDb = -80.0f;

    static void paint (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f)
    {
        if (f.isEmpty())
            return;

        // The grid the scale earns — the same rules WAVE draws.
        for (float db : { -20.0f, -40.0f, -60.0f })
        {
            const float t = (db - floorDb) / -floorDb;
            g.setColour (theme::hair);
            g.fillRect (r.getX(), r.getBottom() - t * r.getHeight(), r.getWidth(), 1.0f);
            if (! axesLabelled (r))
                continue;

            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String ((int) db),
                                { r.getX() + 4.0f, r.getBottom() - t * r.getHeight() - 12.0f,
                                  34.0f, 11.0f },
                                theme::displayFont (10.0f), 0.06f, juce::Justification::centredLeft);
        }

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
            {
                const float db = juce::Decibels::gainToDecibels (e[(size_t) b], floorDb);
                const float t  = juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
                p.lineTo (r.getX() + (float) b / (buckets - 1) * r.getWidth(),
                          r.getBottom() - t * r.getHeight());
            }
            p.lineTo (r.getRight(), r.getBottom());
            p.closeSubPath();

            g.setColour (c.withAlpha (alpha));
            g.fillPath (p);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        };

        fill (de, theme::violet, 0.15f);
        fill (we, theme::orange, 0.22f);
    }
};

} // namespace orbitamp::scope
