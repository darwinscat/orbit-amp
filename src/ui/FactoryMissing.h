// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "../Product.h"
#include "../device/FactorySet.h"

#include <juce_audio_processors/juce_audio_processors.h>

/** WHEN THE FACTORY FOLDER IS NOT THERE. Everything an amp plays out of the box — the packs, the cabinets,
    a face's pictures — lies in the family's factory folder, which the installer lays out
    (device/FactorySet.h); an amp copied off a zip without it, or a folder someone deleted, leaves the amp
    with its code and nothing to play. It says so, plainly, and says where the folder belongs: a face,
    which has nothing to draw, in place of its whole window (FactoryMissingEditor); OrbitAmp, which still
    plays what the player adds, where its empty device block stands. Never in type a player cannot read. */
namespace orbitamp::ui
{

/** Where the folder belongs on this system, one place to the line. */
inline juce::String factoryPlaces()
{
    juce::StringArray places;
    for (const auto& place : device::FactorySet::installedPlaces())
        places.add (place.getFullPathName());
    return places.joinIntoString ("\n");
}

/** What the window says, in full. */
inline juce::String factoryMissingText()
{
    return juce::String (product::name) + " cannot find its data folder.\n\n"
         + "The packs, the cabinets and the pictures the Darwin's Cat amps play are installed once, in a "
           "folder they share, and it is not there. It belongs here:\n\n"
         + factoryPlaces() + "\n\n"
         + "Run the Darwin's Cat Amps installer again. From a zip, copy its Factory folder to that place.";
}

/** ...and in a few words, under OrbitAmp's empty device block. */
inline juce::String factoryMissingLine()
{
    return "The amps' data folder is missing. It belongs in " + factoryPlaces().replace ("\n", " or ") + ".";
}

/** A FACE'S WINDOW WITH NO FACTORY FOLDER: the notice and nothing else. */
class FactoryMissingEditor final : public juce::AudioProcessorEditor
{
public:
    /** The sizes it is set in: nothing a player reads is smaller than 13 px. */
    static constexpr float headingPx = 22.0f;
    static constexpr float textPx    = 16.0f;

    explicit FactoryMissingEditor (juce::AudioProcessor& p) : juce::AudioProcessorEditor (p)
    {
        setSize (720, 420);
    }

    /** The notice, as it reads. */
    static juce::String text() { return factoryMissingText(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::ground);

        auto area = getLocalBounds().reduced (48, 40);
        const auto lines = juce::StringArray::fromLines (text());

        g.setColour (theme::tx);
        g.setFont (juce::FontOptions (headingPx, juce::Font::bold));
        g.drawText (lines[0], area.removeFromTop (34), juce::Justification::centredLeft, true);

        area.removeFromTop (14);
        g.setColour (theme::txDim);
        g.setFont (juce::FontOptions (textPx));
        g.drawFittedText (text().fromFirstOccurrenceOf ("\n", false, false).trim(), area,
                          juce::Justification::topLeft, 20, 1.0f);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FactoryMissingEditor)
};

} // namespace orbitamp::ui
