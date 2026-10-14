// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE COCKPIT'S SET AS IT LEAVES THE BINARY (channels/Set.h, over the cockpit's set data): that what is laid out is
// what was built in, byte for byte; that a start after the first writes nothing; that a file damaged
// or lost comes back; that what an earlier build left behind goes — and that nothing else does; and
// that the cockpit's cabinets are its own, whatever another product lays out beside them.
//
//   CatpitSetTests [folder]     the folder to work in, under the current directory; one of its own
//                               under the system's temporary folder when none is named
//
// It works in that folder alone, and leaves it behind to be looked at.

#include "Product.h"
#include "channels/Set.h"
#include "device/FactorySet.h"

#if PRODUCT_SET_EMBEDDED
 #include <ProductSetData.h>
#endif

#include <cstdio>
#include <cstring>

int main (int argc, char** argv)
{
    using namespace orbitamp::channels;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    const auto root = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (juce::CharPointer_UTF8 (argv[1])))
                               : juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("catpit-set-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));

    // The real folder's own shape: an apostrophe and a space on the way down. The family's, not the cockpit's.
    const auto factory = root.getChildFile ("Darwin's Cat").getChildFile ("Factory");
    const auto list    = factory.getChildFile ("sets").getChildFile (juce::String (orbitamp::product::name) + ".txt");
    const juce::int64 build = 20261014120000;     // the build the gate plays: a stamp of make-believe
    std::printf ("in %s\n", factory.getFullPathName().toRawUTF8());
    check (! factory.exists(), "the folder is not there before the first start");

    // What the binary carries, as the layout sorts it.
    struct Carried { juce::File file; const char* data; int size; };
    std::vector<Carried> carried;
    int packs = 0, cabinets = 0;
    juce::int64 bytes = 0;
   #if PRODUCT_SET_EMBEDDED
    for (int i = 0; i < ProductSetData::namedResourceListSize; ++i)
    {
        const auto name = juce::String::fromUTF8 (ProductSetData::originalFilenames[i]);
        const bool pack = name.endsWithIgnoreCase (".orbitrig.zip");
        int size = 0;
        const char* data = ProductSetData::getNamedResource (ProductSetData::namedResourceList[i], size);
        carried.push_back ({ (pack ? factory : set::cabinetsIn (factory)).getChildFile (name), data, size });
        (pack ? packs : cabinets) += 1;
        bytes += size;
    }
   #endif
    std::printf ("the binary carries %d packs, %d cabinets, %lld bytes\n", packs, cabinets, (long long) bytes);

    const auto allThere = [&carried]
    {
        for (const auto& c : carried)
        {
            juce::MemoryBlock there;
            if (! c.file.loadFileAsData (there) || there.getSize() != (size_t) c.size
                || std::memcmp (there.getData(), c.data, (size_t) c.size) != 0)
                return false;
        }
        return true;
    };

    // The first start.
    check (set::layOut (factory, build) == (int) carried.size(), "the first start writes every file of the set");
    check (allThere(), "...each of them byte for byte what was built in");
    check (carried.empty() || list.existsAsFile(), "...and the cockpit's own list of them");

    // Every start after it.
    std::vector<juce::Time> stamps;
    for (const auto& c : carried)
        stamps.push_back (c.file.getLastModificationTime());
    juce::Thread::sleep (1100);       // a file system that keeps whole seconds would hide a rewrite
    check (set::layOut (factory, build) == 0, "the second start writes nothing");
    bool untouched = true;
    for (size_t i = 0; i < carried.size(); ++i)
        untouched = untouched && carried[i].file.getLastModificationTime() == stamps[i];
    check (untouched, "...and no file's date has moved");

    if (! carried.empty())
    {
        // A file damaged, a file lost.
        const auto& first = carried.front();
        const auto& last  = carried.back();
        first.file.replaceWithData (first.data, (size_t) first.size / 2);
        check (set::layOut (factory, build) == 1 && allThere(), "a file cut short is written again");
        last.file.deleteFile();
        check (set::layOut (factory, build) == 1 && allThere(), "a file that went missing comes back");

        // What an earlier build laid here and this one does not carry.
        const auto leftPack = factory.getChildFile ("Gone.orbitrig.zip");
        const auto leftCab  = set::cabinetsIn (factory).getChildFile ("99-gone.wav");
        leftPack.replaceWithText ("an earlier build's pack");
        leftCab.replaceWithText ("an earlier build's cabinet");
        list.appendText ("Gone.orbitrig.zip\nIRs/99-gone.wav\n");

        // ...beside a file the list does not name, and a line that points out of the folder.
        const auto players = set::cabinetsIn (factory).getChildFile ("mine.wav");
        const auto outside = root.getChildFile ("outside.txt");
        players.replaceWithText ("the player's own");
        outside.replaceWithText ("not the amp's");
        list.appendText ("../../outside.txt\n" + outside.getFullPathName() + "\n");

        check (set::layOut (factory, build) == 2, "two files an earlier build left are taken away");
        check (! leftPack.exists() && ! leftCab.exists(), "...those two");
        check (players.existsAsFile(), "a file the list does not name stays");
        check (outside.existsAsFile(), "a line that points out of the folder takes nothing away");
        check (allThere(), "the set is whole after it");
        check (set::layOut (factory, build) == 0, "and the next start writes nothing again");

        // ANOTHER PRODUCT IN THE SAME FOLDER, with a cabinet of its own beside the cockpit's.
        const juce::String another ("another product's cabinet");
        orbitamp::device::FactorySet::layOut (factory, "AnotherAmp", build, { { "IRs/50-another.wav", another.toRawUTF8(), (size_t) another.length() } });
        check (set::cabinetsIn (factory).getChildFile ("50-another.wav").existsAsFile(), "another product lays a cabinet out beside the cockpit's");

        const auto own = set::ownCabinets (factory);
        bool mine = own.size() == cabinets;
        for (const auto& c : carried)
            mine = mine && (c.file.getParentDirectory() != set::cabinetsIn (factory) || own.contains (c.file));
        check (mine, "the cockpit's cabinets are the ones it carries, all " + juce::String (own.size()) + " of them");
        check (! own.contains (set::cabinetsIn (factory).getChildFile ("50-another.wav")), "...and not the other product's");
        check (! own.contains (players), "...nor a file of the player's");
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
