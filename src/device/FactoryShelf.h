// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "FactorySet.h"
#include "../PerProduct.h"

#include <CabinetCatalog.h>   // written by CMake from resources/cabinets.txt

#include <memory>
#include <vector>

namespace orbitamp::device::shelf
{

/** THE FAMILY'S CABINET SHELF: every cabinet IR a Darwin's Cat amp plays — Darwin's Cat's own, shot off
    the Orange (resources/set/IRs.md says what they are), and the third-party ones OrbitAmp has always
    played (resources/ir). Not in any binary: in the family's factory folder, `IRs` (FactorySet.h), one
    file for every amp that plays it, read into memory once when the amp first asks for it.

    ON THE SHELF IN FOLDERS, one for each maker's cabinets — `Orange PPC212V` for Darwin's Cat's own and
    `Other` for the rest. OrbitAmp plays the whole shelf and lists it by them, a submenu each; a face plays
    Darwin's Cat's own folder alone, one list. Which a product plays is its own list's to say: an IR it
    does not name is a place held empty, with no bytes and no name, whatever the folder holds. A cabinet
    joining the shelf joins a folder, or brings a new one.

    IN THE ORDER OF THE `cab_ir` PARAMETER (params::cabIrNames): what an OrbitAmp session keeps of its
    cabinet is an index into this list, so a cabinet joins it at the end and nothing already here ever
    moves — wherever its folder stands, and whether or not the product plays it. The cabinet catalog
    (resources/cabinets.txt) says every IR of it once — its number, its folder, its name, its file — and
    CMake lays the files out and builds the catalog into the table read here. */

/** Where the shelf lies in a factory folder. */
inline constexpr const char* folderName = "IRs";

/** The shelf's folder every product plays — Darwin's Cat's own. */
inline constexpr const char* ours = catalog::ours;

/** One IR of the shelf: the folder it lies in, its file's name as the tree has it, and its bytes — no
    bytes and no name for one the product does not play, or that is not there to be read. */
struct Ir
{
    const char*  folder = nullptr;
    juce::String file;
    const char*  data = nullptr;
    int          size = 0;
    std::shared_ptr<const juce::MemoryBlock> held;   // what `data` points into, as long as the IR is kept
};

/** Where an IR of the catalog lies, as a path inside the factory folder: `IRs/<folder>/<file>`. */
inline juce::String pathOf (const char* folder, const juce::String& file)
{
    return juce::String (folderName) + "/" + folder + "/" + file;
}

/** ...and of an IR the shelf holds. */
inline juce::String pathOf (const Ir& ir)
{
    return pathOf (ir.folder, ir.file);
}

/** The shelf as `product` plays it from `factory`: every IR of the catalog, in the parameter's order —
    read from the folder where the product's list names it and the file is there with something in it,
    and held empty where not. A product whose folder is missing holds every place empty. */
inline std::vector<Ir> read (const juce::File& factory, const juce::String& product)
{
    const auto named = FactorySet::listOf (factory, product);
    std::vector<Ir> out;

    for (const auto& ir : catalog::irs)
    {
        const auto file = juce::String::fromUTF8 (ir.file);
        const auto path = pathOf (ir.folder, file);

        Ir one;
        one.folder = ir.folder;

        if (auto bytes = std::make_shared<juce::MemoryBlock>();
            named.contains (path) && factory.getChildFile (path).loadFileAsData (*bytes) && bytes->getSize() > 0)
        {
            one.file = file;
            one.data = static_cast<const char*> (bytes->getData());
            one.size = (int) bytes->getSize();
            one.held = std::move (bytes);
        }

        out.push_back (std::move (one));
    }

    return out;
}

/** The whole shelf a product plays, by its name, from the folder this build reads — read once in a
    process for each product. */
inline const std::vector<Ir>& irs (const juce::String& product)
{
    static PerProduct<const std::vector<Ir>> shelves;
    return shelves.of (product.toStdString(), [&product] { return read (FactorySet::location(), product); });
}


} // namespace orbitamp::device::shelf
