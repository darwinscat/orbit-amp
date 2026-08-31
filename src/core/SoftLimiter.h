// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cmath>

namespace orbitamp::core
{

/** The safety at the door: a peak limiter with instant attack and a fast exponential release.

    Not a loudness tool — a ceiling. The envelope jumps to any peak instantly (no lookahead, no
    latency) and lets go over ~60 ms, so a stray transient is caught and the tail breathes. Gain
    is computed once per sample from the envelope, which makes the curve continuous — no steps,
    no zipper.

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles. */
class SoftLimiter
{
public:
    void prepare (double sampleRate)
    {
        releaseCoef = (float) std::exp (-1.0 / (0.060 * sampleRate));
        envelope    = 0.0f;
        minGain     = 1.0f;
    }

    /** `ceilingDb` is the lid in dBFS (negative). Runs unconditionally cheap when `on` is false —
        the envelope resets so re-enabling starts clean. */
    void process (float* const* channels, int numChannels, int numSamples, bool on, float ceilingDb)
    {
        minGain = 1.0f;

        if (! on || numChannels <= 0)
        {
            envelope = 0.0f;
            return;
        }

        const float ceiling = std::pow (10.0f, ceilingDb / 20.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                peak = std::max (peak, std::abs (channels[ch][i]));

            // Instant up, exponential down — the classic peak follower.
            envelope = peak > envelope ? peak : peak + (envelope - peak) * releaseCoef;

            const float gain = envelope > ceiling ? ceiling / envelope : 1.0f;
            minGain = std::min (minGain, gain);

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
        }
    }

    /** The deepest squeeze of the last block, linear — 1.0 means the limiter never touched it. */
    float lastMinGain() const noexcept { return minGain; }

private:
    float releaseCoef = 0.999f;
    float envelope    = 0.0f;
    float minGain     = 1.0f;
};

} // namespace orbitamp::core
