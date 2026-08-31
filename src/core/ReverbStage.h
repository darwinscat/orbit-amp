#pragma once

#include <felitronics/eq/Svf.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** The reverb tail — the last of our own layers before the signal leaves for the power amp.

    Built on juce::Reverb (Freeverb), which is simply the right tool here: the plugin is a JUCE
    application and has no reason to avoid it. The only consequence worth recording is placement —
    felitronics-core stays JUCE-free (for possible embedded / web targets later), so this stage
    lives in the product, not in core.

    The design calls for Mix as the hero, the character as the title, and everything else as a
    late refinement: DECAY scales the chosen character's tail rather than replacing it, PREDELAY
    holds the tail back so the attack stays dry, and the HPF cleans the WET only — this reverb
    feeds a power amp, and a low tail into distortion is mud multiplied.

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

    void reset() noexcept
    {
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
    void setPredelayMs (float ms) noexcept { predelayMs = juce::jlimit (0.0f, 100.0f, ms); }

    /** The tail's own high-pass — the WET only, always in: a low tail into a driven power amp is
        mud, and at the 40 Hz floor the filter is as good as air. */
    void setHpfHz (float hz) noexcept
    {
        if (! juce::approximatelyEqual (hz, hpfHz))
        {
            hpfHz = hz;
            applyHpf();
        }
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels < 1 || numSamples <= 0)
            return;

        const int nch = juce::jmin (2, numChannels);
        const int n   = juce::jmin (numSamples, (int) wet[0].size());

        // The wet copy: the whole wet chain runs on it, and the dry never enters.
        for (int ch = 0; ch < nch; ++ch)
        {
            float* w = wet[(size_t) ch].data();
            const float* d = channels[ch];

            for (int i = 0; i < n; ++i)
                w[i] = d[i];
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
    void apply() noexcept
    {
        juce::Reverb::Parameters p;

        switch (character)
        {
            // The room you don't hear as an effect: tiny, dark, gone before the next note.
            case Character::ambience: p.roomSize = 0.15f; p.damping = 0.85f; p.width = 0.90f; break;
            case Character::room:     p.roomSize = 0.35f; p.damping = 0.50f; p.width = 0.80f; break;
            case Character::hall:     p.roomSize = 0.85f; p.damping = 0.30f; p.width = 1.00f; break;
            case Character::plate:    p.roomSize = 0.60f; p.damping = 0.15f; p.width = 1.00f; break;
            case Character::spring:   p.roomSize = 0.25f; p.damping = 0.70f; p.width = 0.45f; break;
            // The dressed tail: a big smooth room the chorus will ride.
            case Character::modulated: p.roomSize = 0.70f; p.damping = 0.25f; p.width = 1.00f; break;
        }

        // DECAY breathes through the room size: half-to-double maps to a quarter of the scale
        // either way, clamped clear of runaway.
        p.roomSize = juce::jlimit (0.05f, 0.98f, p.roomSize + std::log2 (decayScale) * 0.25f);

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
    float     predelayMs = 0.0f;
    float     hpfHz      = 120.0f;

    double sampleRate = 48000.0;

    std::array<std::vector<float>, 2> wet, wetOut;
    std::array<std::vector<float>, 2> preLine, modLine;
    int   prePos = 0, modPos = 0;
    float lfoPhase = 0.0f;
};

} // namespace orbitamp::core
