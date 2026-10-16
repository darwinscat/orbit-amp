// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::GateLearn — the gate's LEARN, as arithmetic. The measurement used
// to be written inside OrbitAmp's GateConsole; it moved out so that the faces' GATE menus run the very
// same one. So the gate holds it to two things: the numbers a player would see on known traces (a
// floor, edges that must be ignored, a muted input, the clamps), and EXACT agreement — tick by tick,
// bit for bit — with the arithmetic as GateConsole had it, kept below as `Before`, on those traces and
// on a few thousand random ones. Returns non-zero on any failure.
//
// JUCE-free, like the code under test: links nothing at all.

#include "core/GateLearn.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using orbitamp::core::GateLearn;

namespace
{
    int failures = 0;

    constexpr float hysteresisDb = 6.0f;   // params::gateHysteresisDb

    void check (const char* what, double got, double want, double tol = 0.0)
    {
        const bool ok = std::fabs (got - want) <= tol;
        if (! ok)
            ++failures;

        std::printf ("%-60s %9.3f  (want %8.3f)  %s\n", what, got, want, ok ? "ok" : "FAIL");
    }

    void checkTrue (const char* what, bool ok)
    {
        if (! ok)
            ++failures;

        std::printf ("%-60s %s\n", what, ok ? "ok" : "FAIL");
    }

    // JUCE's own spellings of the two helpers the old code called, so `Before` is the old code as it
    // ran — juce::jlimit and juce::jmax, operand for operand.
    float jlimit (float lower, float upper, float v) { return v < lower ? lower : (upper < v ? upper : v); }
    float jmax (float a, float b) { return a < b ? b : a; }

    /** THE ARITHMETIC AS GATECONSOLE HAD IT before it moved — apply(5), timerCallback and
        learnPendingDb, with the clock, the parameters and the words taken away. */
    struct Before
    {
        static constexpr int learnTotalTicks = 90;
        static constexpr int learnEdgeTicks  = 15;

        bool  learning   = false;
        int   learnTicks = 0;
        float learnPeak  = -120.0f;
        std::vector<float> learnTrace;

        void start()
        {
            learning   = true;
            learnTicks = 0;
            learnPeak  = -120.0f;
            learnTrace.clear();
        }

        float learnPendingDb() const
        {
            if (! learning || learnPeak <= -119.0f)
                return -999.0f;

            return jlimit (-80.0f, -10.0f, learnPeak + hysteresisDb);
        }

        /** One tick. True when it finished; `set` and `th` say what it would write. */
        bool tick (float now, bool& set, float& th)
        {
            ++learnTicks;
            learnTrace.push_back (now);

            if (learnTicks > learnEdgeTicks && learnTicks <= learnTotalTicks - learnEdgeTicks)
                learnPeak = jmax (learnPeak, now);

            if (learnTicks < learnTotalTicks)
                return false;

            learning = false;
            set = false;

            if (learnPeak >= -75.0f)
            {
                th  = jlimit (-80.0f, -10.0f, learnPeak + hysteresisDb);
                set = true;
            }

            return true;
        }
    };

    bool same (float a, float b) { return std::memcmp (&a, &b, sizeof a) == 0; }

    /** Runs one trace through both, tick by tick. True when they agree on everything a player could
        see: the trace, the live dashed line every tick, when it ends, and what it writes. */
    bool agree (const std::vector<float>& trace)
    {
        Before old;
        GateLearn now (hysteresisDb);
        old.start();
        now.begin();

        for (size_t i = 0; i < trace.size(); ++i)
        {
            bool set = false;
            float th = 0.0f;
            const bool oldDone = old.tick (trace[i], set, th);
            const bool newDone = now.hear (trace[i]);

            if (oldDone != newDone || ! same (old.learnPendingDb(), now.pendingDb())
                || old.learnTrace != now.heard() || old.learning != now.isRunning())
                return false;

            if (newDone)
            {
                const float result = now.resultDb();
                return set ? same (result, th) : same (result, GateLearn::nothing);
            }
        }
        return false;   // every trace here is a whole take
    }

    /** What a finished take sets for `trace`, or GateLearn::nothing. */
    float learned (const std::vector<float>& trace)
    {
        GateLearn take (hysteresisDb);
        take.begin();
        for (const float db : trace)
            if (take.hear (db))
                return take.resultDb();
        return 12345.0f;   // never finished: a failure every check below will show
    }

    std::vector<float> flat (float db) { return std::vector<float> ((size_t) GateLearn::totalTicks, db); }

    /** A deterministic generator, so a failure is the same failure on every machine. */
    struct Lcg
    {
        uint32_t s;
        float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f; }
    };
}

int main()
{
    std::printf ("core::GateLearn — the gate's LEARN\n\n");

    // The window: three seconds at 30 Hz, half a second thrown away at each end.
    check ("ticks in a take", GateLearn::totalTicks, 90);
    check ("ticks thrown away at each end", GateLearn::edgeTicks, 15);
    check ("ticks a second", GateLearn::ticksPerSecond, 30);

    // A steady floor: the threshold lands the hysteresis above it.
    check ("floor -70 -> threshold", learned (flat (-70.0f)), -64.0f);
    check ("floor -60 -> threshold", learned (flat (-60.0f)), -54.0f);

    // The edges are not the floor: the click that started it, the hand leaving the mouse.
    {
        auto t = flat (-70.0f);
        for (int i = 0; i < GateLearn::edgeTicks; ++i)
        {
            t[(size_t) i] = -20.0f;
            t[(size_t) (GateLearn::totalTicks - 1 - i)] = -20.0f;
        }
        check ("loud edges are ignored", learned (t), -64.0f);
    }

    // The first and the last trusted tick count; one either side of them does not.
    {
        auto t = flat (-70.0f);
        t[(size_t) GateLearn::edgeTicks] = -50.0f;                                  // tick 16
        check ("first trusted tick counts", learned (t), -44.0f);
        t = flat (-70.0f);
        t[(size_t) (GateLearn::totalTicks - GateLearn::edgeTicks - 1)] = -50.0f;    // tick 75
        check ("last trusted tick counts", learned (t), -44.0f);
        t = flat (-70.0f);
        t[(size_t) (GateLearn::edgeTicks - 1)] = -50.0f;                            // tick 15
        t[(size_t) (GateLearn::totalTicks - GateLearn::edgeTicks)] = -50.0f;        // tick 76
        check ("a tick either side of the window does not", learned (t), -64.0f);
    }

    // A muted input teaches nothing; -75 is the quietest that does.
    check ("muted input (-90) sets nothing", learned (flat (-90.0f)), GateLearn::nothing);
    check ("-75.01 sets nothing", learned (flat (-75.01f)), GateLearn::nothing);
    check ("-75 is heard", learned (flat (-75.0f)), -69.0f);

    // A loud room does not push the threshold past -10.
    check ("floor -5 -> clamped at -10", learned (flat (-5.0f)), -10.0f);

    // The live dashed line: nothing until the window has heard something, then the threshold-to-be,
    // clamped at -80 below; nothing again once the take is over or stopped.
    {
        GateLearn take (hysteresisDb);
        checkTrue ("not running before begin", ! take.isRunning());
        take.begin();
        for (int i = 0; i < GateLearn::edgeTicks; ++i)
            take.hear (-40.0f);
        check ("pending: nothing while only the edge is heard", take.pendingDb(), GateLearn::nothing);
        take.hear (-100.0f);
        check ("pending: a -100 floor shows at -80", take.pendingDb(), -80.0f);
        take.hear (-66.0f);
        check ("pending: follows the peak", take.pendingDb(), -60.0f);
        take.stop();
        check ("pending: nothing once stopped", take.pendingDb(), GateLearn::nothing);
        check ("a stopped take keeps what it heard", (double) take.heard().size(), GateLearn::edgeTicks + 2);
        take.begin();
        checkTrue ("begin starts over", take.isRunning() && take.heard().empty()
                                            && take.pendingDb() == GateLearn::nothing);
    }

    // The quiet window, as a second measurement would ask for it.
    {
        GateLearn take (hysteresisDb);
        take.begin();
        int trusted = 0;
        while (! take.hear (-70.0f))
            trusted += take.trusting() ? 1 : 0;
        check ("ticks trusted in a take", trusted, GateLearn::totalTicks - 2 * GateLearn::edgeTicks);
    }

    // AND EXACTLY WHAT GATECONSOLE DID: every trace above, and random ones — floors, spikes, silences,
    // anywhere from muted to loud.
    {
        std::vector<std::vector<float>> traces { flat (-70.0f), flat (-60.0f), flat (-90.0f), flat (-75.0f),
                                                 flat (-75.01f), flat (-5.0f), flat (-100.0f), flat (-119.5f) };
        Lcg rng { 20261016u };
        for (int n = 0; n < 4000; ++n)
        {
            std::vector<float> t ((size_t) GateLearn::totalTicks);
            const float floor = -110.0f + 105.0f * rng.next();
            const float jitter = 12.0f * rng.next();
            for (auto& db : t)
                db = floor + jitter * (rng.next() - 0.5f);
            for (int spikes = (int) (4.0f * rng.next()); spikes > 0; --spikes)
                t[(size_t) (rng.next() * (float) GateLearn::totalTicks) % t.size()] = -90.0f + 85.0f * rng.next();
            traces.push_back (std::move (t));
        }

        int disagreements = 0;
        for (const auto& t : traces)
            disagreements += agree (t) ? 0 : 1;

        std::printf ("%-60s %d traces\n", "compared with GateConsole's own arithmetic", (int) traces.size());
        check ("traces where anything differs", disagreements, 0);
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
