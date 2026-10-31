// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::PitchTracker — the tuner's ear, checked against signals with
// a KNOWN pitch. Sines because their answer is exact; a sawtooth because a real string is not a
// sine and a strong harmonic is how tuners read octaves wrong; a Karplus-Strong pluck because it
// decays and drifts the way a string does. Returns non-zero on any failure.
//
// JUCE-free, like the code under test: links nothing at all.

#include "core/PitchTracker.h"
#include "core/TunerEar.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

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
        };
        const auto room = [] (double sr, double seconds, const Hum& h, double from, std::uint32_t seed)
        {
            std::vector<float> y ((size_t) (seconds * sr));
            double power = 0.0;
            for (const auto& p : h.partials)
                power += 0.5 * std::pow (10.0, p.second / 10.0);
            const double g  = std::pow (10.0, h.rmsDb / 20.0) / std::sqrt (power);
            const double ph = 0.37 * seed;
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
                                   double levelDb, std::uint32_t seed, double cut = -1.0)
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
        //    fundamental), hum whose 3rd harmonic is 20 dB over the mains (it reads 150 or 180 Hz), and
        //    hum whose level swings +-1.6 dB at 1 Hz — at -40 and -55 dBFS, over the floor. HUM 50 /
        //    HUM 60 within a second (the ear opens knowing nothing, and takes 0.8 s to tell), and never
        //    a note's name after it.
        {
            int runs = 0, never = 0, notesAfter = 0, wrongMains = 0;
            double slowest = 0.0;
            std::uint32_t seed = 3;
            for (const double mains : { 50.0, 60.0 })
                for (int shape = 0; shape < 4; ++shape)
                    for (const double db : { -40.0, -55.0 })
                        for (const double sr : { 44100.0, 48000.0, 96000.0 })
                        {
                            if (sr != 48000.0 && shape != 0)
                                continue;
                            Hum h { mains, { { 1, 0.0 } }, db };
                            if (shape == 1) h.partials = { { 1, -10.5 }, { 2, 0.0 }, { 3, -10.5 } };
                            if (shape == 2) h.partials = { { 1, -20.0 }, { 3, 0.0 } };
                            if (shape == 3) { h.swingDb = 1.6; h.swingHz = 1.0; }
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
        //    its NOTE; once it has sunk 10 dB under, what stands on a line is HUM again, and in the last
        //    second the reading stands on the line, as HUM. (Between, the tracker reads string and hum
        //    together, off the line; that is shown as it is read.)
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
                        if (sr != 48000.0 && (under != 40.0 || c.hz != 100.0))
                            continue;
                        ++runs;
                        const std::uint32_t sd = seed++;
                        auto y = room (sr, 9.0, Hum { c.mains, { { 1, 0.0 } }, -15.0 - under }, 0.0, sd);
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
                            if (k.t > (under + 10.0) / 9.0 && k.live && onLine (k.hz) && k.hum == 0)
                                unlabelledOnLine += 1.0 / 30.0;
                            if (k.t >= 6.5 && k.live && onLine (k.hz))
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
            check ("string over hum: runs not on HUM 5 s after it", notBack, 0.0, 0.0);
            check ("string over hum: on a line unlabelled once it died, s", longestOnLine, 0.0, 0.5);
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
                        if (sr != 48000.0 && f0 != 50.0 && f0 != 120.0 && f0 != 240.0)
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
        //    - a string falling slowly: 0.5 and 0.9 dB/s.
        {
            struct Pluck { double at, hz, db; };
            struct Run { const char* what; std::vector<Pluck> plucks; double decay, opened, seconds; std::pair<double, double> closed; double cut; };
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
            runs.push_back ({ "100 Hz falling 0.5 dB/s", { { 1.5, 100.0, -20.0 } }, 0.5, 0.5, 8.0, { -1, -1 }, -1 });
            runs.push_back ({ "100 Hz falling 0.9 dB/s", { { 1.5, 100.0, -20.0 } }, 0.9, 0.5, 8.0, { -1, -1 }, -1 });

            int worst = 0, unread = 0, n = 0;
            std::uint32_t seed = 301;
            for (const auto& r : runs)
                for (const double sr : { 44100.0, 48000.0, 96000.0 })
                {
                    const bool slow = r.decay < 1.0, quieter = std::strncmp (r.what, "a quieter", 9) == 0;
                    if (sr != 48000.0 && ! slow)
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

        // 7. HUM THAT SWINGS: +-4 dB at 1 Hz and +-6 dB at 0.5 Hz with the tuner opened onto it, +-3 dB at
        //    0.3 Hz the same, and +-4 dB at 0.1 Hz coming in two seconds after the tuner opened. Its rises
        //    are no plucks: once it reads HUM, never a note's name again — for seven seconds. Opened onto it,
        //    HUM within 4 s; hum that came in (an onset) and then FALLS — the slow swing's downward half —
        //    falls like a string, and reads as one until it stops falling: up to 7 s here.
        {
            struct Swing { double mains, db, hz, from; };
            const Swing swings[] = { { 50.0, 4.0, 1.0, 0.0 }, { 60.0, 4.0, 1.0, 0.0 }, { 50.0, 6.0, 0.5, 0.0 },
                                     { 50.0, 3.0, 0.3, 0.0 }, { 50.0, 4.0, 0.1, 2.0 } };
            double slowest = 0.0, slowestIn = 0.0;
            int never = 0, notesAfter = 0;
            for (const auto& w : swings)
            {
                Hum h { w.mains, { { 1, 0.0 } }, -40.0 };
                h.swingDb = w.db;
                h.swingHz = w.hz;
                double first = -1.0;
                for (const auto& k : listenTo (room (48000.0, w.from > 0.0 ? w.from + 9.0 : 8.0, h, w.from, 13), 48000.0, 1.0, std::max (1.0, w.from)))
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

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
