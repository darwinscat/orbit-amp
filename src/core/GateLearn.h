// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "HumLearnLink.h"

#include <algorithm>
#include <vector>

namespace orbitamp::core
{

/** THE GATE'S LEARN, AS ARITHMETIC. Three seconds of the gate's key level, one reading a tick at 30 Hz;
    the half-second at each end thrown away — the click that started it, and the hand leaving the mouse,
    are not the noise floor; and the threshold the loudest of what is left asks for: that peak plus the
    gate's hysteresis, kept between -80 and -10 dB. A peak under -75 is a muted input, and teaches
    nothing.

    No clock, no parameters, no pixels. Whoever holds it ticks it and decides where the answer goes
    (ui/GateLearner.h) — OrbitAmp's LEARN under its big overlay and every face's LEARN on its own glass
    tick this same one, so there is one measurement and not two that agree today. JUCE-free, like the
    tuner's ear, so its sums are checked with no window open (tests/GateLearnTests.cpp).

    THE QUIET SECONDS TEACH THE HUM TOO. Asking the player for three seconds of silence is the expensive
    part; what is measured in them is cheap. So with a hum mode in force (HumLearnLink::on) the take
    asks the hum stage to learn in the same quiet: it begins where `begin()` begins, ends when the
    ticks are done — the take then waits, up to half a second, for the stage's answer — and is thrown
    away by the same stop.
    The two do not simply share the window, though: a hum learn starts from nothing and lets the hum
    back while it learns, and the gate's key hears it. So the gate trusts only the ticks from where the
    stage says its settled window opened (its answer's settledAtMs, read against the learn's own clock);
    until the answer comes, from where that window is planned to open — at one second when the stage
    already had a model (warm), at 1.75 s when it starts cold, deciding the family first. A cold take is
    lengthened to 3.5 s, so a second of trusted floor still follows; a warm one keeps its three. A take
    the stage calls DISTURBED (a string, a knock — what its own triggers catch) sets nothing. With the
    mode OFF the take is exactly what it was. */
class GateLearn
{
public:
    static constexpr int ticksPerSecond = 30;
    static constexpr int totalTicks = 90;   // 3 s at 30 Hz
    static constexpr int edgeTicks  = 15;   // half a second each end, thrown away
    static constexpr int coldTicks  = 105;  // 3.5 s: a cold hum learn settles at about 1.75 s
    static constexpr int answerTicks = 15;  // how long a finished take waits for the hum's answer

    static constexpr float warmTrustMs = 1000.0f;   // where a warm hum learn's settled window opens
    static constexpr float coldTrustMs = 1750.0f;   // ...and a cold one's, as planned

    static constexpr float lowestDb  = -80.0f;      // where a learned threshold may land...
    static constexpr float highestDb = -10.0f;      // ...at the most
    static constexpr float heardDb   = -75.0f;      // a peak under this is a muted input
    static constexpr float nothing   = -999.0f;     // the answer while there is none

    /** `hum`: the processor's link to its hum stage, or none — the take then never asks it anything. */
    explicit GateLearn (float hysteresisDb, HumLearnLink* hum = nullptr) noexcept
        : hysteresis (hysteresisDb), link (hum) {}

    /** A fresh take: nothing heard, the trace empty — and the hum stage asked to learn too, when a hum
        mode is in force. */
    void begin()
    {
        running = true;
        ticks   = 0;
        peak    = -120.0f;
        withHum = link != nullptr && link->on.load (std::memory_order_relaxed);
        cold    = withHum && ! link->running.load (std::memory_order_relaxed);
        length  = cold ? coldTicks : totalTicks;
        trustFromMs = withHum ? (cold ? coldTrustMs : warmTrustMs) : 0.0f;
        waiting = 0;
        reply   = {};
        trace.clear();
        trace.reserve ((size_t) length);
        clock.clear();
        clock.reserve ((size_t) length);

        if (withHum)
            link->ask.store (HumLearnLink::begin, std::memory_order_release);
    }

    /** STOP, mid-take. What was heard stays — the trace is still a picture of it — and nothing is set;
        the hum stage throws its learn away and goes back to the model it had. */
    void stop() noexcept
    {
        if (running && withHum)
            link->ask.store (HumLearnLink::drop, std::memory_order_release);
        running = false;
    }

    /** One tick's key level, dB. True on the tick that completes the take — the gate's ticks heard and,
        with a hum learn, its answer in (or given up on). */
    bool hear (float db)
    {
        if (waiting > 0)
        {
            if (link->answers.load (std::memory_order_acquire) == answersBefore && ++waiting <= answerTicks)
                return false;
            if (link->answers.load (std::memory_order_acquire) != answersBefore)
                takeAnswer (link->answer());
            running = false;
            return true;
        }

        ++ticks;
        trace.push_back (db);   // the trace IS the progress: it grows a reading a tick
        clock.push_back (withHum ? link->learnMs.load (std::memory_order_relaxed) : 0.0f);

        if (trusting())
            peak = std::max (peak, db);

        if (ticks < length)
            return false;

        if (withHum)
        {
            answersBefore = link->answers.load (std::memory_order_acquire);
            link->ask.store (HumLearnLink::keep, std::memory_order_release);
            waiting = 1;
            return false;
        }

        running = false;
        return true;
    }

    /** Whether the tick just heard lies inside the quiet window — after the first half-second, up to the
        last one, and, with a hum learn, from where its settled window opens. */
    bool trusting() const noexcept { return ticks > 0 && trusted ((size_t) ticks - 1); }

    bool isRunning() const noexcept { return running; }
    const std::vector<float>& heard() const noexcept { return trace; }

    /** How many ticks this take runs: three seconds, or three and a half for a cold hum learn. */
    int  ticksInTake() const noexcept { return length; }
    bool learnsHum() const noexcept   { return withHum; }

    /** The hum stage's answer — `verdict` is HumLearnLink::unanswered until it comes, and stays so
        for a take that learned no hum. */
    const HumLearnLink::Answer& humAnswer() const noexcept { return reply; }

    /** The stage called the quiet DISTURBED: the take sets nothing. */
    bool disturbed() const noexcept { return reply.verdict == HumLearnLink::disturbed; }

    /** The threshold a peak asks for. */
    float thresholdFor (float peakDb) const noexcept { return std::clamp (peakDb + hysteresis, lowestDb, highestDb); }

    /** The threshold the take would set if it ended now — the live dashed line. `nothing` while it is not
        running, or the trusted window has heard nothing yet. */
    float pendingDb() const noexcept
    {
        if (! running || peak <= -119.0f)
            return nothing;

        return thresholdFor (peak);
    }

    /** What a finished take sets: the threshold, or `nothing` when all it heard was a muted input — or
        the stage called the quiet disturbed. */
    float resultDb() const noexcept { return ! disturbed() && peak >= heardDb ? thresholdFor (peak) : nothing; }

private:
    /** Tick `i` (from 0) as the window trusts it. */
    bool trusted (size_t i) const noexcept
    {
        const int t = (int) i + 1;
        return t > edgeTicks && t <= length - edgeTicks && i < clock.size() && clock[i] >= trustFromMs;
    }

    /** The stage's answer moves where the trusted window starts — to where it says its settled window
        opened, or to the very start when it heard no hum to learn (the key was the input as it came) —
        and the peak is taken again over the ticks that are trusted now. */
    void takeAnswer (const HumLearnLink::Answer& a)
    {
        reply = a;
        if (a.verdict == HumLearnLink::nothingHeard)
            trustFromMs = 0.0f;
        else if (a.settledAtMs >= 0.0f)
            trustFromMs = a.settledAtMs;

        peak = -120.0f;
        for (size_t i = 0; i < trace.size(); ++i)
            if (trusted (i))
                peak = std::max (peak, trace[i]);
    }

    const float hysteresis;
    HumLearnLink* const link;
    bool  running = false;
    bool  withHum = false, cold = false;
    int   ticks   = 0;
    int   length  = totalTicks;
    int   waiting = 0;
    unsigned answersBefore = 0;
    float trustFromMs = 0.0f;
    float peak    = -120.0f;
    HumLearnLink::Answer reply;
    std::vector<float> trace;
    std::vector<float> clock;   // the hum learn's own time at each tick, ms (-1 before it began)
};

} // namespace orbitamp::core
