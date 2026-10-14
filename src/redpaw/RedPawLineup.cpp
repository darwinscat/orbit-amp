// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawLineup.h"

#include "../channels/Engine.h"
#include "../channels/Set.h"
#include "../device/FactorySet.h"

#include <OrbitAmpVersion.h>

namespace orbitamp::redpaw
{

const channels::Lineup& lineup()
{
    // A channel's pack is the capture it plays, known by the `rig_id` its manifest carries. What each
    // opens on: a pedal's first channel a little of its room and as much delay, its TURBO twice as much
    // of both — the Copper in a hall, the Sheriff on a plate, the Kraken in an ambience with no delay
    // at all. The Kraken's two are the hot packs, the ones that would carry FAT.
    static const channels::Lineup rover {
        "redpaw", "redpaw_channel", "redpaw_input_zero", "dark-matter",
        {
            { "Copper",      "v4-copper-4ae0",      "Hall",     15.0f, 15.0f, false },
            { "Copper TB",   "v4-copper-tb-5841",   "Hall",     30.0f, 30.0f, false },
            { "Sheriff Ch1", "v4-sheriff-ch1-82ab", "Plate",    15.0f, 15.0f, false },
            { "Sheriff Ch2", "v4-sheriff-ch2-2f2f", "Plate",    30.0f, 30.0f, false },
            { "Kraken Ch1",  "v4-kraken-ch1-f7fc",  "Ambience", 15.0f, 0.0f,  true  },
            { "Kraken Ch2",  "v4-kraken-ch2-5f71",  "Ambience", 30.0f, 0.0f,  true  },
        },
        "redpaw_park"       // PARK: the lever's fourth position, the whole amp out of the sound
    };
    return rover;
}

} // namespace orbitamp::redpaw

namespace orbitamp
{

// The rover's half of the two things a product gives the processor beside its face (PluginProcessor.h).
void addProductParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    channels::Engine::addParameters (layout, redpaw::lineup());
}

std::unique_ptr<ProductEngine> makeProductEngine (AmpProcessor& p)
{
    return std::make_unique<channels::Engine> (p, redpaw::lineup());
}

/** The rover's set is in its binary (channels/Set.h), laid out in the family's Factory folder once in a
    process, as the build this is. The rover is new: there is no folder of its own from before to empty. */
void layOutFactorySet()
{
    static const int laid = channels::set::layOut (device::DeviceLibrary::factoryDirectory(), version::kBuildNumber);
    juce::ignoreUnused (laid);
}

} // namespace orbitamp
