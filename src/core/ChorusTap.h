// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/chorus/BassChorus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace orbitamp::core
{

/** WHERE THE BASS'S CHORUS SWINGS, FOR ITS PICTURE (ui/ChorusBlock.h): the phase of its LFO, 0..1, every `hopSeconds`
    of audio, in a ring the picture reads the newest few dozen of — the two voices' dots and their short trails.

    THE PHASE IS DERIVED HERE, not read: felitronics::chorus::BassChorus keeps its LFO to itself. follow() is told of
    every call the stage got and mirrors what the stage does with its clock: the phase advances by the rate over the
    samples the call carried (a gap's too), the rate gliding to what it was told over BassChorus::kRateGlide exactly as
    the stage's one-pole does (summed in closed form); an idle stage's clock stands; a WAKE — an idle stage told a mix
    above 0 — is the stage's restart: the phase back to 0, the rate snapped. The one thing it does not see is the
    sample a stage fading to idle lands on mid-call; from there on the stage stands with no swing, and its next wake
    starts it from 0 again. The derivation runs on every call (a few operations); the RING is written only while the
    picture is on screen (`write`), and nothing at all while the stage idles: the picture stands still.

    The audio thread writes (follow); the message thread reads (read) on its own timer. No lock and no allocation:
    fixed arrays of relaxed atomics, one release count, and a GENERATION per slot — the entry's own number, 0 while it
    is being written — that the reader checks before and after taking the slot, so a slot the writer laps during a
    read is dropped, never mixed with its successor.

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles (where the stage could simply
    tell its phase). */
class ChorusTap
{
public:
    using Stage = felitronics::chorus::BassChorus;

    static constexpr int    slots      = 256;     // 1.28 s at 5 ms a hop
    static constexpr double hopSeconds = 0.005;

    struct Entry
    {
        float phase  = 0.0f;    // the LFO, 0..1: the left voice swings by sin 2π·phase, the right by cos
        bool  joined = false;   // continues the entry before it: no restart and no unwritten hop between them
        bool  ok     = false;   // taken whole; a slot overwritten while it was read is not
    };

    /** Off the audio thread, after the stage's own prepare (which restarts it on the knobs it was told). */
    void prepare (double sampleRate, const Stage& stage) noexcept
    {
        fs     = sampleRate > 0.0 ? sampleRate : 48000.0;
        keep   = std::exp (-1.0 / (Stage::kRateGlide * fs));
        hop    = std::max (1, (int) std::lround (fs * hopSeconds));
        filled = 0;
        restart (stage);
    }

    /** The audio thread, after each call of the stage: whether it stood idle BEFORE the call, and the samples the call
        carried (0 for a call the stage refused). Writes the ring only when `write`. */
    void follow (const Stage& stage, bool wasIdle, int numSamples, bool write) noexcept
    {
        if (numSamples <= 0)
            return;
        const auto p = stage.params();
        if (wasIdle)
        {
            if (! (p.mix > 0.0f))
                return;        // idle: nothing computed, the clock stands
            restart (stage);   // a wake is a restart
        }

        for (int n = numSamples; n > 0;)
        {
            const int m = std::min (n, hop - filled);
            advance (m, (double) p.rateHz);
            filled += m;
            n -= m;
            if (filled == hop)
            {
                filled = 0;
                if (write)
                    push();
                else
                    joined = false;   // a hop not written: the next one written starts a new trail
            }
        }
    }

    /** The derived phase now, 0..1 — the audio thread's (a gate reads it between calls). */
    double phase() const noexcept { return phase_; }

    /** Any thread: how many entries have gone in since construction. */
    std::uint32_t written() const noexcept { return count.load (std::memory_order_acquire); }

    /** The message thread: the newest `want` entries (at most `slots`), oldest first, into `dst`; how many there were.
        An entry the writer lapped while it was read comes back with `ok` false. */
    int read (Entry* dst, int want) const noexcept
    {
        const auto k    = count.load (std::memory_order_acquire);
        const int  have = (int) std::min<std::uint32_t> (k, (std::uint32_t) std::clamp (want, 0, slots));
        for (int i = 0; i < have; ++i)
        {
            const auto  n  = k - (std::uint32_t) have + (std::uint32_t) i;   // the entry's own number
            const auto& s  = ring[(size_t) (n % (std::uint32_t) slots)];
            const auto  g1 = s.gen.load (std::memory_order_acquire);
            const float ph = s.phase.load (std::memory_order_relaxed);
            const bool  jn = s.joined.load (std::memory_order_relaxed) != 0;
            std::atomic_thread_fence (std::memory_order_acquire);
            const auto  g2 = s.gen.load (std::memory_order_relaxed);
            dst[i] = { ph, jn, g1 == n + 1 && g2 == n + 1 };
        }
        return have;
    }

    /** The writer's own step, for a gate that laps the reader on purpose: one entry of `phase`, joined. */
    void pushForTest (float phase, bool join) noexcept
    {
        phase_ = (double) phase;
        joined = join;
        push();
    }

private:
    void restart (const Stage& stage) noexcept
    {
        phase_ = 0.0;
        rate   = (double) stage.params().rateHz;
        joined = false;
    }

    /** m samples of the stage's clock: per sample the rate steps toward `target`, then the phase moves by it. */
    void advance (int m, double target) noexcept
    {
        const double d  = rate - target;
        const double km = std::pow (keep, (double) m);
        const double sum = (double) m * target + (d != 0.0 ? d * keep * (1.0 - km) / (1.0 - keep) : 0.0);
        phase_ += sum / fs;
        phase_ -= std::floor (phase_);
        rate = target + d * km;
        if (std::abs (rate - target) < 1.0e-12)
            rate = target;   // the stage's glide lands there
    }

    void push() noexcept
    {
        const auto k = count.load (std::memory_order_relaxed);
        auto& s = ring[(size_t) (k % (std::uint32_t) slots)];
        s.gen.store (0, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        s.phase.store ((float) phase_, std::memory_order_relaxed);
        s.joined.store (joined ? 1u : 0u, std::memory_order_relaxed);
        s.gen.store (k + 1, std::memory_order_release);
        count.store (k + 1, std::memory_order_release);
        joined = true;
    }

    struct Slot
    {
        std::atomic<std::uint32_t> gen { 0 };
        std::atomic<float>         phase { 0.0f };
        std::atomic<std::uint32_t> joined { 0 };
    };

    std::array<Slot, slots> ring {};
    std::atomic<std::uint32_t> count { 0 };

    double fs = 48000.0, keep = 0.0, rate = 0.0, phase_ = 0.0;
    int    hop = 240, filled = 0;
    bool   joined = false;
};

} // namespace orbitamp::core
