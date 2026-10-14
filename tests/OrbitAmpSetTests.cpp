// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITAMP'S SET AS IT LEAVES THE BINARY (OrbitAmpSet.h): that the X88-IR's four packs are
// laid out byte for byte what was built in; that each lies under the name of the capture it holds;
// that the preamp list shows four devices by four names, not one name four times; and that a start
// after the first writes nothing. How a set is laid out — what is rewritten, what an earlier build
// left that goes — is the cockpit's gate's to hold (CatpitSetTests): it is the same code.
//
// It works in a folder of its own under the system's temporary folder — the machine's real Factory
// and Devices folders are never read or written — and leaves it behind to be looked at.

#include "OrbitAmpSet.h"
#include "device/DeviceLibrary.h"

#include <cstdio>
#include <cstring>

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

    // The real folders' own shape: an apostrophe and a space on the way down.
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("orbitamp-set-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()))
                          .getChildFile ("Darwin's Cat").getChildFile ("OrbitAmp");
    const auto factory = root.getChildFile ("Factory");
    const auto own     = root.getChildFile ("Devices");
    own.createDirectory();
    std::printf ("in %s\n", root.getFullPathName().toRawUTF8());

    const auto carried = set::carried();
    check (carried.size() == std::size (set::names), "the binary carries the X88-IR's four packs, "
                                                     + juce::String ((int) carried.size()) + " of them");

    // The first start.
    check (FactorySet::layOut (factory, carried) == (int) carried.size(), "the first start writes every one of them");

    bool whole = true;
    for (const auto& e : carried)
    {
        juce::MemoryBlock there;
        whole = whole && factory.getChildFile (e.path).loadFileAsData (there) && there.getSize() == e.size
                      && std::memcmp (there.getData(), e.data, e.size) == 0;
    }
    check (whole, "...each of them byte for byte what was built in");

    // A name is a promise about the capture: "X88-IR Crunch" is the pack whose rig_id is x88-ir-crunch-….
    juce::StringArray astray;
    for (const auto& n : set::names)
    {
        const auto file = factory.getChildFile (n.laidAs);
        const auto stem = file.getFileName().upToFirstOccurrenceOf (".", false, false);
        const auto rig  = DeviceLibrary::rigIdOf (file);
        if (! rig.startsWith (stem.toLowerCase().replaceCharacter (' ', '-') + "-"))
            astray.add (stem + " holds " + (rig.isEmpty() ? juce::String ("no capture") : rig));
    }
    check (astray.isEmpty(), "each lies under the name of the capture it holds" + (astray.isEmpty() ? juce::String() : ": " + astray.joinIntoString (", ")));

    // What the preamp's list offers, read from these two folders as the amp reads its own.
    juce::StringArray listed;
    for (const auto& p : DeviceLibrary::scan (DeviceLibrary::Slot::preamp, true, factory, own))
        if (p.bundled)
            listed.add (p.displayName());

    juce::StringArray wanted;
    for (const auto& n : set::names)
        wanted.add (juce::String (n.laidAs).upToFirstOccurrenceOf (".", false, false));

    bool allListed = listed.size() == wanted.size();
    for (const auto& w : wanted)
        allListed = allListed && listed.contains (w);
    check (allListed, "the preamp list shows four devices by four names: " + listed.joinIntoString (", "));

    // Every start after it.
    check (FactorySet::layOut (factory, carried) == 0, "the second start writes nothing");

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
