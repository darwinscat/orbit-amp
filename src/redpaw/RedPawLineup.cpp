// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawLineup.h"

#include "../channels/Engine.h"
#include "../channels/Set.h"
#include "../device/FactorySet.h"

#include <OrbitAmpVersion.h>

namespace orbitamp::redpaw
{

channels::Lineup roverLineup (std::vector<channels::Lineup::Channel> channels)
{
    jassert (! channels.empty() && channels.size() % 2 == 0 && channels.size() <= (size_t) maxGears * 2);
    return { "rover", "rover_channel", "rover_input_zero", "dark-matter",
             std::move (channels),
             "rover_park",      // PARK: the lever's top end, the whole amp out of the sound
             true };            // a gear and its TURBO: one memory
}

int numGears() noexcept
{
    return lineup().count() / 2;
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
