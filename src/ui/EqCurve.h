#pragma once

#include "Theme.h"

namespace orbitamp
{

/** The tone-stack response, drawn on a log frequency axis.

    It asks the owner for dB at a frequency rather than holding filters of its own — the curve is
    read from the coefficients the audio thread actually runs, so what is drawn can never drift from
    what is heard. A dumb view: a callback in, pixels out. */
class EqCurve : public juce::Component
{
public:
    explicit EqCurve (std::function<double (double)> magnitudeDbAt)
        : magnitudeDb (std::move (magnitudeDbAt))
    {
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        drawGrid (g, r);

        // One point per pixel column — cheaper and smoother than a coarse grid interpolated up.
        juce::Path curve;
        const int w = juce::jmax (2, getWidth());

        for (int x = 0; x < w; ++x)
        {
            const float px = (float) x + r.getX();
            const float y  = dbToY (r, (float) magnitudeDb (xToHz (r, px)));

            if (x == 0) curve.startNewSubPath (px, y);
            else        curve.lineTo (px, y);
        }

        g.setColour (theme::violet);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);
    }

private:
    static constexpr double minHz  = 20.0;
    static constexpr double maxHz  = 20000.0;
    static constexpr float  rangeDb = 15.0f;   // a touch past the +-12 dB controls, so the curve never clips

    static double xToHz (juce::Rectangle<float> r, float x)
    {
        const double t = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / juce::jmax (1.0f, r.getWidth())));
        return minHz * std::pow (maxHz / minHz, t);
    }

    static float hzToX (juce::Rectangle<float> r, double hz)
    {
        const double t = std::log (hz / minHz) / std::log (maxHz / minHz);
        return r.getX() + (float) t * r.getWidth();
    }

    static float dbToY (juce::Rectangle<float> r, float db)
    {
        return r.getCentreY() - juce::jlimit (-rangeDb, rangeDb, db) / rangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        g.setColour (theme::hair);

        for (double hz : { 100.0, 1000.0, 10000.0 })
            g.fillRect (hzToX (r, hz), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);

        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            g.fillRect (r.getX() + 4.0f, dbToY (r, db), r.getWidth() - 8.0f, 1.0f);

        // The 0 dB line reads brighter — it is the reference the curve is judged against.
        g.setColour (theme::hair2);
        g.fillRect (r.getX() + 4.0f, dbToY (r, 0.0f), r.getWidth() - 8.0f, 1.0f);
    }

    std::function<double (double)> magnitudeDb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurve)
};

} // namespace orbitamp
