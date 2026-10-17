// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FactorySet.h"

#include <cstring>
#include <map>

namespace orbitamp::device
{
namespace
{
    // Where every product's list lies in the folder, one file each: what the folder holds of that
    // product's, one path to the line — the list its next build reads to know what it may take away,
    // and every other product reads to know what is not its to take.
    constexpr const char* listsName = "sets";

    // ...and the line above the paths that says which build laid them: what a product compares its own
    // build with before it writes over a file another product names.
    constexpr const char* buildMark = "#build ";

    // The one list a folder held when it was a single product's own.
    constexpr const char* formerListName = "set.txt";

    bool holds (const juce::File& file, const void* data, size_t size)
    {
        if (! file.existsAsFile() || file.getSize() != (juce::int64) size)
            return false;

        juce::MemoryBlock there;
        return file.loadFileAsData (there) && there.getSize() == size && std::memcmp (there.getData(), data, size) == 0;
    }

    juce::File listOfProduct (const juce::File& factory, const juce::String& product)
    {
        return factory.getChildFile (listsName).getChildFile (juce::File::createLegalFileName (product) + ".txt");
    }

    // A list as read: the paths it names, and the build that laid them — none, for a list that says none.
    struct List
    {
        juce::StringArray paths;
        juce::int64 build = 0;
    };

    List read (const juce::File& file)
    {
        List list;
        juce::StringArray lines;
        lines.addLines (file.loadFileAsString());

        for (const auto& line : lines)
            if (line.startsWith (buildMark))
                list.build = line.substring ((int) std::strlen (buildMark)).trim().getLargeIntValue();
            else if (line.isNotEmpty())
                list.paths.add (line);

        return list;
    }

    // The list is a file anyone can write. A line takes a file away only when it names one INSIDE the folder.
    bool takeAway (const juce::File& folder, const juce::String& path)
    {
        const auto gone = folder.getChildFile (path);
        return gone.isAChildOf (folder) && gone.existsAsFile() && gone.deleteFile();
    }
}

juce::StringArray FactorySet::listOf (const juce::File& factory, const juce::String& product)
{
    return read (listOfProduct (factory, product)).paths;
}

int FactorySet::layOut (const juce::File& factory, const juce::String& product, juce::int64 build,
                        const std::vector<Entry>& set)
{
    const auto list = listOfProduct (factory, product);
    const auto before = read (list);

    // What the other products' lists name — theirs still, whatever this one carries now — and the
    // newest build among those that name each file.
    std::map<juce::String, juce::int64> theirs;
    for (const auto& other : factory.getChildFile (listsName).findChildFiles (juce::File::findFiles, false, "*.txt"))
        if (other != list)
        {
            const auto their = read (other);
            for (const auto& path : their.paths)
                if (const auto [at, added] = theirs.emplace (path, their.build); ! added)
                    at->second = juce::jmax (at->second, their.build);
        }

    int changed = 0;
    juce::StringArray laid;

    for (const auto& e : set)
    {
        if (e.data == nullptr || e.path.isEmpty())
            continue;

        const auto file = factory.getChildFile (e.path);
        laid.add (e.path);

        // A file another product names, written by a build as new as this one or newer, is that build's
        // to say: this one leaves it, and plays it. A file that is missing is written whoever is newer.
        const auto named = theirs.find (e.path);
        const bool newest = named == theirs.end() || build > named->second;

        if (! holds (file, e.data, e.size)
            && (newest || ! file.exists())
            && file.getParentDirectory().createDirectory()
            && file.replaceWithData (e.data, e.size))     // through a temporary file: never half of one
            ++changed;
    }

    for (const auto& was : before.paths)
        if (! laid.contains (was) && theirs.count (was) == 0 && takeAway (factory, was))
            ++changed;

    if (before.paths != laid || before.build != build)
    {
        if (laid.isEmpty())
            list.deleteFile();
        else if (list.getParentDirectory().createDirectory())
            list.replaceWithText (buildMark + juce::String (build) + "\n" + laid.joinIntoString ("\n") + "\n");
    }

    return changed;
}

int FactorySet::retire (const juce::File& former)
{
    const auto list = former.getChildFile (formerListName);
    if (! list.existsAsFile())
        return 0;

    int removed = 0;

    for (const auto& was : read (list).paths)
    {
        if (! takeAway (former, was))
            continue;

        ++removed;

        // The folders it lay in, where it was the last thing in them — `IRs`, and the like.
        for (auto dir = former.getChildFile (was).getParentDirectory(); dir.isAChildOf (former); dir = dir.getParentDirectory())
            if (dir.isSymbolicLink() || dir.getNumberOfChildFiles (juce::File::findFilesAndDirectories) > 0 || ! dir.deleteFile())
                break;
    }

    list.deleteFile();

    if (! former.isSymbolicLink() && former.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
        former.deleteFile();

    return removed;
}

} // namespace orbitamp::device
