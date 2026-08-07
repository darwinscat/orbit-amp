#pragma once

#include "Theme.h"

namespace orbitamp
{

/** The reverb's decay envelope — an impulse's peak level over time, in dB.

    A dumb view: it is handed a ready envelope and draws it. The owner measures, because measuring
    means running audio through a reverb and that is not a drawing's job.

    The tail is drawn in dB rather than linear amplitude on purpose: a linear decay collapses into
    nothing after the first tenth of a second, which would say "there is a reverb here" and nothing
    else. In dB the difference between a spring and a hall is the shape you can see. */
class ReverbTailView final : public juce::Component
{
public:
    ReverbTailView() { setInterceptsMouseClicks (false, false); }   // an illustration, not a control

    /** Peak level per time bucket, in dB, oldest first. */
    void setEnvelope (std::vector<float> newEnvelope, double lengthSeconds)
    {
        envelope = std::move (newEnvelope);
        seconds  = lengthSeconds;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        {
            const juce::Graphics::ScopedSaveState clipped (g);
            juce::Path well;
            well.addRoundedRectangle (r, theme::radiusMd);
            g.reduceClipRegion (well);

            drawGrid (g, r);

            if (envelope.size() >= 2)
            {
                const auto inner = r.reduced (4.0f);

                juce::Path tail;
                tail.startNewSubPath (inner.getX(), inner.getBottom());

                for (size_t i = 0; i < envelope.size(); ++i)
                {
                    const float t = (float) i / (float) (envelope.size() - 1);
                    tail.lineTo (inner.getX() + t * inner.getWidth(), dbToY (inner, envelope[i]));
                }

                tail.lineTo (inner.getRight(), inner.getBottom());
                tail.closeSubPath();

                g.setColour (theme::violet.withAlpha (0.22f));
                g.fillPath (tail);
                g.setColour (theme::violet);
                g.strokePath (tail, juce::PathStrokeType (1.4f));
            }
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

        if (seconds > 0.0)
        {
            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String (seconds, 1) + " S", r.reduced (8.0f, 5.0f),
                                theme::displayFont (7.5f), 0.1f, juce::Justification::bottomRight);
        }
    }

private:
    static constexpr float floorDb = -60.0f;

    static float dbToY (juce::Rectangle<float> r, float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb);
        return r.getBottom() - t * r.getHeight();
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto inner = r.reduced (4.0f);

        g.setColour (theme::hair);
        for (float db : { -12.0f, -24.0f, -36.0f, -48.0f })
            g.fillRect (inner.getX(), dbToY (inner, db), inner.getWidth(), 1.0f);
    }

    std::vector<float> envelope;
    double seconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbTailView)
};

} // namespace orbitamp
