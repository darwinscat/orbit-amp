// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "device/FactorySet.h"
#include "device/FactoryShelf.h"

#include <OrbitAmpSetData.h>

/** ORBITAMP'S OWN SET: every pack in the tree's set (resources/set/Devices) — the X88-IR's four
    channels, the Friedman IR-X's two, the Victory V4's six — and the family's cabinet shelf
    (device/FactoryShelf.h), inside the binary and laid out in the family's `Factory` folder when it
    starts (device/FactorySet.h says why, and how). OrbitAmp is the player of the whole library, so what
    the set holds, it carries.

    Each pack lies under its own file's name, which is the name the device list shows it by: the
    manifest's own name is the voice's alias, and "X88-IR" for all four of one amp. The cabinets lie in
    the shelf's folders, for every product of the family to play; OrbitAmp itself plays its shelf from
    the binary, by the number its parameter keeps.

    THE V4'S SIX ARE PUT AWAY. OrbitAmp still carries them, but they are no longer among the family's
    devices — the family will play the V4 again from new captures, with the power amp inside. They lie
    in the factory's `OLD`, and the AMP's menu lists them there, one to a line, in its OLD submenu: the
    same submenu as a player's own `OLD`, when he keeps one, since the menu takes a folder by its name
    (DeviceLibrary::shelfOf). The faces that play them as channels find them there by the capture they
    hold, as they find everything. */
namespace orbitamp::set
{
/** The factory's folder for what OrbitAmp carries and no longer counts among the family's devices. */
inline constexpr const char* oldFolder = "OLD";

/** ...and what lies in it, by the name each pack lies under. */
inline const juce::StringArray old { "V4 Copper", "V4 Copper TB", "V4 Sheriff Ch1", "V4 Sheriff Ch2",
                                     "V4 Kraken Ch1", "V4 Kraken Ch2" };

/** What OrbitAmp carries: every pack the build took in, under its own name — at the top of the folder,
    or in `OLD` — and the shelf in its folders. */
inline std::vector<device::FactorySet::Entry> carried()
{
    std::vector<device::FactorySet::Entry> out;

    for (int i = 0; i < OrbitAmpSetData::namedResourceListSize; ++i)
    {
        const auto file = juce::String::fromUTF8 (OrbitAmpSetData::originalFilenames[i]);

        int size = 0;
        const char* data = OrbitAmpSetData::getNamedResource (OrbitAmpSetData::namedResourceList[i], size);

        if (data != nullptr && file.endsWithIgnoreCase (".orbitrig.zip"))
            out.push_back ({ old.contains (file.upToFirstOccurrenceOf (".", false, false))
                                 ? juce::String (oldFolder) + "/" + file : file,
                             data, (size_t) size });
    }

    for (auto& ir : device::shelf::carried())
        out.push_back (std::move (ir));

    return out;
}
} // namespace orbitamp::set
