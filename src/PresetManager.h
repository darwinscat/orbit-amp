#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace orbitamp
{

/** User presets on disk. Files only — it reads and writes trees and never touches the live state,
    so that loading a preset can go through the history engine as one undoable step instead of
    quietly replacing everything behind undo's back.

    Factory presets will live alongside these as embedded resources; there are none yet because
    there is nothing curated to ship. */
class PresetManager
{
public:
    static constexpr const char* extension = ".orbitamp";

    /** JUCE's userApplicationDataDirectory is ~/Library on macOS, not ~/Library/Application Support —
        without this, presets were being written one level too high. */
    static juce::File directory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        dir = dir.getChildFile ("Application Support");
       #endif

        dir = dir.getChildFile ("Darwin's Cat").getChildFile ("OrbitAmp").getChildFile ("Presets");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    static juce::StringArray names()
    {
        juce::StringArray out;

        for (const auto& f : directory().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
            out.add (f.getFileNameWithoutExtension());

        out.sortNatural();
        return out;
    }

    static juce::File fileFor (const juce::String& name)
    {
        return directory().getChildFile (juce::File::createLegalFileName (name) + extension);
    }

    /** The stored state, or an invalid tree if the preset is missing or unreadable. */
    static juce::ValueTree read (const juce::String& name)
    {
        if (auto xml = juce::XmlDocument::parse (fileFor (name)))
            return juce::ValueTree::fromXml (*xml);

        return {};
    }

    static bool write (const juce::String& name, const juce::ValueTree& state)
    {
        if (name.isEmpty() || ! state.isValid())
            return false;

        if (auto xml = state.createXml())
            return fileFor (name).replaceWithText (xml->toString());

        return false;
    }

    static bool remove (const juce::String& name)
    {
        return fileFor (name).deleteFile();
    }
};

} // namespace orbitamp
