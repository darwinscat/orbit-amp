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

    /** The live threshold-to-be, dB (-999 = nothing heard yet) — the dashed corridor. */
    std::function<float()> pendingDb;

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

        // A real WAVEFORM: log-magnitude mirrored about the centre line — the oscilloscope
        // silhouette, with the noise floor still visible the way only a log scale keeps it.
        const float cy = area.getCentreY();

        // The scale, mirrored faintly; numbers ride the top half only.
        for (float db : { -20.0f, -40.0f, -60.0f })
        {
            const float h = dbToHalf (area, db);
            g.setColour (theme::hair);
            g.fillRect (area.getX(), cy - h, area.getWidth(), 1.0f);
            g.fillRect (area.getX(), cy + h, area.getWidth(), 1.0f);
            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String ((int) db),
                                { area.getX() + 4.0f, cy - h - 13.0f, 40.0f, 12.0f },
                                theme::displayFont (10.0f), 0.06f, juce::Justification::centredLeft);
        }

        g.setColour (theme::hair2);
        g.fillRect (area.getX(), cy - 0.5f, area.getWidth(), 1.0f);

        if (trace != nullptr && ! trace->empty())
        {
            const float barW = area.getWidth() / (float) juce::jmax (1, totalTicks);

            g.setColour (theme::violet.withAlpha (0.9f));
            for (size_t i = 0; i < trace->size(); ++i)
            {
                const float h = juce::jmax (0.75f, dbToHalf (area, (*trace)[i]));
                g.fillRect (area.getX() + (float) i * barW, cy - h,
                            juce::jmax (1.0f, barW - 1.0f), h * 2.0f);
            }
        }

        // The gate corridor being measured: the threshold-to-be as a mirrored dashed pair,
        // walking live with the running maximum, its number beside it.
        if (const float pend = pendingDb != nullptr ? pendingDb() : -999.0f;
            pend > -200.0f && message.isEmpty())
        {
            const float h = dbToHalf (area, pend);
            const float dashes[] = { 5.0f, 4.0f };

            g.setColour (theme::lilac);
            g.drawDashedLine ({ area.getX(), cy - h, area.getRight(), cy - h }, dashes, 2, 1.4f);
            g.drawDashedLine ({ area.getX(), cy + h, area.getRight(), cy + h }, dashes, 2, 1.4f);

            theme::drawTracked (g, juce::String (juce::roundToInt (pend)) + " DB",
                                { area.getRight() - 74.0f, cy - h - 18.0f, 70.0f, 14.0f },
                                theme::displayFont (13.0f), 0.08f, juce::Justification::centredRight);
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

    /** Log magnitude to half-height about the centre line — the mirrored waveform's ruler. */
    static float dbToHalf (juce::Rectangle<float> r, float db)
    {
        constexpr float floorDb = -80.0f;
        return (r.getHeight() * 0.5f - 2.0f)
             * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    juce::String message;
    int holdTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnOverlay)
};

} // namespace orbitamp
