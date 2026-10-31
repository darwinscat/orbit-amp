// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/envfilter/EnvelopeFilter.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

namespace orbitamp::core
{

/** WHERE THE BASS'S ENVELOPE FILTER'S SWEEP STOOD, FOR ITS PICTURE (ui/FilterBlock.h): the cutoff, one COLUMN every
    `hopSeconds` of audio, in a ring the picture reads the newest few dozen of — the newest is the curve it draws,
    the ones before it the trail.

    The audio thread writes (write), once a block, only while the picture is on screen (AmpProcessor::filterWatched);
    the message thread reads (read) on its own timer. No lock and no allocation: fixed arrays of relaxed atomics, a
    sequence per slot and one release count. THE READER NEVER MIXES TWO LAPS OF THE RING: each slot carries the
    number of the column it holds, cleared while it is rewritten, and a column whose number is not the one asked
    for — overwritten during the read — is dropped, not shown in another column's place.

    A STILL SWEEP IS NOT WRITTEN: once the cutoff has stood within `stillOctaves` for `holdColumns` columns — longer
    than the trail, so the trail has landed on it — nothing more goes in until it moves, and the picture stands still
    instead of redrawing the same curve.

    JUCE-free on purpose: built here first, moves to felitronics-core when it settles. */
class FilterTap
{
public:
    static constexpr int    columns      = 256;     // 1.28 s at 5 ms a column
    static constexpr double hopSeconds   = 0.005;
    static constexpr int    holdColumns  = 48;      // 240 ms: longer than the picture's trail
    static constexpr double stillOctaves = 1.0e-3;

    struct Column
    {
        std::uint32_t index = 0;   // which column since the prepare, 0 the first
        float hz = 0.0f;           // where the cutoff stood at its end
    };

    /** Off the audio thread, with the rate; starts empty. */
    void prepare (double sampleRate) noexcept
    {
        hop = std::max (1, (int) std::lround (sampleRate * hopSeconds));
        filled = 0;
        quiet  = 0;
        lastHz = 0.0f;
    }

    /** The audio thread, when the picture has just come on screen: the next column goes in whatever it holds — a
        sweep that stood still while nobody looked is still news to a picture that was not there. */
    void wake() noexcept { quiet = 0; }

    /** The audio thread: one block of `n` samples has played, the cutoff standing at `cutoffHz` at its end. */
    void write (float cutoffHz, int n) noexcept
    {
        if (! (cutoffHz > 0.0f && std::isfinite (cutoffHz)) || n <= 0)
            return;
        filled += n;
        while (filled >= hop)
        {
            filled -= hop;
            close (cutoffHz);
        }
    }

    /** Any thread: how many columns have gone in, ever (a prepare does not restart the count: a picture's
        "since I looked" stays true across one). */
    std::uint32_t written() const noexcept { return count.load (std::memory_order_acquire); }

    /** The message thread: the newest `want` columns (at most `columns`), oldest first, into `dst` — less any
        overwritten while they were read; how many there are. */
    int read (Column* dst, int want) const noexcept
    {
        const auto k    = count.load (std::memory_order_acquire);
        const int  have = (int) std::min<std::uint32_t> (k, (std::uint32_t) std::clamp (want, 0, columns));
        int got = 0;
        for (int i = 0; i < have; ++i)
        {
            const std::uint32_t j  = k - (std::uint32_t) have + (std::uint32_t) i;
            const auto          at = (size_t) (j % (std::uint32_t) columns);
            const auto s1 = seqs[at].load (std::memory_order_acquire);
            const auto v  = hzs[at].load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            const auto s2 = seqs[at].load (std::memory_order_relaxed);
            if (s1 == j + 1 && s2 == j + 1)
                dst[got++] = { j, v };
        }
        return got;
    }

private:
    void close (float hz) noexcept
    {
        const bool still = lastHz > 0.0f && std::abs (std::log2 ((double) hz / (double) lastHz)) < stillOctaves;
        quiet  = still ? std::min (quiet + 1, holdColumns + 1) : 0;
        lastHz = hz;
        if (quiet > holdColumns)
            return;

        const auto j  = count.load (std::memory_order_relaxed);
        const auto at = (size_t) (j % (std::uint32_t) columns);
        seqs[at].store (0, std::memory_order_relaxed);           // being rewritten: no reader takes it
        std::atomic_thread_fence (std::memory_order_release);
        hzs[at].store (hz, std::memory_order_relaxed);
        seqs[at].store (j + 1, std::memory_order_release);
        count.store (j + 1, std::memory_order_release);
    }

    std::array<std::atomic<float>, columns>         hzs {};
    std::array<std::atomic<std::uint32_t>, columns> seqs {};
    std::atomic<std::uint32_t> count { 0 };
    float lastHz = 0.0f;
    int   filled = 0, hop = 240, quiet = 0;
};

/** WHAT THE ENVELOPE FILTER DOES TO A STEADY TONE, for its picture: felitronics::envfilter::EnvelopeFilter's settled
    path, gain in dB at `hz`, its swept filter standing at `cutoffHz` — the module's own composition, term for term
    (EnvelopeFilter::channelStep): the low pass's complement split or the band pass's LR4 pair at the clean-lows
    corner, the swept state-variable filter scaled by 1/sqrt(max(1, Q)), the mix over each mode's own dry. Every
    filter in it is a Cytomic TPT one — a bilinear transform warped to land exactly on its corner — so each is its
    analog prototype at tan(pi f / fs) / tan(pi fc / fs): the curve is the stage's at the rate it runs, not an
    analog approximation of it. */
inline double envelopeResponseDb (const felitronics::envfilter::EnvelopeFilterParams& p, double cutoffHz, double hz,
                                  double sampleRate) noexcept
{
    using C = std::complex<double>;
    using F = felitronics::envfilter::EnvelopeFilter;
    const double fs   = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double pi   = 3.14159265358979323846;
    const double fMax = F::kMaxCutoffShare * fs;
    const auto   warp = [&] (double corner) { return C (0.0, std::tan (pi * std::min (hz, 0.499 * fs) / fs) / std::tan (pi * std::min (corner, fMax) / fs)); };

    const double q    = std::clamp ((double) p.resonance, (double) F::kMinQ, (double) F::kMaxQ);
    const double comp = q > 1.0 ? 1.0 / std::sqrt (q) : 1.0;
    const double keep = std::clamp ((double) p.keepLows, 0.0, 1.0);
    const double mix  = std::clamp ((double) p.mix, 0.0, 1.0);

    const C sc = warp (std::clamp (cutoffHz, (double) F::kMinHz, fMax));
    const C den = sc * sc + sc / q + 1.0;
    const C sk = warp (std::clamp ((double) p.keepLowsHz, (double) F::kMinKeepHz, (double) F::kMaxKeepHz));

    C out;
    if (p.mode == felitronics::envfilter::Mode::lowPass)
    {
        const C hp   = sk * sk / (sk * sk + 2.0 * sk + 1.0);    // the complement split's high pass, Q 0.5
        const C lowC = 1.0 - hp;
        const C wet  = keep * lowC + comp * (1.0 / den) * (1.0 - keep * lowC);
        out = 1.0 + mix * (wet - 1.0);
    }
    else
    {
        const C bw  = sk * sk + std::sqrt (2.0) * sk + 1.0;     // the LR4 pair: two Butterworth sections each side
        const C l4  = 1.0 / (bw * bw);
        const C h4  = (sk * sk) * (sk * sk) / (bw * bw);
        const C wet = keep * l4 + comp * (sc / den) * (h4 + (1.0 - keep) * l4);
        const C dry = l4 + h4;
        out = dry + mix * (wet - dry);
    }
    return 20.0 * std::log10 (std::max (std::abs (out), 1.0e-9));
}

/** Where the sweep RESTS — the corner it starts from: the range's low end going up, its high end going down. */
inline double envelopeRestHz (const felitronics::envfilter::EnvelopeFilterParams& p) noexcept
{
    using F = felitronics::envfilter::EnvelopeFilter;
    const double lo = std::clamp ((double) p.sweepLoHz, (double) F::kMinHz, (double) F::kMaxHz);
    const double hi = std::clamp ((double) p.sweepHiHz, (double) F::kMinHz, (double) F::kMaxHz);
    return p.direction == felitronics::envfilter::Direction::up ? lo : hi;
}

/** How far a note `hitDb` dBFS at the key takes the sweep with these knobs: the module's own line (cutoffTarget) —
    fully open where the envelope and the sensitivity reach 0 dB, at rest kSpanDb under it, log-frequency between. At
    mix 0 nowhere: the stage stands idle and its sweep does not move. */
inline double envelopeReachHz (const felitronics::envfilter::EnvelopeFilterParams& p, double hitDb) noexcept
{
    using F = felitronics::envfilter::EnvelopeFilter;
    if (! (p.mix > 0.0f))
        return envelopeRestHz (p);
    const double sens = std::clamp ((double) p.sensitivityDb, (double) F::kMinSensDb, (double) F::kMaxSensDb);
    const double at   = std::clamp (1.0 + (hitDb + sens) / F::kSpanDb, 0.0, 1.0);
    const double lo   = std::log2 (std::clamp ((double) p.sweepLoHz, (double) F::kMinHz, (double) F::kMaxHz));
    const double hi   = std::log2 (std::clamp ((double) p.sweepHiHz, (double) F::kMinHz, (double) F::kMaxHz));
    return std::exp2 (p.direction == felitronics::envfilter::Direction::up ? lo + at * (hi - lo) : hi - at * (hi - lo));
}

} // namespace orbitamp::core
