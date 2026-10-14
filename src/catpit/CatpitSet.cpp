// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSet.h"

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

int layOut (const juce::File& factory)
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

    return device::FactorySet::layOut (factory, carried);
}
} // namespace orbitamp::catpit::set
