// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "HumLearnLink.h"

#include <felitronics/dehum/HumRemover.h>

#include <algorithm>
#include <vector>

namespace orbitamp::core
{

/** THE HUM'S LEARN, AS A TAKE — its own, apart from the gate's (GateLearn.h): the player keeps quiet,
    the hum stage learns the hum anew, and keeps what it learned if it heard one. The stage does the
    learning on the audio thread (HumLearnLink); what is here is the clock: when to ask it to begin,
    when it has heard enough, and how long to wait for its answer.

    WHEN IT HAS HEARD ENOUGH is the stage's own timeline (felitronics::dehum::HumRemover, its LEARN):
    the model starts from nothing, so the stage first has the family — at once when it already ran
    one, in about three quarters of a second when it starts cold — then converges, and opens its floor
    window no earlier than a second in and no earlier than seven learn time constants after it armed.
    The take reads when the stage armed off the link, works out when that window opens, and ends once
    it has been open its minimum and half a second more: two and a half seconds when the stage already
    had a model, longer when it starts cold, and three seconds when it never arms at all — it heard no
    hum. The arithmetic of the window's opening is the stage's, restated: the stage says when it opened
    only in its answer, and the take has to end before that.

    Ticked at 30 Hz with the input's level, which it keeps as a trace — the take's picture, as the
    gate's take keeps its own. JUCE-free; checked with the processor in tests/HumTests.cpp. */
class HumLearnTake
{
public:
    static constexpr int   ticksPerSecond = 30;
    static constexpr float shortestMs     = 2500.0f;  // a warm learn: settled at one second, then a floor worth trusting
    static constexpr float unarmedMs      = 3000.0f;  // a learn the stage has not armed by now has heard no hum
    static constexpr float marginMs       = 500.0f;   // past the floor window's minimum, before the answer is asked
    static constexpr int   unservedTicks  = 30;       // a second with no audio to learn from: given up
    static constexpr int   answerTicks    = 15;       // how long the answer is waited for

    explicit HumLearnTake (HumLearnLink& l) noexcept : link (l) {}

    /** Begin: the stage is asked to learn. `quietWidthHz` is the stage's learn width, which sets how
        long it converges. */
    void begin (float quietWidthHz)
    {
        running = true;
        ticks   = 0;
        waiting = 0;
        unserved = 0;
        width   = quietWidthHz > 0.0f ? quietWidthHz : 3.0f;
        reply   = {};
        trace.clear();
        trace.reserve ((size_t) (ticksPerSecond * 6));
        link.ask.store (HumLearnLink::begin, std::memory_order_release);
    }

    /** STOP: the stage throws its learn away and goes back to the model it had. */
    void stop() noexcept
    {
        if (running && waiting == 0)
            link.ask.store (HumLearnLink::drop, std::memory_order_release);
        running = false;
    }

    /** One tick, the input's level in dB. True on the tick that completes the take — the answer in, or
        given up on. */
    bool hear (float db)
    {
        if (! running)
            return false;

        if (waiting > 0)
        {
            if (link.answers.load (std::memory_order_acquire) != answersBefore)
                reply = link.answer();
            else if (++waiting <= answerTicks)
                return false;
            running = false;
            return true;
        }

        ++ticks;
        trace.push_back (db);

        const float ms = link.learnMs.load (std::memory_order_relaxed);
        if (ms < 0.0f)
        {
            if (++unserved > unservedTicks)     // nothing is running the stage: there is nothing to learn from
            {
                stop();
                return true;
            }
            return false;
        }

        if (ms < endMs())
            return false;

        answersBefore = link.answers.load (std::memory_order_acquire);
        link.ask.store (HumLearnLink::keep, std::memory_order_release);
        waiting = 1;
        return false;
    }

    /** When the take ends, in the learn's own milliseconds, as far as it can tell now. */
    float endMs() const noexcept
    {
        using Stage = felitronics::dehum::HumRemover<1>;
        const float armed = link.armedAtMs.load (std::memory_order_relaxed);
        if (armed < 0.0f)
            return unarmedMs;
        const double converge = std::max (Stage::kLearnConvergeSec, 7.0 / (3.14159265358979 * (double) width));
        const double opens    = std::max (1000.0 * Stage::kLearnSettleSec, (double) armed + 1000.0 * converge);
        return std::max (shortestMs, (float) (opens + 1000.0 * Stage::kLearnFloorMinSec) + marginMs);
    }

    /** How many ticks the take now looks like lasting — a picture's width. */
    int ticksPlanned() const noexcept
    {
        return std::max ((int) trace.size(), (int) (endMs() * (float) ticksPerSecond / 1000.0f) + 1);
    }

    bool isRunning() const noexcept { return running; }
    const std::vector<float>& heard() const noexcept { return trace; }

    /** The stage's answer — `verdict` HumLearnLink::unanswered when none came. */
    const HumLearnLink::Answer& answer() const noexcept { return reply; }

private:
    HumLearnLink& link;
    bool  running = false;
    int   ticks = 0, waiting = 0, unserved = 0;
    float width = 3.0f;
    unsigned answersBefore = 0;
    HumLearnLink::Answer reply;
    std::vector<float> trace;
};

} // namespace orbitamp::core
