// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "FactorySet.h"

#include <BinaryData.h>

#include <vector>

namespace orbitamp::device::shelf
{

/** THE FAMILY'S CABINET SHELF: every cabinet IR a Darwin's Cat amp carries — Darwin's Cat's own, shot
    off the Orange (resources/set/IRs.md says what they are), and the third-party ones OrbitAmp has always
    played (resources/ir). Inside the binary of each, in the data every product links (OrbitAmpData), and
    laid out with the product's set in the family's `Factory/IRs` (FactorySet.h), so that a cabinet is one
    file on the machine however many of the family carry it.

    ON THE SHELF IN FOLDERS, one for each maker's cabinets — `Orange PPC112` for Darwin's Cat's own and
    `Other` for the rest. OrbitAmp carries the whole shelf and lists it by them, a submenu each; the faces
    carry Darwin's Cat's own folder alone (`carried (ours)`), one list. A cabinet joining the shelf joins
    a folder, or brings a new one.

    IN THE ORDER OF THE `cab_ir` PARAMETER (params::cabIrNames): what an OrbitAmp session keeps of its
    cabinet is an index into this list, so a cabinet joins it at the end and nothing already here ever
    moves — wherever its folder stands. */

/** Where the shelf lies in a factory folder. */
inline constexpr const char* folderName = "IRs";

/** The shelf's folders. */
inline constexpr const char* ours  = "Orange PPC112";
inline constexpr const char* other = "Other";

/** One IR of the shelf: the folder it lies in, its file's name as the tree has it, and its bytes. */
struct Ir
{
    const char*  folder = nullptr;
    juce::String file;
    const char*  data = nullptr;
    int          size = 0;
};

/** The whole shelf, in the parameter's order. */
inline const std::vector<Ir>& irs()
{
    static const std::vector<Ir> shelf = []
    {
        struct Held { const char* folder; const char* data; int size; };

        const Held held[] {
            // The Jesters Brutal Pack's fifteen, then the Jesters Emerald Pack's six.
            { other, BinaryData::_01cookiemonster_wav,       BinaryData::_01cookiemonster_wavSize },
            { other, BinaryData::_02darthgenocider_wav,      BinaryData::_02darthgenocider_wavSize },
            { other, BinaryData::_03kittenslayer_wav,        BinaryData::_03kittenslayer_wavSize },
            { other, BinaryData::_04kaijutamer_wav,          BinaryData::_04kaijutamer_wavSize },
            { other, BinaryData::_05iceburnsuicide_wav,      BinaryData::_05iceburnsuicide_wavSize },
            { other, BinaryData::_06verticallipstabber_wav,  BinaryData::_06verticallipstabber_wavSize },
            { other, BinaryData::_07manslaughterjoe_wav,     BinaryData::_07manslaughterjoe_wavSize },
            { other, BinaryData::_08bigbubba_wav,            BinaryData::_08bigbubba_wavSize },
            { other, BinaryData::_09devilscunnilingus_wav,   BinaryData::_09devilscunnilingus_wavSize },
            { other, BinaryData::_10october32th_wav,         BinaryData::_10october32th_wavSize },
            { other, BinaryData::_11wumbo_wav,               BinaryData::_11wumbo_wavSize },
            { other, BinaryData::_12worldcollider_wav,       BinaryData::_12worldcollider_wavSize },
            { other, BinaryData::_13cannibalchoir_wav,       BinaryData::_13cannibalchoir_wavSize },
            { other, BinaryData::_14cathoderayfleshburn_wav, BinaryData::_14cathoderayfleshburn_wavSize },
            { other, BinaryData::_15impalerjim_wav,          BinaryData::_15impalerjim_wavSize },
            { other, BinaryData::_16nachoguacamole_wav,      BinaryData::_16nachoguacamole_wavSize },
            { other, BinaryData::_17picklepunisher_wav,      BinaryData::_17picklepunisher_wavSize },
            { other, BinaryData::_18wasabiwarrior_wav,       BinaryData::_18wasabiwarrior_wavSize },
            { other, BinaryData::_19pestopaladin_wav,        BinaryData::_19pestopaladin_wavSize },
            { other, BinaryData::_20donspinacio_wav,         BinaryData::_20donspinacio_wavSize },
            { other, BinaryData::_21killdill_wav,            BinaryData::_21killdill_wavSize },

            // Darwin's Cat's own, in the order of their files.
            { ours, BinaryData::_02darkmatter_wav,           BinaryData::_02darkmatter_wavSize },
            { ours, BinaryData::_03ioncavity_wav,            BinaryData::_03ioncavity_wavSize },
            { ours, BinaryData::_04eventhorizon_wav,         BinaryData::_04eventhorizon_wavSize },
            { ours, BinaryData::_06coronalflare_wav,         BinaryData::_06coronalflare_wavSize },
            { ours, BinaryData::_07browndwarf_wav,           BinaryData::_07browndwarf_wavSize },
            { ours, BinaryData::_08accretiongap_wav,         BinaryData::_08accretiongap_wavSize },
            { ours, BinaryData::_09dustcloud_wav,            BinaryData::_09dustcloud_wavSize },
            { ours, BinaryData::_10blueshift_wav,            BinaryData::_10blueshift_wavSize },
            { ours, BinaryData::_11redgiant_wav,             BinaryData::_11redgiant_wavSize },
            { ours, BinaryData::_12pulsarbeam_wav,           BinaryData::_12pulsarbeam_wavSize },
        };

        std::vector<Ir> out;

        for (const auto& h : held)
        {
            // The file's name is the one the binary keeps beside the bytes: said once, in the tree.
            juce::String file;
            for (int i = 0; i < BinaryData::namedResourceListSize && file.isEmpty(); ++i)
            {
                int size = 0;
                if (BinaryData::getNamedResource (BinaryData::namedResourceList[i], size) == h.data)
                    file = juce::String::fromUTF8 (BinaryData::originalFilenames[i]);
            }

            out.push_back ({ h.folder, file, h.data, h.size });
        }

        return out;
    }();

    return shelf;
}

/** Where an IR of the shelf lies, as a path inside the factory folder: `IRs/<folder>/<file>`. */
inline juce::String pathOf (const Ir& ir)
{
    return juce::String (folderName) + "/" + ir.folder + "/" + ir.file;
}

/** The shelf as a set's files, for a product to lay out with its own (FactorySet::layOut): the whole of
    it, or the one folder named. */
inline std::vector<FactorySet::Entry> carried (const char* onlyFolder = nullptr)
{
    std::vector<FactorySet::Entry> out;

    for (const auto& ir : irs())
        if (ir.data != nullptr && ir.file.isNotEmpty())
            if (onlyFolder == nullptr || juce::String (ir.folder) == onlyFolder)
                out.push_back ({ pathOf (ir), ir.data, (size_t) ir.size });

    return out;
}

} // namespace orbitamp::device::shelf
