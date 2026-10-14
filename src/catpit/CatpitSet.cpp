// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSet.h"

#include "../Product.h"
#include "../device/FactorySet.h"

#if CATPIT_SET_EMBEDDED
 #include <CatpitSetData.h>
#endif

namespace orbitamp::catpit::set
{
namespace
{
    constexpr const char* cabinetsName = "IRs";
}

juce::File cabinetsIn (const juce::File& factory)
{
    return factory.getChildFile (cabinetsName);
}

juce::Array<juce::File> ownCabinets (const juce::File& factory)
{
    juce::Array<juce::File> own;

    for (const auto& path : device::FactorySet::listOf (factory, product::name))
        if (path.startsWith (juce::String (cabinetsName) + "/"))
            if (const auto file = factory.getChildFile (path); file.isAChildOf (cabinetsIn (factory)) && file.existsAsFile())
                own.add (file);

    return own;
}

int layOut (const juce::File& factory, juce::int64 build)
{
    std::vector<device::FactorySet::Entry> carried;

   #if CATPIT_SET_EMBEDDED
    for (int i = 0; i < CatpitSetData::namedResourceListSize; ++i)
    {
        const auto name = juce::String::fromUTF8 (CatpitSetData::originalFilenames[i]);
        const bool pack    = name.endsWithIgnoreCase (".orbitrig.zip");
        const bool cabinet = name.endsWithIgnoreCase (".wav") || name.endsWithIgnoreCase (".aif") || name.endsWithIgnoreCase (".aiff");
        if (! pack && ! cabinet)
            continue;

        int size = 0;
        const char* data = CatpitSetData::getNamedResource (CatpitSetData::namedResourceList[i], size);
        if (data == nullptr)
            continue;

        carried.push_back ({ pack ? name : juce::String (cabinetsName) + "/" + name, data, (size_t) size });
    }
   #endif

    return device::FactorySet::layOut (factory, product::name, build, carried);
}
} // namespace orbitamp::catpit::set
