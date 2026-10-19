// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "FactorySet.h"

#include <BinaryData.h>

/** Whether this build carries the shelf's `Other` — OrbitAmp does; the faces do not. Said by the data that
    holds those IRs (CMakeLists.txt: OrbitAmpShelfOther), which is linked only where it is said. */
#ifndef SHELF_CARRIES_OTHER
 #define SHELF_CARRIES_OTHER 0
#endif

#if SHELF_CARRIES_OTHER
 #include <ShelfOtherData.h>
#endif

#include <vector>

namespace orbitamp::device::shelf
{

/** THE FAMILY'S CABINET SHELF: every cabinet IR a Darwin's Cat amp carries — Darwin's Cat's own, shot
    off the Orange (resources/set/IRs.md says what they are), and the third-party ones OrbitAmp has always
    played (resources/ir). Inside the binary — Darwin's Cat's own in the data every product links
    (OrbitAmpData), the third-party ones in data OrbitAmp alone links (ShelfOtherData) — and laid out with
    the product's set in the family's `Factory/IRs` (FactorySet.h), so that a cabinet is one file on the
    machine however many of the family carry it.

    ON THE SHELF IN FOLDERS, one for each maker's cabinets — `Orange PPC212V` for Darwin's Cat's own and
    `Other` for the rest. OrbitAmp carries the whole shelf and lists it by them, a submenu each; the faces
    carry Darwin's Cat's own folder alone (`carried (ours)`), one list, and not a byte of `Other`: in a
    face its places are held with nothing in them (SHELF_CARRIES_OTHER). A cabinet joining the shelf joins
    a folder, or brings a new one.

    IN THE ORDER OF THE `cab_ir` PARAMETER (params::cabIrNames): what an OrbitAmp session keeps of its
    cabinet is an index into this list, so a cabinet joins it at the end and nothing already here ever
    moves — wherever its folder stands, and whether or not the build carries it. */

/** Where the shelf lies in a factory folder. */
inline constexpr const char* folderName = "IRs";

/** The shelf's folders. */
inline constexpr const char* ours  = "Orange PPC212V";
inline constexpr const char* other = "Other";

/** One IR of the shelf: the folder it lies in, its file's name as the tree has it, and its bytes — no
    bytes and no name for one the build does not carry. */
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

        // `Other`'s IRs where the build carries them, and their places, empty, where it does not.
       #if SHELF_CARRIES_OTHER
        #define ORBITAMP_SHELF_OTHER(ir) { other, ShelfOtherData::ir, ShelfOtherData::ir##Size }
       #else
        #define ORBITAMP_SHELF_OTHER(ir) { other, nullptr, 0 }
       #endif

        const Held held[] {
            // The Jesters Brutal Pack's fifteen, then the Jesters Emerald Pack's six.
            ORBITAMP_SHELF_OTHER (_01cookiemonster_wav),
            ORBITAMP_SHELF_OTHER (_02darthgenocider_wav),
            ORBITAMP_SHELF_OTHER (_03kittenslayer_wav),
            ORBITAMP_SHELF_OTHER (_04kaijutamer_wav),
            ORBITAMP_SHELF_OTHER (_05iceburnsuicide_wav),
            ORBITAMP_SHELF_OTHER (_06verticallipstabber_wav),
            ORBITAMP_SHELF_OTHER (_07manslaughterjoe_wav),
            ORBITAMP_SHELF_OTHER (_08bigbubba_wav),
            ORBITAMP_SHELF_OTHER (_09devilscunnilingus_wav),
            ORBITAMP_SHELF_OTHER (_10october32th_wav),
            ORBITAMP_SHELF_OTHER (_11wumbo_wav),
            ORBITAMP_SHELF_OTHER (_12worldcollider_wav),
            ORBITAMP_SHELF_OTHER (_13cannibalchoir_wav),
            ORBITAMP_SHELF_OTHER (_14cathoderayfleshburn_wav),
            ORBITAMP_SHELF_OTHER (_15impalerjim_wav),
            ORBITAMP_SHELF_OTHER (_16nachoguacamole_wav),
            ORBITAMP_SHELF_OTHER (_17picklepunisher_wav),
            ORBITAMP_SHELF_OTHER (_18wasabiwarrior_wav),
            ORBITAMP_SHELF_OTHER (_19pestopaladin_wav),
            ORBITAMP_SHELF_OTHER (_20donspinacio_wav),
            ORBITAMP_SHELF_OTHER (_21killdill_wav),

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
        #undef ORBITAMP_SHELF_OTHER

        // The file's name is the one the binary keeps beside the bytes: said once, in the tree — in
        // whichever of the two data holds them.
        const auto fileOf = [] (const char* data)
        {
            for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            {
                int size = 0;
                if (BinaryData::getNamedResource (BinaryData::namedResourceList[i], size) == data)
                    return juce::String::fromUTF8 (BinaryData::originalFilenames[i]);
            }
           #if SHELF_CARRIES_OTHER
            for (int i = 0; i < ShelfOtherData::namedResourceListSize; ++i)
            {
                int size = 0;
                if (ShelfOtherData::getNamedResource (ShelfOtherData::namedResourceList[i], size) == data)
                    return juce::String::fromUTF8 (ShelfOtherData::originalFilenames[i]);
            }
           #endif
            return juce::String();
        };

        std::vector<Ir> out;

        for (const auto& h : held)
            out.push_back ({ h.folder, h.data != nullptr ? fileOf (h.data) : juce::String(), h.data, h.size });

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
    what the build carries, or the one folder named. */
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
