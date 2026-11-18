// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "UnderwaterLineup.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../channels/Engine.h"

#include <BassDozen.h>

#include <cstring>

namespace orbitamp::underwater
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
    // THE THREE GEARS, CLEAN, DIRT AND DRIVE: the owner's Two Notes ReVolt Bass, a pack a channel, each its own capture
    // of the pedal's channel at nine GAIN positions (CMakeLists.txt: DEEPGROOVEBASS_SET) — CLEAN's tone BASS and TREBLE,
    // DIRT's and DRIVE's BASS, MID and TREBLE, as the pedal's are. DRIVE was captured at its blend's 100% wet: its dry
    // is BLEND's.
    //
    // EACH GEAR IS AN AMP OF ITS OWN: beside what every channel keeps — DRIVE, the tones, the room, the cabinet — its
    // BLEND and its three effects' amounts. BLEND is DIRT's and DRIVE's, opening at 80/20 and 90/10 (the owner, 17.11);
    // CLEAN has none, and stands at the capture alone (16.11). The instrument and the hand are the player's on every gear: 5 STR / 4 STR and FINGERS / SLAP
    // are kept by no channel.
    //
    // WHAT THE BATHYSCAPHE HOLDS WHERE IT IS: COMP on, three quarters of the signal through it at 4:1 on OPTO, its fader
    // the threshold alone (the owner, 17.11, as the walker's knob); no ENVELOPE (its effects are OCTAVE, CHORUS and REVERB,
    // 16.11); OCTAVE and CHORUS in the strip and on, their faders their amounts, 0 the dry; the room in the strip
    // and on, its fader its share; both cuts on, the switches choosing between their positions; no delay.
    static const channels::Lineup sub {
        "underwater", "underwater_channel", "4x10-classic",
        {
            { "CLEAN", "revolt-bass-clean-0fba", "Plate", 0.0f, 0.0f, false, {}, dozenAt ("rock-14-1x15-thump"), 1.0f, true },
            { "DIRT",  "revolt-bass-dirt-e9d4",  "Plate", 0.0f, 0.0f, false, {}, dozenAt ("rock-03-8x10-stack"), 0.8f },
            { "DRIVE", "revolt-bass-drive-2655", "Plate", 0.0f, 0.0f, false, {}, dozenAt ("rock-03-8x10-stack"), 0.9f },
        },
        false,
        {
            params::ampBlend, params::octaveAmount, params::chorusAmount, params::compThreshold,
        },
        {
            { params::ampBlendInvert, 0.0f },    // the clean leg as it comes: no flip (17.11, BLEND's menu has none)
            { params::compOn,        1.0f },     // COMP (the owner, 17.11): on, three quarters through it at 4:1 on OPTO
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
        1,      // ...and it opens on DIRT
        true,   // ITS CABINETS ARE OURS, DRAWN: the bass's dozen, as the walker's
        dozen(),
    };
    return sub;
}

} // namespace orbitamp::underwater

namespace orbitamp
{

namespace
{
    // The bathyscaphe's half of the two things a product gives the processor beside its face (Product.h): CHANNEL over
    // its lineup, and the bass's own — BLEND, the cuts and the effects — each declaring where the bathyscaphe holds it
    // (Lineup::pinned, read into the Opening by Engine::opening).
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, underwater::lineup());
        params::Opening held;
        channels::Engine::opening (underwater::lineup(), held);
        params::addBassParameters (layout, held);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, underwater::lineup());
    }

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (underwater::lineup(), o);
    }
}

/** DEEPGROOVEBASS (Product.h), the owner's name (16.11): the bathyscaphe DC-744 — its face (underwater/UnderwaterEditor.h) over
    its three channels and the bass's chain (Chain::bass), its tuner down to a drop-A's A0, its sea answering the bass
    from the first note as the walker's forest does. No first start: it has no player's files of before to gather. */
const Product& products::deepGrooveBass()
{
    static const Product sub = []
    {
        Product p { "DeepGrooveBass", underwater::makeEditor };
        p.addParameters = addParameters;
        p.makeEngine    = makeEngine;
        p.opening       = opening;
        p.chain         = Chain::bass;
        p.tunerLowestHz = core::PitchTracker::bassLowestHz;
        p.viewLitByDefault = true;
        return p;
    }();
    return sub;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in the bathyscaphe's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::deepGrooveBass());
}
#endif
