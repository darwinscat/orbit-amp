// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for AN AMP'S SET AS THE BUILD LAYS IT OUT in the factory folder (CMakeLists.txt: THE FACTORY
// FOLDER), under the amp's own list there (channels/Set.h): that the list names the packs the product's
// channels play (GATE_CHANNELS of them, as the build says) and nothing else among the packs, Darwin's
// Cat's own ten cabinets and none of the shelf's Other, and the face's own pictures in its own folder;
// that every file it names is there; and that the amp's cabinets are its own, whatever another product
// lays out beside them. Every amp over the channels' engine runs it as itself — CatpitAmp, RedPawAmp,
// FrostPawAmp, AmberPawAmp (CMakeLists.txt: amp_set_test), each as AmpGate.h knows it.
//
// It reads the build's own <build>/Factory (gate_reads_factory) — never the machine's — and, for the
// product beside it, a folder of its own under the system's temporary folder, which it leaves behind.

#include "AmpGate.h"
#include "Product.h"
#include "channels/Set.h"
#include "device/DeviceLibrary.h"
#include "device/FactorySet.h"
#include "device/FactoryShelf.h"

#include <cstdio>

int main()
{
    using namespace orbitamp::channels;
    using orbitamp::device::FactorySet;

    const auto& product = gate::product();
    const juce::String amp (product.amp);
    const juce::String name (orbitamp::product::name);
    gate::Checks check;

    const auto factory = FactorySet::location();
    const auto list = FactorySet::listOf (factory, name);
    std::printf ("reading %s\n", factory.getFullPathName().toRawUTF8());
    check (list.size() > 0, "the " + amp + " has a list in the build's factory folder, " + juce::String (list.size()) + " files");

    juce::StringArray packs, cabinets, pictures, astray, missing;
    const juce::String faces ("Faces/" + name + "/");
    for (const auto& path : list)
    {
        if (path.endsWithIgnoreCase (".orbitrig.zip") && ! path.containsChar ('/'))
            packs.add (path);
        else if (path.startsWith (juce::String (orbitamp::device::shelf::folderName) + "/"))
            cabinets.add (path);
        else if (path.startsWith (faces))
            pictures.add (path);
        else
            astray.add (path);

        if (factory.getChildFile (path).getSize() <= 0)
            missing.add (path);
    }
    check (astray.isEmpty(), "the list names packs, cabinets and the " + amp + "'s own pictures, nothing else"
                                 + (astray.isEmpty() ? juce::String() : ": " + astray.joinIntoString (", ")));
    check (missing.isEmpty(), "every file it names is there, with something in it"
                                  + (missing.isEmpty() ? juce::String() : ": " + missing.joinIntoString (", ")));

    // THE CHANNELS' PACKS, each by the capture it holds, and nothing else among the packs: one for every
    // capture the channels the build plays hold — two channels may play one, as a rover's gear and its
    // TURBO do on the IR-X (AmpGate.h).
    {
        juce::StringArray wanted;
        for (const auto& c : gate::played())
            wanted.addIfNotAlreadyThere (c.capture);
        juce::StringArray found;
        for (const auto& p : packs)
            found.add (orbitamp::device::DeviceLibrary::rigIdOf (factory.getChildFile (p)));
        found.sort (false);
        wanted.sort (false);
        check (found == wanted, "the " + juce::String (packs.size()) + " packs are the " + juce::String (GATE_CHANNELS)
                                    + " channels' captures: " + found.joinIntoString (" "));
    }

    // THE FAMILY'S SHELF, Darwin's Cat's own ten in the Orange's folder, none in Other — those are
    // OrbitAmp's — and the ones the amp steps through are those ten.
    {
        int orange = 0, other = 0;
        for (const auto& c : cabinets)
            (c.startsWith ("IRs/Orange PPC212V/") ? orange : other) += 1;
        check (orange == 10 && other == 0, "the cabinets are the Orange's ten, none in Other: " + juce::String (orange) + " + " + juce::String (other));
        check (cabinets.contains ("IRs/Orange PPC212V/02-dark-matter.wav"), "...DARK MATTER among them");

        const auto ownCabinets = set::ownCabinets (factory);
        bool mine = ownCabinets.size() == cabinets.size();
        for (const auto& c : cabinets)
            mine = mine && ownCabinets.contains (factory.getChildFile (c));
        check (mine, "the " + amp + "'s cabinets are the ones its list names, all " + juce::String (ownCabinets.size()) + " of them");
    }

    // THE FACE'S OWN PICTURES, in its own folder of the factory: what it opens on among them.
    {
        const auto first = juce::String (product.amp) == "cockpit" ? "cockpit.webp" : "dashboard.webp";
        check (pictures.contains (faces + first) && pictures.contains (faces + "midi.txt"),
               "its pictures lie in " + faces + ", " + juce::String (pictures.size()) + " of them, " + first + " and its MIDI sheet among them");
    }

    // ANOTHER PRODUCT IN THE SAME FOLDER, with a cabinet of its own beside the amp's, under a name that
    // would stand first among them: it is not one of the amp's. In a folder of its own, laid out as the
    // build's is.
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile (juce::String (product.name).toLowerCase() + "-set-test-"
                                             + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        const auto beside = root.getChildFile ("Darwin's Cat").getChildFile ("Factory");
        std::printf ("beside another product, in %s\n", beside.getFullPathName().toRawUTF8());

        const auto lay = [&beside] (const juce::String& path, const juce::String& text)
        {
            const auto file = beside.getChildFile (path);
            file.create();
            file.replaceWithText (text);
        };
        for (const auto& c : cabinets)
        {
            beside.getChildFile (c).getParentDirectory().createDirectory();
            factory.getChildFile (c).copyFileTo (beside.getChildFile (c));
        }
        lay (juce::String (FactorySet::listsName) + "/" + name + ".txt", list.joinIntoString ("\n") + "\n");
        lay ("IRs/Orange PPC212V/00-another.wav", "another product's cabinet");
        lay (juce::String (FactorySet::listsName) + "/AnotherAmp.txt", "IRs/Orange PPC212V/00-another.wav\n");
        const auto players = beside.getChildFile ("IRs/mine.wav");
        lay ("IRs/mine.wav", "the player's own");

        const auto ownCabinets = set::ownCabinets (beside);
        check (ownCabinets.size() == cabinets.size(), "beside another product's, the " + amp + "'s cabinets are still its "
                                                          + juce::String (cabinets.size()) + ": " + juce::String (ownCabinets.size()));
        check (! ownCabinets.contains (beside.getChildFile ("IRs/Orange PPC212V/00-another.wav")), "...and not the other product's");
        check (! ownCabinets.contains (players), "...nor a file of the player's");
    }

    return check.finish();
}
