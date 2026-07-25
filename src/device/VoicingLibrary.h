#pragma once

#include <juce_core/juce_core.h>

namespace orbitamp::device
{

/** The curated voicings, grouped by type.

    PLACEHOLDER CONTENT: the real set is captured hardware and does not exist yet, so these are
    stand-in names that let the selector be built and driven. What is NOT placeholder is the shape —
    a type holds a small, curated set of voices, and the addressable unit is the voicing, never a
    "channel". When the captures land, this is where they are declared; nothing in the UI changes. */
struct VoicingLibrary
{
    static juce::StringArray voicesFor (int typeIndex)
    {
        switch (typeIndex)
        {
            case 0:  return { "Glass", "Bloom", "Pearl" };            // clean
            case 1:  return { "Ember", "Grain" };                     // edge
            case 2:  return { "Rust", "Anvil", "Tarmac" };            // crunch
            case 3:  return { "Obsidian", "Vertigo", "Kilo" };        // high-gain
            case 4:  return { "Halo", "Nought" };                     // modern
            default: return {};
        }
    }

    /** Clamps a stored voice index to what the given type actually offers — a saved session may name
        a voice the type no longer has. */
    static int clampVoice (int typeIndex, int voiceIndex)
    {
        const int n = voicesFor (typeIndex).size();
        return n == 0 ? 0 : juce::jlimit (0, n - 1, voiceIndex);
    }
};

} // namespace orbitamp::device
