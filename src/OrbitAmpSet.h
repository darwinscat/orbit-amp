// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "device/FactorySet.h"
#include "device/FactoryShelf.h"

#include <OrbitAmpSetData.h>

/** ORBITAMP'S OWN SET: every pack in the tree's set (resources/set/Devices) — the X88-IR's four
    channels, the Victory V4's six — and the family's cabinet shelf (device/FactoryShelf.h), inside the
    binary and laid out in the family's `Factory` folder when it starts (device/FactorySet.h says why,
    and how). OrbitAmp is the player of the whole library, so what the set holds, it carries.

    Each pack lies under its own file's name, which is the name the device list shows it by: the
    manifest's own name is the voice's alias, and "X88-IR" for all four of one amp. The cabinets lie in
    the shelf's folders, for every product of the family to play; OrbitAmp itself plays its shelf from
    the binary, by the number its parameter keeps. */
namespace orbitamp::set
{
/** What OrbitAmp carries: every pack the build took in, at the top of the folder, under its own name,
    and the shelf in its folders. */
inline std::vector<device::FactorySet::Entry> carried()
{
    std::vector<device::FactorySet::Entry> out;

    for (int i = 0; i < OrbitAmpSetData::namedResourceListSize; ++i)
    {
        const auto file = juce::String::fromUTF8 (OrbitAmpSetData::originalFilenames[i]);

        int size = 0;
        const char* data = OrbitAmpSetData::getNamedResource (OrbitAmpSetData::namedResourceList[i], size);

        if (data != nullptr && file.endsWithIgnoreCase (".orbitrig.zip"))
            out.push_back ({ file, data, (size_t) size });
    }

    for (auto& ir : device::shelf::carried())
        out.push_back (std::move (ir));

    return out;
}
} // namespace orbitamp::set
