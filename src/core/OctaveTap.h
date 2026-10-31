// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace orbitamp::core
{

/** WHAT THE BASS'S OCTAVE DID, FOR ITS PICTURE (ui/OctaveScope.h): the stage's input — the note — and its SUB, the
    stage's output less its input (the octaver adds its sub and never touches the dry), as SAMPLES, at the host's rate
    divided down to at most `maxRateHz` (a box average of each step's samples), in a ring the picture reads the
    newest third of a second of.

    The audio thread writes (write), only while the picture is on screen (AmpProcessor::octaveWatched); the message
    thread reads (read) on its own timer. No lock and no allocation: fixed arrays of relaxed atomics and two counts.
    THE READER DOES NOT MIX GENERATIONS: the writer CLAIMS the samples it is about to write before it writes them and
    PUBLISHES them after; the reader copies what was published, then looks at what had been claimed by then, and drops
    every sample whose slot a claimed one may have overwritten during the copy — the oldest, never a torn middle.

    SILENCE IS NOT WRITTEN: once the note and the sub have stood under -80 dBFS for `holdSeconds`, nothing more goes
    in until the playing comes back — the picture stands still on the last of the note rather than scroll a flat line.

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles. */
class OctaveTap
{
public:
    static constexpr int    capacity    = 16384;    // ~0.34 s at the tap's rate: two sub periods of the lowest note and more
    static constexpr double maxRateHz   = 48000.0;
    static constexpr double holdSeconds = 0.25;     // a tail written after the playing stops
    static constexpr float  silence     = 1.0e-4f;  // -80 dBFS

    /** Off the audio thread, with the host's rate; starts empty. */
    void prepare (double sampleRate) noexcept
    {
        stepLen = std::max (1, (int) std::ceil (sampleRate / maxRateHz - 1.0e-9));
        rate.store ((float) (sampleRate / stepLen), std::memory_order_relaxed);
        holdLen = std::max (1, (int) std::lround (sampleRate / stepLen * holdSeconds));
        dryAcc = subAcc = 0.0f;
        filled = 0;
        quiet  = holdLen + 1;   // nothing has played yet: nothing to write
        claimed.store (0, std::memory_order_relaxed);
        published.store (0, std::memory_order_release);
    }

    /** The audio thread: one stretch of the stage's input `dry` and output `wet`, the same instant. */
    void write (const float* dry, const float* wet, int n) noexcept
    {
        const auto k = published.load (std::memory_order_relaxed);
        // The most this stretch can close, claimed before a slot is touched (silence written as nothing only lowers it).
        claimed.store (k + (std::uint32_t) ((filled + n) / stepLen), std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);

        auto at = k;
        for (int i = 0; i < n; ++i)
        {
            dryAcc += dry[i];
            subAcc += wet[i] - dry[i];
            if (++filled < stepLen)
                continue;

            const float d = dryAcc / (float) stepLen, s = subAcc / (float) stepLen;
            dryAcc = subAcc = 0.0f;
            filled = 0;
            const bool silent = std::abs (d) < silence && std::abs (s) < silence;
            quiet = silent ? std::min (quiet + 1, holdLen + 1) : 0;
            if (quiet > holdLen)
                continue;

            const auto slot = (size_t) (at % (std::uint32_t) capacity);
            drys[slot].store (d, std::memory_order_relaxed);
            subs[slot].store (s, std::memory_order_relaxed);
            ++at;
        }
        if (at != k)
            published.store (at, std::memory_order_release);
    }

    /** Any thread: how many samples have gone in since the prepare. */
    std::uint32_t written() const noexcept { return published.load (std::memory_order_acquire); }

    /** Any thread: the tap's own rate, Hz — the host's divided down to at most maxRateHz. */
    float rateHz() const noexcept { return rate.load (std::memory_order_relaxed); }

    /** The message thread: the newest `want` samples (at most `capacity`), oldest first, into `dry` and `sub`; how
        many there were — fewer when the writer overtook the oldest of them during the copy. `newest`, if given, gets
        the count the newest of them closed (written() as of the copy). */
    int read (float* dry, float* sub, int want, std::uint32_t* newest = nullptr) const noexcept
    {
        const auto p     = published.load (std::memory_order_acquire);
        const auto have  = std::min<std::uint32_t> (p, (std::uint32_t) std::clamp (want, 0, capacity));
        const auto first = p - have;
        for (std::uint32_t i = 0; i < have; ++i)
        {
            const auto slot = (size_t) ((first + i) % (std::uint32_t) capacity);
            dry[i] = drys[slot].load (std::memory_order_relaxed);
            sub[i] = subs[slot].load (std::memory_order_relaxed);
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        const auto c = claimed.load (std::memory_order_relaxed);

        // Sample j's slot is overwritten by sample j + capacity: every j under c - capacity may have been.
        const auto ahead = c - first;   // unsigned: a prepare between the two loads reads as huge, and drops all
        const auto lost  = ahead > (std::uint32_t) capacity ? ahead - (std::uint32_t) capacity : 0u;
        if (newest != nullptr)
            *newest = p;
        if (lost >= have)
            return 0;
        if (lost > 0)
        {
            std::copy (dry + lost, dry + have, dry);
            std::copy (sub + lost, sub + have, sub);
        }
        return (int) (have - lost);
    }

private:
    std::array<std::atomic<float>, capacity> drys {}, subs {};
    std::atomic<std::uint32_t> claimed { 0 }, published { 0 };
    std::atomic<float> rate { 48000.0f };
    float dryAcc = 0.0f, subAcc = 0.0f;
    int   stepLen = 1, filled = 0, holdLen = 12000, quiet = 12001;
};

} // namespace orbitamp::core
