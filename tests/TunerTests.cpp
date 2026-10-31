// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::PitchTracker — the tuner's ear, checked against signals with
// a KNOWN pitch. Sines because their answer is exact; a sawtooth because a real string is not a
// sine and a strong harmonic is how tuners read octaves wrong; a Karplus-Strong pluck because it
// decays and drifts the way a string does. Returns non-zero on any failure.
//
// And the STROBE (core::TunerStrobe): tones a known fraction of a cent off their note, read off the
// stripes' speed; its phase across ticks of any length; the note it holds to; that it allocates nothing
// (this program counts every allocation of its own — it is kept apart from the gates binary, so the count
// is nobody else's); and that the ear beside it, HUM label and all, reads bit for bit as without it.
//
// JUCE-free, like the code under test: links nothing at all.

#include "core/PitchTracker.h"
#include "core/TunerEar.h"
#include "core/TunerStrobe.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <thread>
#include <vector>

// THE STROBE ALLOCATES NOTHING: every allocation in this program is counted, so a tick can be held to none.
// (gcc reads malloc in a replaced new and free in its delete as a mismatched pair; they are the pair.)
namespace { std::atomic<long> allocations { 0 }; }
#if defined (__GNUC__) && ! defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new (std::size_t n)
{
    allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n > 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept                 { std::free (p); }
void operator delete (void* p, std::size_t) noexcept    { std::free (p); }
#if defined (__GNUC__) && ! defined (__clang__)
 #pragma GCC diagnostic pop
#endif

using orbitamp::core::PitchTracker;

namespace
{
    int failures = 0;

    constexpr double pi = 3.14159265358979323846;
    constexpr int    windowLen = orbitamp::core::TunerTap::size;   // what the tap hands the panel

    void check (const char* what, double got, double want, double tol)
    {
        const bool ok = std::fabs (got - want) <= tol;
        if (! ok)
            ++failures;

        std::printf ("%-52s %9.3f  (want %8.3f +-%.2f)  %s\n", what, got, want, tol, ok ? "ok" : "FAIL");
    }

    void checkTrue (const char* what, bool ok)
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s\n", what, ok ? "ok" : "FAIL");
    }

    double centsBetween (double got, double want)
    {
        return 1200.0 * std::log2 (got / want);
    }

    /** The tracker's answer for a signal, fed the same way the panel feeds it. */
    PitchTracker::Reading analyse (const std::vector<float>& signal, double sampleRate)
    {
        PitchTracker t;
        t.prepare (sampleRate);
        return t.analyse (signal.data(), (int) signal.size());
    }

    std::vector<float> sine (double hz, double sampleRate, float amp = 0.5f, float dc = 0.0f)
    {
        std::vector<float> s ((size_t) windowLen);
        for (int i = 0; i < windowLen; ++i)
            s[(size_t) i] = dc + amp * (float) std::sin (2.0 * pi * hz * i / sampleRate);
        return s;
    }

    std::vector<float> sawtooth (double hz, double sampleRate)
    {
        std::vector<float> s ((size_t) windowLen);
        for (int i = 0; i < windowLen; ++i)
        {
            const double phase = std::fmod (hz * i / sampleRate, 1.0);
            s[(size_t) i] = 0.5f * (float) (2.0 * phase - 1.0);
        }
        return s;
    }

    /** Deterministic noise — the gate must not roll dice. */
    float lcgNoise (std::uint32_t& seed)
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 9) / 4194304.0f - 1.0f;
    }

    /** Karplus-Strong: y[n] = (y[n-N] + y[n-N-1]) / 2. The averager's group delay is exactly half
        a sample at every frequency, so the partials sit at k * sr / (N + 0.5) — a pluck with an
        exact expected pitch. */
    std::vector<float> pluck (int N, int total)
    {
        std::uint32_t seed = 1;
        std::vector<float> y ((size_t) total);
        for (int n = 0; n <= N; ++n)
            y[(size_t) n] = lcgNoise (seed);
        for (int n = N + 1; n < total; ++n)
            y[(size_t) n] = 0.5f * (y[(size_t) (n - N)] + y[(size_t) (n - N - 1)]);
        return y;
    }

    // ---- THE STROBE (core::TunerStrobe) ----------------------------------------------------------

    /** A string's worth of partials — 1 to 6, the 2nd over the 1st, as a wound string rings — at `hz`, each
        partial p optionally `stretch[p]` cents off p * hz. Generated sample by sample, in double. */
    struct Tone
    {
        double hz = 110.0, sr = 48000.0;
        std::array<double, 7> stretch {};
        std::array<double, 7> amp { 0.0, 0.30, 0.36, 0.18, 0.14, 0.08, 0.05 };
        double vibratoCents = 0.0, vibratoHz = 0.0;
        std::array<double, 7> phase {};
        long n = 0;

        float next()
        {
            const double t   = (double) n++ / sr;
            const double bend = vibratoCents != 0.0 ? std::pow (2.0, vibratoCents * std::sin (2.0 * pi * vibratoHz * t) / 1200.0) : 1.0;
            double y = 0.0;
            for (int p = 1; p < 7; ++p)
            {
                y += amp[(size_t) p] * std::sin (2.0 * pi * phase[(size_t) p]);
                phase[(size_t) p] += p * hz * bend * std::pow (2.0, stretch[(size_t) p] / 1200.0) / sr;
                phase[(size_t) p] -= std::floor (phase[(size_t) p]);
            }
            return (float) y;
        }
    };

    /** The tuner as the processor runs it: 512-sample blocks into the tap, and every tick — uneven, as a
        timer's are — the ear, then the strobe. `each` sees every tick. */
    template <typename Each>
    void playThroughStrobe (Tone& tone, double seconds, orbitamp::core::TunerTap& tap, orbitamp::core::TunerEar& ear,
                            orbitamp::core::TunerStrobe& strobe, unsigned& nowMs, long& played, Each&& each)
    {
        static constexpr int ticks[] = { 1536, 1600, 1700, 1650 };
        std::vector<float> block (512);
        const long end = played + (long) (seconds * tone.sr);
        int sinceTick = 0, t = 0;
        while (played < end)
        {
            for (auto& v : block)
                v = tone.next();
            tap.write (block.data(), (int) block.size());
            played += (long) block.size();
            sinceTick += (int) block.size();
            if (sinceTick >= ticks[t % 4])
            {
                nowMs += (unsigned) std::lround (1000.0 * sinceTick / tone.sr);
                ear.update (tap, nowMs);
                strobe.update (tap, ear);
                each (played);
                sinceTick = 0;
                ++t;
            }
        }
    }

    /** What band `k` reads, in cents of its partial, from its phase between seconds `from` and `to`. */
    struct StrobeRun
    {
        std::array<double, 3> cents {};
        int note = -1;
    };

    StrobeRun measureStrobe (Tone tone, double from = 1.0, double to = 3.0)
    {
        using namespace orbitamp::core;
        TunerTap tap;
        TunerEar ear;
        TunerStrobe strobe;
        ear.prepare (tone.sr);
        strobe.prepare (tone.sr);
        unsigned now = 0;
        long played = 0;
        std::array<double, 3> at {}, after {};
        long atN = 0, afterN = 0;
        playThroughStrobe (tone, to + 0.05, tap, ear, strobe, now, played, [&] (long n)
        {
            if (atN == 0 && n >= (long) (from * tone.sr)) { atN = n; for (int k = 0; k < 3; ++k) at[(size_t) k] = strobe.turns (k); }
            if (afterN == 0 && n >= (long) (to * tone.sr)) { afterN = n; for (int k = 0; k < 3; ++k) after[(size_t) k] = strobe.turns (k); }
        });

        StrobeRun r;
        r.note = strobe.note();
        const double f = strobe.noteFrequency(), seconds = (double) (afterN - atN) / tone.sr;
        for (int k = 0; k < 3; ++k)
            r.cents[(size_t) k] = f > 0.0 ? 1200.0 * std::log2 (1.0 + (after[(size_t) k] - at[(size_t) k]) / seconds / f) : 999.0;
        return r;
    }

    /** ACROSS A RESTART — a note change, a stall, a flip, a new A: what each band's stripes did from the
        first tick after it. While a band is blank (strength 0: its filters are settling) every step it takes
        is false motion; once it shows, each step's miss from `expected` stripes a second — but for the tick it
        came back in, which was blank for part of it. The step is the stripes' change in a tick, wrapped to the
        nearest. */
    struct Across
    {
        double falseMotion = 0.0, worstMiss = 0.0;
        int    blankTicks = 0, shownTicks = 0;
    };

    Across watchAcross (orbitamp::core::TunerStrobe& s, orbitamp::core::TunerTap& tap, Tone& tone, double seconds,
                        double expected, const std::function<void()>& atFirstTick = {})
    {
        static constexpr int ticks[] = { 1536, 1600, 1700, 1650 };
        Across a;
        std::array<double, 3> was { s.stripes (0), s.stripes (1), s.stripes (2) };
        std::array<int, 3> shown {}, blanked {};
        std::vector<float> chunk;
        const long end = (long) (seconds * tone.sr);
        for (long played = 0, t = 0; played < end; ++t)
        {
            const int n = ticks[t % 4];
            chunk.resize ((size_t) n);
            for (auto& v : chunk)
                v = tone.next();
            tap.write (chunk.data(), n);
            played += n;
            if (t == 0 && atFirstTick)
                atFirstTick();
            s.listen (tap);
            for (int k = 0; k < 3; ++k)
            {
                double step = s.stripes (k) - was[(size_t) k];
                step -= std::round (step);
                was[(size_t) k] = s.stripes (k);
                if (s.strength (k) <= 0.0f && shown[(size_t) k] == 0)
                {
                    a.falseMotion = std::max (a.falseMotion, std::fabs (step));
                    ++a.blankTicks;
                    blanked[(size_t) k] = 1;
                }
                else if (s.strength (k) > 0.0f && (shown[(size_t) k]++ > 0 || blanked[(size_t) k] == 0))
                {
                    a.worstMiss = std::max (a.worstMiss, std::fabs (step - expected * n / tone.sr));
                    ++a.shownTicks;
                }
            }
        }
        return a;
    }

    void reportAcross (const char* what, const Across& a)
    {
        char name[96];
        std::snprintf (name, sizeof (name), "strobe: %s, false motion settling", what);
        check (name, a.falseMotion, 0.0, 0.01);
        std::snprintf (name, sizeof (name), "strobe: %s, worst step miss after", what);
        check (name, a.worstMiss, 0.0, 0.01);
        checkTrue ("...and it shows again", a.shownTicks > 30);
    }

    /** The stripes a second a string at `hz` moves against a strobe held to `noteHz`. */
    double stripesPerSecond (double hz, double noteHz)
    {
        return (hz - noteHz) * orbitamp::core::TunerStrobe::speedHz / noteHz;
    }

    void strobeTests()
    {
        using namespace orbitamp::core;

        // The stripes' speed IS the error: a string x cents off its note turns every band x cents' worth.
        struct Case { const char* what; double note; double cents; double sr; };
        const Case cases[] = {
            { "A2",  110.0,    0.1, 48000.0 }, { "A2", 110.0,  0.5, 48000.0 }, { "A2",  110.0,  2.0, 48000.0 },
            { "A2",  110.0,   10.0, 48000.0 }, { "A2", 110.0, -3.0, 48000.0 }, { "A2",  110.0, -0.1, 48000.0 },
            { "A2",  110.0,   30.0, 48000.0 }, { "E1", 41.2034, 0.5, 48000.0 }, { "E1", 41.2034, -3.0, 48000.0 },
            { "A1",  55.0,     2.0, 48000.0 }, { "E4", 329.628, 0.1, 48000.0 }, { "E4", 329.628, -10.0, 48000.0 },
            { "A2",  110.0,    2.0, 44100.0 }, { "A2", 110.0,  2.0, 96000.0 }, { "E1", 41.2034, 0.1, 96000.0 },
        };
        double worst = 0.0;
        for (const auto& c : cases)
        {
            Tone tone;
            tone.hz = c.note * std::pow (2.0, c.cents / 1200.0);
            tone.sr = c.sr;
            const auto r = measureStrobe (tone);
            for (int k = 0; k < 3; ++k)
            {
                char name[96];
                std::snprintf (name, sizeof (name), "strobe: %s %+.1f c at %.0f k, band %d reads", c.what, c.cents, c.sr / 1000.0,
                               TunerStrobe::partials[(size_t) k]);
                check (name, r.cents[(size_t) k], c.cents, 0.05);
                worst = std::max (worst, std::fabs (r.cents[(size_t) k] - c.cents));
            }
            checkTrue ("...held to the note it is nearest", r.note == PitchTracker::nearestNote ((float) c.note).midi);
        }
        std::printf ("strobe: the worst band of all, cents off            %.4f\n", worst);

        // The bands against each other: a string whose 2nd partial runs 3 c sharp and 4th 6 c, in tune
        // itself — the octave band drifts 3 c, the next 6, the fundamental stands.
        {
            Tone tone;
            tone.stretch[2] = 3.0;
            tone.stretch[4] = 6.0;
            const auto r = measureStrobe (tone);
            check ("strobe: stretched partials, fundamental band", r.cents[0], 0.0, 0.05);
            check ("strobe: stretched partials, octave band",      r.cents[1], 3.0, 0.05);
            check ("strobe: stretched partials, two-octave band",  r.cents[2], 6.0, 0.05);
        }

        // PHASE CONTINUITY: the same samples cut into ticks of any length leave the same phase, bit for bit.
        {
            TunerTap tap;
            TunerStrobe even, uneven;
            even.prepare (48000.0);
            uneven.prepare (48000.0);
            even.hold (45, 440.0f);
            uneven.hold (45, 440.0f);

            Tone tone;
            tone.hz = 110.0 * std::pow (2.0, 4.0 / 1200.0);
            const int cuts[] = { 37, 2000, 1, 900, 4096, 333, 1600, 7, 12000, 64 };
            std::vector<float> chunk;
            bool same = true;
            int compared = 0;
            double turnsAt10 = 0.0;
            long samples = 0, samplesAt10 = 0;
            for (int round = 0; round < 40; ++round)
            {
                for (const int cut : cuts)
                {
                    chunk.resize ((size_t) cut);
                    for (auto& v : chunk)
                        v = tone.next();
                    tap.write (chunk.data(), cut);
                    uneven.listen (tap);
                    samples += cut;
                }
                even.listen (tap);
                if (round == 10)
                {
                    turnsAt10   = even.turns (0);
                    samplesAt10 = samples;
                }
                for (int k = 0; k < 3; ++k)
                    same = same && even.turns (k) == uneven.turns (k) && even.stripes (k) == uneven.stripes (k);
                ++compared;
            }
            checkTrue ("strobe: ticks of uneven length leave the phase bit for bit", same && compared == 40);
            check ("...and reads the 4 c it was given",
                   1200.0 * std::log2 (1.0 + (even.turns (0) - turnsAt10) / ((double) (samples - samplesAt10) / 48000.0) / 110.0), 4.0, 0.05);
        }

        // THE NOTE HOLDS: a string halfway between A2 and A#2, wavering 6 c either side, never flips the
        // strobe — though the ear's nearest note does.
        {
            Tone tone;
            tone.hz = 110.0 * std::pow (2.0, 50.0 / 1200.0);
            tone.vibratoCents = 6.0;
            tone.vibratoHz    = 3.0;
            TunerTap tap;
            TunerEar ear;
            TunerStrobe strobe;
            ear.prepare (tone.sr);
            strobe.prepare (tone.sr);
            unsigned now = 0;
            long played = 0;
            int first = -1, flips = 0, nearestFlips = 0, lastNearest = -1;
            playThroughStrobe (tone, 4.0, tap, ear, strobe, now, played, [&] (long)
            {
                if (! ear.live())
                    return;
                if (first < 0)
                    first = strobe.note();
                flips += strobe.note() != first ? 1 : 0;
                const int nearest = ear.nearestNote().midi;
                nearestFlips += lastNearest >= 0 && nearest != lastNearest ? 1 : 0;
                lastNearest = nearest;
            });
            check ("strobe: between two notes, ticks off its first note", flips, 0.0, 0.0);
            checkTrue ("...though the ear's nearest note flipped", nearestFlips > 0);

            // ...and 70 c up, the next note is the nearer by far: it moves.
            TunerStrobe s;
            s.prepare (48000.0);
            s.follow (true, 110.0f * std::pow (2.0f, 45.0f / 1200.0f), 440.0f);
            const int a2 = s.note();
            s.follow (true, 110.0f * std::pow (2.0f, 60.0f / 1200.0f), 440.0f);
            const int stillA2 = s.note();
            s.follow (true, 110.0f * std::pow (2.0f, 70.0f / 1200.0f), 440.0f);
            checkTrue ("strobe: A2 +45, +60 holds A2, +70 takes A#2", a2 == 45 && stillA2 == 45 && s.note() == 46);
            s.follow (false, 0.0f, 440.0f);
            s.follow (true, 110.0f * std::pow (2.0f, 55.0f / 1200.0f), 440.0f);
            checkTrue ("...and a note after silence takes its own nearest", s.note() == 46);
            s.follow (false, 0.0f, 440.0f);
            s.follow (true, 110.0f * std::pow (2.0f, 45.0f / 1200.0f), 440.0f);
            checkTrue ("...whichever side it is on", s.note() == 45);

            // The tuner switched off while the player reads by the strobe: still a strobe; the needle's: not.
            s.stop (true);
            const bool offButChosen = s.on();
            s.stop();
            checkTrue ("strobe: off but chosen shows as a strobe, the needle's mode not", offButChosen && ! s.on());
        }

        // NO ALLOCATION: a strobe tick allocates nothing — the ear's ticks are not counted.
        {
            Tone tone;
            TunerTap tap;
            TunerEar ear;
            TunerStrobe strobe;
            ear.prepare (tone.sr);
            strobe.prepare (tone.sr);
            std::vector<float> block (512);
            long during = 0;
            for (int b = 0; b < 400; ++b)
            {
                for (auto& v : block)
                    v = tone.next();
                tap.write (block.data(), 512);
                if (b % 3 == 2)
                {
                    ear.update (tap, (unsigned) b * 11u);
                    const long before = allocations.load();
                    strobe.update (tap, ear);
                    during += allocations.load() - before;
                }
            }
            check ("strobe: allocations in its ticks", (double) during, 0.0, 0.0);
        }

        // THE EAR IS UNTOUCHED: with the strobe pumped beside it, every tick of the ear — the needle, the
        // reading, the note, the HUM label — is what it is without, bit for bit. Hum, then a string, then hum.
        {
            const auto run = [] (bool withStrobe)
            {
                std::vector<float> trace;
                TunerTap tap;
                TunerEar ear;
                TunerStrobe strobe;
                ear.prepare (48000.0);
                strobe.prepare (48000.0);
                std::vector<float> block (512);
                long n = 0;
                int hum50 = 0;
                for (int b = 0; b < 1200; ++b)
                {
                    const bool string = b >= 400 && b < 700;
                    for (auto& v : block)
                    {
                        const double t = (double) n++ / 48000.0;
                        v = (float) (0.02 * std::sin (2.0 * pi * 50.0 * t) + 0.004 * std::sin (2.0 * pi * 100.0 * t)
                                     + (string ? 0.3 * std::exp (-(t - 400 * 512 / 48000.0)) * std::sin (2.0 * pi * 146.83 * t) : 0.0));
                    }
                    tap.write (block.data(), 512);
                    if (b % 3 == 2)
                    {
                        ear.update (tap, (unsigned) (n * 1000 / 48000));
                        if (withStrobe)
                            strobe.update (tap, ear);
                        trace.push_back (ear.needle());
                        trace.push_back (ear.hz());
                        trace.push_back ((float) ear.nearestNote().midi);
                        trace.push_back ((float) ear.humMains());
                        hum50 += ear.humMains() == 50 ? 1 : 0;
                    }
                }
                trace.push_back ((float) hum50);
                return trace;
            };
            const auto alone = run (false), beside = run (true);
            checkTrue ("strobe: the ear beside it reads bit for bit as alone",
                       alone.size() == beside.size() && std::memcmp (alone.data(), beside.data(), alone.size() * sizeof (float)) == 0);
            std::printf ("strobe: ticks labelled HUM 50 with the strobe pumped %.0f\n", (double) beside.back());
            checkTrue ("...and the hum still reads HUM 50 in STROBE mode", beside.back() > 100.0f);
        }

        // ---- RESTARTS: whatever starts its filters again shows nothing until they have settled, and then
        //      the true motion — never the start-up's lurch (a backward kick on every new string). ----
        {
            // A NOTE CHANGE: A2 + 2 c, then E2 + 2 c.
            TunerTap tap;
            TunerStrobe s;
            s.prepare (48000.0);
            s.hold (45, 440.0f);
            Tone tone;
            tone.hz = 110.0 * std::pow (2.0, 2.0 / 1200.0);
            watchAcross (s, tap, tone, 2.0, 0.0);
            tone.hz = 82.4069 * std::pow (2.0, 2.0 / 1200.0);
            reportAcross ("A2 to E2 +2 c", watchAcross (s, tap, tone, 2.0, stripesPerSecond (tone.hz, 82.4069),
                                                     [&s] { s.hold (40, 440.0f); }));
        }
        {
            // ...on a bass: E1 + 2 c from A1.
            TunerTap tap;
            TunerStrobe s;
            s.prepare (48000.0);
            s.hold (33, 440.0f);
            Tone tone;
            tone.hz = 55.0;
            watchAcross (s, tap, tone, 2.0, 0.0);
            tone.hz = 41.2034 * std::pow (2.0, 2.0 / 1200.0);
            reportAcross ("A1 to E1 +2 c", watchAcross (s, tap, tone, 3.0, stripesPerSecond (tone.hz, 41.2034),
                                                     [&s] { s.hold (28, 440.0f); }));
        }
        {
            // A STALL: E1 in tune at 96 k, the strobe not pumped for 800 ms — more than the tap keeps for it.
            TunerTap tap;
            TunerStrobe s;
            s.prepare (96000.0);
            s.hold (28, 440.0f);
            Tone tone;
            tone.hz = 41.2034;
            tone.sr = 96000.0;
            watchAcross (s, tap, tone, 2.0, 0.0);
            std::vector<float> lost (76800);
            for (auto& v : lost)
                v = tone.next();
            tap.write (lost.data(), (int) lost.size());
            reportAcross ("E1 after an 800 ms stall at 96 k", watchAcross (s, tap, tone, 3.0, 0.0));
        }
        {
            // THE FLIP: a string at A2 + 60 c, read first at + 45 (A2 held), then at + 70: A#2, 40 c flat.
            TunerTap tap;
            TunerStrobe s;
            s.prepare (48000.0);
            Tone tone;
            tone.hz = 110.0 * std::pow (2.0, 60.0 / 1200.0);
            s.follow (true, 110.0f * std::pow (2.0f, 45.0f / 1200.0f), 440.0f);
            watchAcross (s, tap, tone, 2.0, 0.0);
            reportAcross ("the flip to A#2 -40 c", watchAcross (s, tap, tone, 2.0, stripesPerSecond (tone.hz, 116.5409),
                                                             [&s] { s.follow (true, 110.0f * std::pow (2.0f, 70.0f / 1200.0f), 440.0f); }));
        }
        {
            // A NEW A: A2 at 440, the reference moved to 442 — now 7.85 c flat.
            TunerTap tap;
            TunerStrobe s;
            s.prepare (48000.0);
            s.hold (45, 440.0f);
            Tone tone;
            watchAcross (s, tap, tone, 2.0, 0.0);
            reportAcross ("A2 when A moves to 442", watchAcross (s, tap, tone, 2.0, stripesPerSecond (110.0, 110.5),
                                                              [&s] { s.hold (45, 442.0f); }));
        }

        // ---- THE STRING DAMPED: the ear holds its note 700 ms more, the input is the interface's hiss —
        //      at -70 dBFS, and at -68, -65 and -62, just under the level floor (-60). The bands go blank
        //      within 100 ms and stay blank while the ear holds: never stripes thrown about by noise. (What a
        //      band did in the tick it went blank in is never drawn.) ----
        for (const double hissDb : { -70.0, -68.0, -65.0, -62.0 })
        for (const double hz : { 110.0, 41.2034 })
        {
            const float hiss = (float) (std::pow (10.0, hissDb / 20.0) * std::sqrt (3.0));   // lcgNoise's RMS is 1/sqrt 3
            Tone tone;
            tone.hz = hz;
            TunerTap tap;
            TunerEar ear;
            TunerStrobe s;
            ear.prepare (tone.sr);
            s.prepare (tone.sr);
            std::vector<float> block (512);
            std::uint32_t seed = 7;
            long n = 0;
            unsigned now = 0;
            const long stopAt = (long) (1.5 * tone.sr);
            double blankAfter = -1.0, motion = 0.0, liveAfter = 0.0;
            int shownAfter = 0;
            std::array<double, 3> was {};
            std::array<float, 3> wasShown {};
            for (int b = 0; n < (long) (2.5 * tone.sr); ++b)
            {
                for (auto& v : block)
                    v = n++ < stopAt ? tone.next() : hiss * lcgNoise (seed);
                tap.write (block.data(), 512);
                if (b % 3 != 2)
                    continue;
                ear.update (tap, now += 32);
                s.update (tap, ear);
                const double t = (double) (n - stopAt) / tone.sr;
                const bool blank = s.strength (0) <= 0.0f && s.strength (1) <= 0.0f && s.strength (2) <= 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    double step = s.stripes (k) - was[(size_t) k];
                    step -= std::round (step);
                    was[(size_t) k] = s.stripes (k);
                    if (t > 0.0 && ear.live() && s.strength (k) > 0.0f && wasShown[(size_t) k] > 0.0f)
                        motion = std::max (motion, std::fabs (step));
                    wasShown[(size_t) k] = s.strength (k);
                }
                if (t > 0.0 && ear.live())
                    liveAfter = t;
                if (t > 0.0 && blank && blankAfter < 0.0)
                    blankAfter = t;
                else if (blankAfter >= 0.0 && ! blank && ear.live())
                    ++shownAfter;
            }
            char name[96];
            std::snprintf (name, sizeof (name), "strobe: %.0f Hz damped, hiss %.0f dB, blank after, s", hz, hissDb);
            check (name, blankAfter, 0.0, 0.1);
            std::snprintf (name, sizeof (name), "strobe: %.0f Hz damped, hiss %.0f dB, ticks shown again", hz, hissDb);
            check (name, shownAfter, 0.0, 0.0);
            std::snprintf (name, sizeof (name), "strobe: %.0f Hz damped, hiss %.0f dB, motion", hz, hissDb);
            check (name, motion, 0.0, 0.01);
            checkTrue ("...while the ear held the note on for half a second more", liveAfter > 0.5);
        }

        // ---- THE HUSH LETS GO when a string comes again. Each a 48 kHz A2 through the ear; `shape` scales the
        //      tone sample by sample, the hiss under it at `hissDb`. Reported: when, counted from `from` seconds,
        //      the bands show again after having been blank, and how many times they went blank once shown. ----
        {
            const auto run = [] (const std::function<double (double)>& shape, double seconds, double from, double hissDb = -65.0)
            {
                Tone tone;
                TunerTap tap;
                TunerEar ear;
                TunerStrobe s;
                ear.prepare (tone.sr);
                s.prepare (tone.sr);
                std::vector<float> block (512);
                std::uint32_t seed = 11;
                const float hiss = (float) (std::pow (10.0, hissDb / 20.0) * std::sqrt (3.0));
                long n = 0;
                unsigned now = 0;
                double shownAt = -1.0;
                int blanks = 0;
                bool was = false, blankSeen = false;
                for (int b = 0; n < (long) (seconds * tone.sr); ++b)
                {
                    for (auto& v : block)
                    {
                        const double t = (double) n++ / tone.sr;
                        v = (float) (shape (t) * tone.next()) + hiss * lcgNoise (seed);
                    }
                    tap.write (block.data(), 512);
                    if (b % 3 != 2)
                        continue;
                    ear.update (tap, now += 32);
                    s.update (tap, ear);
                    const double t = (double) n / tone.sr;
                    const bool shown = s.strength (0) > 0.0f;
                    blankSeen = blankSeen || (t > from && ! shown);
                    if (blankSeen && shown && shownAt < 0.0)
                        shownAt = t - from;
                    if (shownAt >= 0.0 && was && ! shown && ear.live())
                        ++blanks;
                    was = shown;
                }
                return std::pair<double, int> { shownAt, blanks };
            };

            // Damped for 0.3 s — the ear still holds the note — then plucked again: shown within the lock-on.
            const auto replucked = run ([] (double t) { return t < 1.0 || t >= 1.3 ? 1.0 : 0.0; }, 2.0, 1.3);
            check ("strobe: plucked again 0.3 s after a damping, shown after, s", replucked.first, 0.2, 0.05);

            // Plucked again with an attack 40 dB over its sustain, gone in a few milliseconds — it falls like a
            // damping: the sustain still shows, and stays.
            const auto popped = run ([] (double t) { return t < 1.0 ? 1.0 : 0.01 * (1.0 + 100.0 * std::exp (-(t - 1.0) / 0.003)); },
                                     2.5, 1.0);
            check ("strobe: a sustain after a falling attack, shown after, s", popped.first, 0.3, 0.1);
            check ("...and went blank again", popped.second, 0.0, 0.0);

            // A note hovering at the floor: A2 at -59.7 dBFS RMS, swinging 0.5 dB either way once a second, no
            // hiss — once it shows, it never flickers off.
            const double atFloor = std::pow (10.0, -59.7 / 20.0) / 0.3745;   // the tone's RMS at 1.0 is 0.3745
            const auto hovering = run ([atFloor] (double t) { return atFloor * std::pow (10.0, 0.5 * std::sin (2.0 * pi * t) / 20.0); }, 4.0, 0.0, -200.0);
            checkTrue ("strobe: a note hovering at the floor shows", hovering.first >= 0.0);
            check ("...and never went blank", hovering.second, 0.0, 0.0);
        }

        // ---- A QUIET, DETUNED HIGH NOTE SHOWS: what the ear hears, the strobe shows — a sine just over the
        //      level floor (peak 0.0025, -55 dBFS RMS), E5, B5 and E6, every 5 c from -50 to +50. The band's own
        //      filter takes a fast beat down 40 dB and more (E6 +45 c beats at 34.6 Hz), so the floor is the
        //      input's, never the filtered band's. Of the ticks the ear names the note, all but the lock-on. ----
        {
            double worstShare = 1.0;
            char worstCase[64] = "";
            for (const double note : { 659.255, 987.767, 1318.51 })
                for (int cents = -50; cents <= 50; cents += 5)
                {
                    const double hz = note * std::pow (2.0, cents / 1200.0), sr = 48000.0;
                    TunerTap tap;
                    TunerEar ear;
                    TunerStrobe s;
                    ear.prepare (sr);
                    s.prepare (sr);
                    std::vector<float> block (512);
                    long n = 0;
                    unsigned now = 0;
                    int heardTicks = 0, shownTicks = 0;
                    for (int b = 0; n < (long) (3.0 * sr); ++b)
                    {
                        for (auto& v : block)
                            v = (float) (0.0025 * std::sin (2.0 * pi * hz * (double) n++ / sr));
                        tap.write (block.data(), 512);
                        if (b % 3 != 2)
                            continue;
                        ear.update (tap, now += 32);
                        s.update (tap, ear);
                        if (n > (long) (0.5 * sr) && ear.live())
                        {
                            ++heardTicks;
                            shownTicks += s.strength (0) > 0.0f ? 1 : 0;
                        }
                    }
                    const double share = heardTicks > 0 ? (double) shownTicks / heardTicks : 0.0;
                    if (share < worstShare)
                    {
                        worstShare = share;
                        std::snprintf (worstCase, sizeof (worstCase), "%.0f Hz %+d c", note, cents);
                    }
                    if (note > 1300.0 && cents == 45)
                        check ("strobe: E6 +45 c at the floor, share of heard ticks shown", share, 1.0, 0.0);
                }
            std::printf ("strobe: quiet high notes, the worst: %s\n", worstCase);
            check ("strobe: quiet high notes +-50 c, worst share of heard ticks shown", worstShare, 1.0, 0.0);
        }

        // ---- SILENCE COSTS NOTHING: once the ear hears no note, the strobe demodulates nothing — the filters
        //      would decay into subnormals on the message thread, ~50x the cost of a tick on x86. ----
        {
            Tone tone;
            tone.sr = 96000.0;
            TunerTap tap;
            TunerEar ear;
            TunerStrobe s;
            ear.prepare (tone.sr);
            s.prepare (tone.sr);
            std::vector<float> block (512);
            long n = 0;
            unsigned now = 0;
            int quietTicks = 0, workingQuiet = 0;
            for (int b = 0; n < (long) (16.0 * tone.sr); ++b)
            {
                for (auto& v : block)
                    v = n++ < (long) tone.sr ? tone.next() : 0.0f;
                tap.write (block.data(), 512);
                if (b % 6 != 5)
                    continue;
                ear.update (tap, now += 32);
                s.update (tap, ear);
                if (! ear.live())
                {
                    ++quietTicks;
                    workingQuiet += s.demodulating() ? 1 : 0;
                }
            }
            checkTrue ("strobe: 15 s of silence went by", quietTicks > 400);
            check ("strobe: ticks of silence it demodulated", workingQuiet, 0.0, 0.0);
        }

        // ---- A TAP LAPPED WHILE IT IS READ: what was overwritten under the copy never reaches the strobe —
        //      it is dropped and counted as a gap. A ramp, so a chunk is whole when its samples count on. ----
        {
            TunerTap tap;
            std::atomic<bool> done { false };
            std::thread writer ([&tap, &done]
            {
                std::vector<float> block (16384);
                unsigned next = 0;
                while (! done.load (std::memory_order_relaxed))
                {
                    for (auto& v : block)
                        v = (float) (next++ & 0xffffffu);
                    tap.write (block.data(), (int) block.size());
                }
            });
            std::vector<float> out ((size_t) TunerTap::size);
            unsigned cursor = 0, skipped = 0;
            long chunks = 0, torn = 0, samples = 0;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (1500);
            while (std::chrono::steady_clock::now() < until)
            {
                const unsigned from = cursor;
                const int got = tap.readSince (cursor, out.data(), (int) out.size(), skipped);
                const unsigned first = from + skipped;
                bool whole = true;
                for (int i = 0; i < got && whole; ++i)
                    whole = out[(size_t) i] == (float) ((first + (unsigned) i) & 0xffffffu);
                torn += whole ? 0 : 1;
                chunks += got > 0 ? 1 : 0;
                samples += got;
            }
            done = true;
            writer.join();
            std::printf ("tap: lapped, %ld chunks, %ld samples read\n", chunks, samples);
            check ("tap: lapped while read, torn chunks", (double) torn, 0.0, 0.0);
            checkTrue ("...and it read on", chunks > 100);
        }

        // THE TAP'S NEW SAMPLES: in order, and what the ring could not keep stepped over and counted.
        {
            TunerTap tap;
            std::vector<float> in (30000), out ((size_t) TunerTap::size);
            for (int i = 0; i < 30000; ++i)
                in[(size_t) i] = (float) i;
            unsigned cursor = 0, skipped = 0;
            tap.write (in.data(), 30000);
            const int n = tap.readSince (cursor, out.data(), (int) out.size(), skipped);
            checkTrue ("tap: 30000 new, the oldest beyond the ring's keep stepped over",
                       n == TunerTap::size - TunerTap::guard && skipped == 30000u - (unsigned) n
                       && out[0] == (float) skipped && out[(size_t) n - 1] == 29999.0f && cursor == 30000u);
            tap.write (in.data(), 10);
            const int m = tap.readSince (cursor, out.data(), (int) out.size(), skipped);
            checkTrue ("tap: ...then only what came after", m == 10 && skipped == 0 && out[9] == 9.0f && cursor == 30010u);
        }
    }

    void checkNote (const char* what, double hz, const char* wantName, int wantOctave)
    {
        const auto n  = PitchTracker::nearestNote ((float) hz);
        const bool ok = std::strcmp (PitchTracker::noteName (n.midi), wantName) == 0
                     && PitchTracker::noteOctave (n.midi) == wantOctave;
        if (! ok)
            ++failures;

        std::printf ("%-52s %s%d  (want %s%d)  %s\n", what, PitchTracker::noteName (n.midi),
                     PitchTracker::noteOctave (n.midi), wantName, wantOctave, ok ? "ok" : "FAIL");
    }
}

int main()
{
    // ---- open strings and beyond, as sines, at the rates hosts actually run ----------------------
    // The claim on the box: within half a cent. A string's own pluck drifts more than that.
    const double strings[] = { 82.407, 110.0, 146.832, 196.0, 246.942, 329.628, 659.255, 1318.51 };

    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const double f : strings)
        {
            const auto r = analyse (sine (f, sr), sr);
            char name[96];
            std::snprintf (name, sizeof (name), "sine %.1f Hz @ %.0fk: cents error", f, sr / 1000.0);
            check (name, r.hz > 0.0f ? centsBetween (r.hz, f) : 999.0, 0.0, 0.5);
        }

    // Drop tunings keep working below the guitar's floor: a bass low E.
    {
        const auto r = analyse (sine (41.203, 48000.0), 48000.0);
        check ("sine 41.2 Hz (bass low E): cents error",
               r.hz > 0.0f ? centsBetween (r.hz, 41.203) : 999.0, 0.0, 1.0);
    }

    // A tuner that only reads in-tune strings is a metronome. Seven cents flat must READ seven flat.
    {
        const double detuned = 110.0 * std::pow (2.0, -7.0 / 1200.0);
        const auto   r       = analyse (sine (detuned, 48000.0), 48000.0);
        check ("A2 seven cents flat: reads (cents vs A2)",
               r.hz > 0.0f ? centsBetween (r.hz, 110.0) : 999.0, -7.0, 0.5);
    }

    // ---- harmonics are where octave errors live ------------------------------------------------
    {
        const auto r = analyse (sawtooth (82.407, 48000.0), 48000.0);
        check ("sawtooth low E: cents error (no octave jump)",
               r.hz > 0.0f ? centsBetween (r.hz, 82.407) : 999.0, 0.0, 1.0);
    }

    // ---- a pluck, not a tone: decaying, noisy attack, string-like spectrum ---------------------
    {
        const double sr = 48000.0;
        const int    N  = 436;                       // ~110 Hz
        const double expected = sr / (N + 0.5);

        const auto y = pluck (N, 48000);
        // The window the tap would hold a beat after the attack.
        std::vector<float> window (y.begin() + 14400, y.begin() + 14400 + windowLen);
        const auto r = analyse (window, sr);
        check ("Karplus-Strong pluck ~110 Hz: cents error",
               r.hz > 0.0f ? centsBetween (r.hz, expected) : 999.0, 0.0, 2.0);
        check ("Karplus-Strong pluck: clarity", r.clarity, 1.0, 0.15);
    }

    // ---- clarity is the height of the peak, not of the sample beside it -----------------------
    // A period that falls exactly between two lags is where the interpolated peak stands furthest
    // above both samples. A clean sine there must score as clean as one on a lag.
    {
        const double sr = 48000.0;                       // decimated to exactly 12 kHz
        const auto onLag  = analyse (sine (12000.0 / 9.0, sr), sr);
        const auto between = analyse (sine (12000.0 / 8.5, sr), sr);
        check ("sine on a whole lag: clarity",         onLag.clarity,   1.0, 0.02);
        check ("sine half a lag between: clarity",     between.clarity, 1.0, 0.02);
    }

    // ---- what must NOT read as a note ----------------------------------------------------------
    {
        std::vector<float> silence ((size_t) windowLen, 0.0f);
        checkTrue ("silence: no reading", analyse (silence, 48000.0).hz == 0.0f);
    }
    {
        std::uint32_t seed = 7;
        std::vector<float> noise ((size_t) windowLen);
        for (auto& v : noise)
            v = 0.5f * lcgNoise (seed);
        checkTrue ("white noise: no reading", analyse (noise, 48000.0).hz == 0.0f);
    }
    {
        checkTrue ("sine at -90 dBFS: below the gate",
                   analyse (sine (110.0, 48000.0, 3.0e-5f), 48000.0).hz == 0.0f);
    }

    // A DC offset is a "period" of every length; the tracker must not tune to it.
    {
        const auto r = analyse (sine (110.0, 48000.0, 0.5f, 0.4f), 48000.0);
        check ("A2 riding a DC offset: cents error",
               r.hz > 0.0f ? centsBetween (r.hz, 110.0) : 999.0, 0.0, 0.5);
    }

    // ---- the EAR: a new note starts from itself ------------------------------------------------
    // The history of the note before must not lean on the note after. A string eight cents flat,
    // then a gap, then the same string in tune: the first needle of the new note reads the new note.
    // Both kinds of gap — the note dying out, and a short one inside the hold, a pick attack.
    {
        using orbitamp::core::TunerEar;
        using orbitamp::core::TunerTap;

        const double sr   = 48000.0;
        const double flat = 110.0 * std::pow (2.0, -8.0 / 1200.0);

        const auto windowOf = [sr] (double hz, float amp)
        {
            std::vector<float> w ((size_t) TunerTap::size);
            for (int i = 0; i < TunerTap::size; ++i)
                w[(size_t) i] = amp * (float) std::sin (2.0 * pi * hz * i / sr);
            return w;
        };

        for (const int gapMs : { 900, 200 })
        {
            TunerEar ear;
            TunerTap tap;
            ear.prepare (sr);
            unsigned now = 0;

            const auto hold = [&] (const std::vector<float>& w, int ms)
            {
                tap.write (w.data(), (int) w.size());
                for (int t = 0; t < ms; t += 33)
                    ear.update (tap, now += 33);
            };

            hold (windowOf (flat, 0.3f), 500);
            const double before = ear.needle();

            hold (windowOf (110.0, 0.0f), gapMs);
            tap.write (windowOf (110.0, 0.3f).data(), TunerTap::size);
            ear.update (tap, now += 33);

            char name[96];
            std::snprintf (name, sizeof (name), "ear: 8 c flat, %d ms gap, in tune: first needle", gapMs);
            check (name, ear.needle(), 0.0, 0.5);
            checkTrue ("...and the flat note read flat before it", std::fabs (before + 8.0) < 0.5);
        }
    }

    // ---- the ear through a whole pluck, to its last breath --------------------------------------
    // Sixteen harmonics falling at 1/k, each decaying faster than the one below it (-20 dB/s at the
    // fundamental), a pick burst, a stiff string's sharp partials (B = 1e-4), a -60 dBFS hiss under
    // all of it — three seconds, fed in host blocks, the ear ticked at 30 Hz. What the needle shows
    // last before the note goes is what a player reads when tuning: it must still be the string,
    // not the noise the tail sank into. Six strings, three rates, three seeds. (On the tracker as it
    // was, fourteen of the fifty-four ended outside green, six cents out: the long-lag search missed
    // its peak under the hiss and fell back to the single period's error.)
    {
        using orbitamp::core::TunerEar;
        using orbitamp::core::TunerTap;

        const double open[] = { 82.407, 110.0, 146.832, 196.0, 247.0, 329.628 };
        double worstLast = 0.0;
        int    lastOutside = 0, notes = 0;

        for (const double sr : { 44100.0, 48000.0, 96000.0 })
            for (const double f0 : open)
                for (std::uint32_t seed = 1; seed <= 3; ++seed)
                {
                    constexpr double B = 1.0e-4;
                    const int total = (int) (3.0 * sr);
                    std::vector<float> y ((size_t) total);
                    std::uint32_t rng = seed * 7919u;
                    double phase[16];
                    for (auto& p : phase)
                        p = pi * (1.0 + lcgNoise (rng));

                    for (int n = 0; n < total; ++n)
                    {
                        const double t = n / sr;
                        double v = 0.0;
                        for (int k = 1; k <= 16; ++k)
                        {
                            phase[k - 1] += 2.0 * pi * k * f0 * std::sqrt (1.0 + B * k * k) / sr;
                            v += std::pow (10.0, -20.0 * (1.0 + 0.3 * (k - 1)) * t / 20.0) / k * std::sin (phase[k - 1]);
                        }
                        if (t < 0.004)
                            v += 0.6 * lcgNoise (rng) * std::exp (-t / 0.0012);
                        y[(size_t) n] = (float) (0.4 * v + 1.0e-3 * lcgNoise (rng));
                    }

                    // The pitch a tuner means by this string: its fundamental.
                    const auto want = PitchTracker::nearestNote ((float) (f0 * std::sqrt (1.0 + B)));

                    TunerTap tap;
                    TunerEar ear;
                    ear.prepare (sr);

                    double nextTick = 0.0, lastLive = 0.0;
                    bool   everLive = false;

                    for (int pos = 0; pos + 512 <= total; pos += 512)
                    {
                        tap.write (y.data() + pos, 512);
                        for (; nextTick <= (pos + 512) / sr; nextTick += 1.0 / 30.0)
                        {
                            ear.update (tap, (unsigned) std::lround (nextTick * 1000.0));
                            if (ear.live() && ear.nearestNote().midi == want.midi)
                            {
                                lastLive = ear.needle() - want.cents;
                                everLive = true;
                            }
                        }
                    }

                    ++notes;
                    if (! everLive || std::fabs (lastLive) > TunerEar::inTuneCents)
                        ++lastOutside;
                    worstLast = std::max (worstLast, std::fabs (lastLive));
                }

        char name[96];
        std::snprintf (name, sizeof (name), "ear: plucks whose last needle leaves green (of %d)", notes);
        check (name, lastOutside, 0.0, 0.0);
        check ("ear: the worst last needle, cents", worstLast, 0.0, TunerEar::inTuneCents);
    }

    // ---- the ring's head is not analysed, at any rate --------------------------------------------
    // The oldest part of the tap holds whatever came before: the anti-alias filter settling, the
    // seam of a torn copy, the last note after a string is plucked again. Its oldest eighth — a
    // semitone away from the note the rest of the window holds — must not move the reading.
    {
        using orbitamp::core::TunerTap;

        for (const double sr : { 44100.0, 48000.0, 96000.0 })
        {
            const double f = 110.0, other = 110.0 * std::pow (2.0, 100.0 / 1200.0);
            std::vector<float> w ((size_t) TunerTap::size);
            for (int i = 0; i < TunerTap::size; ++i)
                w[(size_t) i] = 0.5f * (float) std::sin (2.0 * pi * (i < TunerTap::size / 8 ? other : f) * i / sr);

            PitchTracker t;
            t.prepare (sr);
            const auto r = t.analyse (w.data(), (int) w.size());

            char name[96];
            std::snprintf (name, sizeof (name), "old note in the ring's head @ %.0fk: cents error", sr / 1000.0);
            check (name, r.hz > 0.0f ? centsBetween (r.hz, f) : 999.0, 0.0, 0.1);
        }
    }

    // ---- the player's level floor ----------------------------------------------------------------
    {
        const auto at = [] (double hz, double sr, float amp, double floorDb)
        {
            std::vector<float> s ((size_t) windowLen);
            for (int i = 0; i < windowLen; ++i)
                s[(size_t) i] = amp * (float) std::sin (2.0 * pi * hz * i / sr);

            PitchTracker t;
            t.prepare (sr);
            t.setLevelFloorDb (floorDb);
            return t.analyse (s.data(), (int) s.size());
        };

        // An A2 whose window RMS is -63 dBFS: under the default -60 floor, over -70.
        checkTrue ("floor -60 (default): a -63 dBFS string is not a note",
                   at (110.0, 48000.0, 1.0e-3f, -60.0).hz == 0.0f);
        checkTrue ("floor -70: the same string reads",
                   at (110.0, 48000.0, 1.0e-3f, -70.0).hz > 0.0f);
        checkTrue ("floor -50: a -57 dBFS string is not a note",
                   at (110.0, 48000.0, 2.0e-3f, -50.0).hz == 0.0f);
        {
            PitchTracker plain;
            plain.prepare (48000.0);
            std::vector<float> s ((size_t) windowLen);
            for (int i = 0; i < windowLen; ++i)
                s[(size_t) i] = 1.0e-3f * (float) std::sin (2.0 * pi * 110.0 * i / 48000.0);
            checkTrue ("a tracker nobody configured sits at -60", plain.analyse (s.data(), windowLen).hz == 0.0f);
        }
    }

    // ---- mains hum: labelled, never hidden --------------------------------------------------------
    // Hum is as periodic as a string, and the tracker reads it with full confidence: 50 Hz as G1 +35,
    // 60 Hz as B1 -49, for as long as the cable hums. The ear does not hide it — it LABELS it: on a
    // mains line, with nothing plucked, humMains() says 50 or 60 and a face writes HUM 50 / HUM 60 where
    // the note's name stood. What the player would read is checked here: HUM, a note's name, or nothing.
    // All of it through the processor's path: host blocks into the tap, the ear at 30 Hz, the default
    // floor, a -80 dBFS hiss under everything.
    {
        using orbitamp::core::TunerEar;
        using orbitamp::core::TunerTap;

        // The lines this test means: 50 and 60 Hz, 2nd to 4th harmonics, within 3.5 cents of each centre.
        const auto onLine = [] (float hz)
        {
            for (const double m : { 50.0, 60.0 })
                for (int k = 1; k <= 4; ++k)
                    if (hz > 0.0f && std::fabs (1200.0 * std::log2 (hz / (m * k))) <= 3.5)
                        return true;
            return false;
        };

        struct Tick { double t; bool live; float hz; int midi; float cents, needle; int hum; };

        // The ear over a whole signal. The first `opened` seconds only fill the tap — the input was
        // there, the tuner was closed — then the ear listens; t counts from `zero` seconds into it.
        // `closed` is a stretch of seconds with no ticks at all: the tuner shut and opened again.
        const auto listenTo = [] (const std::vector<float>& y, double sr, double opened, double zero,
                                  std::pair<double, double> closed = { -1.0, -1.0 })
        {
            auto tap = std::make_unique<TunerTap>();
            auto ear = std::make_unique<TunerEar>();
            ear->prepare (sr);
            std::vector<Tick> ticks;
            double next = opened;
            for (int pos = 0; pos + 512 <= (int) y.size(); pos += 512)
            {
                tap->write (y.data() + pos, 512);
                for (; next <= (pos + 512) / sr; next += 1.0 / 30.0)
                {
                    if (next >= closed.first && next < closed.second)
                        continue;
                    ear->update (*tap, (unsigned) std::lround (next * 1000.0));
                    ticks.push_back ({ next - zero, ear->live(), ear->hz(), ear->nearestNote().midi, ear->nearestNote().cents, ear->needle(), ear->humMains() });
                }
            }
            return ticks;
        };

        // Mains hum: partials of the mains at levels relative to each other (dB), `rmsDb` in all, the
        // level swinging +-swingDb at swingHz, from `from` seconds on. The hiss everywhere.
        struct Hum
        {
            double mains;
            std::vector<std::pair<int, double>> partials;
            double rmsDb, swingDb = 0.0, swingHz = 0.0;
            double phase = -1.0;   // of the mains, radians; from the seed when negative
        };
        const auto room = [] (double sr, double seconds, const Hum& h, double from, std::uint32_t seed)
        {
            std::vector<float> y ((size_t) (seconds * sr));
            double power = 0.0;
            for (const auto& p : h.partials)
                power += 0.5 * std::pow (10.0, p.second / 10.0);
            const double g  = std::pow (10.0, h.rmsDb / 20.0) / std::sqrt (power);
            const double ph = h.phase >= 0.0 ? h.phase : 0.37 * seed;
            for (size_t n = 0; n < y.size(); ++n)
            {
                const double t = (double) n / sr;
                double v = 0.0;
                if (t >= from && h.rmsDb > -150.0)
                {
                    for (const auto& p : h.partials)
                        v += std::pow (10.0, p.second / 20.0) * std::sin (2.0 * pi * p.first * h.mains * t + p.first * ph);
                    v *= g * std::pow (10.0, h.swingDb * std::sin (2.0 * pi * h.swingHz * t) / 20.0);
                }
                y[n] = (float) v + 1.7e-4f * lcgNoise (seed);
            }
            return y;
        };
        const Hum silent { 50.0, { { 1, 0.0 } }, -200.0 };

        // A plucked string added in at `at` seconds: a stiff string's partials, the fundamental 6 dB
        // over the 2nd, the fundamental falling at `decay` dB/s and each partial faster than the one
        // below, a pick's burst; `levelDb` RMS over its first 300 ms. Its fundamental at `f0`.
        const auto pluckInto = [] (std::vector<float>& y, double sr, double f0, double at, double decay,
                                   double levelDb, std::uint32_t seed, double cut = -1.0, double attack = 0.0)
        {
            constexpr double stiff = 1.0e-4;
            const int start = (int) (at * sr);
            std::vector<double> s (y.size() - (size_t) start, 0.0);
            for (int k = 1; k <= 16 && k * f0 < std::min (2500.0, 0.45 * sr); ++k)
            {
                const double fk = k * f0 * std::sqrt (1.0 + stiff * k * k) / std::sqrt (1.0 + stiff);
                const double r  = std::pow (10.0, -decay * (1.0 + 0.25 * (k - 1)) / 20.0 / sr);
                const double ph = pi * (1.0 + lcgNoise (seed));
                double re = (k == 1 ? 1.0 : 0.5 / k) * std::cos (ph), im = (k == 1 ? 1.0 : 0.5 / k) * std::sin (ph);
                const double cw = r * std::cos (2.0 * pi * fk / sr), sw = r * std::sin (2.0 * pi * fk / sr);
                for (double& v : s)
                {
                    v += im;
                    const double nr = re * cw - im * sw;
                    im = re * sw + im * cw;
                    re = nr;
                }
            }
            if (attack > 0.0)   // a soft attack, rising over `attack` seconds, no pick's click
                for (size_t n = 0; n < s.size() && (double) n < attack * sr; ++n)
                    s[n] *= (double) n / (attack * sr);
            else
                for (int n = 0; n < (int) (0.02 * sr); ++n)
                    s[(size_t) n] += 0.25 * lcgNoise (seed) * std::exp (-n / sr / 0.001);

            double e = 0.0;
            const int len = (int) (0.3 * sr);
            for (int n = 0; n < len; ++n)
                e += s[(size_t) n] * s[(size_t) n];
            const double g = std::pow (10.0, levelDb / 20.0) / std::sqrt (e / len);
            const size_t until = cut > at ? std::min (s.size(), (size_t) ((cut - at) * sr)) : s.size();   // re-plucked, or stopped
            for (size_t n = 0; n < until; ++n)
                y[(size_t) start + n] += (float) (g * s[n]);
        };

        // 1. HUM ALONE, the tuner opened onto it: a sine, a rectifier's buzz (the 2nd over the
        //    fundamental), and hum whose 3rd harmonic is 20 dB over the mains (it reads 150 or 180 Hz) —
        //    at -40 and -55 dBFS, over the floor. HUM 50 / HUM 60 within a second (the ear opens knowing
        //    nothing, and takes 0.8 s of a flat level to tell), and never a note's name after it. (Hum
        //    whose level swings: 7. below.)
        {
            int runs = 0, never = 0, notesAfter = 0, wrongMains = 0;
            double slowest = 0.0;
            std::uint32_t seed = 3;
            for (const double mains : { 50.0, 60.0 })
                for (int shape = 0; shape < 3; ++shape)
                    for (const double db : { -40.0, -55.0 })
                        for (const double sr : { 44100.0, 48000.0, 96000.0 })
                        {
                            if (sr != 48000.0 && shape != 0)
                                continue;
                            Hum h { mains, { { 1, 0.0 } }, db };
                            if (shape == 1) h.partials = { { 1, -10.5 }, { 2, 0.0 }, { 3, -10.5 } };
                            if (shape == 2) h.partials = { { 1, -20.0 }, { 3, 0.0 } };
                            ++runs;
                            double first = -1.0;
                            for (const auto& k : listenTo (room (sr, 3.0, h, 0.0, seed++), sr, 1.0, 1.0))
                            {
                                wrongMains += k.hum != 0 && k.hum != (int) mains ? 1 : 0;
                                if (first < 0.0 && k.hum == (int) mains)
                                    first = k.t;
                                else if (first >= 0.0 && k.live && k.hum == 0)
                                    ++notesAfter;
                            }
                            never += first < 0.0 ? 1 : 0;
                            slowest = std::max (slowest, first);
                        }
            char name[96];
            std::snprintf (name, sizeof (name), "hum alone: runs never reading HUM (of %d)", runs);
            check (name, never, 0.0, 0.0);
            check ("hum alone: slowest first HUM, s", slowest, 0.0, 1.0);
            check ("hum alone: ticks naming a note after it", notesAfter, 0.0, 0.0);
            check ("hum alone: ticks with the wrong mains", wrongMains, 0.0, 0.0);
        }

        // 2. HUM COMING IN with the tuner open — a cable plugged in, a lamp switched on: its step is an
        //    onset, so it may read as a note for a moment; then HUM, and never a note after.
        {
            double slowest = 0.0;
            int never = 0, notesAfter = 0;
            for (const double mains : { 50.0, 60.0 })
                for (const double db : { -45.0 })
                {
                    const Hum h { mains, { { 1, 0.0 } }, db };
                    double first = -1.0;
                    for (const auto& k : listenTo (room (48000.0, 7.0, h, 2.0, 11), 48000.0, 1.0, 2.0))
                    {
                        if (k.t >= 0.0 && first < 0.0 && k.hum == (int) mains)
                            first = k.t;
                        else if (first >= 0.0 && k.live && k.hum == 0)
                            ++notesAfter;
                    }
                    never += first < 0.0 ? 1 : 0;
                    slowest = std::max (slowest, first);
                }
            check ("hum coming in: runs never reading HUM", never, 0.0, 0.0);
            check ("hum coming in: slowest first HUM, s", slowest, 0.0, 3.0);
            check ("hum coming in: ticks naming a note after it", notesAfter, 0.0, 0.0);
        }

        // 3. A STRING PLUCKED OVER HUM, the tuner open on the hum a second before. Strings ON a line of
        //    the hum's mains (100 Hz over 50, 120 over 60), one beating against it (49 Hz over 50) and
        //    one clear of it (110 over 60), the hum 20 and 40 dB under the note, the string falling 9 dB/s.
        //    While the string stands 6 dB over the hum, every tick that reads it (within 5 cents) reads
        //    its NOTE; once it has sunk 25 dB under — the level flat again, the hum's own — what stands on
        //    a line is HUM again, and in the last second the reading stands on the line, as HUM. (Between,
        //    the tracker reads string and hum together; no label is put on that until the level is flat:
        //    the hum after a passage comes back late, the safe way — later still where the string beats
        //    against it.)
        {
            struct Case { double hz, mains; };
            const Case cases[] = { { 100.0, 50.0 }, { 120.0, 60.0 }, { 48.999, 50.0 }, { 110.0, 60.0 } };
            int runs = 0, unread = 0, labelledString = 0, stringTicks = 0, notBack = 0;
            double slowest = 0.0, longestOnLine = 0.0;
            std::uint32_t seed = 41;
            for (const auto& c : cases)
                for (const double under : { 20.0, 40.0 })
                    for (const double sr : { 44100.0, 48000.0, 96000.0 })
                    {
                        if (sr != 48000.0)
                            continue;
                        ++runs;
                        const std::uint32_t sd = seed++;
                        auto y = room (sr, 11.0, Hum { c.mains, { { 1, 0.0 } }, -15.0 - under }, 0.0, sd);
                        pluckInto (y, sr, c.hz, 1.5, 9.0, -15.0, sd);

                        double first = -1.0, unlabelledOnLine = 0.0;
                        bool endsOnHum = true;
                        int  endOnLine = 0;
                        for (const auto& k : listenTo (y, sr, 0.5, 1.5))
                        {
                            const bool string = k.live && std::fabs (1200.0 * std::log2 (k.hz / c.hz)) <= 5.0;
                            if (k.t >= 0.0 && first < 0.0 && k.live && std::fabs (1200.0 * std::log2 (k.hz / c.hz)) <= 15.0)
                                first = k.t;
                            if (k.t >= 0.0 && k.t < (under - 6.0) / 9.0 && string)
                            {
                                ++stringTicks;
                                labelledString += k.hum != 0 ? 1 : 0;
                            }
                            if (k.t > (under + 25.0) / 9.0 && k.live && onLine (k.hz) && k.hum == 0)
                                unlabelledOnLine += 1.0 / 30.0;
                            if (k.t >= 8.5 && k.live && onLine (k.hz))
                            {
                                endsOnHum = endsOnHum && k.hum == (int) c.mains;
                                ++endOnLine;
                            }
                        }
                        unread += first < 0.0 ? 1 : 0;
                        slowest = std::max (slowest, first);
                        longestOnLine = std::max (longestOnLine, unlabelledOnLine);
                        notBack += endsOnHum && endOnLine >= 15 ? 0 : 1;   // the last second: on the line, and HUM
                    }
            char name[96];
            std::snprintf (name, sizeof (name), "string over hum: runs never reading it (of %d)", runs);
            check (name, unread, 0.0, 0.0);
            check ("string over hum: slowest first reading, s", slowest, 0.0, 0.15);
            std::snprintf (name, sizeof (name), "string over hum: its ticks labelled (of %d)", stringTicks);
            check (name, labelledString, 0.0, 0.0);
            checkTrue ("...and those ticks are there to count", stringTicks > 20 * runs);
            check ("string over hum: runs not on HUM at the end", notBack, 0.0, 0.0);
            check ("string over hum: on a line unlabelled 25 dB under it, s", longestOnLine, 0.0, 1.5);
        }

        // 4. A STRING ON A MAINS LINE, no hum anywhere: plucked strings at the centre of every line,
        //    falling at 2 dB/s: their note, never the label (faster and slower: 6. below). (One that does not fall at all — a
        //    sustainer, an e-bow — holds like hum and is labelled once it has held two seconds; the
        //    reading stays.)
        {
            const double centres[] = { 50.0, 60.0, 100.0, 120.0, 150.0, 180.0, 200.0, 240.0 };
            int runs = 0, labelled = 0, unread = 0;
            std::uint32_t seed = 101;
            for (const double f0 : centres)
                for (const double decay : { 2.0 })
                    for (const double sr : { 44100.0, 48000.0, 96000.0 })
                    {
                        if (sr != 48000.0 && f0 != 120.0)
                            continue;
                        ++runs;
                        auto y = room (sr, 5.5, silent, 0.0, seed);
                        pluckInto (y, sr, f0, 1.5, decay, -20.0, seed++);
                        int onString = 0;
                        for (const auto& k : listenTo (y, sr, 1.0, 1.5))
                        {
                            onString += k.live && std::fabs (1200.0 * std::log2 (k.hz / f0)) <= 5.0 ? 1 : 0;
                            labelled += k.hum != 0 ? 1 : 0;
                        }
                        unread += onString < 30 ? 1 : 0;
                    }
            char name[96];
            std::snprintf (name, sizeof (name), "string on a line: runs reading it < 1 s (of %d)", runs);
            check (name, unread, 0.0, 0.0);
            check ("string on a line: ticks labelled HUM", labelled, 0.0, 0.0);
        }

        // 5. THE TUNER SHUT AND OPENED AGAIN: a string plucked while it was open, the tuner closed for a
        //    second while the string died, opened again onto the hum alone — HUM as on any opening,
        //    not what the pluck left behind.
        {
            auto y = room (48000.0, 6.0, Hum { 50.0, { { 1, 0.0 } }, -45.0 }, 0.0, 7);
            pluckInto (y, 48000.0, 110.0, 1.5, 20.0, -15.0, 7);
            double first = -1.0;
            for (const auto& k : listenTo (y, 48000.0, 1.0, 4.0, { 2.0, 4.0 }))
                if (k.t >= 0.0 && first < 0.0 && k.hum == 50)
                    first = k.t;
            check ("reopened onto hum: first HUM, s", first, 0.0, 1.0);
        }

        // 6. A REAL STRING, NO HUM ANYWHERE: the way a player tunes and plays, with the string standing
        //    on a line (100 Hz is a bass G 35 cents sharp, 200 Hz a G3 as sharp). Every tick that reads
        //    the string — within 5 cents of where it is — reads its NOTE, never HUM:
        //    - re-plucked every second at the same level, falling 6 dB/s; every 1.2 s each 1 dB softer,
        //      falling 9 dB/s;
        //    - a G3 tuned down from +45 cents to in tune, plucked once a second (+36 c is 200 Hz) at
        //      -20 +-1 dB, falling 9 and 6 dB/s; a bass G2 the same, falling 4 dB/s, plucked every 1 s
        //      and every 2 s;
        //    - a quieter note after silence: 110 Hz at -15 dBFS held 3 s, half a second of nothing, then
        //      100 Hz at -20, falling 2 dB/s;
        //    - the ear not ticking 0.3 s while the string rings (falling 6 and 2 dB/s), and the tuner
        //      opened half a second after the pluck (100 and 200 Hz);
        //    - a string falling slowly: 0.9, 0.5, 0.2 and 0.15 dB/s — its pluck seen, and the tuner opened
        //      0.5 or 1 s after it, when the ear has not seen the attack and has only the fall to go by.
        {
            struct Pluck { double at, hz, db; };
            struct Run { const char* what; std::vector<Pluck> plucks; double decay, opened, seconds; std::pair<double, double> closed; double cut;
                         bool allRates = false; };
            std::vector<Run> runs;
            const auto every = [] (double from, double step, int n, double hz, double db, double softer)
            {
                std::vector<Pluck> p;
                for (int i = 0; i < n; ++i)
                    p.push_back ({ from + step * i, hz, db - softer * i });
                return p;
            };
            const auto sweep = [] (double hz, double step)
            {
                std::vector<Pluck> p;
                for (int i = 0; i < 6; ++i)
                    p.push_back ({ 1.5 + step * i, hz * std::pow (2.0, (45.0 - 9.0 * i) / 1200.0), i % 2 == 0 ? -20.0 : -21.0 });
                return p;
            };
            runs.push_back ({ "100 Hz re-plucked every 1 s", every (1.5, 1.0, 6, 100.0, -20.0, 0.0), 6.0, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "100 Hz every 1.2 s, 1 dB softer", every (1.5, 1.2, 5, 100.0, -20.0, 1.0), 9.0, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "G3 tuned +45 c to 0, 9 dB/s", sweep (196.0, 1.0), 9.0, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "G3 tuned +45 c to 0, 6 dB/s", sweep (196.0, 1.0), 6.0, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "G2 tuned +45 c to 0, every 1 s", sweep (98.0, 1.0), 4.0, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "G2 tuned +45 c to 0, every 2 s", sweep (98.0, 2.0), 4.0, 0.5, 14.0, { -1, -1 }, -1 });
            runs.push_back ({ "a quieter note after silence", { { 5.0, 100.0, -20.0 } }, 2.0, 0.5, 10.0, { -1, -1 }, -1 });
            runs.push_back ({ "no ticks 0.3 s, 6 dB/s", { { 1.5, 100.0, -20.0 } }, 6.0, 0.5, 7.0, { 2.0, 2.3 }, -1 });
            runs.push_back ({ "no ticks 0.3 s, 2 dB/s", { { 1.5, 100.0, -20.0 } }, 2.0, 0.5, 7.0, { 3.0, 3.3 }, -1 });
            runs.push_back ({ "opened after the pluck, 100 Hz", { { 1.0, 100.0, -20.0 } }, 6.0, 1.5, 7.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened after the pluck, 200 Hz", { { 1.0, 200.0, -20.0 } }, 6.0, 1.5, 7.0, { -1, -1 }, -1 });
            runs.push_back ({ "100 Hz falling 0.5 dB/s", { { 1.5, 100.0, -20.0 } }, 0.5, 0.5, 8.0, { -1, -1 }, -1, true });
            runs.push_back ({ "100 Hz falling 0.9 dB/s", { { 1.5, 100.0, -20.0 } }, 0.9, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "100 Hz falling 0.2 dB/s", { { 1.5, 100.0, -20.0 } }, 0.2, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "100 Hz falling 0.15 dB/s", { { 1.5, 100.0, -20.0 } }, 0.15, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 1 s after, 0.5 dB/s", { { 1.5, 100.0, -20.0 } }, 0.5, 2.5, 8.0, { -1, -1 }, -1, true });
            runs.push_back ({ "opened 1 s after, 0.9 dB/s", { { 1.5, 100.0, -20.0 } }, 0.9, 2.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 0.5 s after, 1 dB/s", { { 1.0, 100.0, -20.0 } }, 1.0, 1.5, 7.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 0.5 s after, 0.5 dB/s", { { 1.0, 100.0, -20.0 } }, 0.5, 1.5, 7.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 1 s after, 1 dB/s", { { 0.5, 200.0, -20.0 } }, 1.0, 1.5, 7.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 1 s after, 0.2 dB/s", { { 1.5, 100.0, -20.0 } }, 0.2, 2.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "opened 1 s after, 0.15 dB/s", { { 1.5, 100.0, -20.0 } }, 0.15, 2.5, 8.0, { -1, -1 }, -1 });

            int worst = 0, unread = 0, n = 0;
            std::uint32_t seed = 301;
            for (const auto& r : runs)
                for (const double sr : { 44100.0, 48000.0, 96000.0 })
                {
                    const bool quieter = std::strncmp (r.what, "a quieter", 9) == 0;
                    if (sr != 48000.0 && ! r.allRates)
                        continue;
                    ++n;
                    auto y = room (sr, r.seconds, silent, 0.0, seed);
                    if (quieter)
                        pluckInto (y, sr, 110.0, 1.5, 0.0, -15.0, seed, 4.5);
                    for (size_t i = 0; i < r.plucks.size(); ++i)
                        pluckInto (y, sr, r.plucks[i].hz, r.plucks[i].at, r.decay, r.plucks[i].db, seed + (std::uint32_t) i,
                                   i + 1 < r.plucks.size() ? r.plucks[i + 1].at : -1.0);
                    ++seed;
                    int labelled = 0, onString = 0;
                    for (const auto& k : listenTo (y, sr, r.opened, 0.0, r.closed))
                    {
                        double want = 0.0;
                        for (const auto& p : r.plucks)
                            if (k.t >= p.at)
                                want = p.hz;
                        if (want > 0.0 && k.live && std::fabs (1200.0 * std::log2 (k.hz / want)) <= 5.0)
                        {
                            ++onString;
                            labelled += k.hum != 0 ? 1 : 0;
                        }
                    }
                    if (labelled > 0)
                        std::printf ("  %s @ %.0fk: %d of its %d ticks labelled HUM\n", r.what, sr / 1000.0, labelled, onString);
                    worst = std::max (worst, labelled);
                    unread += onString < 30 ? 1 : 0;
                }
            char name[96];
            std::snprintf (name, sizeof (name), "real strings: runs reading the string < 1 s (of %d)", n);
            check (name, unread, 0.0, 0.0);
            check ("real strings: most ticks of one labelled HUM", worst, 0.0, 0.0);
        }

        // 7. HUM THAT SWINGS: +-1.6 and +-4 dB at 1 Hz, +-6 dB at 0.5 Hz with the tuner opened onto it, +-3 dB at
        //    0.3 Hz the same, and +-4 dB at 0.1 Hz coming in two seconds after the tuner opened. Its rises
        //    are no plucks: once it reads HUM, never a note's name again, to the end. Opened onto it,
        //    HUM once its level is seen to hold (a fitted 2.5 s slope): within 4 s; hum that came in (an
        //    onset) and then FALLS — the slow swing's downward half — falls like a string, and reads as one
        //    until it stops falling: up to 7 s here. In doubt, no label: hum read as a note is only the old
        //    tuner.
        {
            struct Swing { double mains, db, hz, from; };
            const Swing swings[] = { { 50.0, 1.6, 1.0, 0.0 }, { 60.0, 1.6, 1.0, 0.0 }, { 50.0, 4.0, 1.0, 0.0 }, { 60.0, 4.0, 1.0, 0.0 },
                                     { 50.0, 6.0, 0.5, 0.0 }, { 50.0, 3.0, 0.3, 0.0 }, { 50.0, 4.0, 0.1, 2.0 } };
            double slowest = 0.0, slowestIn = 0.0;
            int never = 0, notesAfter = 0;
            for (const auto& w : swings)
            {
                Hum h { w.mains, { { 1, 0.0 } }, -40.0 };
                h.swingDb = w.db;
                h.swingHz = w.hz;
                double first = -1.0;
                for (const auto& k : listenTo (room (48000.0, w.from > 0.0 ? w.from + 9.0 : (w.hz >= 0.5 ? 6.0 : 8.0), h, w.from, 13), 48000.0, 1.0, std::max (1.0, w.from)))
                {
                    if (k.t >= 0.0 && first < 0.0 && k.hum == (int) w.mains)
                        first = k.t;
                    else if (first >= 0.0 && k.live && k.hum == 0)
                        ++notesAfter;
                }
                never += first < 0.0 ? 1 : 0;
                (w.from > 0.0 ? slowestIn : slowest) = std::max (w.from > 0.0 ? slowestIn : slowest, first);
            }
            check ("swinging hum: runs never reading HUM", never, 0.0, 0.0);
            check ("swinging hum, opened onto it: slowest first HUM, s", slowest, 0.0, 4.0);
            check ("swinging hum, coming in falling: first HUM, s", slowestIn, 0.0, 7.0);
            check ("swinging hum: ticks naming a note after it", notesAfter, 0.0, 0.0);
        }

        // 9. A HELD NOTE LET GO: 100 Hz held flat for 3 s — a sustainer; on a line it may read HUM, the
        //    price written in TunerEar — then let fall at 3 dB/s. Once it has fallen like a string for
        //    2.5 s, no tick that reads it is labelled HUM.
        {
            int labelled = 0, onString = 0;
            for (const double sr : { 48000.0, 96000.0 })
            {
                auto y = room (sr, 9.0, silent, 0.0, 23);
                pluckInto (y, sr, 100.0, 1.5, 0.0, -20.0, 23);
                for (size_t n = (size_t) (4.5 * sr); n < y.size(); ++n)   // let go: from here it falls 3 dB/s
                    y[n] *= (float) std::pow (10.0, -3.0 * ((double) n / sr - 4.5) / 20.0);
                for (const auto& k : listenTo (y, sr, 0.5, 4.5))
                    if (k.t >= 2.7 && k.live && std::fabs (1200.0 * std::log2 (k.hz / 100.0)) <= 5.0)
                    {
                        ++onString;
                        labelled += k.hum != 0 ? 1 : 0;
                    }
            }
            char name[96];
            std::snprintf (name, sizeof (name), "a held note let go: its falling ticks labelled (of %d)", onString);
            check (name, labelled, 0.0, 0.0);
            checkTrue ("...and those ticks are there to count", onString > 60);
        }

        // A gain over a signal, in dB, linear between (seconds, dB) points; 0 dB before the first.
        const auto shape = [] (std::vector<float>& y, double sr, std::vector<std::pair<double, double>> points)
        {
            for (size_t n = 0; n < y.size(); ++n)
            {
                const double t = (double) n / sr;
                double g = 0.0;
                for (size_t i = 0; i < points.size(); ++i)
                    if (t >= points[i].first)
                        g = i + 1 < points.size() && t < points[i + 1].first
                                ? points[i].second + (points[i + 1].second - points[i].second)
                                                         * (t - points[i].first) / (points[i + 1].first - points[i].first)
                                : points[i].second;
                y[n] *= (float) std::pow (10.0, g / 20.0);
            }
        };

        // 10. A HUM THAT HAS GONE is no hum to measure a string by. 100 Hz strings (on the line of 50 Hz
        //     hum): plucked a second after the hum stopped, at -43, -40 and -30 dBFS (the hum was -45),
        //     falling 4 dB/s; plucked over the hum, which a hand on the strings then silences; plucked
        //     0.2 s after a -25 dBFS hum went, at -15, falling 2 dB/s, and at -19 a second after; and
        //     plucked with a finger (its attack 60 ms) at -15 over a hum of its own pitch 4 dB under it,
        //     falling 2 dB/s — its own attack must not raise the hum's level — and the same with the hum's
        //     phase set so the string lifts it or cancels it. And strings AT the pitch of a hum (one that
        //     reads 100 Hz) that has gone, plucked near its level (-47 to -41 dBFS, the hum was -45),
        //     falling 4 and 1 dB/s; the hum gone under a string of its pitch. Coming back to a hum's level
        //     at its pitch is not the hum coming back: only a flat level is. (Over hum that swings, never
        //     flat, only a change of 3 dB in 100 ms gives such a pluck away — the limit in TunerEar.)
        {
            struct Case { const char* what; double humDb, opened, goneAt, pluckAt, pluckDb, decay; bool humOnString; double humPhase = -1.0;
                          bool allRates = false; };
            const Case cases[] = { { "gone, then -43", -45.0, 1.0, 3.0, 4.0, -43.0, 4.0, false },
                                   { "gone, then -40", -45.0, 1.0, 3.0, 4.0, -40.0, 4.0, false },
                                   { "gone, then -30", -45.0, 1.0, 3.0, 4.0, -30.0, 4.0, false },
                                   { "silenced under the string", -45.0, 1.0, 4.0, 2.0, -20.0, 6.0, false },
                                   { "gone 0.2 s before, -15", -25.0, 0.3, 1.3, 1.5, -15.0, 2.0, false, -1.0, true },
                                   { "gone 1 s before, -19", -25.0, 0.3, 2.0, 3.0, -19.0, 2.0, false },
                                   { "over a hum of its pitch", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true },
                                   // round 4: a string AT a gone hum's pitch, near its level; the hum gone under a string
                                   // of its pitch; a pluck in and out of step with a hum of its pitch
                                   { "its pitch, gone, then -47 at 4 dB/s", -45.0, 1.0, 3.0, 4.0, -47.0, 4.0, true },
                                   { "its pitch, gone, then -42.5 at 4 dB/s", -45.0, 1.0, 3.0, 4.0, -42.5, 4.0, true },
                                   { "its pitch, gone, then -44 at 1 dB/s", -45.0, 1.0, 3.0, 4.0, -44.0, 1.0, true },
                                   { "its pitch, gone, then -41 at 1 dB/s", -45.0, 1.0, 3.0, 4.0, -41.0, 1.0, true },
                                   { "its pitch, gone under it, 6 dB/s", -45.0, 1.0, 4.0, 2.0, -20.0, 6.0, true },
                                   { "its pitch, gone under it, 4 dB/s", -45.0, 1.0, 4.0, 2.0, -20.0, 4.0, true },
                                   { "its pitch, hum at phase 0", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true, 0.0 },
                                   { "its pitch, hum at phase 4", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true, 4.0, true },
                                   { "its pitch, hum at phase 1.6", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true, 1.6 },
                                   { "its pitch, hum at phase 4.8", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true, 4.8 },
                                   { "its pitch, hum at phase 5.6", -19.0, 0.3, -1.0, 1.5, -15.0, 2.0, true, 5.6 } };
            int worst = 0, unread = 0, runs = 0;
            for (const auto& c : cases)
                for (const double sr : { 44100.0, 48000.0, 96000.0 })
                {
                    if (sr != 48000.0 && ! c.allRates)
                        continue;
                    ++runs;
                    Hum h { 50.0, { { 1, 0.0 } }, c.humDb };
                    if (c.humOnString)
                        h.partials = { { 2, 0.0 } };   // hum that reads 100 Hz
                    h.phase = c.humPhase >= 0.0 ? c.humPhase / 2.0 : -1.0;   // the 100 Hz component's phase
                    const double seconds = c.pluckAt + 3.5;
                    auto y = room (sr, seconds, h, 0.0, 29);
                    if (c.goneAt > 0.0)
                        shape (y, sr, { { c.goneAt, 0.0 }, { c.goneAt + 0.02, -100.0 } });
                    {
                        auto clean = room (sr, seconds, silent, 0.0, 31);   // the hiss stays when the hum goes
                        for (size_t n = 0; n < y.size(); ++n)
                            if ((double) n / sr >= c.goneAt + 0.02 && c.goneAt > 0.0)
                                y[n] += clean[n];
                    }
                    {
                        // A finger's pluck: no pick's click, the string coming up over 60 ms.
                        std::vector<float> string (y.size(), 0.0f);
                        pluckInto (string, sr, 100.0, c.pluckAt, c.decay, c.pluckDb, 33);
                        for (size_t n = 0; n < y.size(); ++n)
                        {
                            const double t = (double) n / sr - c.pluckAt;
                            y[n] += string[n] * (float) (c.humOnString ? std::clamp (t / 0.06, 0.0, 1.0) : 1.0);
                        }
                    }
                    int labelled = 0, onString = 0;
                    for (const auto& k : listenTo (y, sr, c.opened, 0.0))
                    {
                        const double above = c.pluckDb - c.decay * (k.t - c.pluckAt) - c.humDb;
                        const bool humThere = c.goneAt < 0.0 || k.t < c.goneAt;
                        const double seen = c.humOnString && humThere ? 0.15 : 0.07;   // a finger's attack in step with the hum takes a while to tell
                        if (k.t >= c.pluckAt + seen && k.live && std::fabs (1200.0 * std::log2 (k.hz / 100.0)) <= 5.0
                            && (! c.humOnString || ! humThere || above > 0.0))
                        {
                            ++onString;
                            labelled += k.hum != 0 ? 1 : 0;
                        }
                    }
                    if (labelled > 0)
                        std::printf ("  %s @ %.0fk: %d of its %d ticks labelled HUM\n", c.what, sr / 1000.0, labelled, onString);
                    worst = std::max (worst, labelled);
                    unread += onString < 15 ? 1 : 0;
                }
            char name[96];
            std::snprintf (name, sizeof (name), "a hum gone: runs reading the string < 0.5 s (of %d)", runs);
            check (name, unread, 0.0, 0.0);
            check ("a hum gone: most ticks of one labelled HUM", worst, 0.0, 0.0);
        }

        // 11. HUM THAT MOVES — the player turning, stepping nearer the amp: -50 dBFS hum up 8 dB in 0.3 s,
        //     down again, up again; four such moves in 13 s; a 6 dB drop; a 5 dB fade over 3 s. Once the
        //     level has settled, HUM again within a second, and no tick on the line names a note after.
        {
            const std::vector<std::vector<std::pair<double, double>>> moves {
                { { 3.0, 0.0 }, { 3.3, 8.0 }, { 6.0, 8.0 }, { 6.3, 0.0 }, { 8.0, 0.0 }, { 8.3, 8.0 } },
                { { 3.0, 0.0 }, { 3.3, 8.0 }, { 5.5, 8.0 }, { 5.8, 0.0 }, { 8.5, 0.0 }, { 8.8, 8.0 }, { 11.0, 8.0 }, { 11.3, 0.0 } },
                { { 3.0, 0.0 }, { 3.02, -6.0 } },
                { { 3.0, 0.0 }, { 6.0, -5.0 } } };
            double slowest = 0.0;
            int late = 0;
            for (const auto& m : moves)
            {
                auto y = room (48000.0, m.back().first + 3.0, Hum { 50.0, { { 1, 0.0 } }, -50.0 }, 0.0, 37);
                shape (y, 48000.0, m);
                const auto ticks = listenTo (y, 48000.0, 1.0, 0.0);
                for (size_t i = 0; i < m.size(); ++i)
                {
                    const bool moving = i + 1 < m.size() && m[i + 1].second != m[i].second;
                    if (moving || (i > 0 && m[i - 1].second == m[i].second && i + 1 < m.size()))
                        continue;
                    // m[i] is where the level settles; until the next move (or the end).
                    const double settled = m[i].first, until = i + 1 < m.size() ? m[i + 1].first : m.back().first + 3.0;
                    double back = -1.0;
                    for (const auto& k : ticks)
                    {
                        if (k.t < settled || k.t >= until)
                            continue;
                        if (back < 0.0 && k.hum == 50)
                            back = k.t - settled;
                        if (k.t >= settled + 1.0 && k.live && onLine (k.hz) && k.hum == 0)
                            ++late;
                    }
                    slowest = std::max (slowest, back < 0.0 ? until - settled : back);
                }
            }
            check ("hum that moves: slowest back on HUM once settled, s", slowest, 0.0, 1.0);
            check ("hum that moves: ticks on the line naming a note later", late, 0.0, 0.0);
        }

        // 12. GENTLE RE-PLUCKS of a string on a line, no hum: 100 Hz (and 99.9, 100.1 — a bass G 35 cents
        //     sharp) at -20 dBFS, re-plucked every 1, 1.5 and 2 s with a soft 10 ms attack, falling 2 and
        //     4 dB/s — each re-pluck lifts the level only as far as it fell. Never HUM.
        {
            struct Gentle { double hz, every, decay; bool allRates; };
            const Gentle gentles[] = { { 100.0, 1.0, 2.0, true }, { 100.0, 1.5, 2.0, false }, { 100.0, 2.0, 2.0, false },
                                       { 100.0, 1.0, 4.0, false }, { 99.9, 1.0, 2.0, false }, { 100.1, 1.0, 2.0, false } };
            int worst = 0, unread = 0, runs = 0;
            for (const auto& g : gentles)
                for (const double sr : { 44100.0, 48000.0, 96000.0 })
                {
                    if (sr != 48000.0 && ! g.allRates)
                        continue;
                    ++runs;
                    auto y = room (sr, 8.0, silent, 0.0, 41);
                    for (double at = 1.5; at < 7.5; at += g.every)
                        pluckInto (y, sr, g.hz, at, g.decay, -20.0, 43, at + g.every, 0.01);
                    int labelled = 0, onString = 0;
                    for (const auto& k : listenTo (y, sr, 0.5, 0.0))
                        if (k.t >= 1.5 && k.live && std::fabs (1200.0 * std::log2 (k.hz / g.hz)) <= 5.0)
                        {
                            ++onString;
                            labelled += k.hum != 0 ? 1 : 0;
                        }
                    worst = std::max (worst, labelled);
                    unread += onString < 60 ? 1 : 0;
                }
            char name[96];
            std::snprintf (name, sizeof (name), "gentle re-plucks: runs reading the string < 2 s (of %d)", runs);
            check (name, unread, 0.0, 0.0);
            check ("gentle re-plucks: most ticks of one labelled HUM", worst, 0.0, 0.0);
        }

        // 13. HUM THAT BREATHES — a single coil, the player swaying: -45 dBFS hum whose level moves +-0.3,
        //     +-0.6 and +-1 dB at 0.3 Hz, +-0.5 dB at 0.1 and 0.2 Hz. Once it reads HUM, it keeps reading
        //     HUM: no tick on the line without the label.
        {
            struct Breath { double db, hz, seconds; };
            const Breath breaths[] = { { 0.3, 0.3, 12.0 }, { 0.6, 0.3, 12.0 }, { 1.0, 0.3, 12.0 }, { 0.5, 0.2, 16.0 }, { 0.5, 0.1, 20.0 } };
            int dropped = 0, never = 0;
            for (const auto& b : breaths)
            {
                Hum h { 50.0, { { 1, 0.0 } }, -45.0 };
                h.swingDb = b.db;
                h.swingHz = b.hz;
                double first = -1.0;
                for (const auto& k : listenTo (room (48000.0, b.seconds, h, 0.0, 47), 48000.0, 1.0, 0.0))
                {
                    if (first < 0.0 && k.hum == 50)
                        first = k.t;
                    else if (first >= 0.0 && k.live && onLine (k.hz) && k.hum == 0)
                        ++dropped;
                }
                never += first < 0.0 ? 1 : 0;
            }
            check ("breathing hum: runs never reading HUM", never, 0.0, 0.0);
            check ("breathing hum: ticks on the line that lost the label", dropped, 0.0, 0.0);
        }

        // 8. HUM THAT DIPS AND COMES BACK — a hand on the strings grounds the player and takes the hum down;
        //    off, it is back. -45 dBFS hum, dips of 6, 12 and 20 dB for 0.2, 0.5, 1 and 2 s: the dip and the
        //    return are sudden changes, no label until the level is flat again — back, within 1.1 s every
        //    tick standing on the line reads HUM, to the end.
        {
            double slowest = 0.0;
            int runs = 0, never = 0;
            for (const double depth : { 6.0, 12.0, 20.0 })
                for (const double length : { 0.2, 0.5, 1.0, 2.0 })
                {
                    ++runs;
                    const double from = 2.0, to = 2.0 + length, ramp = 0.02;
                    auto y = room (48000.0, to + 2.5, Hum { 50.0, { { 1, 0.0 } }, -45.0 }, 0.0, 17);
                    for (size_t n = 0; n < y.size(); ++n)
                    {
                        const double t = (double) n / 48000.0;
                        const double in = std::clamp ((t - from) / ramp, 0.0, 1.0) * std::clamp ((to + ramp - t) / ramp, 0.0, 1.0);
                        y[n] *= (float) std::pow (10.0, -depth * in / 20.0);
                    }
                    // Back for good: the last tick after the return that stands on the line and is not HUM.
                    double lastNote = to + ramp;
                    bool endsOnHum = false;
                    for (const auto& k : listenTo (y, 48000.0, 1.0, 0.0))
                        if (k.t >= to + ramp && k.live && onLine (k.hz))
                        {
                            if (k.hum == 0)
                                lastNote = k.t;
                            endsOnHum = k.hum == 50;
                        }
                    never += endsOnHum ? 0 : 1;
                    slowest = std::max (slowest, lastNote - (to + ramp));
                }
            char name[96];
            std::snprintf (name, sizeof (name), "hum that dips: runs not back on HUM (of %d)", runs);
            check (name, never, 0.0, 0.0);
            check ("hum that dips: slowest back on HUM for good, s", slowest, 0.0, 1.1);
        }
    }

    // ---- the naming the panel prints -----------------------------------------------------------
    checkNote ("440 names as", 440.0, "A", 4);
    checkNote ("82.4 names as", 82.407, "E", 2);
    checkNote ("261.6 names as", 261.626, "C", 4);
    checkNote ("466.2 names as", 466.164, "A#", 4);
    check ("445 Hz vs A4 in cents", (double) PitchTracker::nearestNote (445.0f).cents, 19.56, 0.05);

    // ---- the reference: an A that is not 440 ----------------------------------------------------
    check ("442 Hz against an A of 442, in cents", (double) PitchTracker::nearestNote (442.0f, 442.0f).cents, 0.0, 0.01);
    check ("440 Hz against an A of 442, in cents", (double) PitchTracker::nearestNote (440.0f, 442.0f).cents, -7.85, 0.05);
    checkTrue ("432 Hz against an A of 432 is A4", PitchTracker::nearestNote (432.0f, 432.0f).midi == 69);
    checkTrue ("an ear nobody told measures from 440", orbitamp::core::TunerEar().referenceHz() == 440.0f);

    strobeTests();

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
