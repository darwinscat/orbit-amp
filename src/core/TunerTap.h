// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <atomic>

namespace orbitamp::core
{

/** The window the tuner listens to: the raw input, before any block colours it.

    ScopeTap's contract, mono and longer: the audio thread writes, the tuner panel snapshots
    whenever its timer fires. No lock — a torn read costs one slightly wrong pitch reading, which
    the panel's median swallows, and a lock on the audio thread costs a dropout. The generation
    counter is only there to say "never written yet".

    32768 samples is ~680 ms at 48 kHz and ~340 ms at 96 kHz. The tracker analyses the NEWEST ~256
    ms of it after decimation and lets the rest go — and the rest is not waste: it is where the
    anti-alias filter settles, where a torn copy's seam lands, and where the last note's tail
    sits after a string is plucked again. At 16384 that head was already gone at 44.1 and 48 kHz
    but at 96 kHz the tracker analysed the whole ring, seam and filter start-up and old note
    included — a window whose oldest quarter still held a note a semitone away read 12.7 cents
    off, and passed the clarity gate. */
class TunerTap
{
public:
    static constexpr int size = 32768;

    void write (const float* in, int numSamples) noexcept
    {
        generation.fetch_add (1, std::memory_order_release);

        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            buf[(size_t) w] = in[i];
            w = (w + 1) % size;
        }
        writePos.store (w, std::memory_order_relaxed);

        generation.fetch_add (1, std::memory_order_release);
    }

    /** Copies the window out, oldest first. Returns false when the tap has never been written. */
    bool read (std::array<float, size>& out) const noexcept
    {
        if (generation.load (std::memory_order_acquire) == 0)
            return false;

        const int start = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < size; ++i)
            out[(size_t) i] = buf[(size_t) ((start + i) % size)];

        return true;
    }

private:
    std::array<float, size> buf {};
    std::atomic<int> writePos { 0 };   // the head is shared with the reader — by the letter, too
    std::atomic<unsigned> generation { 0 };
};

} // namespace orbitamp::core
