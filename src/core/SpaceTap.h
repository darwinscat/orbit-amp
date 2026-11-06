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

/** WHAT THE BASS'S DELAY OR REVERB ADDS, FOR ITS PICTURE (ui/SpaceBlocks.h): the loudest sample of the wet the stage
    added — after the bass's low cut on it, at the mix — every `hopSeconds` of audio, in a ring the picture reads the
    newest few hundred of: the repeats as they come round, the tail as it falls.

    The audio thread counts every sample the stage ran (take); the RING is written only while the picture is on screen
    (`write`) and the stage is at work (switched on, its MIX up), and nothing at all while it is out of the chain: the
    picture stands still. A hop not written
    breaks the trace — the next one written starts a new one.

    The audio thread writes; the message thread reads on its own timer. No lock and no allocation: fixed arrays of
    relaxed atomics, one release count, and a GENERATION per slot — the entry's own number, 0 while it is being
    written — that the reader checks before and after taking the slot, so a slot the writer laps during a read is
    dropped, never mixed with its successor (the family's tap, as core::ChorusTap). JUCE-free. */
class SpaceTap
{
public:
    static constexpr int    slots      = 512;     // 5.12 s at 10 ms a hop
    static constexpr double hopSeconds = 0.01;

    struct Entry
    {
        float level  = 0.0f;    // the hop's loudest wet sample, linear
        bool  joined = false;   // continues the entry before it: no unwritten hop between them
        bool  ok     = false;   // taken whole; a slot overwritten while it was read is not
    };

    /** Off the audio thread. */
    void prepare (double sampleRate) noexcept
    {
        hop    = std::max (1, (int) std::lround ((sampleRate > 0.0 ? sampleRate : 48000.0) * hopSeconds));
        filled = 0;
        peak   = 0.0f;
        joined = false;
    }

    /** The audio thread, a sample at a time: the wet's magnitude at it (the loudest of the planes). */
    void take (float magnitude, bool write) noexcept
    {
        peak = std::max (peak, std::isfinite (magnitude) ? magnitude : 0.0f);
        if (++filled < hop)
            return;
        filled = 0;
        if (write)
            push (peak);
        else
            joined = false;
        peak = 0.0f;
    }

    /** The audio thread: the stage left the chain or was cleared — the trace breaks here. */
    void breakTrace() noexcept { joined = false; filled = 0; peak = 0.0f; }

    /** Any thread: how many entries have gone in since construction. */
    std::uint32_t written() const noexcept { return count.load (std::memory_order_acquire); }

    /** The message thread: the newest `want` entries (at most `slots`), oldest first, into `dst`; how many there were. */
    int read (Entry* dst, int want) const noexcept
    {
        const auto k    = count.load (std::memory_order_acquire);
        const int  have = (int) std::min<std::uint32_t> (k, (std::uint32_t) std::clamp (want, 0, slots));
        for (int i = 0; i < have; ++i)
        {
            const auto  n  = k - (std::uint32_t) have + (std::uint32_t) i;
            const auto& s  = ring[(size_t) (n % (std::uint32_t) slots)];
            const auto  g1 = s.gen.load (std::memory_order_acquire);
            const float lv = s.level.load (std::memory_order_relaxed);
            const bool  jn = s.joined.load (std::memory_order_relaxed) != 0;
            std::atomic_thread_fence (std::memory_order_acquire);
            const auto  g2 = s.gen.load (std::memory_order_relaxed);
            dst[i] = { lv, jn, g1 == n + 1 && g2 == n + 1 };
        }
        return have;
    }

    /** The writer's own step, for a gate: one entry of `level`, joined or not. */
    void pushForTest (float level, bool join) noexcept
    {
        joined = join;
        push (level);
    }

private:
    struct Slot
    {
        std::atomic<std::uint32_t> gen { 0 };
        std::atomic<float>         level { 0.0f };
        std::atomic<int>           joined { 0 };
    };

    void push (float level) noexcept
    {
        const auto n = count.load (std::memory_order_relaxed);
        auto& s = ring[(size_t) (n % (std::uint32_t) slots)];
        s.gen.store (0, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        s.level.store (level, std::memory_order_relaxed);
        s.joined.store (joined ? 1 : 0, std::memory_order_relaxed);
        s.gen.store (n + 1, std::memory_order_release);
        count.store (n + 1, std::memory_order_release);
        joined = true;
    }

    std::array<Slot, (size_t) slots> ring {};
    std::atomic<std::uint32_t> count { 0 };
    int   hop = 480, filled = 0;
    float peak = 0.0f;
    bool  joined = false;
};

} // namespace orbitamp::core
