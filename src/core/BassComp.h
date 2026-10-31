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

/** OrbitBass's COMP: felitronics-core's compressor under ONE KNOB, the law agreed with the owner.

    The knob, 0..1:
      - 0 is no compressor — once the knob has glided there, nothing is computed and the signal passes bit
        for bit;
      - up the knob the threshold falls from -12 to -36 dB and the ratio rises from 2:1 to 6:1, over a soft
        knee of 8 dB, with core's static auto make-up;
      - over the knob's FIRST 5 % the compressor is crossfaded in against the signal, so the knob is
        continuous from its zero: 0+ is not already the make-up's +3 dB;
      - up the knob a share of the signal passes UNCOMPRESSED beside it, parallel — a quarter at the top —
        so a note's attack survives the squeeze;
      - the knob GLIDES (20 ms for its whole travel): a recall, a register or automation moves the threshold,
        the ratio and the parallel share as a hand would, never as a step;
      - no lookahead: no latency, and the uncompressed share lines up with the compressed one sample for sample.

    STANDBY (the strip's arrow) is the house's crossfade (BypassFade, 15 ms), handed in per block. While the
    link stands by with the knob up the compressor is still FED — its detector and its make-up stay where the
    playing has put them — so switching it in fades to a compressor already settled, not to a fresh one that
    lets the first 15 ms through at the whole make-up. Only with the knob at zero does it stop, and its memory
    is cleared then, when nothing of it is in the sound. A knob recalled up from zero starts a FRESH
    compressor, which has heard nothing yet: it is fed and not heard for three of its attacks and `warmMs` — the
    detector's window, the squeeze and the make-up catching up with the playing — and only then faded in. A
    PREPARE is not a recall: a chain that arrives with COMP up plays it from its first sample (snapAmount).

    ITS MAKE-UP IS ITS OWN, not core's: the static auto make-up's number (half what the curve takes from 0 dBFS),
    gliding toward it as the squeeze does — up at the attack's pace, down at the release's — so a knob turned
    down mid-note does not drop the make-up in 30 ms while the squeeze lets go over 400, a duck of the whole
    note for half a second; and a fresh start's make-up rises with its squeeze, not ahead of it.

    The detector hears the bass through a 30 Hz high-pass, 12 dB/oct (the key; core gives the input, the
    product the filter): the lowest fundamentals do not pump the rest. It was 40 Hz at 24 dB/oct, and the
    harder it squeezed the louder the low B came out against the strings above it — a key deaf to a note is a
    compressor that leaves that note alone.

    The menu: ATTACK slow 30 / MED 15 / fast 5 ms; RELEASE AUTO — program-dependent, 80 ms after a short peak
    and growing toward 400 ms as the squeeze is held, the smooth, optical way — or FAST 80 / SLOW 400 ms;
    CHARACTER SMOOTH hears the level's average (RMS over 10 ms), PUNCHY its peaks, its threshold
    `punchyOffsetDb` higher, so the two play about as loud on a bass.

    ITS TIME IS ITS OWN, NOT THE HOST'S BLOCKS: the knob and the release reach the compressor on a fixed
    cadence counted in samples from the prepare, and the hold behind AUTO is read sample by sample from the
    compressor's own gain-reduction tap — the same playing gives the same samples whether a host hands 32
    samples a block or 4096.

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
    static constexpr double knobFadeTop       = 0.05;   // the knob's share the compressor fades in over
    static constexpr double amountGlideMs     = 20.0;   // the knob's whole travel
    static constexpr double holdMs            = 500.0;  // AUTO's memory of the squeeze
    static constexpr double holdFullDb        = 6.0;    // ...held this deep, the release is at its slowest
    static constexpr double cadenceMs         = 1.0;    // how often the knob and the release reach the compressor
    static constexpr double warmMs            = 30.0;   // a fresh start is fed, not heard, three attacks and this long...
    static constexpr double warmFadeMs        = 15.0;   // ...then fades in over BypassFade's length

    /** PUNCHY's threshold over SMOOTH's. A peak detector reads a sine 3 dB over its 10 ms RMS and a plucked
        bass more — a real DI 8 to 12 dB (reviewed 31.10) — so PUNCHY, at SMOOTH's threshold, squeezed the
        same playing harder and played it quieter. The offset is measured on the gate's plucked bass line,
        whose attack puts its peaks about as far over its RMS as a finger on a DI (tests/OrbitBassTests.cpp
        prints both); the owner sets the rest by ear. */
    static constexpr double punchyOffsetDb = 4.0;

    /** The key's high-pass: this one, or another for a comparison — the first version's was 40 Hz, two
        sections (24 dB/oct). */
    struct KeyCut
    {
        double hz       = 30.0;
        int    sections = 1;    // 12 dB/oct each
    };

    /** Where the knob puts the curve. */
    struct Law
    {
        double thresholdDb;
        double ratio;
        double uncompressed;   // the parallel share, 0..uncompressedAtTop
    };

    /** THE THRESHOLD LINE AND THE SLIDER ARE ONE CONTROL (the COMP block): where the knob puts the threshold the detector
        works at — the law's, PUNCHY's offset over it — and back, the knob whose threshold that is, clamped to the
        knob's travel. */
    static double thresholdAt (double amount, Character c) noexcept
    {
        return lawAt (amount).thresholdDb + (c == Character::punchy ? punchyOffsetDb : 0.0);
    }

    static double amountForThreshold (double db, Character c) noexcept
    {
        const double law = db - (c == Character::punchy ? punchyOffsetDb : 0.0);
        return std::clamp ((law - thresholdTopDb) / (thresholdBottomDb - thresholdTopDb), 0.0, 1.0);
    }

    /** A RATIO THE HAND HOLDS (params::compRatio, dragged in the block's transfer square), within these; the knob's
        threshold, its parallel share and its fade stay the law's. Taken and given back by GLIDING, as the knob does —
        twice the law's own pace, 10 ms for 2:1 to 6:1 — never by a step; following the law the ratio IS the law's,
        sample for sample. */
    static constexpr double heldRatioMin = 2.0, heldRatioMax = 10.0;

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

    [[nodiscard]] bool prepare (double sampleRate, int maxBlock) { return prepare (sampleRate, maxBlock, KeyCut {}); }

    [[nodiscard]] bool prepare (double sampleRate, int maxBlock, KeyCut cut)
    {
        fs        = sampleRate > 0.0 ? sampleRate : 48000.0;
        block     = std::max (1, maxBlock);
        cadence   = std::max (1, (int) std::lround (fs * cadenceMs * 0.001));
        glideStep = 1.0 / (amountGlideMs * 0.001 * fs);
        ratioStep = 2.0 * (ratioHigh - ratioLow) * (double) cadence * glideStep;   // faster than the law can move it
        warmSamples = (3.0 * attackMedMs + warmMs) * 0.001 * fs;   // the attack's own, from the first apply
        warmFade    = warmFadeMs * 0.001 * fs;
        holdK     = 1.0 - std::exp (-1.0 / (holdMs * 0.001 * fs));
        key.assign ((size_t) block, 0.0f);
        dry.assign ((size_t) block, 0.0f);
        work.assign ((size_t) block, 0.0f);
        gr.assign ((size_t) block, 0.0f);

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
        parallelNow = 0.0;
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
                  Attack attack, Release release, Character character, double heldRatio = 0.0) noexcept
    {
        const double target = std::clamp (amountTarget, 0.0, 1.0);
        ratioHeld = heldRatio > 0.0 ? std::clamp (heldRatio, heldRatioMin, heldRatioMax) : 0.0;

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
                work[(size_t) k] *= (float) std::exp (makeupNowDb * 0.11512925464970228);   // ln 10 / 20

                if (! heard)
                    continue;

                // The knob's first 5 % is a crossfade of the compressor against the signal, so the knob is continuous
                // from its zero. A JUMP to zero is the link's own fade's (the processor fades the link toward
                // "on AND up"), which takes it out in 15 ms, before this glide lands and the stage stops.
                // The parallel share follows the knob at the make-up's paces too: down as the squeeze lets go, or a knob
                // turned down would take the uncompressed attack away before the compressed note came back up.
                const double pWant = uncompressedAtTop * amt;
                parallelNow += (pWant - parallelNow) * (pWant > parallelNow ? makeupUpK : makeupDownK);
                const double p     = parallelNow;
                const double fresh = std::clamp (((double) (sinceStart + (k - i)) - warmSamples) / warmFade, 0.0, 1.0);
                const double knob  = std::min (1.0, amt / knobFadeTop) * fresh;
                const float  g    = link.moving ? (k < ramp ? link.from + linkStep * (float) k : link.to) : link.to;
                const float  d    = dry[(size_t) k];
                const float  wet  = (float) ((1.0 - p) * (double) work[(size_t) k] + p * (double) d);
                x[k] = d + (wet - d) * (float) (knob * (double) g);
            }

            i          += len;
            clock      += len;
            sinceStart += len;
        }

        // The indicator shows the squeeze that is heard: none while the link stands by, fed or not.
        grDb.store (heard ? gr[(size_t) n - 1] : 0.0f, std::memory_order_relaxed);
        if (tap != nullptr)
            tap->write (key.data(), x, heard ? gr.data() : nullptr, n, character == Character::smooth);   // what the detector hears
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
        const auto law = lawAt (amt);

        // The ratio: the law's, or the hand's, gliding between them; a fresh compressor starts on its own.
        const double ratioWant = ratioHeld > 0.0 ? ratioHeld : law.ratio;
        if (! paramsValid)
            ratioAt = ratioWant;
        else
            ratioAt = ratioWant > ratioAt ? std::min (ratioWant, ratioAt + ratioStep) : std::max (ratioWant, ratioAt - ratioStep);

        double releaseMs = release == Release::fast ? releaseFastMs : releaseSlowMs;
        if (release == Release::automatic)
            releaseMs = releaseFastMs + (releaseSlowMs - releaseFastMs) * std::clamp (heldDb / holdFullDb, 0.0, 1.0);

        felitronics::dynamics::CompressorParams p;
        p.thresholdDb = law.thresholdDb + (character == Character::punchy ? punchyOffsetDb : 0.0);

        // The make-up's number — core's static auto make-up, half what the curve takes from 0 dBFS, which lies past
        // the knee for every threshold the law reaches — and the paces it glides at (see the class comment).
        makeupTargetDb = 0.5 * (-p.thresholdDb) * (1.0 - 1.0 / ratioAt);
        makeupUpK      = 1.0 - std::exp (-1.0 / (attackMsOf (attack) * 0.001 * fs));
        makeupDownK    = 1.0 - std::exp (-1.0 / (releaseMs * 0.001 * fs));
        warmSamples    = (3.0 * attackMsOf (attack) + warmMs) * 0.001 * fs;
        if (snapMakeup)
        {
            makeupNowDb = makeupTargetDb;
            parallelNow = law.uncompressed;
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
    std::vector<float> key, dry, work, gr;
    double fs = 48000.0, heldDb = 0.0, amt = 0.0, glideStep = 0.001, holdK = 0.001;
    double ratioHeld = 0.0, ratioAt = ratioLow, ratioStep = 0.4;
    long long clock = 0, sinceStart = 0;
    double warmSamples = 960.0, warmFade = 720.0;
    double makeupNowDb = 0.0, makeupTargetDb = 0.0, makeupUpK = 0.001, makeupDownK = 0.001, parallelNow = 0.0;
    bool   snapMakeup = false;
    int    block = 0, cadence = 48;
    bool   ready = false, running = false, paramsValid = false;
};

} // namespace orbitamp::core
