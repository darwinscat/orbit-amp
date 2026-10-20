// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE FAMILY'S FACTORY FOLDER WITH MORE THAN ONE PRODUCT READING IT (device/FactorySet.h). Two
// products of make-believe, and OrbitAmp, read one folder the installer could have laid out:
//   - a product's list is read as the build writes it — one path to the line — and as builds before
//     wrote it, a `#` line above the paths;
//   - each product's device list shows the packs its own list names and none of the other's, a pack both
//     name is in both, and a pack no list names is in neither;
//   - each product's cabinet shelf holds the IRs its own list names, in the catalog's places; an IR its
//     list names that is missing, or empty, is a place held empty — as is every IR in a folder that is
//     not there at all.
//
// It works in a folder of its own under the system's temporary folder, and leaves it behind to be looked at.

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

    // The real folder's own shape: an apostrophe and a space on the way down.
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("orbitamp-factory-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()))
                          .getChildFile ("Darwin's Cat");
    const auto factory = root.getChildFile ("Factory");
    const auto own     = root.getChildFile ("Devices");
    own.createDirectory();
    std::printf ("in %s\n", factory.getFullPathName().toRawUTF8());

    // THE PACKS: three of the tree's set, each as a folder at the factory's top, the way a pack is read.
    const juce::File set (ORBITAMP_SET_DIR);
    for (const auto* pack : { "IR-X Ch1 PowerAmp", "IR-X Ch2 PowerAmp", "X88-IR Crunch" })
        set.getChildFile ("Devices").getChildFile (juce::String (pack) + ".orbitrig")
           .copyDirectoryTo (factory.getChildFile (juce::String (pack) + ".orbitrig"));

    // THE CABINETS: Darwin's Cat's first three of the catalog's, as a list names them — bytes that are
    // some cabinet's, for the shelf only reads them.
    juce::StringArray irs;
    for (const auto& ir : catalog::irs)
        if (juce::String (ir.folder) == device::shelf::ours && irs.size() < 3)
            irs.add (device::shelf::pathOf (ir.folder, juce::String::fromUTF8 (ir.file)));
    check (irs.size() == 3, "the catalog has three of Darwin's Cat's own to lay out: " + irs.joinIntoString (", "));
    factory.getChildFile (irs[0]).create();
    factory.getChildFile (irs[0]).replaceWithText ("the first cabinet's bytes");
    factory.getChildFile (irs[1]).create();      // ...an empty file, as a copy cut short leaves one; irs[2] is not there

    // THE LISTS, as the build writes them — and the second as builds before wrote one, with a line above.
    const auto writeList = [&factory] (const juce::String& product, const juce::String& text)
    {
        const auto list = factory.getChildFile (FactorySet::listsName).getChildFile (product + ".txt");
        list.create();
        list.replaceWithText (text);
    };
    writeList ("FirstAmp",  "IR-X Ch1 PowerAmp.orbitrig\nX88-IR Crunch.orbitrig\n" + irs.joinIntoString ("\n") + "\n");
    writeList ("SecondAmp", "#build 20261014120000\n\nIR-X Ch2 PowerAmp.orbitrig\nX88-IR Crunch.orbitrig\n" + irs[0] + "\n");

    check (FactorySet::listOf (factory, "FirstAmp").size() == 5, "the first product's list names its five files");
    check (FactorySet::listOf (factory, "SecondAmp").joinIntoString ("|") == "IR-X Ch2 PowerAmp.orbitrig|X88-IR Crunch.orbitrig|" + irs[0],
           "...the second's its three, a `#` line and an empty one passed over");
    check (FactorySet::listOf (factory, "ThirdAmp").isEmpty(), "...and a product with no list there names nothing");

    // WHAT EACH PRODUCT'S DEVICE LIST SHOWS of the folder.
    const auto shown = [&factory, &own] (const juce::String& product)
    {
        juce::StringArray names;
        for (const auto& p : DeviceLibrary::scan (DeviceLibrary::Slot::any, true, factory, own, FactorySet::listOf (factory, product)))
            if (p.bundled)
                names.add (p.displayName());
        names.sort (false);
        return names.joinIntoString (", ");
    };
    check (shown ("FirstAmp") == "IR-X Ch1 PowerAmp, X88-IR Crunch", "the first product sees what its list names: " + shown ("FirstAmp"));
    check (shown ("SecondAmp") == "IR-X Ch2 PowerAmp, X88-IR Crunch", "...the second, its own, and the pack both name: " + shown ("SecondAmp"));
    check (shown ("ThirdAmp").isEmpty(), "...and a product with no list there sees none of them: " + shown ("ThirdAmp"));

    // ...and the same through the device list's own reading, by THIS product's list (OrbitAmp's, here).
    writeList ("OrbitAmp", "IR-X Ch1 PowerAmp.orbitrig\n");
    {
        juce::StringArray names;
        for (const auto& p : DeviceLibrary::scan ("OrbitAmp", DeviceLibrary::Slot::any, true, factory, own))
            if (p.bundled)
                names.add (p.displayName());
        check (names.joinIntoString (", ") == "IR-X Ch1 PowerAmp", juce::String ("OrbitAmp") + "'s device list reads its own list: " + names.joinIntoString (", "));
    }

    // THE CABINET SHELF each product holds: every place of the catalog, the ones its list names filled
    // where the file is there and holds something.
    const auto filled = [&factory] (const juce::String& product)
    {
        juce::StringArray paths;
        const auto shelf = device::shelf::read (factory, product);
        for (const auto& ir : shelf)
            if (ir.data != nullptr)
                paths.add (device::shelf::pathOf (ir) + "=" + juce::String (ir.size));
        return std::make_pair ((int) shelf.size(), paths.joinIntoString (", "));
    };
    const int places = (int) std::size (catalog::irs);
    const auto first = filled ("FirstAmp");
    const auto second = filled ("SecondAmp");
    const auto third = filled ("ThirdAmp");
    const auto bytes = juce::String (juce::String ("the first cabinet's bytes").getNumBytesAsUTF8());
    check (first.first == places && first.second == irs[0] + "=" + bytes,
           "the first product's shelf holds the one of its three that is there with something in it: " + first.second);
    check (second.first == places && second.second == irs[0] + "=" + bytes, "...the second's, the same one: " + second.second);
    check (third.first == places && third.second.isEmpty(), "...and a product with no list there holds every place empty");

    const auto none = device::shelf::read (root.getChildFile ("Nowhere"), "FirstAmp");
    bool empty = (int) none.size() == places;
    for (const auto& ir : none)
        empty = empty && ir.data == nullptr && ir.size == 0 && ir.file.isEmpty() && ir.folder != nullptr;
    check (empty, "with no folder at all, every place is held empty, each in its folder of the shelf");

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
