// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for the picture's memory: what the ribbon recorded must not change afterwards.
//
// The bug this exists to stop: the input and output waveforms are matched for level, and when that
// matching was done at READ time it used one factor for the whole window, recomputed every frame from
// whatever was playing right then. Turning the gain up rescaled the entire history — a peak recorded
// clipped drifted off the left edge rounded, because it was being drawn by a factor that did not
// exist when it happened. The picture showed the past rearranged by the present.
//
// JUCE-light on purpose: the ribbon is arrays and arithmetic, so this needs no host and no device.

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/WaveRibbon.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::WaveRibbon;

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail = {})
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    constexpr double sampleRate = 48000.0;

    /** Feed a stretch of signal at a steady level: `amplitude` in, `amplitude * wetGain` out.

        Deliberately not a sine. A column holds a whole number of samples, so the column a moment of
        audio lands in shifts by one or two against any arithmetic done in seconds — and on a sine
        that is the difference between a peak and a zero crossing. A steady level makes every column
        the same, so a comparison across the ribbon measures what it means to. */
    void feed (WaveRibbon& r, double seconds, float amplitude, float wetGain, int& phase)
    {
        const int n = (int) (sampleRate * seconds);
        std::vector<float> dry ((size_t) n), wet ((size_t) n);

        for (int i = 0; i < n; ++i, ++phase)
        {
            dry[(size_t) i] = (phase & 1) != 0 ? amplitude : -amplitude;   // full-scale square
            wet[(size_t) i] = dry[(size_t) i] * wetGain;
        }

        r.write (dry.data(), wet.data(), n);
    }

    float peakOf (const std::array<WaveRibbon::Column, WaveRibbon::buckets>& c, int from, int to)
    {
        float p = 0.0f;
        for (int i = from; i < to; ++i)
            p = juce::jmax (p, c[(size_t) i].wetHi);

        return p;
    }
}

int main()
{
    std::printf ("orbitamp wave-ribbon gate\n\n");

    WaveRibbon ribbon;
    ribbon.setResolution (WaveRibbon::buckets);
    ribbon.prepare (sampleRate);

    std::array<WaveRibbon::Column, WaveRibbon::buckets> before {}, after {};
    int phase = 0;

    // A second at a settled output, then read it back.
    feed (ribbon, 1.0, 0.5f, 1.0f, phase);
    int n = 0;
    float ph = 0.0f;
    report ("the ribbon has something to show", ribbon.read (before, n, ph));

    // The newest columns of that first stretch, once the slow level estimate has settled on it.
    const float recorded = peakOf (before, WaveRibbon::buckets - 200, WaveRibbon::buckets - 1);
    report ("...matched to the input while it was steady",
            std::abs (recorded - 0.5f) < 0.02f, juce::String (recorded, 4));

    // Now the output jumps tenfold — a gain knob thrown up. The NEW columns may show whatever they
    // show; the ones already recorded must not move.
    feed (ribbon, 0.5, 0.5f, 10.0f, phase);
    ribbon.read (after, n, ph);

    // Where that first stretch has slid to. Two columns of slack, because a column is a whole number
    // of samples and 0.5 s is not.
    const int shift = (int) (0.5 * sampleRate) / (int) (sampleRate * WaveRibbon::seconds
                                                        / (double) WaveRibbon::buckets);
    const float moved = peakOf (after, WaveRibbon::buckets - 200 - shift + 2,
                                       WaveRibbon::buckets - 1 - shift - 2);

    std::printf ("\nrecorded at %.4f, reads %.4f after the knob\n", recorded, moved);

    report ("a knob does not rewrite what already happened",
            std::abs (moved - recorded) < 0.02f,
            juce::String (recorded, 4) + " then, " + juce::String (moved, 4) + " now");

    // The matching is what makes the picture about shape rather than volume, so it still has to
    // happen — just at the right moment. Ten times the output must not draw ten times the height.
    {
        WaveRibbon steady;
        steady.setResolution (WaveRibbon::buckets);
        steady.prepare (sampleRate);

        int p2 = 0;
        feed (steady, 3.0, 0.5f, 8.0f, p2);

        std::array<WaveRibbon::Column, WaveRibbon::buckets> c {};
        int n2 = 0; float ph2 = 0.0f;
        steady.read (c, n2, ph2);

        const float wet = peakOf (c, WaveRibbon::buckets - 200, WaveRibbon::buckets - 1);

        float dry = 0.0f;
        for (int i = WaveRibbon::buckets - 200; i < WaveRibbon::buckets - 1; ++i)
            dry = juce::jmax (dry, c[(size_t) i].dryHi);

        report ("a steadily louder output is still matched, not drawn louder",
                std::abs (wet - dry) < 0.1f * juce::jmax (0.001f, dry),
                "dry " + juce::String (dry, 3) + ", wet " + juce::String (wet, 3));
    }

    // Past the first full turn of the ring. A column closes on a sample counter, and when that
    // counter stopped being reset the ribbon wrote one screenful and then raced — every sample
    // closing a column, nothing accumulating — so the picture scrolled away into blank.
    {
        WaveRibbon lap;
        lap.setResolution (512);
        lap.prepare (sampleRate);

        int p3 = 0;
        feed (lap, WaveRibbon::seconds * 2.5, 0.5f, 1.0f, p3);   // two and a half windows

        std::array<WaveRibbon::Column, WaveRibbon::buckets> c {};
        int n3 = 0;
        float ph3 = 0.0f;
        lap.read (c, n3, ph3);

        int drawn = 0;
        for (int i = 0; i < n3; ++i)
            if (c[(size_t) i].dryHi > 0.1f)
                ++drawn;

        std::printf ("\n%d of %d columns still hold signal after 2.5 windows\n", drawn, n3);

        report ("the ribbon keeps writing past its first lap", drawn > n3 * 9 / 10,
                juce::String (drawn) + "/" + juce::String (n3));

        report ("...and the slide fraction stays a fraction", ph3 >= 0.0f && ph3 <= 1.0f,
                juce::String (ph3, 3));
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
