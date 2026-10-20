// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Set.h"

#include "../device/FactorySet.h"
#include "../device/FactoryShelf.h"

namespace orbitamp::channels::set
{
juce::File cabinetsIn (const juce::File& factory)
{
    return factory.getChildFile (device::shelf::folderName);
}

juce::Array<juce::File> ownCabinets (const juce::File& factory, const juce::String& product)
{
    juce::Array<juce::File> own;

    for (const auto& path : device::FactorySet::listOf (factory, product))

        if (path.startsWith (juce::String (device::shelf::folderName) + "/"))
            if (const auto file = factory.getChildFile (path); file.isAChildOf (cabinetsIn (factory)) && file.existsAsFile())
                own.add (file);

    return own;
}

} // namespace orbitamp::channels::set
