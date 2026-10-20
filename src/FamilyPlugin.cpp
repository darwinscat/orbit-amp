// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE FAMILY'S PLUG-IN ENTRY — in the family's one VST3 and one CLAP alone (CMakeLists.txt: THE FAMILY'S
// PLUG-INS), which hold every amp of the release. The wrappers list the amps of FamilyPluginList.h, each
// under its own ids, and ask for a processor of the entry a host opened; FamilyProducts.h says which Product
// each entry is. Both are written by the build, from installer/family.ini's `amps=` and each amp's row in
// THE AMPS. No amp's own entry is compiled in here (ORBITAMP_PLUGIN_ENTRY): this one makes them all.

#include "PluginProcessor.h"

#include JUCE_PLUGIN_LIST_HEADER
#include "FamilyProducts.h"

#include <cstring>
#include <iterator>

static_assert (std::size (jucePluginList) == std::size (orbitamp::family::plugins),
               "the family's plug-in list and its products are written together, one for one");

juce::AudioProcessor* createPluginFilter (const juce::PluginListEntry& plugin)
{
    for (const auto& entry : orbitamp::family::plugins)
    {
        if (std::strcmp (entry.code, plugin.code) == 0)
        {
            const auto& product = entry.product();
            jassert (std::strcmp (product.name, plugin.name) == 0);   // the entry is named as its Product
            return new orbitamp::AmpProcessor (product);
        }
    }

    jassertfalse;   // an entry of the list with no Product: the build writes both, so never
    return nullptr;
}
