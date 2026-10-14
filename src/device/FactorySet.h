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

    THE FOLDER IS THE AMP'S, NOT THE PLAYER'S: `Factory` beside the player's own `Devices` and `IRs`
    (DeviceLibrary::factoryDirectory), where the device library looks for the packs the build carries.
    What a product carries, and under which names, is the product's to say — OrbitAmpSet.h for
    OrbitAmp, catpit/CatpitSet.h for the cockpit; how it lands in the folder is said once, here. */
struct FactorySet
{
    /** One file of a set: where it lies, as a path inside the factory folder, and the bytes it holds. */
    struct Entry
    {
        juce::String path;
        const char*  data = nullptr;
        size_t       size = 0;
    };

    /** Makes `factory` hold `set`, whatever it held before: a file that is missing or differs is
        written, and one an EARLIER build laid here that this one no longer carries is removed — a pack
        or a cabinet left behind would go on being offered. What is removed is only what the folder's
        own list says a build put there: nothing else in it is the build's to touch. Returns how many
        files it wrote or removed, which on every start after the first is none. */
    static int layOut (const juce::File& factory, const std::vector<Entry>& set);
};

} // namespace orbitamp::device
