// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::PitchTracker — the tuner's ear, checked against signals with
// a KNOWN pitch. Sines because their answer is exact; a sawtooth because a real string is not a
// sine and a strong harmonic is how tuners read octaves wrong; a Karplus-Strong pluck because it
// decays and drifts the way a string does. Returns non-zero on any failure.
//
// JUCE-free, like the code under test: links nothing at all.

#include "core/PitchTracker.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using orbitamp::core::PitchTracker;

namespace
{
    int failures = 0;

    constexpr double pi = 3.14159265358979323846;
    constexpr int    windowLen = 16384;   // what the tap hands the panel

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

    // ---- the naming the panel prints -----------------------------------------------------------
    checkNote ("440 names as", 440.0, "A", 4);
    checkNote ("82.4 names as", 82.407, "E", 2);
    checkNote ("261.6 names as", 261.626, "C", 4);
    checkNote ("466.2 names as", 466.164, "A#", 4);
    check ("445 Hz vs A4 in cents", (double) PitchTracker::nearestNote (445.0f).cents, 19.56, 0.05);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
