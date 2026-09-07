// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/eq/Svf.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** The reverb tail — the last of our own layers before the signal leaves for the speaker.

    Built on juce::Reverb (Freeverb), which is simply the right tool here: the plugin is a JUCE
    application and has no reason to avoid it. The only consequence worth recording is placement —
    felitronics-core stays JUCE-free (for possible embedded / web targets later), so this stage
    lives in the product, not in core.

    The design calls for Mix as the hero, the character as the title, and everything else as a
    late refinement: DECAY scales the chosen character's tail rather than replacing it, PREDELAY
    holds the tail back so the attack stays dry, and the HPF cleans the WET only — a low tail is
    mud, and every layer after this one can only pass it on.

    The dry path is the amp's law: an amp's reverb control ADDS the tank return to the dry signal —
    it does not crossfade away from it. Dry stays at unity at every setting; the whole wet chain
    (reverb, modulation, predelay, HPF) runs on a scratch copy and is added on top.

    Two of the characters are more than presets: AMBIENCE is the room you don't hear as an
    effect; MODULATED wears a slow chorus on its tail (the Lexicon dress).

    Real-time rule: process() never allocates, locks, or throws — every buffer is sized in
    prepare(). */
class ReverbStage
{
public:
    enum class Character { ambience, room, hall, plate, spring, modulated };

    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        sampleRate = newSampleRate;
        reverb.setSampleRate (sampleRate);
        reverb.reset();

        for (auto& b : wet)
            b.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);
        for (auto& b : wetOut)
            b.assign ((size_t) juce::jmax (1, maxBlockSize), 0.0f);

        const auto preMax = (size_t) std::ceil (0.1 * sampleRate) + 8;   // 100 ms of predelay
        const auto modMax = (size_t) std::ceil (0.02 * sampleRate) + 8;  // 12 ms base + depth

        for (size_t ch = 0; ch < 2; ++ch)
        {
            preLine[ch].assign (preMax, 0.0f);
            modLine[ch].assign (modMax, 0.0f);
        }

        prePos = modPos = 0;
        lfoPhase = 0.0f;

        hpf.prepare (sampleRate, 2);
        hpf.reset();
        applyHpf();
        apply();
    }

    /** Idempotent on purpose. The chain calls this every block a room is out of the rig, and the
        clear underneath is eight combs, four allpasses and two delay lines — about a hundred and
        fifty kilobytes. At sixty-four samples that is a hundred and twenty megabytes a second of
        memset for a block nobody is listening to, and it is not even timed, because the row it
        belongs to is not in the cost list. The delay next door already guarded itself. */
    void reset() noexcept
    {
        if (cleared)
            return;

        cleared = true;
        reverb.reset();
        hpf.reset();
        for (size_t ch = 0; ch < 2; ++ch)
        {
            std::fill (preLine[ch].begin(), preLine[ch].end(), 0.0f);
            std::fill (modLine[ch].begin(), modLine[ch].end(), 0.0f);
        }
    }

    void setCharacter (Character c) noexcept
    {
        if (c == character)
            return;

        character = c;
        apply();
    }

    /** 0 = fully dry, 1 = the tail added at unity. Dry never moves. */
    void setMix (float newMix) noexcept { mix = juce::jlimit (0.0f, 1.0f, newMix); }

    /** Scales the character's decay, 0.5..2 — the character stays the voice, this is its breath. */
    void setDecay (float scale) noexcept
    {
        const float s = juce::jlimit (0.5f, 2.0f, scale);
        if (! juce::approximatelyEqual (s, decayScale))
        {
            decayScale = s;
            apply();
        }
    }

    /** What the stage ADDED last block, per channel — for the picture that shows the pair: the
        door and the tail, the cab grammar on the frequency axis. Message-thread read of an
        audio-thread buffer is the taps' business; this just hands the pointer. */
    const float* addedWet (int ch) const noexcept
    {
        return wetOut[(size_t) juce::jlimit (0, 1, ch)].data();
    }

    /** How long the attack stays dry before the tail arrives, 0..100 ms. */
    void setPredelayMs (float ms) noexcept
    {
        const float want = juce::jlimit (0.0f, 100.0f, ms);

        // Guarded because `processBlock` sets this every block and `refreshTail` takes a
        // logarithm. `apply()` needs no such check: its own two setters already have one.
        if (juce::approximatelyEqual (want, predelayMs))
            return;

        predelayMs = want;
        refreshTail();
    }

    /** HOW LONG THE ROOM GOES ON SOUNDING after the last note went in, in seconds — what the
        plugin has to be able to tell a host that is rendering offline.

        Freeverb is a bank of combs, and the longest of them is 1617 samples of the 44.1 kHz it was
        written for — 36.7 ms, whatever this session runs at, because JUCE scales the tuning with
        the rate. Each pass round loses `roomSize × 0.28 + 0.7` of itself, so reaching a thousandth
        takes that comb's length times `ln(0.001) / ln(g)`. A HALL at DECAY ×2 clamps the room to
        0.98, which is 0.974 per pass and a shade under ten seconds — longer than the flat eight
        this used to be declared as.

        The damping in each comb's loop takes the top off faster than this, so the estimate is long
        rather than short, which is the side to be wrong on. */
    float tailSeconds() const noexcept { return tailSec.load (std::memory_order_relaxed); }

    /** The tail's own high-pass — the WET only, always in: a low tail is mud in any speaker, and
        at the 40 Hz floor the filter is as good as air. */
    void setHpfHz (float hz) noexcept
    {
        if (! juce::approximatelyEqual (hz, hpfHz))
        {
            hpfHz = hz;
            applyHpf();
        }
    }

    /** `feed` false is the INSERT'S BYPASS: nothing new enters the wet chain, so the room that is
        already ringing decays into the dry rather than being cut. The dry is untouched either way —
        it always was; this stage only ever ADDS. */
    void process (float* const* channels, int numChannels, int numSamples, bool feed = true) noexcept
    {
        if (numChannels < 1 || numSamples <= 0)
            return;

        const int nch = juce::jmin (2, numChannels);
        const int n   = juce::jmin (numSamples, (int) wet[0].size());

        cleared = false;   // there is state in here again; the next reset() has work to do

        // The wet copy: the whole wet chain runs on it, and the dry never enters.
        for (int ch = 0; ch < nch; ++ch)
        {
            float* w = wet[(size_t) ch].data();
            const float* d = channels[ch];

            for (int i = 0; i < n; ++i)
                w[i] = feed ? d[i] : 0.0f;
        }

        // The room itself, 100% wet (the constants undo Freeverb's internal ×3 on wet).
        if (nch >= 2)
            reverb.processStereo (wet[0].data(), wet[1].data(), n);
        else
            reverb.processMono (wet[0].data(), n);

        // The slow chorus on the tail — a modulated delay, the Lexicon dress.
        if (character == Character::modulated)
            modulateTail (nch, n);

        // Predelay, then the tail's own high-pass, then ADD at the mix — dry stays unity.
        const int preSamples = (int) ((double) predelayMs * 0.001 * sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const int preN = (int) preLine[0].size();
            const int rd   = (prePos - preSamples + preN) % preN;

            for (int ch = 0; ch < nch; ++ch)
            {
                float* w = wet[(size_t) ch].data();
                preLine[(size_t) ch][(size_t) prePos] = w[i];
                float s = preLine[(size_t) ch][(size_t) rd];

                s = hpf.processSample (ch, s);

                const float added = s * mix;
                wetOut[(size_t) ch][(size_t) i] = added;
                channels[ch][i] += added;
            }

            prePos = (prePos + 1) % (int) preLine[0].size();
        }
    }

private:
    /** The room the character asks for, as DECAY leaves it — the one place this is worked out. */
    float roomSizeNow() const noexcept
    {
        float room = 0.35f;

        switch (character)
        {
            // The room you don't hear as an effect: tiny, dark, gone before the next note.
            case Character::ambience:  room = 0.15f; break;
            case Character::room:      room = 0.35f; break;
            case Character::hall:      room = 0.85f; break;
            case Character::plate:     room = 0.60f; break;
            case Character::spring:    room = 0.25f; break;
            // The dressed tail: a big smooth room the chorus will ride.
            case Character::modulated: room = 0.70f; break;
        }

        // DECAY breathes through the room size: half-to-double maps to a quarter of the scale
        // either way, clamped clear of runaway.
        return juce::jlimit (0.05f, 0.98f, room + std::log2 (decayScale) * 0.25f);
    }

    void refreshTail() noexcept
    {
        const double g    = (double) roomSizeNow() * 0.28 + 0.7;   // juce::Reverb's own room scaling
        // Its longest comb, in seconds — the RIGHT channel's, which JUCE spreads 23 samples past
        // the left one's 1617. Rate-independent: the tunings are scaled by the sample rate.
        const double comb = (1617.0 + 23.0) / 44100.0;
        tailSec.store ((float) ((double) predelayMs * 0.001 + comb * std::log (0.001) / std::log (g)),
                       std::memory_order_relaxed);
    }

    void apply() noexcept
    {
        juce::Reverb::Parameters p;
        p.roomSize = roomSizeNow();

        switch (character)
        {
            // The room you don't hear as an effect: tiny, dark, gone before the next note.
            case Character::ambience:  p.damping = 0.85f; p.width = 0.90f; break;
            case Character::room:      p.damping = 0.50f; p.width = 0.80f; break;
            case Character::hall:      p.damping = 0.30f; p.width = 1.00f; break;
            case Character::plate:     p.damping = 0.15f; p.width = 1.00f; break;
            case Character::spring:    p.damping = 0.70f; p.width = 0.45f; break;
            // The dressed tail: a big smooth room the chorus will ride.
            case Character::modulated: p.damping = 0.25f; p.width = 1.00f; break;
        }

        refreshTail();

        // The wet path is OURS now: Freeverb runs fully wet (1/3 undoes its ×3), the dry never
        // enters it, and the ADD at the mix happens in process().
        p.dryLevel   = 0.0f;
        p.wetLevel   = 1.0f / 3.0f;
        p.freezeMode = 0.0f;

        reverb.setParameters (p);
    }

    void applyHpf() noexcept
    {
        hpf.setParams (felitronics::eq::FilterType::HighPass, (double) hpfHz, 0.7071, 0.0);
    }

    /** The tail through a slowly breathing delay — depth and rate fixed by taste, not knobs. */
    void modulateTail (int nch, int n) noexcept
    {
        const int   modN   = (int) modLine[0].size();
        const float baseS  = (float) (0.012 * sampleRate);
        const float depthS = (float) (0.003 * sampleRate);
        const float inc    = (float) (0.45 / sampleRate);   // Hz

        for (int i = 0; i < n; ++i)
        {
            lfoPhase += inc;
            if (lfoPhase >= 1.0f)
                lfoPhase -= 1.0f;

            for (int ch = 0; ch < nch; ++ch)
            {
                modLine[(size_t) ch][(size_t) modPos] = wet[(size_t) ch][i];

                // Quadrature between the channels, so the dress swirls instead of pumping.
                const float ph  = lfoPhase + (ch == 1 ? 0.25f : 0.0f);
                const float lfo = std::sin (ph * juce::MathConstants<float>::twoPi);
                const float dly = baseS + depthS * lfo;

                double pos = (double) modPos - (double) dly;
                while (pos < 0.0) pos += (double) modN;

                const int i0 = (int) pos;
                const int i1 = (i0 + 1) % modN;
                const float f = (float) (pos - (double) i0);
                wet[(size_t) ch][i] = modLine[(size_t) ch][(size_t) i0] * (1.0f - f)
                                    + modLine[(size_t) ch][(size_t) i1] * f;
            }

            modPos = (modPos + 1) % modN;
        }
    }

    juce::Reverb reverb;
    felitronics::eq::Svf hpf;

    Character character  = Character::room;
    float     mix        = 0.2f;
    float     decayScale = 1.0f;
    std::atomic<float> tailSec { 1.0f };   // see tailSeconds()
    float     predelayMs = 0.0f;
    float     hpfHz      = 120.0f;

    double sampleRate = 48000.0;

    std::array<std::vector<float>, 2> wet, wetOut;
    std::array<std::vector<float>, 2> preLine, modLine;
    bool cleared = true;   // see reset(): the clear underneath is ~150 KB and must not be per-block
    int   prePos = 0, modPos = 0;
    float lfoPhase = 0.0f;
};

} // namespace orbitamp::core
