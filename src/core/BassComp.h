// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassCuts.h"
#include "BypassFade.h"
#include "CompTap.h"

#include <felitronics/dynamics/Compressor.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** OrbitBass's COMP: felitronics-core's compressor, EACH CONTROL ONE JOB (the owner, 31.10).

      - THE THRESHOLD, dB (-48..0), its own: the line on the picture is where the detector works, in either character.
      - THE RATIO, 2:1..10:1, its own.
      - THE KNOB IS BLEND — parallel compression: 0 is no compressor at all (once glided there nothing is computed and
        the signal passes bit for bit), 1 all of the signal compressed, between the compressed signal over the clean one,
        sample-aligned (no lookahead, no latency).
      - MAKE-UP is automatic: core's static make-up from the threshold and the ratio (half what the curve takes from 0
        dBFS), gliding as the squeeze does — up at the attack's pace, down at the release's.
      - Every move GLIDES: the blend over 20 ms for its whole travel, the threshold over 20 ms for its 48 dB, the ratio
        over 20 ms for 2:1..10:1 — a drag, a recall or automation is never a step.
      - CHARACTER chooses the detector: SMOOTH hears the level's RMS over 10 ms, PUNCHY its peaks. (The old law raised
        PUNCHY's threshold 4 dB so the two played about as loud; with a threshold of its own the line says where it works,
        and PUNCHY at the same line squeezes harder — the player sets the line.)

    STANDBY (the strip's arrow) is the house's crossfade (BypassFade, 15 ms), handed in per block. While the link stands
    by with BLEND up the compressor is still FED, so switching it in fades to a compressor already settled. Only with
    BLEND at zero does it stop, its memory cleared then. A BLEND recalled up from zero starts a FRESH compressor, fed and
    not heard for three of its attacks and `warmMs`, then faded in. A PREPARE is not a recall: a chain that arrives with
    BLEND up plays it from its first sample (snapAmount).

    The detector hears the bass through a 30 Hz high-pass, 12 dB/oct (the key): the lowest fundamentals do not pump the
    rest. ATTACK slow 30 / MED 15 / fast 5 ms; RELEASE AUTO — 80 ms after a short peak growing toward 400 ms as the squeeze
    is held — or FAST 80 / SLOW 400 ms. ITS TIME IS ITS OWN: the controls and the release reach the compressor on a fixed
    cadence counted in samples from the prepare — the same playing gives the same samples whatever the host's blocks.

    Mono, as the bass's chain is. Real-time safe: prepare() allocates, process() does not. */
class BassComp
{
public:
    enum class Attack    { slow, med, fast };
    enum class Release   { automatic, fast, slow };
    enum class Character { smooth, punchy };

    static constexpr double kneeDb            = 8.0;
    static constexpr double attackSlowMs      = 30.0,  attackMedMs = 15.0, attackFastMs = 5.0;
    static constexpr double releaseFastMs     = 80.0,  releaseSlowMs = 400.0;
    static constexpr double amountGlideMs     = 20.0;   // the knob's whole travel
    static constexpr double holdMs            = 500.0;  // AUTO's memory of the squeeze
    static constexpr double holdFullDb        = 6.0;    // ...held this deep, the release is at its slowest
    static constexpr double cadenceMs         = 1.0;    // how often the knob and the release reach the compressor
    static constexpr double warmMs            = 30.0;   // a fresh start is fed, not heard, three attacks and this long...
    static constexpr double warmFadeMs        = 15.0;   // ...then fades in over BypassFade's length


    /** The key's high-pass: this one, or another for a comparison — the first version's was 40 Hz, two
        sections (24 dB/oct). */
    struct KeyCut
    {
        double hz       = 30.0;
        int    sections = 1;    // 12 dB/oct each
    };


    /** EACH CONTROL ONE JOB (the owner, 31.10): the THRESHOLD is the threshold alone, in dB; the RATIO the ratio alone;
        the knob is BLEND — parallel compression, 0 none at all, 1 all of the signal compressed. Make-up is automatic,
        from the threshold and the ratio. */
    static constexpr double thresholdMinDb = -48.0, thresholdMaxDb = 0.0, defaultThresholdDb = -24.0;
    static constexpr double ratioMin = 2.0, ratioMax = 10.0, defaultRatio = 4.0;

    static double attackMsOf (Attack a) noexcept
    {
        return a == Attack::slow ? attackSlowMs : a == Attack::fast ? attackFastMs : attackMedMs;
    }

    [[nodiscard]] bool prepare (double sampleRate, int maxBlock) { return prepare (sampleRate, maxBlock, KeyCut {}); }

    [[nodiscard]] bool prepare (double sampleRate, int maxBlock, KeyCut cut)
    {
        fs        = sampleRate > 0.0 ? sampleRate : 48000.0;
        block     = std::max (1, maxBlock);
        cadence   = std::max (1, (int) std::lround (fs * cadenceMs * 0.001));
        glideStep = 1.0 / (amountGlideMs * 0.001 * fs);
        ratioStep = (ratioMax - ratioMin) * (double) cadence * glideStep;                      // 2:1 to 10:1 in 20 ms
        thrStep   = (thresholdMaxDb - thresholdMinDb) * (double) cadence * glideStep;          // the whole travel in 20 ms
        warmSamples = (3.0 * attackMedMs + warmMs) * 0.001 * fs;   // the attack's own, from the first apply
        warmFade    = warmFadeMs * 0.001 * fs;
        holdK     = 1.0 - std::exp (-1.0 / (holdMs * 0.001 * fs));
        key.assign ((size_t) block, 0.0f);
        dry.assign ((size_t) block, 0.0f);
        work.assign ((size_t) block, 0.0f);
        gr.assign ((size_t) block, 0.0f);
        shown.assign ((size_t) block, 0.0f);
        path.assign ((size_t) block, 0.0f);

        keySections = std::clamp (cut.sections, 1, (int) keyCut.size());
        for (auto& b : keyCut)
            b.design (true, cut.hz, fs);

        ready = comp.prepare (fs, block, 1, 0.0);
        reset();
        return ready;
    }

    /** Silence: no squeeze held, the cadence started again. */
    void reset() noexcept
    {
        comp.reset();
        for (auto& b : keyCut)
            b.reset();
        heldDb      = 0.0;
        clock       = 0;
        sinceStart  = 0;
        makeupNowDb = 0.0;
        snapMakeup  = false;
        running     = false;
        paramsValid = false;
        grDb.store (0.0f, std::memory_order_relaxed);
    }

    /** The knob lands at `amount` without a glide — a prepare's first block. */
    void snapAmount (double amount) noexcept
    {
        amt        = std::clamp (amount, 0.0, 1.0);
        snapMakeup = amt > 0.0;
        if (amt > 0.0)
            sinceStart = 1LL << 40;   // past the warm: what a prepare finds up plays at once
    }

    /** One block of the mono chain, in place. `link` is the strip's STANDBY crossfade for this block; `heldRatio` the
        ratio the hand holds, or 0 or below for the law's. */
    void process (float* x, int n, double amountTarget, const BypassFade::Span& link,
                  Attack attack, Release release, Character character,
                  double thresholdDb = defaultThresholdDb, double ratio = defaultRatio) noexcept
    {
        const double target = std::clamp (amountTarget, 0.0, 1.0);
        thrWant   = std::isfinite (thresholdDb) ? std::clamp (thresholdDb, thresholdMinDb, thresholdMaxDb) : thrWant;
        ratioWant = std::isfinite (ratio) ? std::clamp (ratio, ratioMin, ratioMax) : ratioWant;

        // The knob at zero and glided there: nothing at all — and the memory cleared, now that nothing of it
        // is in the sound.
        if (! (target > 0.0) && ! (amt > 0.0))
        {
            if (running)
                reset();
            grDb.store (0.0f, std::memory_order_relaxed);
            if (tap != nullptr && x != nullptr && n > 0)
                tap->write (x, x, nullptr, n, character == Character::smooth);   // the signal through, untouched
            return;
        }
        if (! ready || x == nullptr || n <= 0 || n > block)
            return;

        running = true;
        const bool heard = link.moving || link.to > 0.0f;   // standing by and settled: fed, not heard

        std::copy (x, x + n, dry.data());
        std::copy (x, x + n, work.data());

        const int   ramp     = std::clamp (link.ramp, 1, n);
        const float linkStep = (link.to - link.from) / (float) ramp;

        for (int i = 0; i < n;)
        {
            // On the cadence, counted from the prepare: the knob as it has glided and the release as AUTO reads
            // the hold, handed to the compressor.
            const int phase = (int) (clock % (long long) cadence);
            if (phase == 0)
                apply (attack, release, character);

            const int len = std::min (n - i, cadence - phase);

            for (int k = i; k < i + len; ++k)
            {
                float v = work[(size_t) k];
                for (int s = 0; s < keySections; ++s)
                    v = keyCut[(size_t) s].process (v);
                key[(size_t) k] = v;
            }

            float* io[] = { work.data() + i };
            const float* keyIn[] = { key.data() + i };
            (void) comp.process (io, 1, len, keyIn, 1, felitronics::dynamics::GainReductionTap { gr.data() + i, len });

            for (int k = i; k < i + len; ++k)
            {
                heldDb += holdK * (std::max (0.0, -(double) gr[(size_t) k]) - heldDb);
                amt = target > amt ? std::min (target, amt + glideStep) : std::max (target, amt - glideStep);

                makeupNowDb += (makeupTargetDb - makeupNowDb) * (makeupTargetDb > makeupNowDb ? makeupUpK : makeupDownK);
                const float squeezed = work[(size_t) k];   // after the squeeze, before the make-up: the picture's
                work[(size_t) k] *= (float) std::exp (makeupNowDb * 0.11512925464970228);   // ln 10 / 20

                if (! heard)
                {
                    shown[(size_t) k] = path[(size_t) k] = dry[(size_t) k];   // standing by: nothing of it, in the sound or the picture
                    continue;
                }

                // BLEND — parallel compression: the compressed signal over the clean one, sample-aligned (no lookahead),
                // in the share the knob has glided to; continuous from its zero by construction. A JUMP to zero is the
                // link's own fade's (the processor fades the link toward "on AND up"), out in 15 ms, before this glide
                // lands and the stage stops.
                const double fresh = std::clamp (((double) (sinceStart + (k - i)) - warmSamples) / warmFade, 0.0, 1.0);
                const double blend = amt * fresh;
                const float  g     = link.moving ? (k < ramp ? link.from + linkStep * (float) k : link.to) : link.to;
                const float  d     = dry[(size_t) k];
                const float  wet   = work[(size_t) k];
                x[k] = d + (wet - d) * (float) (blend * (double) g);
                // THE PICTURE'S OUT: the same blend of the squeezed signal BEFORE the make-up — so the gap under IN is what
                // COMP took (the owner, 31.10: no make-up number anywhere). The sound is x, unchanged.
                shown[(size_t) k] = d + (squeezed - d) * (float) (blend * (double) g);
                path[(size_t) k]  = squeezed;   // AFTER COMP itself: under the input by exactly the squeeze
            }

            i          += len;
            clock      += len;
            sinceStart += len;
        }

        // The indicator shows the squeeze that is heard: none while the link stands by, fed or not.
        grDb.store (heard ? gr[(size_t) n - 1] : 0.0f, std::memory_order_relaxed);
        if (tap != nullptr)
            tap->write (heard ? key.data() : dry.data(), heard ? path.data() : dry.data(), shown.data(), heard ? gr.data() : nullptr, n,
                        character == Character::smooth);   // IN the detector's; standing by, the signal as all three
    }

    /** Where the picture's columns go, while it is on screen — null otherwise (the processor sets it per block). */
    CompTap* tap = nullptr;

    /** The ratio the compressor works at now — the law's, or gliding to or holding the hand's (a gate reads it). */
    double ratioNow() const noexcept { return ratioAt; }

    /** The squeeze now, in dB (0 or below) — for the indicator beside the knob. */
    std::atomic<float> grDb { 0.0f };

    /** Where the knob has glided to — for a gate. */
    double amountNow() const noexcept { return amt; }

private:
    void apply (Attack attack, Release release, Character character) noexcept
    {
        // The threshold and the ratio, each its own control, GLIDING to where they are asked — a drag or a recall is a
        // glide, never a step; a fresh compressor starts on them.
        if (! paramsValid)
        {
            thrAt   = thrWant;
            ratioAt = ratioWant;
        }
        else
        {
            thrAt   = thrWant > thrAt ? std::min (thrWant, thrAt + thrStep) : std::max (thrWant, thrAt - thrStep);
            ratioAt = ratioWant > ratioAt ? std::min (ratioWant, ratioAt + ratioStep) : std::max (ratioWant, ratioAt - ratioStep);
        }

        double releaseMs = release == Release::fast ? releaseFastMs : releaseSlowMs;
        if (release == Release::automatic)
            releaseMs = releaseFastMs + (releaseSlowMs - releaseFastMs) * std::clamp (heldDb / holdFullDb, 0.0, 1.0);

        felitronics::dynamics::CompressorParams p;
        p.thresholdDb = thrAt;   // the line is where the detector works, in either character (no offset any more)

        // The make-up's number — core's static auto make-up, half what the curve takes from 0 dBFS, which lies past
        // the knee for every threshold the law reaches — and the paces it glides at (see the class comment).
        makeupTargetDb = 0.5 * (-p.thresholdDb) * (1.0 - 1.0 / ratioAt);
        makeupUpK      = 1.0 - std::exp (-1.0 / (attackMsOf (attack) * 0.001 * fs));
        makeupDownK    = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * fs));
        warmSamples    = (3.0 * attackMsOf (attack) + warmMs) * 0.001 * fs;
        if (snapMakeup)
        {
            makeupNowDb = makeupTargetDb;
            snapMakeup  = false;
        }
        p.ratio       = ratioAt;
        p.kneeDb      = kneeDb;
        p.attackMs    = attackMsOf (attack);
        p.releaseMs   = releaseMs;
        p.detector    = character == Character::smooth ? felitronics::dynamics::Detector::Rms
                                                       : felitronics::dynamics::Detector::Peak;
        p.rmsWindowMs = 10.0;
        p.autoMakeup  = false;   // the make-up is ours (above)
        p.makeupDb    = 0.0;
        p.lookaheadMs = 0.0;

        if (! paramsValid || ! same (p, set))
        {
            comp.setParams (p);
            set = p;
            paramsValid = true;
        }
    }

    static bool same (const felitronics::dynamics::CompressorParams& a,
                      const felitronics::dynamics::CompressorParams& b) noexcept
    {
        const auto near = [] (double u, double v, double within) { return std::abs (u - v) < within; };
        return near (a.thresholdDb, b.thresholdDb, 1.0e-9) && near (a.ratio, b.ratio, 1.0e-9)
            && near (a.attackMs, b.attackMs, 1.0e-9) && near (a.releaseMs, b.releaseMs, 1.0) && a.detector == b.detector;
    }

    felitronics::dynamics::Compressor       comp;
    felitronics::dynamics::CompressorParams set;
    std::array<CutBiquad, 2> keyCut;
    int    keySections = 1;
    std::vector<float> key, dry, work, gr, shown, path;
    double fs = 48000.0, heldDb = 0.0, amt = 0.0, glideStep = 0.001, holdK = 0.001;
    double thrWant = defaultThresholdDb, thrAt = defaultThresholdDb, thrStep = 0.05;
    double ratioWant = defaultRatio, ratioAt = defaultRatio, ratioStep = 0.4;
    long long clock = 0, sinceStart = 0;
    double warmSamples = 960.0, warmFade = 720.0;
    double makeupNowDb = 0.0, makeupTargetDb = 0.0, makeupUpK = 0.001, makeupDownK = 0.001;
    bool   snapMakeup = false;
    int    block = 0, cadence = 48;
    bool   ready = false, running = false, paramsValid = false;
};

} // namespace orbitamp::core
