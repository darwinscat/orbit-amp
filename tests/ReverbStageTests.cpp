// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::ReverbStage — that Mix actually crossfades, that a tail exists
// and decays, and that disabling the block cannot spill the previous tail back in. Returns non-zero
// on any failure.
//
// Links juce_audio_basics: unlike the tone stack, this stage is JUCE-bound by construction (Freeverb).

#include "core/ReverbStage.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::ReverbStage;

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const char* detail = "")
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail);
    }

    constexpr double sampleRate = 48000.0;

    /** Feeds one impulse, then silence, and returns the peak level over a window (in ms). */
    double tailPeak (ReverbStage& r, int fromMs, int toMs)
    {
        const int n = (int) (sampleRate * toMs / 1000.0);
        std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
        left[0] = right[0] = 1.0f;

        float* channels[2] = { left.data(), right.data() };
        r.process (channels, 2, n);

        const int from = (int) (sampleRate * fromMs / 1000.0);
        double peak = 0.0;
        for (int i = from; i < n; ++i)
            peak = std::max (peak, (double) std::fabs (left[(size_t) i]));

        return peak;
    }
}

int main()
{
    ReverbStage r;
    r.prepare (sampleRate, (int) sampleRate * 3);   // the tests hand whole seconds in one call

    // An amp's reverb ADDS to the dry signal, it does not crossfade away from it. So the dry path
    // must sit at unity at EVERY mix setting — including the maximum. This is the behaviour the
    // whole control hangs on, so it is checked at both ends.
    auto dryLevelAt = [&r] (float mix)
    {
        r.setMix (mix);
        r.reset();

        // juce::Reverb ramps its gains over ~10 ms so a mix change never zips. Run silence through
        // first so the measurement is of the settled level, not of a gain still on its way there.
        {
            std::vector<float> l (4800, 0.0f), rr (4800, 0.0f);
            float* warm[2] = { l.data(), rr.data() };
            r.process (warm, 2, 4800);
        }

        std::vector<float> left (256, 0.0f), right (256, 0.0f);
        left[0] = right[0] = 1.0f;
        float* channels[2] = { left.data(), right.data() };
        r.process (channels, 2, 256);

        return (double) left[0];
    };

    report ("mix 0: dry at unity",   std::fabs (dryLevelAt (0.0f) - 1.0) < 0.02, "");
    report ("mix 1: dry STILL unity (adds, never replaces)", std::fabs (dryLevelAt (1.0f) - 1.0) < 0.02, "");

    // Mix at zero must be silent after the dry impulse — inaudible, not "nearly" inaudible.
    r.setMix (0.0f);
    r.reset();
    {
        std::vector<float> left (256, 0.0f), right (256, 0.0f);
        left[0] = right[0] = 1.0f;
        float* channels[2] = { left.data(), right.data() };
        r.process (channels, 2, 256);

        double spill = 0.0;
        for (size_t i = 1; i < left.size(); ++i)
            spill = std::max (spill, (double) std::fabs (left[i]));

        report ("mix 0: impulse passes, nothing follows it", spill < 1.0e-4,
                ("spill " + std::to_string (spill)).c_str());
    }

    // Fully wet: there must still be a tail 200 ms after the impulse, and it must be decaying.
    r.setMix (1.0f);
    r.setCharacter (ReverbStage::Character::hall);
    r.reset();
    const double early = tailPeak (r, 50, 150);
    r.reset();
    const double late  = tailPeak (r, 400, 500);

    report ("mix 1: tail present at 50-150 ms", early > 1.0e-3,
            ("peak " + std::to_string (early)).c_str());
    report ("mix 1: tail is decaying, not sustaining", late < early,
            ("late " + std::to_string (late) + " < early " + std::to_string (early)).c_str());

    // reset() must actually empty the network — otherwise re-enabling the block spills the tail of
    // whatever was playing before it was switched off.
    r.reset();
    {
        std::vector<float> left (4096, 0.0f), right (4096, 0.0f);
        float* channels[2] = { left.data(), right.data() };
        r.process (channels, 2, 4096);

        double residue = 0.0;
        for (float v : left)
            residue = std::max (residue, (double) std::fabs (v));

        report ("reset: silence in, silence out", residue < 1.0e-6,
                ("residue " + std::to_string (residue)).c_str());
    }

    // Character must be audible: a spring is small and damped, a hall is not.
    r.setMix (1.0f);
    r.setCharacter (ReverbStage::Character::spring);
    r.reset();
    const double spring = tailPeak (r, 300, 500);
    r.setCharacter (ReverbStage::Character::hall);
    r.reset();
    const double hall = tailPeak (r, 300, 500);

    report ("character: hall rings longer than spring", hall > spring,
            ("hall " + std::to_string (hall) + " > spring " + std::to_string (spring)).c_str());

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
