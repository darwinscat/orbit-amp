// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveLineup.h"

#include "../PluginProcessor.h"
#include "../channels/Engine.h"

namespace orbitamp::redgroove
{

const channels::Lineup& lineup()
{
    // A channel's pack is the capture it plays, known by the `rig_id` its manifest carries. Until the bass's
    // captures are trained, TWO TEMPORARY PACKS, a guitar amp's clean and crunch, each with its Boost held
    // off, to play the bass's chain through (CMakeLists.txt: REDGROOVEBASS_SET). No room and no delay: the
    // bass's chain holds neither (params::bassChainHolds).
    static const channels::Lineup walker {
        "walker", "walker_channel", "red-giant",
        {
            { "B15", "revolt-clean-poweramp-bf62",  "Plate", std::nullopt, 0.0f, false, { "Boost", "off" } },
            { "SVT", "revolt-crunch-poweramp-73a7", "Plate", std::nullopt, 0.0f, false, { "Boost", "off" } },
        },
        false
    };
    return walker;
}

} // namespace orbitamp::redgroove

namespace orbitamp
{

namespace
{
    // The walker's half of the two things a product gives the processor beside its face (Product.h): CHANNEL
    // over its lineup, and the bass's own — COMP, BLEND, DI/CAB, its cuts and its effects.
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, redgroove::lineup());
        params::addBassParameters (layout);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, redgroove::lineup());
    }

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (redgroove::lineup(), o);
    }
}

/** REDGROOVEBASS (Product.h): the walker — its face (redgroove/RedGrooveEditor.h) over its two channels and
    the bass's chain (Chain::bass), its tuner down to a drop-A's A0. No first start: it has no player's files
    of before to gather. */
const Product& products::redGrooveBass()
{
    static const Product walker = []
    {
        Product p { "RedGrooveBass", redgroove::makeEditor };
        p.addParameters = addParameters;
        p.makeEngine    = makeEngine;
        p.opening       = opening;
        p.chain         = Chain::bass;
        p.tunerLowestHz = core::PitchTracker::bassLowestHz;
        return p;
    }();
    return walker;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in the walker's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::redGrooveBass());
}
#endif
