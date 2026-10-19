// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "FactorySet.h"

#include <BinaryData.h>
#include <CabinetCatalog.h>   // written by CMake from resources/cabinets.txt

/** Whether this build carries the shelf's `Other` — the folders the cabinet catalog gives OrbitAmp alone.
    OrbitAmp does; the faces do not. Said by the data that holds those IRs (CMakeLists.txt:
    OrbitAmpShelfOther), which is linked only where it is said. */
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
    moves — wherever its folder stands, and whether or not the build carries it. The cabinet catalog
    (resources/cabinets.txt) says every IR of it once — its number, its folder, its name, its file — and
    CMake builds the files into the data and the catalog into the table read here. */

/** Where the shelf lies in a factory folder. */
inline constexpr const char* folderName = "IRs";

/** The shelf's folder every product carries — Darwin's Cat's own. */
inline constexpr const char* ours = catalog::ours;

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
        // An IR's bytes, found by its file's name in the data that holds its folder: Darwin's Cat's own
        // in the data every product links, the rest in `Other`'s, where the build carries it — and none
        // where it does not, so its place is held empty.
        const auto held = [] (const catalog::Ir& ir, int& size) -> const char*
        {
            const auto file = juce::String::fromUTF8 (ir.file);

            if (juce::String (ir.folder) == ours)
            {
                for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
                    if (juce::String::fromUTF8 (BinaryData::originalFilenames[i]) == file)
                        return BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                return nullptr;
            }

           #if SHELF_CARRIES_OTHER
            for (int i = 0; i < ShelfOtherData::namedResourceListSize; ++i)
                if (juce::String::fromUTF8 (ShelfOtherData::originalFilenames[i]) == file)
                    return ShelfOtherData::getNamedResource (ShelfOtherData::namedResourceList[i], size);
           #endif
            return nullptr;
        };

        std::vector<Ir> out;

        for (const auto& ir : catalog::irs)
        {
            int size = 0;
            const char* data = held (ir, size);

            if (data != nullptr)
                out.push_back ({ ir.folder, juce::String::fromUTF8 (ir.file), data, size });
            else
                out.push_back ({ ir.folder, juce::String(), nullptr, 0 });
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
