// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Controls.h"
#include "../ui/GateLearner.h"
#include "../ui/HumLearner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace orbitamp::face
{

/** LEARN, ON A FACE'S OWN GLASS. The gate's measurement (ui/GateLearner.h) — the same three seconds
    OrbitAmp projects on its big overlay — shown where the face already has a screen: one short line
    asking for quiet, the key level growing left to right as it is heard, the threshold it would set
    walking along with it as a dashed line, and at the end the verdict — the threshold set, or
    CANCELLED or NOTHING HEARD — until a click puts it away, ten seconds at the most, before the glass
    goes back to what it showed. Two seconds were too short to read it with the guitar in the hands.
    THE HUM'S LEARN (ui/HumLearner.h) is shown the same way, in its own words: HUM LEARN asking for
    quiet, the input's level as it is heard, and the stage's verdict (HumLearner::words) — no
    threshold line, the hum's learn sets none.

    Written in the family's ink for a dark glass (Ink's own), the trace violet — the measurement at
    work — and the threshold-to-be lilac, as OrbitAmp's overlay draws them. No grid, no frame: bare
    light on whatever glass the product lays it over, as the faces' pictures are.

    THE PRODUCT PLACES IT over its glass, the glass's whole size, and says where on it the words start
    (`inset`) — where they start there already; and is told by `onShow` when to put away what the glass
    shows otherwise, and when to bring it back. A click on it while it is learning cancels the take; on the verdict, puts it
    away sooner. Down, it draws nothing and takes no click: whatever lies under it answers as before. */
class LearnGlass final : public juce::Component,
                         private juce::Timer
{
public:
    LearnGlass (GateLearner& l, HumLearner& h) : learner (l), humLearner (h)
    {
        learner.onBegin = [this] { hum = false; begin(); };
        learner.onDone  = [this] (const GateLearner::Take& take) { finish (take); };
        humLearner.onBegin = [this] { hum = true; begin(); };
        humLearner.onDone  = [this] (const HumLearner::Take& take)
        {
            verdictIsValue = take.outcome == HumLearner::Outcome::done && take.hum.kept;
            verdict = HumLearner::words (take);
            holdTicks = holdFor;
            repaint();
        };
    }

    ~LearnGlass() override
    {
        learner.onBegin = nullptr;
        learner.onDone  = nullptr;
        humLearner.onBegin = nullptr;
        humLearner.onDone  = nullptr;
    }

    /** Up, or down again: the product hides what its glass shows otherwise while this is over it. */
    std::function<void (bool up)> onShow;

    /** From the glass's corner to where its words and its trace start. */
    juce::Point<float> inset;

    bool hitTest (int, int) override { return up; }

    void paint (juce::Graphics& g) override
    {
        if (! up)
            return;

        auto area = getLocalBounds().toFloat().reduced (inset.x, inset.y);
        const Ink ink {};
        const auto words = labelFont (stage::px13).withExtraKerningFactor (0.04f);

        // The line asking for quiet; once it has spoken, only its name.
        g.setColour (ink.words);
        g.setFont (words);
        const juce::String name (hum ? "HUM LEARN" : "LEARN");
        g.drawText (verdict.isEmpty() ? name + juce::String::fromUTF8 (" \xe2\x80\x94 STAY QUIET") : name,
                    area.removeFromTop (stage::px13 * 1.35f), juce::Justification::centredLeft, false);
        area.removeFromTop (4.0f);

        // The key level as it is heard, a reading a tick across the whole width the take will fill —
        // the trace IS the progress — its area filled faintly under it. Dimmed under a verdict.
        const auto& trace = hum ? humLearner.trace() : learner.trace();
        const float shade = verdict.isEmpty() ? 1.0f : 0.35f;
        if (trace.size() > 1)
        {
            const float step = area.getWidth() / (float) ((hum ? humLearner.ticksPlanned() : core::GateLearn::totalTicks) - 1);
            juce::Path curve, under;
            under.startNewSubPath (area.getX(), area.getBottom());
            for (size_t i = 0; i < trace.size(); ++i)
            {
                const juce::Point<float> at { area.getX() + (float) i * step, yOf (area, trace[i]) };
                i == 0 ? curve.startNewSubPath (at) : curve.lineTo (at);
                under.lineTo (at);
            }
            under.lineTo (area.getX() + (float) (trace.size() - 1) * step, area.getBottom());
            under.closeSubPath();

            g.setColour (violet.withAlpha (0.18f * shade));
            g.fillPath (under);
            g.setColour (violet.withAlpha (shade));
            g.strokePath (curve, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // The threshold-to-be, walking live: a dashed lilac line across, its number over its right end —
        // under it when the line runs too near the top to leave room.
        if (const float pending = hum ? core::GateLearn::nothing : learner.pendingDb(); verdict.isEmpty() && pending > core::GateLearn::nothing)
        {
            const float y = yOf (area, pending);
            const float dashes[] { 8.0f, 6.0f };
            g.setColour (lilac);
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 2.0f);

            const float h = stage::px13 * 1.2f;
            g.setColour (ink.values);
            g.drawText (decibels (pending), juce::Rectangle<float> (area.getRight() - 160.0f, y - h - 2.0f < area.getY() ? y + 2.0f : y - h - 2.0f, 160.0f, h),
                        juce::Justification::centredRight, false);
        }

        // The verdict, large, over the trace: the threshold in the ink of a value, anything else in the
        // ink of a name — made smaller only if it would not fit the glass.
        if (verdict.isNotEmpty())
        {
            const auto room = area;
            auto big = labelFont (24.0f / stage::readingScale).withExtraKerningFactor (0.06f);
            const float wide = juce::GlyphArrangement::getStringWidth (big, verdict);
            if (wide > room.getWidth())
                big = big.withHeight (juce::jmax (stage::px16, big.getHeight() * room.getWidth() / wide));
            if (big.getHeight() > room.getHeight())
                big = big.withHeight (juce::jmax (stage::px16, room.getHeight()));
            g.setColour (verdictIsValue ? ink.values : ink.words);
            g.setFont (big);
            g.drawText (verdict, room, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (learner.isLearning())
            learner.cancel();       // ...which speaks its verdict, CANCELLED
        else if (humLearner.isLearning())
            humLearner.cancel();
        else
            fold();
    }

private:
    GateLearner& learner;
    HumLearner&  humLearner;
    bool up = false;
    bool hum = false;               // the take shown is the hum's
    juce::String verdict;           // empty while it is learning
    bool verdictIsValue = false;
    int holdTicks = 0;

    static constexpr int ticksPerSecond = core::GateLearn::ticksPerSecond;
    static constexpr int holdFor = 10 * ticksPerSecond;     // the verdict stays until a click, ten seconds at the most

    static juce::String decibels (float db) { return juce::String (juce::roundToInt (db)) + " dB"; }

    /** -90 dB, the key's own floor, at the bottom; -10, the highest a threshold may be, at the top. */
    static float yOf (juce::Rectangle<float> r, float db)
    {
        constexpr float floorDb = -90.0f, topDb = -10.0f;
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, topDb, db) - floorDb) / (topDb - floorDb);
    }

    void begin()
    {
        verdict.clear();
        holdTicks = 0;
        up = true;
        if (onShow != nullptr)
            onShow (true);
        startTimerHz (ticksPerSecond);
        repaint();
    }

    void finish (const GateLearner::Take& take)
    {
        using Outcome = GateLearner::Outcome;
        verdictIsValue = take.outcome == Outcome::set;
        verdict = verdictIsValue ? decibels (take.thresholdDb)
                : take.outcome == Outcome::cancelled ? juce::String ("CANCELLED") : juce::String ("NOTHING HEARD");
        holdTicks = holdFor;
        repaint();
    }

    void fold()
    {
        stopTimer();
        verdict.clear();
        up = false;
        if (onShow != nullptr)
            onShow (false);
        repaint();
    }

    void timerCallback() override
    {
        if (verdict.isNotEmpty() && --holdTicks <= 0)
        {
            fold();
            return;
        }
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LearnGlass)
};

} // namespace orbitamp::face
