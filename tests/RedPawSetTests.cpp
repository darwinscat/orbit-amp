// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE ROVER'S SET AS IT LEAVES THE BINARY (channels/Set.h, over the rover's set data): that
// what is laid out is what was built in, byte for byte — the packs the product's channels play
// (ROVER_CHANNELS of them, as the build says), and the cabinets; that a start after the first
// writes nothing; that a file damaged
// or lost comes back; that what an earlier build left behind goes — and that nothing else does; and
// that the rover's cabinets are its own, whatever another product lays out beside them. Every rover that
// wears the dashboard runs it as itself — RedPawAmp, FrostPawAmp, AmberPawAmp (CMakeLists.txt: rover_set_test).
//
//   RedPawSetTests [folder]     the folder to work in, under the current directory; one of its own
//                               under the system's temporary folder when none is named
//
// It works in that folder alone, and leaves it behind to be looked at.

#include "Product.h"
#include "channels/Set.h"
#include "device/DeviceLibrary.h"
#include "device/FactorySet.h"
#include "device/FactoryShelf.h"

#if PRODUCT_SET_EMBEDDED
 #include <ProductSetData.h>
#endif

#include <cstdio>
#include <cstring>

#ifndef ROVER_CHANNELS
 #error "the build says how many channels the rover plays"
#endif

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
                                     .getChildFile ("redpaw-set-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));

    // The real folder's own shape: an apostrophe and a space on the way down. The family's, not the rover's.
    const auto factory = root.getChildFile ("Darwin's Cat").getChildFile ("Factory");
    const auto list    = factory.getChildFile ("sets").getChildFile (juce::String (orbitamp::product::name) + ".txt");
    const juce::int64 build = 20261014120000;     // the build the gate plays: a stamp of make-believe
    std::printf ("in %s\n", factory.getFullPathName().toRawUTF8());
    check (! factory.exists(), "the folder is not there before the first start");

    // What the binary carries, as the layout sorts it.
    struct Carried { juce::File file; const char* data; int size; };
    std::vector<Carried> carried;
    int packs = 0, cabinets = 0, stray = 0;
    juce::int64 bytes = 0;
   #if PRODUCT_SET_EMBEDDED
    for (int i = 0; i < ProductSetData::namedResourceListSize; ++i)
    {
        const auto name = juce::String::fromUTF8 (ProductSetData::originalFilenames[i]);
        if (! name.endsWithIgnoreCase (".orbitrig.zip"))
        {
            ++stray;        // the product's own data holds its packs; the cabinets are the family's
            continue;
        }
        int size = 0;
        const char* data = ProductSetData::getNamedResource (ProductSetData::namedResourceList[i], size);
        carried.push_back ({ factory.getChildFile (name), data, size });
        ++packs;
        bytes += size;
    }
   #endif

    // ...and Darwin's Cat's own cabinets of the family's shelf, in their folder: OrbitAmp alone carries
    // the whole of it.
    for (const auto& ir : orbitamp::device::shelf::irs())
    {
        if (juce::String (ir.folder) != orbitamp::device::shelf::ours)
            continue;
        carried.push_back ({ factory.getChildFile (orbitamp::device::shelf::pathOf (ir)), ir.data, ir.size });
        ++cabinets;
        bytes += ir.size;
    }
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
    check (carried.empty() || list.existsAsFile(), "...and the rover's own list of them");

    // THE FAMILY'S SHELF, Darwin's Cat's own ten in the Orange's folder, none in Other — those are
    // OrbitAmp's — and none loose at the top.
    {
        const auto shelf = set::cabinetsIn (factory);
        const auto in = [&shelf] (const char* folder) { return shelf.getChildFile (folder).findChildFiles (juce::File::findFiles, false, "*.wav").size(); };
        check (stray == 0, "the rover's own set data holds packs alone");
        check (in ("Orange PPC212V") == 10 && in ("Other") == 0 && shelf.findChildFiles (juce::File::findFiles, false).isEmpty(),
               "the cabinets lie in the Orange's folder, all ten, none in Other and none loose");
        check (shelf.getChildFile ("Orange PPC212V/02-dark-matter.wav").existsAsFile(), "...DARK MATTER among the Orange's");
    }

    // THE CHANNELS' PACKS, each by the capture it holds, and nothing else among the packs. On Mars the
    // Friedman IR-X's two, each played by two channels (a gear, and its TURBO with the Boost thrown);
    // elsewhere the V4's in order, one to a channel, as far as the product goes — Copper, Copper TB,
    // the Sheriff's two, the Kraken's two.
    {
        juce::StringArray wanted;
        if (juce::String (ORBITAMP_PRODUCT_NAME) == "RedPawAmp")
        {
            wanted = { "ir-x-ch1-poweramp-3e66", "ir-x-ch2-poweramp-67e9" };
        }
        else
        {
            const juce::StringArray v4 { "v4-copper-4ae0", "v4-copper-tb-5841", "v4-sheriff-ch1-82ab",
                                         "v4-sheriff-ch2-2f2f", "v4-kraken-ch1-f7fc", "v4-kraken-ch2-5f71" };
            for (int i = 0; i < juce::jmin (ROVER_CHANNELS, v4.size()); ++i)
                wanted.add (v4[i]);
        }
        juce::StringArray found;
        for (const auto& c : carried)
            if (c.file.getFileName().endsWithIgnoreCase (".orbitrig.zip"))
                found.add (orbitamp::device::DeviceLibrary::rigIdOf (c.file));
        found.sort (false);
        auto sorted = wanted;
        sorted.sort (false);
        check (packs == wanted.size() && found == sorted,
               "the " + juce::String (packs) + " packs are the " + juce::String (ROVER_CHANNELS) + " channels' captures: " + found.joinIntoString (" "));
        check (cabinets == 10, "...and Darwin's Cat's own ten cabinets with them");
    }

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

        // ANOTHER PRODUCT IN THE SAME FOLDER, with a cabinet of its own beside the rover's.
        const juce::String another ("another product's cabinet");
        orbitamp::device::FactorySet::layOut (factory, "AnotherAmp", build, { { "IRs/50-another.wav", another.toRawUTF8(), (size_t) another.length() } });
        check (set::cabinetsIn (factory).getChildFile ("50-another.wav").existsAsFile(), "another product lays a cabinet out beside the rover's");

        const auto own = set::ownCabinets (factory);
        bool mine = own.size() == cabinets;
        for (const auto& c : carried)
            mine = mine && (! c.file.isAChildOf (set::cabinetsIn (factory)) || own.contains (c.file));
        check (mine, "the rover's cabinets are the ones it carries, all " + juce::String (own.size()) + " of them");
        check (! own.contains (set::cabinetsIn (factory).getChildFile ("50-another.wav")), "...and not the other product's");
        check (! own.contains (players), "...nor a file of the player's");
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
