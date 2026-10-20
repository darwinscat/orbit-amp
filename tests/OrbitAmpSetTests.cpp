// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITAMP'S SET AS THE BUILD LAYS IT OUT in the factory folder (CMakeLists.txt: THE FACTORY
// FOLDER), under OrbitAmp's own list there: that the list names every pack OrbitAmp plays — every pack of
// the tree's set: the X88-IR's four channels, the Friedman IR-X's two, the ReVolt Guitar's three and the
// Victory V4's three — and nothing else; that it names the whole cabinet shelf in its folders, and every
// file it names is there; that the shelf OrbitAmp plays holds every place of the catalog; that each pack
// lies under the name of the capture it holds; that the preamp list shows each by a name of its own; and
// that the AMP's menu shows the X88-IR, the IR-X, the ReVolt and the V4 as the family's devices and no OLD.
//
// It reads the build's own <build>/Factory (gate_reads_factory) — never the machine's — and the player's
// Devices folder is one of its own, empty, under the system's temporary folder.

#include "device/DeviceLibrary.h"
#include "device/FactorySet.h"
#include "device/FactoryShelf.h"

#include <cstdio>

int main()
{
    using namespace orbitamp;
    using device::DeviceLibrary;
    using device::FactorySet;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    const auto factory = DeviceLibrary::factoryDirectory();
    const auto own = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("orbitamp-set-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()))
                         .getChildFile ("Devices");
    own.createDirectory();
    std::printf ("reading %s\n", factory.getFullPathName().toRawUTF8());

    const auto list = FactorySet::listOf (factory, "OrbitAmp");
    check (list.size() > 0, "OrbitAmp has a list in the build's factory folder, " + juce::String (list.size()) + " files");

    // What OrbitAmp plays, by the names its list shows them by: a pack that went missing from the tree
    // would otherwise go missing from the product without a word.
    const juce::StringArray wanted { "X88-IR Normal", "X88-IR Altered", "X88-IR Crunch", "X88-IR Overdrive",
                                     "IR-X Ch1 PowerAmp", "IR-X Ch2 PowerAmp",
                                     "ReVolt Clean PowerAmp", "ReVolt Crunch PowerAmp", "ReVolt Lead PowerAmp",
                                     "V4 Copper (clean)", "V4 Sheriff (crunch)", "V4 Kraken (hi-gain)" };

    juce::StringArray packs, cabinets, astray, missing;
    for (const auto& path : list)
    {
        if (path.endsWithIgnoreCase (".orbitrig.zip"))
            packs.add (path.upToFirstOccurrenceOf (".", false, false));
        else if (path.startsWith (juce::String (device::shelf::folderName) + "/"))
            cabinets.add (path);
        else
            astray.add (path);

        if (factory.getChildFile (path).getSize() <= 0)
            missing.add (path);
    }

    bool everyOne = packs.size() == wanted.size();
    for (const auto& w : wanted)
        everyOne = everyOne && packs.contains (w);
    check (everyOne, "the list names every pack OrbitAmp plays, the X88-IR's four, the IR-X's two, the ReVolt's three and the V4's three: "
                         + packs.joinIntoString (", "));
    check (astray.isEmpty(), "...and nothing but packs and cabinets — no face's pictures" + (astray.isEmpty() ? juce::String() : ": " + astray.joinIntoString (", ")));
    check (missing.isEmpty(), "every file it names is there, with something in it" + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));

    // THE WHOLE SHELF, in its folders: Darwin's Cat's own ten in the Orange's, the twenty-one others in Other,
    // each where the catalog has it — and every place of it filled in the shelf OrbitAmp plays.
    {
        const auto in = [&cabinets] (const char* folder)
        {
            int n = 0;
            for (const auto& c : cabinets)
                n += c.startsWith (juce::String (device::shelf::folderName) + "/" + folder + "/") ? 1 : 0;
            return n;
        };
        check (cabinets.size() == 31 && in ("Orange PPC212V") == 10 && in ("Other") == 21,
               "the whole shelf: ten in Orange PPC212V, twenty-one in Other (" + juce::String (cabinets.size()) + ")");

        bool cataloged = true, whole = device::shelf::irs ("OrbitAmp").size() == std::size (catalog::irs);
        for (const auto& ir : catalog::irs)
            cataloged = cataloged && cabinets.contains (device::shelf::pathOf (ir.folder, juce::String::fromUTF8 (ir.file)));
        for (const auto& ir : device::shelf::irs ("OrbitAmp"))
            whole = whole && ir.data != nullptr && ir.size > 0;
        check (cataloged, "...each at the path the catalog gives it");
        check (whole, "...and OrbitAmp's shelf holds every place of the catalog, read from the folder");
    }

    // A name is a promise about the capture: "X88-IR Crunch" is the pack whose rig_id is x88-ir-crunch-…,
    // "V4 Copper (clean)" the one whose rig_id is v4-copper-… — a word in brackets says its character.
    {
        juce::StringArray wrong;
        for (const auto& p : packs)
        {
            const auto rig = DeviceLibrary::rigIdOf (factory.getChildFile (p + ".orbitrig.zip"));
            const auto promised = p.upToFirstOccurrenceOf (" (", false, false);
            if (! rig.startsWith (promised.toLowerCase().replaceCharacter (' ', '-') + "-"))
                wrong.add (p + " holds " + (rig.isEmpty() ? juce::String ("no capture") : rig));
        }
        check (wrong.isEmpty(), "each lies under the name of the capture it holds" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString (", ")));
    }

    // What the preamp's list offers, read as the amp reads its own.
    {
        juce::StringArray listed;
        for (const auto& p : DeviceLibrary::scan ("OrbitAmp", DeviceLibrary::Slot::preamp, true, factory, own))
            if (p.bundled)
                listed.add (p.displayName());

        bool allListed = listed.size() == wanted.size();
        for (const auto& w : wanted)
            allListed = allListed && listed.contains (w);
        check (allListed, "the preamp list shows each by a name of its own, and nothing else of the folder's: " + listed.joinIntoString (", "));
    }

    // WHAT THE AMP'S MENU SHOWS (DeviceLibrary::shelfOf, which the menu is built from): the family's
    // devices, and no folder of the factory's — there is no OLD any more.
    {
        const auto shelf = DeviceLibrary::shelfOf (DeviceLibrary::scan ("OrbitAmp", DeviceLibrary::ampSlot, true, factory, own));
        juce::StringArray devices, folders;
        for (const auto& d : shelf.factory)
            devices.add (d.name);
        for (const auto& f : shelf.folders)
            folders.add (f.name);
        check (devices.joinIntoString (", ") == "IR-X, ReVolt, V4, X88-IR", "the AMP's menu: the family's devices are the IR-X, the ReVolt, the V4 and the X88-IR: "
                                                                  + devices.joinIntoString (", "));
        check (folders.isEmpty(), "...and no folder of the factory's, OLD or any other: " + folders.joinIntoString (", "));
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
