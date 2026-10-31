// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "EqLink.h"

#include <felitronics/dynamics/Compressor.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** OrbitBass's COMP: felitronics-core's compressor under ONE KNOB, the law agreed with the owner.

    The knob, 0..1:
      - 0 is no compressor — nothing is computed and the signal passes bit for bit;
      - up the knob the threshold falls from -12 to -36 dB and the ratio rises from 2:1 to 6:1, over a soft
        knee of 8 dB, with core's static auto make-up;
      - up the knob a share of the signal passes UNCOMPRESSED beside it, parallel — a quarter at the top —
        so a note's attack survives the squeeze;
      - no lookahead: no latency, and the uncompressed share lines up with the compressed one sample for sample.

    The detector hears the bass through a 40 Hz high-pass (the key; core gives the input, the product the
    filter): the lowest strings' fundamentals do not pump the rest.

    The menu: ATTACK slow 30 / MED 15 / fast 5 ms; RELEASE AUTO — program-dependent, 80 ms after a short peak
    and growing toward 400 ms as the squeeze is held, the smooth, optical way — or FAST 80 / SLOW 400 ms;
    CHARACTER SMOOTH hears the level's average (RMS over 10 ms), PUNCHY its peaks.

    Mono, as the bass's chain is. Real-time safe: prepare() allocates, process() does not. */
class BassComp
{
public:
    enum class Attack    { slow, med, fast };
    enum class Release   { automatic, fast, slow };
    enum class Character { smooth, punchy };

    static constexpr double thresholdTopDb    = -12.0, thresholdBottomDb = -36.0;
    static constexpr double ratioLow          = 2.0,   ratioHigh         = 6.0;
    static constexpr double kneeDb            = 8.0;
    static constexpr double attackSlowMs      = 30.0,  attackMedMs = 15.0, attackFastMs = 5.0;
    static constexpr double releaseFastMs     = 80.0,  releaseSlowMs = 400.0;
    static constexpr double uncompressedAtTop = 0.25;
    static constexpr double keyCutHz          = 40.0;
    static constexpr int    keyCutSlope       = 24;   // dB/oct

    /** Where the knob puts the curve. */
    struct Law
    {
        double thresholdDb;
        double ratio;
        double uncompressed;   // the parallel share, 0..uncompressedAtTop
    };

    static Law lawAt (double amount) noexcept
    {
        const double a = std::clamp (amount, 0.0, 1.0);
        return { thresholdTopDb + (thresholdBottomDb - thresholdTopDb) * a,
                 ratioLow + (ratioHigh - ratioLow) * a,
                 uncompressedAtTop * a };
    }

    static double attackMsOf (Attack a) noexcept
    {
        return a == Attack::slow ? attackSlowMs : a == Attack::fast ? attackFastMs : attackMedMs;
    }

    [[nodiscard]] bool prepare (double sampleRate, int maxBlock)
    {
        fs    = sampleRate > 0.0 ? sampleRate : 48000.0;
        block = std::max (1, maxBlock);
        key.assign ((size_t) block, 0.0f);
        dry.assign ((size_t) block, 0.0f);

        keyCut.prepare (fs, 1);
        EqLink::Settings cut;
        cut.hpfOn    = true;
        cut.hpfHz    = keyCutHz;
        cut.hpfSlope = keyCutSlope;
        keyCut.setSettings (cut);

        ready = comp.prepare (fs, block, 1, 0.0);
        reset();
        return ready;
    }

    void reset() noexcept
    {
        comp.reset();
        keyCut.reset();
        heldDb  = 0.0;
        running = false;
        grDb.store (0.0f, std::memory_order_relaxed);
    }

    /** One block of the mono chain, in place. Amount 0 touches nothing: the block is the input. */
    void process (float* x, int n, double amount, Attack attack, Release release, Character character) noexcept
    {
        if (! (amount > 0.0) || ! ready || x == nullptr || n <= 0 || n > block)
        {
            if (running)
                reset();   // the next time it is turned up it starts from no squeeze, not from a held one
            return;
        }

        running = true;
        const auto law = lawAt (amount);

        // The release: AUTO follows how long the squeeze has been held — a one-pole over ~0.5 s of the
        // reduction, 6 dB of it held reaching the slow end.
        double releaseMs = release == Release::fast ? releaseFastMs : releaseSlowMs;
        if (release == Release::automatic)
        {
            const double held = std::clamp (heldDb / 6.0, 0.0, 1.0);
            releaseMs = releaseFastMs + (releaseSlowMs - releaseFastMs) * held;
        }

        felitronics::dynamics::CompressorParams p;
        p.thresholdDb = law.thresholdDb;
        p.ratio       = law.ratio;
        p.kneeDb      = kneeDb;
        p.attackMs    = attackMsOf (attack);
        p.releaseMs   = releaseMs;
        p.detector    = character == Character::smooth ? felitronics::dynamics::Detector::Rms
                                                       : felitronics::dynamics::Detector::Peak;
        p.rmsWindowMs = 10.0;
        p.autoMakeup  = true;
        p.lookaheadMs = 0.0;
        if (! same (p, set))
        {
            comp.setParams (p);
            set = p;
        }

        // The key: the same signal, its lowest strings' fundamentals cut away.
        std::copy (x, x + n, key.data());
        float* keyCh[] = { key.data() };
        keyCut.process (keyCh, 1, n);

        if (law.uncompressed > 0.0)
            std::copy (x, x + n, dry.data());

        float* io[] = { x };
        const float* keyIn[] = { key.data() };
        (void) comp.process (io, 1, n, keyIn, 1);

        if (law.uncompressed > 0.0)
        {
            const auto wet = (float) (1.0 - law.uncompressed), par = (float) law.uncompressed;
            for (int i = 0; i < n; ++i)
                x[i] = wet * x[i] + par * dry[(size_t) i];
        }

        const double gr = -comp.gainReductionDb();   // positive dB of squeeze
        const double k  = 1.0 - std::exp (-(double) n / (0.5 * fs));
        heldDb += k * (std::max (0.0, gr) - heldDb);
        grDb.store ((float) comp.gainReductionDb(), std::memory_order_relaxed);
    }

    /** The squeeze now, in dB (0 or below) — for the indicator beside the knob. */
    std::atomic<float> grDb { 0.0f };

private:
    static bool same (const felitronics::dynamics::CompressorParams& a,
                      const felitronics::dynamics::CompressorParams& b) noexcept
    {
        const auto near = [] (double x, double y, double within) { return std::abs (x - y) < within; };
        return near (a.thresholdDb, b.thresholdDb, 1.0e-9) && near (a.ratio, b.ratio, 1.0e-9)
            && near (a.attackMs, b.attackMs, 1.0e-9) && near (a.releaseMs, b.releaseMs, 1.0) && a.detector == b.detector;
    }

    felitronics::dynamics::Compressor       comp;
    felitronics::dynamics::CompressorParams set { [] { felitronics::dynamics::CompressorParams p; p.ratio = 0.0; return p; }() };
    EqLink             keyCut;
    std::vector<float> key, dry;
    double fs = 48000.0, heldDb = 0.0;
    int    block = 0;
    bool   ready = false, running = false;
};

} // namespace orbitamp::core
