#pragma once

#include "../../core/MeasuredFilter.h"
#include "../Theme.h"
#include "ScopeFrame.h"

#include <cmath>
#include <functional>

namespace orbitamp::scope
{

/** The measured tone controls, summed, at wherever their knobs sit.

    The axis IS the band the curve is believed in. Drawn from 20 Hz to 20 kHz, most of the width went
    to the shelves where the curve is held flat — a long dead line at each end, and the bass one read
    as the pedal doing something. It is not doing anything out there; it is not KNOWN out there, and
    the honest way to show that is not to show it. */
class ToneView
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<float> r,
                const std::function<double (double)>& toneDb, const Frame&)
    {
        if (toneDb == nullptr)
            return;

        const double lo = core::MeasuredFilter::bandLoHz;
        const double hi = core::MeasuredFilter::bandHiHz;
        constexpr float range = 20.0f;

        juce::Path p;
        const int w = juce::jmax (2, (int) r.getWidth());

        for (int x = 0; x < w; ++x)
        {
            const double t  = (double) x / (w - 1);
            const double hz = lo * std::pow (hi / lo, t);
            const float  y  = r.getCentreY() - juce::jlimit (-range, range, (float) toneDb (hz))
                                                 / range * r.getHeight() * 0.5f;

            if (x == 0) p.startNewSubPath (r.getX() + (float) x, y);
            else        p.lineTo (r.getX() + (float) x, y);
        }

        g.setColour (theme::hair);
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        g.setColour (theme::orange);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
};

} // namespace orbitamp::scope
