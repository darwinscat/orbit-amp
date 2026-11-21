// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MeterRail.h"
#include "Theme.h"

#include <functional>

namespace orbitamp
{

/** A PEDAL'S DOUBLE COLUMN (the owner, 21.11): two thin meters, what comes IN to the block and what goes OUT of it — peak,
    falling back 20 dB a second — in the house's rail grammar (MeterRail.h): the violet flood for the working level, the
    thermometer's heat from -6 dBFS up ("HOT", nothing above -1 was ever trained), and the AMP's trained-input ZONE as two
    lilac marks on both columns — the IN meter's zone (captureColdDb..captureHotDb) carried back to this block's output
    by what the chain still adds before the network (the pack's chain input_db, TURBO's push): the player sees whether
    the block lands the amp in its zone.

    Dumb: the levels and the zone come in through callbacks, read on its own timer. One class for every pedal that wants
    it — the BOOSTER's and DRIVE's. Words at 13 px. */
class LevelColumns final : public juce::Component,
                           private juce::Timer
{
public:
    /** Linear peaks since the last read — the reader takes and clears (an atomic's exchange). */
    std::function<float()> takeIn, takeOut;
    /** The zone at this block's output, dBFS: low and high. */
    std::function<juce::Range<float>()> zone;

    static constexpr float floorDb = -48.0f, hotDb = -6.0f;
    static constexpr int designWidth = 44;   // two columns and their words

    LevelColumns() { startTimerHz (30); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto words = r.removeFromBottom (16.0f);
        const float w = (r.getWidth() - 4.0f) / 2.0f;
        const auto zoneNow = zone ? zone() : juce::Range<float> (-18.0f, -6.0f);
        const bool hot = juce::jmax (shown[0], shown[1]) > hotDb;

        for (int k = 0; k < 2; ++k)
        {
            const auto track = juce::Rectangle<float> (r.getX() + (float) k * (w + 4.0f), r.getY(), w, r.getHeight());
            g.setColour (theme::panel2.darker (0.4f));
            g.fillRoundedRectangle (track, 2.0f);
            meterrail::paintFill (g, track, yOf (track, shown[k]), 0.0f, shown[k] > hotDb);
            g.setColour (theme::lilac);
            for (const float db : { zoneNow.getStart(), zoneNow.getEnd() })
                g.fillRect (juce::Rectangle<float> (track.getX(), yOf (track, db) - 0.75f, track.getWidth(), 1.5f));
            g.setColour (theme::txDim);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (k == 0 ? "IN" : "OUT", juce::Rectangle<float> (track.getX() - 4.0f, words.getY(), track.getWidth() + 8.0f, 16.0f),
                        juce::Justification::centred, false);
        }
        if (hot)
        {
            g.setColour (theme::heatYellow);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText ("HOT", r.removeFromTop (16.0f), juce::Justification::centred, false);
        }
    }

private:
    float yOf (juce::Rectangle<float> track, float db) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
        return track.getBottom() - t * track.getHeight();
    }

    void timerCallback() override
    {
        const float fall = 20.0f / 30.0f;
        const std::function<float()>* from[2] = { &takeIn, &takeOut };
        for (int k = 0; k < 2; ++k)
        {
            const float peak = *from[k] ? (*from[k])() : 0.0f;
            const float db = juce::Decibels::gainToDecibels (peak, floorDb);
            shown[k] = juce::jmax (db, shown[k] - fall);
        }
        repaint();
    }

    float shown[2] { floorDb, floorDb };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelColumns)
};

} // namespace orbitamp
