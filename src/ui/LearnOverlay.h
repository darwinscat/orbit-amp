// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

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

        // The waveform as a CURVE: the peak envelope (per-block peak sampled at 30 Hz, log
        // scale so the noise floor stays in the picture), one smooth path growing rightward,
        // its area filled faintly under it — half-height, floor-anchored.
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

        if (trace != nullptr && trace->size() > 1)
        {
            const float stepW = area.getWidth() / (float) juce::jmax (1, totalTicks - 1);

            juce::Path curve, fill;
            fill.startNewSubPath (area.getX(), area.getBottom());

            for (size_t i = 0; i < trace->size(); ++i)
            {
                const float x = area.getX() + (float) i * stepW;
                const float y = dbToY (area, (*trace)[i]);

                if (i == 0) curve.startNewSubPath (x, y);
                else        curve.lineTo (x, y);
                fill.lineTo (x, y);
            }

            fill.lineTo (area.getX() + (float) (trace->size() - 1) * stepW, area.getBottom());
            fill.closeSubPath();

            g.setColour (theme::violet.withAlpha (0.18f));
            g.fillPath (fill);
            g.setColour (theme::violet);
            g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        // The threshold-to-be walks live: one lilac dashed line with its number counting.
        if (const float pend = pendingDb != nullptr ? pendingDb() : -999.0f;
            pend > -200.0f && message.isEmpty())
        {
            const float y = dbToY (area, pend);
            const float dashes[] = { 5.0f, 4.0f };

            g.setColour (theme::lilac);
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 1.4f);

            theme::drawTracked (g, juce::String (juce::roundToInt (pend)) + " DB",
                                { area.getRight() - 74.0f, y - 18.0f, 70.0f, 14.0f },
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
