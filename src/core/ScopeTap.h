// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <array>

namespace orbitamp::core
{

/** A window of what went into a block and what came out of it, for the pictures to read.

    The audio thread writes; the editor reads whenever it repaints. There is no lock: a torn read
    costs a slightly wrong pixel for one frame, and a lock on the audio thread costs a dropout. The
    generation counter lets a reader notice it caught a tear and simply draw the previous frame.

    Level-matched on the way out, not on the way in: the whole point of a before/after picture is to
    show what the block DID, and a boost that is merely louder would drown that. */
class ScopeTap
{
public:
    static constexpr int size = 2048;

    void write (const float* dry, const float* wet, int numSamples) noexcept
    {
        generation.fetch_add (1, std::memory_order_release);

        for (int i = 0; i < numSamples; ++i)
        {
            const int w = writePos;
            dryBuf[(size_t) w] = dry[i];
            wetBuf[(size_t) w] = wet[i];
            writePos = (w + 1) % size;
        }

        generation.fetch_add (1, std::memory_order_release);
    }

    /** Copies the window out, oldest first, with the wet side scaled so both have the same RMS.
        Returns false when the tap has never been written. */
    bool read (std::array<float, size>& dryOut, std::array<float, size>& wetOut) const noexcept
    {
        const auto before = generation.load (std::memory_order_acquire);
        if (before == 0)
            return false;

        const int start = writePos;
        for (int i = 0; i < size; ++i)
        {
            const int idx = (start + i) % size;
            dryOut[(size_t) i] = dryBuf[(size_t) idx];
            wetOut[(size_t) i] = wetBuf[(size_t) idx];
        }

        // Match the levels. Without this the picture says "louder", which is not what any of the
        // modes are trying to show.
        double dr = 0.0, wr = 0.0;
        for (int i = 0; i < size; ++i)
        {
            dr += (double) dryOut[(size_t) i] * dryOut[(size_t) i];
            wr += (double) wetOut[(size_t) i] * wetOut[(size_t) i];
        }

        if (wr > 1.0e-12 && dr > 1.0e-12)
        {
            const float k = (float) std::sqrt (dr / wr);
            for (auto& s : wetOut)
                s *= k;
        }

        return true;
    }

private:
    std::array<float, size> dryBuf {}, wetBuf {};
    int writePos = 0;
    std::atomic<unsigned> generation { 0 };
};

} // namespace orbitamp::core
