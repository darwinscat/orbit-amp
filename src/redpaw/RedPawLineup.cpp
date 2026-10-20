// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawLineup.h"
#include "RedPawSkin.h"

#include "../channels/Engine.h"

namespace orbitamp::redpaw
{

channels::Lineup roverLineup (std::vector<channels::Lineup::Channel> channels)
{
    jassert (! channels.empty() && channels.size() % 2 == 0 && channels.size() <= (size_t) maxGears * 2);
    return { "rover", "rover_channel", "dark-matter",
             std::move (channels),
             "rover_park",      // PARK: the lever's top end, the whole amp out of the sound
             true };            // a gear and its TURBO: one memory
}

int numGears (const channels::Lineup& lineup) noexcept
{
    return lineup.count() / 2;
}

namespace
{
    // The rover's half of the two things a product gives the processor beside its face (Product.h): over
    // its skin's channels.
    void addParameters (const Product& p, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, skinOf (p).lineup);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, skinOf (p.product()).lineup);
    }

    /** The rover's first start in a process: the player's IRs it kept in its own folder are gathered into
        the family's (gatherFormerIrs). Its set is the family's factory folder's, laid out by the installer. */
    void firstStart (const Product& p)
    {
        gatherFormerIrs (p);
    }
}

Product rover (const Skin& skin)
{
    return { skin.product, makeEditor, firstStart, addParameters, makeEngine, &skin, "rover_input_zero" };
}

} // namespace orbitamp::redpaw

