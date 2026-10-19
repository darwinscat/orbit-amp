// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace orbitamp
{

/** A PRODUCT'S PRESET FOLDER: where its presets lie, what they are called, and the trees in them. Every
    amp of the family keeps its presets the same way — one XML file a preset, named as the player named
    it, in `Darwin's Cat/<product>/Presets` — and what differs is the product's to say around this: the
    folder's name and the files' extension, what a preset holds, and what its menu does with them
    (PresetManager.h for OrbitAmp, channels/Presets.h for the faces).

    Files only — it reads and writes trees and never touches the live state, so that a preset landing
    goes through the history as one undoable step instead of quietly replacing everything behind
    undo's back. */
class PresetStore
{
public:
    /** `product` names the folder, `extension` is the files' own, its dot included. */
    PresetStore (juce::String product, juce::String extension)
        : productName (std::move (product)), fileExtension (std::move (extension)) {}

    /** Made if it is not there yet. JUCE's userApplicationDataDirectory is ~/Library on the Mac, not
        ~/Library/Application Support — without the step down, presets would land one level too high. */
    juce::File directory() const
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        dir = dir.getChildFile ("Application Support");
       #endif

        dir = dir.getChildFile ("Darwin's Cat").getChildFile (productName).getChildFile ("Presets");
        if (! dir.isDirectory())
            dir.createDirectory();
        return dir;
    }

    /** Every preset in the folder, in natural order: "Preset 2" before "Preset 10". */
    juce::StringArray names() const
    {
        juce::StringArray out;
        for (const auto& f : directory().findChildFiles (juce::File::findFiles, false, "*" + fileExtension))
            out.add (f.getFileNameWithoutExtension());
        out.sortNatural();
        return out;
    }

    /** The file a name is kept in — the name made legal for the file system, which is the name the
        list gives back. */
    juce::File fileFor (const juce::String& name) const
    {
        return directory().getChildFile (juce::File::createLegalFileName (name) + fileExtension);
    }

    bool exists (const juce::String& name) const
    {
        return name.isNotEmpty() && fileFor (name).existsAsFile();
    }

    /** The stored tree, or an invalid one if the preset is missing or unreadable. */
    juce::ValueTree read (const juce::String& name) const
    {
        if (auto xml = juce::XmlDocument::parse (fileFor (name)))
            return juce::ValueTree::fromXml (*xml);
        return {};
    }

    /** False, and nothing written, for an empty name or an invalid tree. */
    bool write (const juce::String& name, const juce::ValueTree& tree) const
    {
        if (name.isEmpty() || ! tree.isValid())
            return false;
        if (auto xml = tree.createXml())
            return fileFor (name).replaceWithText (xml->toString());
        return false;
    }

    /** True when the file is gone — and when there was none to remove. */
    bool remove (const juce::String& name) const
    {
        return fileFor (name).deleteFile();
    }

    bool move (const juce::String& from, const juce::String& to) const
    {
        return fileFor (from).moveFileTo (fileFor (to));
    }

private:
    juce::String productName, fileExtension;
};

} // namespace orbitamp
