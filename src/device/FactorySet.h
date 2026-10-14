// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace orbitamp::device
{

/** A BUILD'S OWN SET — its packs, and the cockpit's cabinets — IS INSIDE THE BINARY, every format of it
    on every system, and is laid out as files the first time the amp starts. A bundle to carry them in
    is the Mac's alone: a Windows standalone and a CLAP are single files, and a set that rode beside
    the binary would need a layout per format and an installer to put it there. Inside the binary there
    is one way, and it is the same one everywhere.

    Laid out rather than played from memory because the whole library reads files: a pack is a path to
    a zip, a cabinet is a file, and what the face offers to save is that file.

    THE FOLDER IS THE FAMILY'S, NOT ONE PRODUCT'S: `Darwin's Cat/Factory`, beside every product's own
    folder (DeviceLibrary::factoryDirectory), packs at its top and cabinets in `IRs`. Every product lays
    out what IT carries there, so a pack two of them carry is one file, and every product's list shows
    every pack the family has laid out. What a product carries, and under which names, is the product's
    to say — OrbitAmpSet.h for OrbitAmp, channels/Set.h for the cockpit; how it lands in the folder
    is said once, here.

    EACH PRODUCT KEEPS ITS OWN LIST there, `sets/<product>.txt`: what it laid out, one path to the line,
    under the BUILD it was laid out by. The lists are what makes the folder safe to share — a file one
    product no longer carries may be one another still plays.

    THE NEWEST BUILD WINS a file two products carry. A pack says nothing of its own version, and one
    retrained keeps its `rig_id`; what can be compared is when each product was built. A product whose
    copy of a file differs from the one in the folder writes it over only when it is newer than every
    other product that names the file — an older one leaves it, and plays the newer file. Without that,
    two products holding two versions of one file would each write theirs back on every start. */
struct FactorySet
{
    /** One file of a set: where it lies, as a path inside the factory folder, and the bytes it holds. */
    struct Entry
    {
        juce::String path;
        const char*  data = nullptr;
        size_t       size = 0;
    };

    /** Makes `factory` hold `product`'s set, as built at `build` (a later build is a bigger number —
        version::kBuildNumber): a file that is missing is written, and one that differs is written over
        when no other product's list names it under a build as new as this one or newer. One an EARLIER
        build of the product laid here that this one no longer carries is removed — a pack or a cabinet
        left behind would go on being offered — unless another product's list names it too: then it is
        that product's still. What is removed is only what the product's own list says it put there:
        nothing else in the folder is its to touch. Returns how many files it wrote or removed, which on
        every start after the first is none. */
    static int layOut (const juce::File& factory, const juce::String& product, juce::int64 build,
                       const std::vector<Entry>& set);

    /** What `product`'s own list in `factory` names: the paths, inside the folder, of what it laid there. */
    static juce::StringArray listOf (const juce::File& factory, const juce::String& product);

    /** EMPTIES THE FOLDER A BUILD OF BEFORE laid its set out in — the product's own `Factory`, from before
        the family shared one — once: exactly the files its `set.txt` names, then any folder that leaves
        empty, then the list, then the folder itself if nothing else is in it. A file the list does not
        name stays, and the folder with it. Returns how many files it removed: with no list there, none. */
    static int retire (const juce::File& former);
};

} // namespace orbitamp::device
