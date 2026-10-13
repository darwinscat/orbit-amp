// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSet.h"

#if CATPIT_SET_EMBEDDED
 #include <CatpitSetData.h>
#endif

namespace orbitamp::catpit::set
{
namespace
{
    // What the folder holds of a build's, one path to the line — the list the next build reads to know
    // what it may take away.
    constexpr const char* listName = "set.txt";
    constexpr const char* cabinetsName = "IRs";

    bool holds (const juce::File& file, const void* data, size_t size)
    {
        if (! file.existsAsFile() || file.getSize() != (juce::int64) size)
            return false;

        juce::MemoryBlock there;
        return file.loadFileAsData (there) && there.getSize() == size && std::memcmp (there.getData(), data, size) == 0;
    }
}

juce::File cabinetsIn (const juce::File& factory)
{
    return factory.getChildFile (cabinetsName);
}

int layOut (const juce::File& factory)
{
    int changed = 0;
    juce::StringArray laid;

   #if CATPIT_SET_EMBEDDED
    for (int i = 0; i < CatpitSetData::namedResourceListSize; ++i)
    {
        const auto name = juce::String::fromUTF8 (CatpitSetData::originalFilenames[i]);
        const bool pack    = name.endsWithIgnoreCase (".orbitrig.zip");
        const bool cabinet = name.endsWithIgnoreCase (".wav") || name.endsWithIgnoreCase (".aif") || name.endsWithIgnoreCase (".aiff");
        if (! pack && ! cabinet)
            continue;

        int size = 0;
        const char* data = CatpitSetData::getNamedResource (CatpitSetData::namedResourceList[i], size);
        if (data == nullptr)
            continue;

        const auto file = (pack ? factory : cabinetsIn (factory)).getChildFile (name);
        laid.add (pack ? name : juce::String (cabinetsName) + "/" + name);

        if (! holds (file, data, (size_t) size)
            && file.getParentDirectory().createDirectory()
            && file.replaceWithData (data, (size_t) size))     // through a temporary file: never half of one
            ++changed;
    }
   #endif

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
} // namespace orbitamp::catpit::set
