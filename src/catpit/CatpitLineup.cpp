// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitLineup.h"

#include "../channels/Engine.h"
#include "../channels/Set.h"
#include "../device/FactorySet.h"

#include <OrbitAmpVersion.h>

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
        "catpit", "catpit_channel", "catpit_input_zero", "dark-matter",
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

// The cockpit's half of the two things a product gives the processor beside its face (PluginProcessor.h).
void addProductParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    channels::Engine::addParameters (layout, catpit::lineup());
}

std::unique_ptr<ProductEngine> makeProductEngine (AmpProcessor& p)
{
    return std::make_unique<channels::Engine> (p, catpit::lineup());
}

/** The cockpit's set is in its binary (channels/Set.h). Once in a process, however many of the amp it
    holds: the first one built lays it out, and the folder is the same for all of them — as the build
    this is, so that of two products carrying one file the newer build's stands. The cockpit's own
    `Factory` of before is emptied with it, of what a build of before laid there, and the player's IRs
    it kept in its own folder are gathered into the family's (gatherFormerIrs). */
void layOutFactorySet()
{
    static const int laid = device::FactorySet::retire (device::DeviceLibrary::formerFactoryDirectory())
                          + channels::set::layOut (device::DeviceLibrary::factoryDirectory(), version::kBuildNumber);
    juce::ignoreUnused (laid);
    gatherFormerIrs();
}

} // namespace orbitamp
