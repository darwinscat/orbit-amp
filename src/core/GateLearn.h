// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

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

    THE QUIET SECONDS ARE A WINDOW OTHER EARS COULD USE. Asking the player for three seconds of silence
    is the expensive part; what is measured in them is cheap. A second measurement — a hum learner,
    finding the mains' fundamental and its harmonics in the same silence — would trust exactly the ticks
    `trusting()` names, start where `begin()` starts, and be thrown away by the same cancel. It is not
    built; the window is said in one place so that it will not need a second one. */
class GateLearn
{
public:
    static constexpr int ticksPerSecond = 30;
    static constexpr int totalTicks = 90;   // 3 s at 30 Hz
    static constexpr int edgeTicks  = 15;   // half a second each end, thrown away

    static constexpr float lowestDb  = -80.0f;      // where a learned threshold may land...
    static constexpr float highestDb = -10.0f;      // ...at the most
    static constexpr float heardDb   = -75.0f;      // a peak under this is a muted input
    static constexpr float nothing   = -999.0f;     // the answer while there is none

    explicit GateLearn (float hysteresisDb) noexcept : hysteresis (hysteresisDb) {}

    /** A fresh take: nothing heard, the trace empty. */
    void begin()
    {
        running = true;
        ticks   = 0;
        peak    = -120.0f;
        trace.clear();
        trace.reserve ((size_t) totalTicks);
    }

    /** STOP, mid-take. What was heard stays — the trace is still a picture of it — and nothing is set. */
    void stop() noexcept { running = false; }

    /** One tick's key level, dB. True on the tick that completes the take. */
    bool hear (float db)
    {
        ++ticks;
        trace.push_back (db);   // the trace IS the progress: it grows a reading a tick

        if (trusting())
            peak = std::max (peak, db);

        if (ticks < totalTicks)
            return false;

        running = false;
        return true;
    }

    /** Whether the tick just heard lies inside the quiet window — after the first half-second, up to the
        last one. */
    bool trusting() const noexcept { return ticks > edgeTicks && ticks <= totalTicks - edgeTicks; }

    bool isRunning() const noexcept { return running; }
    const std::vector<float>& heard() const noexcept { return trace; }

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

    /** What a finished take sets: the threshold, or `nothing` when all it heard was a muted input. */
    float resultDb() const noexcept { return peak >= heardDb ? thresholdFor (peak) : nothing; }

private:
    const float hysteresis;
    bool  running = false;
    int   ticks   = 0;
    float peak    = -120.0f;
    std::vector<float> trace;
};

} // namespace orbitamp::core
