// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE FAMILY'S FACTORY FOLDER WITH MORE THAN ONE PRODUCT IN IT (device/FactorySet.h). Two
// products of make-believe, their sets in memory, lay out into one folder:
//   - each writes what it carries, and keeps a list of its own;
//   - a file both carry is one file, written once;
//   - when one of them stops carrying it, the file stays for the other; what only that one named goes;
//   - a file both carry with different bytes is the newer build's: the newer writes over it, the older
//     leaves it, and starting the two by turns writes nothing — while a missing file is written by either;
//   - the folder a product kept before the family shared one is emptied once — what its list named,
//     the folders that leaves empty, the list, the folder itself if nothing else is in it — and nothing
//     else is touched.
//
// What a single product's lay-out does — byte for byte, a second start writes nothing, a damaged file
// comes back — is the cockpit's set gate's to hold (CatpitSetTests). This works in a folder of its own
// under the system's temporary folder, and leaves it behind to be looked at.

#include "device/FactorySet.h"

#include <cstdio>

int main()
{
    using orbitamp::device::FactorySet;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    // The real folders' own shape: an apostrophe and a space on the way down.
    const auto family = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("orbitamp-factory-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()))
                            .getChildFile ("Darwin's Cat");
    const auto factory = family.getChildFile ("Factory");
    std::printf ("in %s\n", family.getFullPathName().toRawUTF8());

    // What the two carry: a pack each of their own, a pack and a cabinet they share, and a cabinet of
    // the first's alone.
    const juce::String ownFirst ("the first's pack"), ownSecond ("the second's pack"), sharedPack ("a pack both carry"),
                       sharedCab ("a cabinet both carry"), firstCab ("the first's cabinet");
    const auto entry = [] (const char* path, const juce::String& bytes)
    {
        return FactorySet::Entry { path, bytes.toRawUTF8(), (size_t) bytes.getNumBytesAsUTF8() };
    };

    const std::vector<FactorySet::Entry> first  { entry ("First.orbitrig.zip", ownFirst), entry ("Shared.orbitrig.zip", sharedPack),
                                                  entry ("IRs/01-shared.wav", sharedCab), entry ("IRs/02-first.wav", firstCab) };
    const std::vector<FactorySet::Entry> second { entry ("Second.orbitrig.zip", ownSecond), entry ("Shared.orbitrig.zip", sharedPack),
                                                  entry ("IRs/01-shared.wav", sharedCab) };

    const auto there = [&factory] (const char* path) { return factory.getChildFile (path).existsAsFile(); };

    // When each was built — the later, the bigger (version::kBuildNumber).
    const juce::int64 firstBuild = 20261014120000, secondBuild = 20261015120000;

    // Both start.
    check (FactorySet::layOut (factory, "FirstAmp", firstBuild, first) == 4, "the first product writes the four files it carries");
    check (FactorySet::layOut (factory, "SecondAmp", secondBuild, second) == 1, "the second writes only its own: the two it shares are there already");
    check (FactorySet::listOf (factory, "FirstAmp").size() == 4 && FactorySet::listOf (factory, "SecondAmp").size() == 3,
           "each keeps a list of its own, of everything it carries");
    check (factory.getChildFile ("sets").getChildFile ("FirstAmp.txt").existsAsFile()
               && factory.getChildFile ("sets").getChildFile ("SecondAmp.txt").existsAsFile(),
           "...as sets/<product>.txt in the folder");
    check (FactorySet::layOut (factory, "FirstAmp", firstBuild, first) == 0 && FactorySet::layOut (factory, "SecondAmp", secondBuild, second) == 0,
           "a start of either after that writes nothing");

    // The first stops carrying the shared pack, the shared cabinet and its own cabinet.
    const std::vector<FactorySet::Entry> firstLater { entry ("First.orbitrig.zip", ownFirst) };
    check (FactorySet::layOut (factory, "FirstAmp", firstBuild, firstLater) == 1, "a product that drops three files takes one away");
    check (! there ("IRs/02-first.wav"), "...the one no other product's list names");
    check (there ("Shared.orbitrig.zip") && there ("IRs/01-shared.wav"), "...and the two the other still carries stay");
    check (FactorySet::listOf (factory, "FirstAmp") == juce::StringArray { "First.orbitrig.zip" }, "...while its own list names what it carries now");

    // ...and then the second drops them too: nobody names them any more.
    const std::vector<FactorySet::Entry> secondLater { entry ("Second.orbitrig.zip", ownSecond) };
    check (FactorySet::layOut (factory, "SecondAmp", secondBuild, secondLater) == 2, "when the last product that carried them drops them, they go");
    check (! there ("Shared.orbitrig.zip") && ! there ("IRs/01-shared.wav"), "...both of them");
    check (there ("First.orbitrig.zip") && there ("Second.orbitrig.zip"), "...and each product's own pack stays");

    // A product that carries nothing any more: what it laid goes, and its list with it.
    check (FactorySet::layOut (factory, "SecondAmp", secondBuild, {}) == 1 && ! there ("Second.orbitrig.zip"), "a product that carries nothing takes its last file away");
    check (! factory.getChildFile ("sets").getChildFile ("SecondAmp.txt").exists(), "...and its list");

    // THE NEWEST BUILD WINS a file the two carry with different bytes — a pack retrained, under the same
    // name and the same rig_id. Nothing inside it says which is newer; the builds do.
    {
        const juce::String older ("the capture as the older build carries it"), newer ("the capture retrained, as the newer build carries it");
        const std::vector<FactorySet::Entry> olderSet { entry ("First.orbitrig.zip", ownFirst), entry ("Retrained.orbitrig.zip", older) };
        const std::vector<FactorySet::Entry> newerSet { entry ("Retrained.orbitrig.zip", newer) };
        const auto holding = [&factory] (const juce::String& bytes) { return factory.getChildFile ("Retrained.orbitrig.zip").loadFileAsString() == bytes; };

        check (FactorySet::layOut (factory, "FirstAmp", firstBuild, olderSet) == 1 && holding (older), "the older build lays its copy out where there is none");
        check (FactorySet::layOut (factory, "SecondAmp", secondBuild, newerSet) == 1 && holding (newer), "the newer build writes over a copy that differs from its own");
        check (FactorySet::layOut (factory, "FirstAmp", firstBuild, olderSet) == 0 && holding (newer), "the older build leaves the newer one's copy as it is");
        check (FactorySet::layOut (factory, "SecondAmp", secondBuild, newerSet) == 0 && FactorySet::layOut (factory, "FirstAmp", firstBuild, olderSet) == 0
                   && holding (newer),
               "...and starting the two by turns writes nothing: neither undoes the other");
        check (FactorySet::layOut (factory, "FirstAmp", secondBuild, olderSet) == 0 && holding (newer), "a build exactly as new leaves it too");

        factory.getChildFile ("Retrained.orbitrig.zip").deleteFile();
        check (FactorySet::layOut (factory, "FirstAmp", firstBuild, olderSet) == 1 && holding (older), "a copy that went missing is written, by the older build too");
        check (FactorySet::layOut (factory, "SecondAmp", secondBuild, newerSet) == 1 && holding (newer), "...and the newer build puts its own back over it");

        const juce::int64 laterStill = 20261016120000;
        check (FactorySet::layOut (factory, "FirstAmp", laterStill, olderSet) == 1 && holding (older),
               "once the other product is built later still, its copy is the one that stands");
        check (factory.getChildFile ("sets").getChildFile ("FirstAmp.txt").loadFileAsString().contains (juce::String (laterStill)),
               "...and its list says which build laid it");

        FactorySet::layOut (factory, "FirstAmp", laterStill, {});
        FactorySet::layOut (factory, "SecondAmp", secondBuild, {});
        check (! there ("Retrained.orbitrig.zip"), "with both carrying nothing, it goes");
    }

    // THE FOLDER A PRODUCT KEPT BEFORE THE FAMILY SHARED ONE: emptied once, of what its list named.
    {
        const auto former = family.getChildFile ("FirstAmp").getChildFile ("Factory");
        for (const auto* path : { "Old.orbitrig.zip", "IRs/01-old.wav", "IRs/02-old.wav" })
            former.getChildFile (path).create();
        const auto players = former.getChildFile ("Mine.orbitrig.zip");
        const auto outside = family.getChildFile ("outside.txt");
        players.replaceWithText ("the player's own");
        outside.replaceWithText ("not the amp's");
        former.getChildFile ("set.txt").replaceWithText ("Old.orbitrig.zip\nIRs/01-old.wav\nIRs/02-old.wav\n../../outside.txt\n"
                                                         + outside.getFullPathName() + "\n");

        check (FactorySet::retire (former) == 3, "the folder of before loses the three files its list named");
        check (! former.getChildFile ("Old.orbitrig.zip").exists() && ! former.getChildFile ("IRs").exists(),
               "...those three, and the folder they leave empty");
        check (! former.getChildFile ("set.txt").exists(), "...and the list");
        check (players.existsAsFile() && former.isDirectory(), "a file the list does not name stays, and the folder with it");
        check (outside.existsAsFile(), "a line that points out of the folder takes nothing away");
        check (FactorySet::retire (former) == 0 && players.existsAsFile(), "once: with the list gone, the next start touches nothing");

        const auto emptied = family.getChildFile ("SecondAmp").getChildFile ("Factory");
        emptied.getChildFile ("Old.orbitrig.zip").create();
        emptied.getChildFile ("set.txt").replaceWithText ("Old.orbitrig.zip\n");
        check (FactorySet::retire (emptied) == 1 && ! emptied.exists() && emptied.getParentDirectory().isDirectory(),
               "a folder of before that held only its set goes altogether, and nothing above it");
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
