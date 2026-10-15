// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <vector>

namespace orbitamp::core
{

/** A STAGE'S OWN LATENESS, FOR WHEN IT IS OUT. A stage that looks ahead hands its signal on late; while
    it is switched out, this hands the input on exactly as late, so the chain reports one latency
    whether the stage works or not, and a switch crossfades two signals that line up. Untouched: what
    goes in comes out, `length` samples later, to the bit.

    Fed every block, in or out, so that it holds the last `length` samples of input whenever it is
    asked for them. Sized once, off the audio thread; nothing on the audio thread allocates.

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles. */
class LateWire
{
public:
    void prepare (int samples, int channels)
    {
        length = std::max (0, samples);
        ring.assign ((size_t) std::max (0, channels), std::vector<float> ((size_t) std::max (1, length), 0.0f));
        at = 0;
    }

    int latency() const noexcept { return length; }

    /** The input goes in; what went in `length` samples ago comes out. `out` may be `in`. Channels
        past the ones prepared go through on time. */
    void pass (const float* const* in, float* const* out, int numChannels, int numSamples) noexcept
    {
        run (in, out, numChannels, numSamples);
    }

    /** The input goes in, and nothing comes out — for a block the stage itself plays. */
    void feed (const float* const* in, int numChannels, int numSamples) noexcept
    {
        run (in, nullptr, numChannels, numSamples);
    }

private:
    std::vector<std::vector<float>> ring;
    int length = 0, at = 0;

    void run (const float* const* in, float* const* out, int numChannels, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (length == 0 || ch >= (int) ring.size())
            {
                if (out != nullptr && out[ch] != in[ch])
                    std::copy (in[ch], in[ch] + numSamples, out[ch]);
                continue;
            }

            auto& r = ring[(size_t) ch];
            int k = at;
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = in[ch][i];      // read first: `out` may be `in`
                if (out != nullptr)
                    out[ch][i] = r[(size_t) k];
                r[(size_t) k] = x;
                if (++k == length)
                    k = 0;
            }
        }

        if (length > 0)
            at = (int) (((long long) at + numSamples) % length);
    }
};

} // namespace orbitamp::core
