// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IoLineup.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../channels/Engine.h"

#include <BassDozen.h>

#include <cstring>

namespace orbitamp::io
{

namespace
{
    /** The slot of the bass's dozen (BassCabinets.h) a preset stands at. */
    int dozenAt (const char* stem)
    {
        for (int i = 0; i < (int) std::size (bassCabinets); ++i)
            if (std::strcmp (bassCabinets[i], stem) == 0)
                return i;
        return 0;
    }

    std::vector<channels::Lineup::Cabinet> dozen()
    {
        std::vector<channels::Lineup::Cabinet> all;
        for (const auto* stem : bassCabinets)
            all.push_back ({ .preset = stem });
        return all;
    }
}

const channels::Lineup& lineup()
{
    // ONE SOUND, TWO SPEEDS. The SansAmp RBI is one preamp with no channels; the lever's SLOW and FAST are the Io rover's
    // two cruise gears, and both play it. CHANNEL wants two choices at least (a host's choice of one has nothing to
    // choose), so the two speeds are a PAIR OVER ONE MEMORY (Lineup::pairs, the rover's gear and its TURBO): the same
    // pack, nothing held, everything else one set — from one to the other nothing changes but the gear the world drives in.
    //
    // A STAND-IN SOUND until the RBI has captures of its own: the Two Notes ReVolt Bass's DIRT channel (CMakeLists.txt:
    // IOTIDEBASS_SET), on everything DeepGrooveBass's DIRT gear stands on — its BLEND opening at 80/20, the dozen's 8x10,
    // COMP on, three quarters through it at 4:1 on OPTO, its fader the threshold alone; OCTAVE and CHORUS in the strip
    // and on, their faders their amounts; the room in the strip and on; both cuts on, the switches choosing between their
    // positions; no ENVELOPE and no delay (underwater/UnderwaterLineup.cpp). Its tones are Bass, Mid and Treble; it has
    // no Presence, so the RBI's PRESENCE turns the rover's own parameter (presenceParam): the world's snow, no sound.
    static const channels::Lineup sub {
        "io", "io_channel", "4x10-classic",
        {
            { "SLOW", "revolt-bass-dirt-e9d4", "Plate", 0.0f, 0.0f, false, {}, dozenAt ("rock-03-8x10-stack"), 0.8f },
            { "FAST", "revolt-bass-dirt-e9d4", "Plate", 0.0f, 0.0f, false, {}, dozenAt ("rock-03-8x10-stack"), 0.8f },
        },
        true,   // the pair: one memory
        {
            params::ampBlend, params::octaveAmount, params::chorusAmount, params::compThreshold,
        },
        {
            { params::ampBlendInvert, 0.0f },    // the clean leg as it comes: no flip (BLEND's menu has none)
            { params::compOn,        1.0f },
            { params::compAmount,    0.75f },
            { params::compRatio,     4.0f },
            { params::compCharacter, (float) params::compCharacters.indexOf ("Opto") },
            { params::octavePresent, 1.0f }, { params::octaveOn, 1.0f },
            { params::filterPresent, 0.0f },
            { params::chorusPresent, 1.0f }, { params::chorusOn, 1.0f },
            { params::bassHpfOn,     1.0f },
            { params::bassLpfOn,     1.0f },
            { params::delayPresent,  0.0f },
            { params::reverbPresent, 1.0f }, { params::reverbOn, 1.0f },
        },
        0,      // ...and it opens on SLOW
        true,   // ITS CABINETS ARE OURS, DRAWN: the bass's dozen, as the bathyscaphe's
        dozen(),
    };
    return sub;
}

} // namespace orbitamp::io

namespace orbitamp
{

namespace
{
    // The rover's half of the two things a product gives the processor beside its face (Product.h): CHANNEL over its
    // lineup, PRESENCE (io::presenceParam, its own: no other amp declares it), and the bass's own — BLEND, the cuts and
    // the effects — each declaring where the rover holds it (Lineup::pinned, read into the Opening by Engine::opening).
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, io::lineup());
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { io::presenceParam, 1 }, "Presence",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                                                 io::presenceDefault));
        params::Opening held;
        channels::Engine::opening (io::lineup(), held);
        params::addBassParameters (layout, held);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, io::lineup());
    }

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (io::lineup(), o);
    }
}

/** IOTIDEBASS (Product.h), the owner's name (20.11): the SansAmp RBI's face in catbay's Io rover cabin (io/IoEditor.h),
    over Jupiter's volcanic moon, on the bass's chain (Chain::bass) — one sound, a stand-in for now, its tuner down to a
    drop-A's A0. No first start: it has no player's files of before to gather. */
const Product& products::ioTideBass()
{
    static const Product sub = []
    {
        Product p { "IoTideBass", io::makeEditor };
        p.addParameters = addParameters;
        p.makeEngine    = makeEngine;
        p.opening       = opening;
        p.chain         = Chain::bass;
        p.tunerLowestHz = core::PitchTracker::bassLowestHz;
        return p;
    }();
    return sub;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in the rover's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::ioTideBass());
}
#endif
