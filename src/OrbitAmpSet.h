// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "device/FactorySet.h"

#include <OrbitAmpSetData.h>

/** ORBITAMP'S OWN SET: the X88-IR's four channels as four preamp packs, inside the binary and laid out
    in OrbitAmp's `Factory` folder when it starts (device/FactorySet.h says why, and how).

    They are the very files the cockpit carries — resources/catpit/Devices, one copy of each in the
    tree — under other names. The cockpit names a pack after the channel that plays it; OrbitAmp's
    device list shows a pack by its FILE's name, and the manifest's own name is "X88-IR" for all four,
    so "Clean" and "Crunch" would stand in the list with nothing to say they are one amp. Here each
    goes by the X88-IR's own name for the channel it captured, which is also what its `rig_id` says. */
namespace orbitamp::set
{
/** A file the cockpit carries, and the name OrbitAmp lays it out under. */
struct Named
{
    const char* carried;
    const char* laidAs;
};

inline constexpr Named names[] {
    { "Clean.orbitrig.zip",   "X88-IR Normal.orbitrig.zip" },
    { "Edge.orbitrig.zip",    "X88-IR Altered.orbitrig.zip" },
    { "Crunch.orbitrig.zip",  "X88-IR Crunch.orbitrig.zip" },
    { "Hi-Gain.orbitrig.zip", "X88-IR Overdrive.orbitrig.zip" },
};

/** What OrbitAmp carries, under the names it lays them out by. A file the build carries that is not
    named above is not laid out: a pack under the cockpit's name is a pack nobody here can place. */
inline std::vector<device::FactorySet::Entry> carried()
{
    std::vector<device::FactorySet::Entry> out;

    for (int i = 0; i < OrbitAmpSetData::namedResourceListSize; ++i)
    {
        const auto file = juce::String::fromUTF8 (OrbitAmpSetData::originalFilenames[i]);

        int size = 0;
        const char* data = OrbitAmpSetData::getNamedResource (OrbitAmpSetData::namedResourceList[i], size);

        for (const auto& n : names)
            if (data != nullptr && file == n.carried)
                out.push_back ({ n.laidAs, data, (size_t) size });
    }

    return out;
}
} // namespace orbitamp::set
