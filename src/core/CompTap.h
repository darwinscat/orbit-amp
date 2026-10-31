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

/** WHAT THE BASS'S COMP DID, FOR ITS PICTURE (ui/CompBlock.h): the level going in, the level coming out and the
    squeeze, as COLUMNS — each the peaks of in and out and the deepest squeeze over `hopSeconds` of audio — in a ring
    the picture reads the newest few hundred of. ALL THREE LEVELS ARE THE FULL-BAND AUDIO, measured alike: BEFORE is the
    signal COMP is handed — not its detector's key, which drops below 30 Hz — so with no squeeze the three lie on each
    other at every note. The THRESHOLD line is the detector's: on the lowest notes (B0, E1) the key reads them a decibel
    or three under the trace, and the squeeze starts that much later than the line says.

    The audio thread writes (write), only while the picture is on screen (AmpProcessor::compWatched) and COMP is heard —
    standing by or at BLEND 0 it writes nothing and the picture stands still (BassComp::process); the message
    thread reads (read) on its own timer. No lock and no allocation: fixed arrays of relaxed atomics and one release
    count. SILENCE IS NOT WRITTEN: once in and out have stood under -80 dBFS, with no squeeze, for `holdColumns`
    columns, nothing more goes in until the playing comes back — the picture stops scrolling rather than scroll
    an empty floor away. A TRACE BREAKS (breakTrace) where the writer stopped writing — COMP standing by, at BLEND 0, the
    picture away: the next column starts a new trace, nothing of the old hop in it (the family's tap, as SpaceTap).

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles. */
class CompTap
{
public:
    static constexpr int    columns     = 1024;    // ~5 s of playing at 5 ms a column
    static constexpr double hopSeconds  = 0.005;
    static constexpr int    holdColumns = 40;      // 200 ms of a tail written after the playing stops
    static constexpr float  silence     = 1.0e-4f; // -80 dBFS

    struct Column
    {
        float in  = 0.0f;   // peak, linear
        float out = 0.0f;   // peak, linear
        float gr  = 0.0f;   // the deepest squeeze, dB (0 or below)
        float mid = 0.0f;   // AFTER COMP, before BLEND and the make-up — measured as `in` and `out`
        bool  joined = false;   // continues the column before it: no break between them
    };

    /** Off the audio thread, with the rate; starts empty. */
    void prepare (double sampleRate) noexcept
    {
        hop = std::max (1, (int) std::lround (sampleRate * hopSeconds));
        rmsK = (float) (1.0 - std::exp (-1.0 / (rmsSeconds * std::max (1.0, sampleRate))));
        meanSquare = meanSquareOut = meanSquareMid = midPeak = 0.0f;
        inPeak = outPeak = grLow = 0.0f;
        filled = 0;
        quiet  = holdColumns + 1;   // nothing has played yet: nothing to scroll
        joinNext = false;
    }

    /** The audio thread: the writer stopped writing a while — the partial hop and the levels' memories go, and the next
        column starts a new trace. */
    void breakTrace() noexcept
    {
        meanSquare = meanSquareOut = meanSquareMid = midPeak = 0.0f;
        inPeak = outPeak = grLow = 0.0f;
        filled   = 0;
        joinNext = false;
    }

    /** The audio thread, one block: the three traces (the owner, 31.10) — BEFORE COMP `in` (the signal it is handed),
        AFTER COMP `mid` (the compressed path, before BLEND) and AFTER COMP + BLEND `out`, all before the make-up, all
        measured alike as the detector measures: their RMS over 10 ms in SMOOTH (`rms`), their peaks in PUNCHY — and the
        squeeze per sample in dB (null for none). */
    void write (const float* in, const float* mid, const float* out, const float* grDb, int n, bool rms) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = std::isfinite (in[i]) ? in[i] : 0.0f;
            const float m = std::isfinite (mid[i]) ? mid[i] : 0.0f;
            const float y = std::isfinite (out[i]) ? out[i] : 0.0f;
            meanSquare    += rmsK * (x * x - meanSquare);
            meanSquareMid += rmsK * (m * m - meanSquareMid);
            meanSquareOut += rmsK * (y * y - meanSquareOut);
            midPeak = std::max (midPeak, rms ? std::sqrt (meanSquareMid) : std::abs (m));
            // ...and the OUTPUT measured exactly as the input: at 0 the two traces lie on each other, and their gap is what
            // COMP did.
            inPeak  = std::max (inPeak,  rms ? std::sqrt (meanSquare)    : std::abs (x));
            outPeak = std::max (outPeak, rms ? std::sqrt (meanSquareOut) : std::abs (y));
            if (grDb != nullptr)
                grLow = std::min (grLow, grDb[i]);
            if (++filled == hop)
                close();
        }
    }

    /** Any thread: how many columns have gone in since the prepare. */
    std::uint32_t written() const noexcept { return count.load (std::memory_order_acquire); }

    /** The message thread: the newest `want` columns (at most `columns`), oldest first, into `dst`; how many there were. */
    int read (Column* dst, int want) const noexcept
    {
        const auto k    = count.load (std::memory_order_acquire);
        const int  have = (int) std::min<std::uint32_t> (k, (std::uint32_t) std::clamp (want, 0, columns));
        for (int i = 0; i < have; ++i)
        {
            const auto at = (size_t) ((k - (std::uint32_t) have + (std::uint32_t) i) % (std::uint32_t) columns);
            dst[i] = { ins[at].load (std::memory_order_relaxed), outs[at].load (std::memory_order_relaxed),
                       grs[at].load (std::memory_order_relaxed), mids[at].load (std::memory_order_relaxed),
                       joins[at].load (std::memory_order_relaxed) != 0 };
        }

        // A writer far ahead (an offline render) may have written over the oldest of these while they were copied — the
        // one being written now included: those are dropped, never shown torn.
        const auto after = count.load (std::memory_order_acquire);
        const auto ahead = after + 1u - k, room = (std::uint32_t) (columns - have);
        const int  lost  = ahead > room ? (int) std::min<std::uint32_t> ((std::uint32_t) have, ahead - room) : 0;
        if (lost > 0)
            std::copy (dst + lost, dst + have, dst);
        return have - lost;
    }

private:
    void close() noexcept
    {
        const bool silent = inPeak < silence && outPeak < silence && grLow > -0.05f;
        quiet = silent ? std::min (quiet + 1, holdColumns + 1) : 0;
        if (quiet <= holdColumns)
        {
            const auto k  = count.load (std::memory_order_relaxed);
            const auto at = (size_t) (k % (std::uint32_t) columns);
            ins[at].store (inPeak, std::memory_order_relaxed);
            outs[at].store (outPeak, std::memory_order_relaxed);
            grs[at].store (grLow, std::memory_order_relaxed);
            mids[at].store (midPeak, std::memory_order_relaxed);
            joins[at].store (joinNext ? 1 : 0, std::memory_order_relaxed);
            count.store (k + 1, std::memory_order_release);
            joinNext = true;
        }
        inPeak = outPeak = grLow = midPeak = 0.0f;
        filled = 0;
    }

    std::array<std::atomic<float>, columns> ins {}, outs {}, grs {}, mids {};
    std::array<std::atomic<int>, columns>   joins {};
    std::atomic<std::uint32_t> count { 0 };
    static constexpr double rmsSeconds = 0.010;   // BassComp's SMOOTH detector: RMS over 10 ms
    float inPeak = 0.0f, outPeak = 0.0f, grLow = 0.0f, meanSquare = 0.0f, meanSquareOut = 0.0f, rmsK = 0.002f;
    float midPeak = 0.0f, meanSquareMid = 0.0f;
    int   filled = 0, hop = 240, quiet = holdColumns + 1;
    bool  joinNext = false;
};

} // namespace orbitamp::core
