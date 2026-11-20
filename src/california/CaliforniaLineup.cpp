// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CaliforniaLineup.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../channels/Engine.h"
#include "../channels/TopTwelve.h"

namespace orbitamp::california
{

const channels::Lineup& lineup()
{
    // A STAND-IN SOUND, until the Orange Super Crush 100 has captures of its own (the library has its panels alone): CLEAN
    // the Victory V4 Copper and DIRTY the V4 Sheriff, each with its Boost held off — the van has no TURBO. Both on a plate,
    // the cleaner the wetter, as AmberPawAmp opens them (amberpaw/AmberPawSkin.cpp). Its cabinets the guitar's TOP 12,
    // drawn (channels/TopTwelve.h), each channel on its first.
    //
    // EACH CHANNEL IS AN AMP OF ITS OWN: beside what every channel keeps — GAIN, the tones, the room, the delay, the
    // cabinet — its COMP threshold.
    //
    // WHAT THE VAN HOLDS WHERE IT IS: COMP in the strip and on, three quarters of the signal through it at 4:1 on OPTO, its
    // fader the threshold alone, as the bathyscaphe's (underwater/UnderwaterLineup.cpp); the delay and the room on, their
    // faders their shares.
    static const channels::Lineup van = channels::withTopTwelve ({
        "van", "van_channel", "red-giant",
        {
            { "CLEAN", "v4-copper-4ae0",      "Plate", 20.0f, 15.0f, false, { "Boost", "off" } },
            { "DIRTY", "v4-sheriff-ch1-82ab", "Plate", 15.0f, 8.0f,  false, { "Boost", "off" } },
        },
        false,
        { params::compThreshold },
        {
            { params::compPresent,   1.0f },
            { params::compOn,        1.0f },
            { params::compAmount,    0.75f },
            { params::compRatio,     4.0f },
            { params::compCharacter, (float) params::compCharacters.indexOf ("Opto") },
            { params::delayOn,       1.0f },
            { params::reverbOn,      1.0f },
        },
    });
    return van;
}

} // namespace orbitamp::california

namespace orbitamp
{

namespace
{
    // The van's half of the two things a product gives the processor beside its face (Product.h): CHANNEL over its
    // lineup, and the guitar's COMP, declaring where the van holds it (Lineup::pinned, read into the Opening by
    // Engine::opening).
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, california::lineup());
        params::Opening held;
        channels::Engine::opening (california::lineup(), held);
        params::addGuitarCompParameters (layout, held);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, california::lineup());
    }

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (california::lineup(), o);
    }
}

/** CALIFORNIAAMP (Product.h), a working name: the Orange Super Crush 100's face in catbay's 1967 hippie van — its face
    (california/CaliforniaEditor.h) over its two channels and the guitar's chain, with a COMP before the AMP. No first
    start: it has no player's files of before to gather. */
const Product& products::californiaAmp()
{
    static const Product van = []
    {
        Product p { "CaliforniaAmp", california::makeEditor };
        p.addParameters = addParameters;
        p.makeEngine    = makeEngine;
        p.opening       = opening;
        p.guitarComp    = true;
        return p;
    }();
    return van;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in the van's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::californiaAmp());
}
#endif
