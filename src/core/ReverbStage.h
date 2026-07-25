#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace orbitamp::core
{

/** The reverb tail — the last of our own layers before the signal leaves for the cabinet.

    Built on juce::Reverb (Freeverb), which is simply the right tool here: the plugin is a JUCE
    application and has no reason to avoid it. The only consequence worth recording is placement —
    felitronics-core stays JUCE-free (for possible embedded / web targets later), so this stage
    lives in the product, not in core. ToneStack is JUCE-free because its maths is portable, not
    because plugin code is supposed to be.

    The design calls for Mix only in the simple case, so size and damping are not loose knobs: they
    come from the chosen character. Everything reachable is on the face.

    Real-time rule: process() never allocates, locks, or throws. */
class ReverbStage
{
public:
    enum class Character { room, hall, plate, spring };

    void prepare (double sampleRate) noexcept
    {
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        apply();
    }

    void reset() noexcept { reverb.reset(); }

    void setCharacter (Character c) noexcept
    {
        if (c == character)
            return;

        character = c;
        apply();
    }

    /** 0 = fully dry, 1 = fully wet. */
    void setMix (float newMix) noexcept
    {
        const float m = juce::jlimit (0.0f, 1.0f, newMix);
        if (! juce::approximatelyEqual (m, mix))
        {
            mix = m;
            apply();
        }
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels >= 2)
            reverb.processStereo (channels[0], channels[1], numSamples);
        else if (numChannels == 1)
            reverb.processMono (channels[0], numSamples);
    }

private:
    void apply() noexcept
    {
        juce::Reverb::Parameters p;

        switch (character)
        {
            case Character::room:   p.roomSize = 0.35f; p.damping = 0.50f; p.width = 0.80f; break;
            case Character::hall:   p.roomSize = 0.85f; p.damping = 0.30f; p.width = 1.00f; break;
            case Character::plate:  p.roomSize = 0.60f; p.damping = 0.15f; p.width = 1.00f; break;
            case Character::spring: p.roomSize = 0.25f; p.damping = 0.70f; p.width = 0.45f; break;
        }

        // An amp's reverb control ADDS the tank return to the dry signal — it does not crossfade
        // away from it. The dry path stays at unity at every setting, exactly as the hardware does;
        // a wet/dry crossfade is a studio-plugin convention and would be wrong here.
        //
        // The constants undo Freeverb's internal scaling (it multiplies dry by 2 and wet by 3), so
        // dryLevel 0.5 is unity dry and Mix 0..1 sweeps the added tail from silent to unity.
        p.dryLevel   = 0.5f;
        p.wetLevel   = mix / 3.0f;
        p.freezeMode = 0.0f;

        reverb.setParameters (p);
    }

    juce::Reverb reverb;
    Character    character = Character::room;
    float        mix       = 0.2f;
};

} // namespace orbitamp::core
