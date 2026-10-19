// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Set.h"

#include "../Product.h"
#include "../device/FactorySet.h"
#include "../device/FactoryShelf.h"

#if PRODUCT_SET_EMBEDDED
 #include <ProductSetData.h>
#endif

namespace orbitamp::channels::set
{
juce::File cabinetsIn (const juce::File& factory)
{
    return factory.getChildFile (device::shelf::folderName);
}

juce::Array<juce::File> ownCabinets (const juce::File& factory)
{
    juce::Array<juce::File> own;

    for (const auto& path : device::FactorySet::listOf (factory, product::name))
        if (path.startsWith (juce::String (device::shelf::folderName) + "/"))
            if (const auto file = factory.getChildFile (path); file.isAChildOf (cabinetsIn (factory)) && file.existsAsFile())
                own.add (file);

    return own;
}

int layOut (const juce::File& factory, juce::int64 build)
{
    // The family's shelf first — a face carries Darwin's Cat's own cabinets of it, OrbitAmp alone the
    // whole — and the product's own packs.
    auto carried = device::shelf::carried (device::shelf::ours);

   #if PRODUCT_SET_EMBEDDED
    for (int i = 0; i < ProductSetData::namedResourceListSize; ++i)
    {
        const auto name = juce::String::fromUTF8 (ProductSetData::originalFilenames[i]);
        if (! name.endsWithIgnoreCase (".orbitrig.zip"))
            continue;

        int size = 0;
        const char* data = ProductSetData::getNamedResource (ProductSetData::namedResourceList[i], size);
        if (data == nullptr)
            continue;

        carried.push_back ({ name, data, (size_t) size });
    }
   #endif

    return device::FactorySet::layOut (factory, product::name, build, carried);
}
} // namespace orbitamp::channels::set
