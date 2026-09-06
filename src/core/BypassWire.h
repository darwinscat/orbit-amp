// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace orbitamp::core
{

/** THE WIRE A BYPASSED BLOCK IS, delayed by exactly what the block costs.

    A captured block that rate-matches has a latency — three to fifteen samples, and none at all
    when the pack's rate is the session's, which is the usual case. The plugin reports it so the
    host can line the track up.

    Bypass it and the signal arrives EARLY: the block's delay left the path but the host is still
    compensating for it. A latency that changes with a switch is precisely what makes a host
    re-align mid-song, and the comment beside `reportLatency` had been promising for a long time
    that the bypass path carried the same delay. It did not; this is that promise.

    It also makes the crossfade honest. Blending the block's output against an undelayed input is
    blending a signal with a slightly early copy of itself, which is a comb — at six samples and
    44.1 kHz the first notch lands near 3.7 kHz, right in the presence region. Swept over fifteen
    milliseconds it reads as a tick rather than a filter, but it is a tick that need not exist.

    Tiny by construction: the delay is a handful of samples, so the history is a handful of floats
    and the whole thing is two copies. At zero delay the caller skips it entirely. */
class BypassWire
{
public:
    /** The most a rate-match can cost: `ceil (3 * hostSR / modelSR) + 3`, which at 192 kHz against
        a 48 kHz pack is fifteen. Sixty-four is room to spare and still nothing. */
    static constexpr int maxDelay = 64;

    void prepare() { reset(); }

    void reset()
    {
        for (auto& h : hist)
            h.fill (0.0f);
    }

    /** Writes `in` delayed by `delay` into `out`. In-place is allowed (`out == in`) — that is what
        a fully bypassed block wants; a crossfading one hands its own scratch instead. */
    void process (const float* const* in, float* const* out, int numChannels, int numSamples,
                  int delay) noexcept
    {
        const int d = juce::jlimit (0, maxDelay, delay);

        if (numSamples <= 0)
            return;

        if (d == 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                if (out[ch] != in[ch])
                    juce::FloatVectorOperations::copy (out[ch], in[ch], numSamples);

            return;
        }

        for (int ch = 0; ch < juce::jmin (numChannels, (int) hist.size()); ++ch)
        {
            auto& h = hist[(size_t) ch];

            // `h[k]` is the input sample that stood at position `k - d` — the d samples that came
            // before this block, oldest first.
            std::array<float, maxDelay> prev {};
            for (int i = 0; i < d; ++i)
                prev[(size_t) i] = h[(size_t) i];

            // What the NEXT block will need: the last d samples of (prev ++ in). Taken before a
            // single byte of `out` is written, because `out` may be `in`.
            std::array<float, maxDelay> next {};
            for (int i = 0; i < d; ++i)
            {
                const int at = numSamples - d + i;     // position of this sample in `in`
                next[(size_t) i] = at >= 0 ? in[ch][at] : prev[(size_t) (at + d)];
            }

            // Backwards, so an in-place shift never eats what it has not read yet.
            for (int i = numSamples - 1; i >= d; --i)
                out[ch][i] = in[ch][i - d];

            for (int i = juce::jmin (d, numSamples) - 1; i >= 0; --i)
                out[ch][i] = prev[(size_t) i];

            for (int i = 0; i < d; ++i)
                h[(size_t) i] = next[(size_t) i];
        }
    }

private:
    std::array<std::array<float, maxDelay>, 2> hist {};
};

} // namespace orbitamp::core
