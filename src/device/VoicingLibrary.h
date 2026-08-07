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

    /** The pedals, by the character they sit at with the gain at noon.

        EMPTY on purpose. The preamp's names above are stand-ins I made up to get the control built;
        adding a second invented set would be doubling down on that. The selector renders correctly
        against an empty list — it reads as "—" and the tree shows the types greyed — so this fills
        in from the descriptors when they exist and nothing in the UI changes. */
    static juce::StringArray pedalsFor (int /*typeIndex*/)
    {
        return {};
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
