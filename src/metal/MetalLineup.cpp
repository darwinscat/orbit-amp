// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "MetalLineup.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../channels/Engine.h"
#include "../channels/TopTwelve.h"

namespace orbitamp::metal
{

const channels::Lineup& lineup()
{
    // A STAND-IN SOUND, until the E530's own captures come: the released Victory V4 packs (CMakeLists.txt: METALAMP_SET),
    // a pack a channel — CLEAN GAIN LO the Copper, CLEAN GAIN HI the Sheriff, LEAD GAIN LO the Kraken, and LEAD GAIN HI
    // the Kraken again, opening on a hotter Gain (Lineup::Channel::drive). Every channel holds the V4's Boost OFF: the key
    // strip is the E530's BRIGHT and CONTOUR (MetalEditor.cpp), with no key for the Boost, so no hand can leave it on —
    // the stand-in plays the Boost-off captures alone (the Copper's 0005 to 0013, the Sheriff's and the Kraken's Ch1),
    // whatever a record, a preset or a host says. What each opens on is AmberPawAmp's for the same pack: all on a plate,
    // the cleaner the wetter, the Copper with 15 % of delay, the Sheriff with about half as much, the Kraken with none.
    //
    // WHAT THE CABIN HOLDS WHERE IT IS, as the bathyscaphe holds its own: COMP in the strip and on, three quarters of the
    // signal through it at 4:1 on OPTO, its fader the threshold alone (the parameters declaring it: Engine::opening reads
    // these into the Opening). The delay and the room are on, as on every channel amp (Engine::setUpChain), their faders,
    // FX 1 and REVERB, their amounts, 0 the dry. Its cabinets the guitar's TOP 12, drawn, as the rovers'.
    static const channels::Lineup e530 = []
    {
        auto line = channels::withTopTwelve ({
            "metal", "metal_channel", "red-giant",
            {
                { "Clean Gain Lo", "v4-copper-4ae0",      "Plate", 20.0f, 15.0f, false, { "Boost", "off" } },
                { "Clean Gain Hi", "v4-sheriff-ch1-82ab", "Plate", 15.0f, 8.0f,  false, { "Boost", "off" } },
                { "Lead Gain Lo",  "v4-kraken-ch1-f7fc",  "Plate", 10.0f, 0.0f,  true,  { "Boost", "off" } },
                { "Lead Gain Hi",  "v4-kraken-ch1-f7fc",  "Plate", 10.0f, 0.0f,  true,  { "Boost", "off" } },
            },
        });
        line.channels[3].drive = leadHiDrive;
        line.kept = { params::compThreshold };
        line.pinned = {
            { params::compPresent,   1.0f },
            { params::compOn,        1.0f },
            { params::compAmount,    0.75f },
            { params::compRatio,     4.0f },
            { params::compCharacter, (float) params::compCharacters.indexOf ("Opto") },
        };
        return line;
    }();
    return e530;
}

} // namespace orbitamp::metal

namespace orbitamp
{

namespace
{
    // The cabin's half of the two things a product gives the processor beside its face (Product.h): CHANNEL over its
    // lineup, and the guitar's COMP before the AMP, which its fader plays.
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, metal::lineup());
        params::Opening held;
        channels::Engine::opening (metal::lineup(), held);
        params::addGuitarCompParameters (layout, held);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, metal::lineup());
    }

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (metal::lineup(), o);
    }
}

/** METALAMP (Product.h), a working name: the ENGL E530 preamp's face in the metal world's cabin (metal/MetalEditor.h) over
    four channels on the guitar's chain, and a COMP before the AMP. No first start: it has no player's files of before to
    gather. */
const Product& products::metalAmp()
{
    static const Product e530 = []
    {
        Product p { "MetalAmp", metal::makeEditor };
        p.addParameters = addParameters;
        p.makeEngine    = makeEngine;
        p.opening       = opening;
        p.guitarComp    = true;
        return p;
    }();
    return e530;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in MetalAmp's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::metalAmp());
}
#endif
