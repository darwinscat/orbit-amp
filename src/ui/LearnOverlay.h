#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace orbitamp
{

/** The LEARN measurement, projected large: half the plugin under a violet waveform growing left
    to right for three seconds, the noise floor visible the whole way, the verdict written big at
    the end and the whole thing folding away by itself. The overlay owns nothing — it reads the
    strip's trace and total through what the editor wires in. */
class LearnOverlay final : public juce::Component,
                           private juce::Timer
{
public:
    LearnOverlay()
    {
        setInterceptsMouseClicks (false, false);   // a projection, not a control
    }

    /** Wired once by the editor: where the samples live and how many make a full take. */
    const std::vector<float>* trace = nullptr;
    int totalTicks = 90;

    void begin()
    {
        message.clear();
        holdTicks = 0;
        setVisible (true);
        toFront (false);
        startTimerHz (30);
    }

    void finish (const juce::String& verdict)
    {
        message   = verdict;
        holdTicks = 50;   // the verdict stays readable, then the sheet folds away
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::panel.withAlpha (0.96f));
        g.fillRoundedRectangle (r, theme::radiusMd);
        g.setColour (theme::violet.withAlpha (0.85f));
        g.drawRoundedRectangle (r.reduced (0.75f), theme::radiusMd, 1.5f);

        auto area = r.reduced (18.0f, 14.0f);

        g.setColour (theme::lilac);
        theme::drawTracked (g, "LEARN", area.removeFromTop (22.0f), theme::displayFont (16.0f),
                            0.15f, juce::Justification::centredLeft);
        area.removeFromTop (6.0f);

        // The scale the measurement is made on: -20/-40/-60 rules, faint.
        for (float db : { -20.0f, -40.0f, -60.0f })
        {
            const float y = dbToY (area, db);
            g.setColour (theme::hair);
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String ((int) db),
                                { area.getX() + 4.0f, y - 13.0f, 40.0f, 12.0f },
                                theme::displayFont (10.0f), 0.06f, juce::Justification::centredLeft);
        }

        // The waveform, one bar per tick, growing rightward — progress and picture in one.
        if (trace != nullptr && ! trace->empty())
        {
            const float barW = area.getWidth() / (float) juce::jmax (1, totalTicks);

            g.setColour (theme::violet.withAlpha (0.9f));
            for (size_t i = 0; i < trace->size(); ++i)
            {
                const float y = dbToY (area, (*trace)[i]);
                g.fillRect (area.getX() + (float) i * barW, y,
                            juce::jmax (1.0f, barW - 1.0f), area.getBottom() - y);
            }
        }

        if (message.isNotEmpty())
        {
            g.setColour (juce::Colours::white);
            theme::drawTracked (g, message, area, theme::displayFont (24.0f), 0.12f,
                                juce::Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
        if (message.isNotEmpty() && --holdTicks <= 0)
        {
            setVisible (false);
            stopTimer();
            return;
        }

        repaint();
    }

    static float dbToY (juce::Rectangle<float> r, float db)
    {
        constexpr float floorDb = -80.0f;
        return r.getBottom()
             - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    juce::String message;
    int holdTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnOverlay)
};

} // namespace orbitamp
