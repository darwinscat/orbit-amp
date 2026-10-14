// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FactorySet.h"

#include <cstring>

namespace orbitamp::device
{
namespace
{
    // What the folder holds of a build's, one path to the line — the list the next build reads to know
    // what it may take away.
    constexpr const char* listName = "set.txt";

    bool holds (const juce::File& file, const void* data, size_t size)
    {
        if (! file.existsAsFile() || file.getSize() != (juce::int64) size)
            return false;

        juce::MemoryBlock there;
        return file.loadFileAsData (there) && there.getSize() == size && std::memcmp (there.getData(), data, size) == 0;
    }
}

int FactorySet::layOut (const juce::File& factory, const std::vector<Entry>& set)
{
    int changed = 0;
    juce::StringArray laid;

    for (const auto& e : set)
    {
        if (e.data == nullptr || e.path.isEmpty())
            continue;

        const auto file = factory.getChildFile (e.path);
        laid.add (e.path);

        if (! holds (file, e.data, e.size)
            && file.getParentDirectory().createDirectory()
            && file.replaceWithData (e.data, e.size))     // through a temporary file: never half of one
            ++changed;
    }

    const auto list = factory.getChildFile (listName);
    juce::StringArray before;
    before.addLines (list.loadFileAsString());
    before.removeEmptyStrings();

    // The list is a file anyone can write. A line is taken away only when it names a file INSIDE this folder.
    for (const auto& was : before)
        if (! laid.contains (was))
            if (const auto gone = factory.getChildFile (was); gone.isAChildOf (factory) && gone.existsAsFile() && gone.deleteFile())
                ++changed;

    if (before != laid)
    {
        if (laid.isEmpty())
            list.deleteFile();
        else if (factory.createDirectory())
            list.replaceWithText (laid.joinIntoString ("\n") + "\n");
    }

    return changed;
}

} // namespace orbitamp::device
