// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>

namespace orbitamp::core
{

/** The window the tuner listens to: the raw input, before any block colours it.

    ScopeTap's contract, mono and longer: the audio thread writes, the tuner panel snapshots
    whenever its timer fires. No lock — a torn read costs one slightly wrong pitch reading, which
    the panel's median swallows, and a lock on the audio thread costs a dropout. The generation
    counter is only there to say "never written yet".

    The STROBE reads it the other way (readSince): not a window, but every sample that arrived since
    it last looked, in order — the count of samples ever written says which those are.

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
        written.fetch_add ((unsigned) numSamples, std::memory_order_release);

        generation.fetch_add (1, std::memory_order_release);
    }

    /** How many samples were ever written, counted round 2^32. 2^32 is a whole number of rings, so
        sample `t` of the count always sits at `t % size` — the count alone says where anything is. */
    unsigned writtenCount() const noexcept { return written.load (std::memory_order_acquire); }

    /** The samples that arrived since `cursor` (a writtenCount() the reader kept), oldest first, into
        `out`; `cursor` moves past them all. At most `capacity`, and never more than the ring holds
        less `guard` — the head the audio thread may be writing into while this copies. What there is
        no room for is the OLDEST: it is stepped over, and `skipped` says how many, so a reader that
        keeps time in samples keeps it across the gap. Returns how many were copied. */
    int readSince (unsigned& cursor, float* out, int capacity, unsigned& skipped) const noexcept
    {
        const unsigned total = written.load (std::memory_order_acquire);
        const unsigned fresh = total - cursor;
        const unsigned keep  = (unsigned) std::max (0, std::min (capacity, size - guard));

        skipped = fresh > keep ? fresh - keep : 0u;
        const int n = (int) (fresh - skipped);
        const unsigned from = cursor + skipped;
        for (int i = 0; i < n; ++i)
            out[i] = buf[(size_t) ((from + (unsigned) i) % (unsigned) size)];

        cursor = total;
        return n;
    }

    /** The newest stretch of the ring readSince leaves alone: more than one audio block. */
    static constexpr int guard = 8192;

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
    std::atomic<unsigned> written { 0 };   // samples ever written, round 2^32 — published after them
};

} // namespace orbitamp::core
