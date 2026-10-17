// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITAMP'S SET AS IT LEAVES THE BINARY (OrbitAmpSet.h): that it carries every pack of the
// set — the X88-IR's four, the Victory V4's six — laid out byte for byte what was built in; that each
// lies under the name of the capture it holds; that the preamp list shows each by a name of its own,
// not one name four times; and that a start after the first writes nothing. How a set is laid out —
// what is rewritten, what an earlier build left that goes — is the cockpit's gate's to hold
// (CatpitSetTests): it is the same code.
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

    // The real folders' own shape: an apostrophe and a space on the way down, the family's Factory
    // beside OrbitAmp's own folder.
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("orbitamp-set-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()))
                          .getChildFile ("Darwin's Cat");
    const auto factory = root.getChildFile ("Factory");
    const auto own     = root.getChildFile (product::name).getChildFile ("Devices");
    own.createDirectory();
    std::printf ("in %s\n", root.getFullPathName().toRawUTF8());

    // What OrbitAmp ships, by the names its list shows them by: a pack that went missing from the tree
    // would otherwise go missing from the product without a word.
    const juce::StringArray wanted { "X88-IR Normal", "X88-IR Altered", "X88-IR Crunch", "X88-IR Overdrive",
                                     "V4 Copper", "V4 Copper TB", "V4 Sheriff Ch1", "V4 Sheriff Ch2",
                                     "V4 Kraken Ch1", "V4 Kraken Ch2" };

    const auto carried = set::carried();
    const juce::int64 build = 20261014120000;     // the build the gate plays: a stamp of make-believe
    juce::StringArray names;
    for (const auto& e : carried)
        names.add (e.path.upToFirstOccurrenceOf (".", false, false));

    bool everyOne = carried.size() == (size_t) wanted.size();
    for (const auto& w : wanted)
        everyOne = everyOne && names.contains (w);
    check (everyOne, "the binary carries every pack of the set — the X88-IR's four, the V4's six: "
                         + names.joinIntoString (", "));

    // The first start.
    check (FactorySet::layOut (factory, product::name, build, carried) == (int) carried.size(), "the first start writes every one of them");

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
    for (const auto& e : carried)
    {
        const auto file = factory.getChildFile (e.path);
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

    bool allListed = listed.size() == wanted.size();
    for (const auto& w : wanted)
        allListed = allListed && listed.contains (w);
    check (allListed, "the preamp list shows each by a name of its own: " + listed.joinIntoString (", "));

    // Every start after it.
    check (FactorySet::layOut (factory, product::name, build, carried) == 0, "the second start writes nothing");

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
