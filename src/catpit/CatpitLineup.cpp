// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitLineup.h"
#include "CatpitEditor.h"

#include "../channels/Engine.h"

namespace orbitamp::catpit
{

const channels::Lineup& lineup()
{
    // A channel's pack is the capture it plays, known by the `rig_id` its manifest carries: the X88-IR's
    // own four channels — Normal for Clean, Altered for Edge, Crunch for Crunch, Overdrive for Hi-Gain.
    // In the channels' order, what each opens on: the two clean ones stand in a hall with a little
    // delay behind them; the two driven ones have each a room of their own, and no delay. FAT is a
    // layer only the two hottest packs carry.
    static const channels::Lineup cockpit {
        "catpit", "catpit_channel", "dark-matter",
        {
            { "Clean",   "x88-ir-normal-bae8",    "Hall",     {}, 15.0f, false },
            { "Edge",    "x88-ir-altered-c14c",   "Hall",     {}, 15.0f, false },
            { "Crunch",  "x88-ir-crunch-2abb",    "Plate",    {}, 0.0f,  true  },
            { "Hi-Gain", "x88-ir-overdrive-0344", "Ambience", {}, 0.0f,  true  },
        }
    };
    return cockpit;
}

} // namespace orbitamp::catpit

namespace orbitamp
{

namespace
{
    // The cockpit's half of the two things a product gives the processor beside its face (Product.h).
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, catpit::lineup());
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, catpit::lineup());
    }

    /** The cockpit's first start in a process: the player's IRs it kept in its own folder are gathered
        into the family's (gatherFormerIrs). Its set is the family's factory folder's, laid out by the
        installer. */
    void firstStart (const Product& cockpit)
    {
        gatherFormerIrs (cockpit);
    }
}

/** CATPITAMP (Product.h): the cockpit — its face (CatpitEditor.h) over its four channels. */
const Product& products::catpitAmp()
{
    static const Product cockpit { .name = "CatpitAmp", .makeEditor = catpit::makeEditor, .firstStart = firstStart,
                                   .addParameters = addParameters, .makeEngine = makeEngine,
                                   .formerInputZeroId = "catpit_input_zero" };
    return cockpit;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in the cockpit's own plug-in alone (CMakeLists.txt: add_amp): a host that opens it
// gets a processor of the cockpit. A gate, or a binary that plays several amps, makes its own.
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::catpitAmp());
}
#endif

