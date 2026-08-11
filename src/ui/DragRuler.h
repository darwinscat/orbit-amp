#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** The runner's OWN scale, summoned by the hand: while a trim runner is being dragged, this
    ruler slides out beside its column — frosted over whatever lives there — with the ±dB ladder
    the runner actually moves on, and an orange line at the value in hand. Let go and it is gone.

    A projection, not a control: it intercepts nothing. Its bounds must match the column's
    vertical extent, so its ladder lands exactly at the runner's heights. */
class DragRuler final : public juce::Component
{
public:
    DragRuler() { setInterceptsMouseClicks (false, false); }

    /** Numbers and ticks hug the edge nearest the column: left when the ruler stands right of
        it (IN), right when it stands left (OUT). */
    bool ticksOnLeft = true;

    float rangeDb = 24.0f;

    /** The value in hand, plain dB — the orange line. */
    std::function<float()> currentDb;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // Frosted glass, honestly faked: a deep translucent sheet with a hairline.
        g.setColour (theme::panel.withAlpha (0.88f));
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        const auto area = r.reduced (2.0f);

        for (int db = -24; db <= 24; db += 3)
        {
            const bool  major = db % 6 == 0;
            const float y     = dbToY (area, (float) db);
            const float len   = major ? 7.0f : 4.5f;

            g.setColour (juce::Colours::white.withAlpha (major ? 0.45f : 0.28f));

            if (ticksOnLeft)
                g.fillRect (area.getX(), y - 1.0f, len, 2.0f);
            else
                g.fillRect (area.getRight() - len, y - 1.0f, len, 2.0f);

            if (major)
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                theme::drawTracked (g, (db > 0 ? "+" : "") + juce::String (db),
                                    { ticksOnLeft ? area.getX() + 10.0f : area.getX() + 2.0f,
                                      y - 6.0f, area.getWidth() - 14.0f, 12.0f },
                                    theme::displayFont (10.5f), 0.04f,
                                    ticksOnLeft ? juce::Justification::centredLeft
                                                : juce::Justification::centredRight);
            }
        }

        if (currentDb != nullptr)
        {
            g.setColour (theme::orange);
            g.fillRect (area.getX(), dbToY (area, currentDb()) - 1.0f, area.getWidth(), 2.0f);
        }
    }

private:
    /** MUST mirror the strips' trimY: centre is unity, 6 px margin at each end of the travel. */
    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getCentreY() - db / rangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragRuler)
};

} // namespace orbitamp
