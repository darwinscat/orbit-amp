// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveLineup.h"

#include "../Parameters.h"
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
    //
    // EACH GEAR IS AN AMP OF ITS OWN: beside what every channel keeps — DRIVE, the tones (the bass's standard's
    // BASS, MIDDLE and TREBLE, Channels::toneOf), the cabinet — its BLEND, MIDDLE's frequency, COMP's threshold,
    // OCTAVE, ENVELOPE, CHORUS and DI/CAB. The instrument and the hand are the player's on every gear: 5 STR /
    // 4 STR and FINGERS / SLAP are kept by no channel.
    //
    // WHAT THE WALKER HOLDS WHERE IT IS: COMP on, all of the signal through it, at 4:1 on OPTO — its knob is the
    // threshold alone; OCTAVE, ENVELOPE and CHORUS in the strip and on — their sliders are their amounts, 0 the
    // dry; both cuts on — the switches choose between their positions; no delay and no room in the strip.
    static const channels::Lineup walker {
        "walker", "walker_channel", "red-giant",
        {
            { "B15", "sgt-di-19a4", "Plate", std::nullopt, 0.0f, false, { "Voice", "B15" } },
            { "SVT", "sgt-di-19a4", "Plate", std::nullopt, 0.0f, false, { "Voice", "SVT" } },
        },
        false,
        {
            params::ampBlend, params::eqBellHz (0, 0), params::compThreshold,
            params::octaveAmount, params::filterAmount, params::chorusAmount, params::diMix,
        },
        {
            { params::compOn,        1.0f },
            { params::compAmount,    1.0f },
            { params::compRatio,     4.0f },
            { params::compCharacter, (float) params::compCharacters.indexOf ("Opto") },
            { params::octavePresent, 1.0f }, { params::octaveOn, 1.0f },
            { params::filterPresent, 1.0f }, { params::filterOn, 1.0f },
            { params::chorusPresent, 1.0f }, { params::chorusOn, 1.0f },
            { params::bassHpfOn,     1.0f },
            { params::bassLpfOn,     1.0f },
            { params::delayPresent,  0.0f },
            { params::reverbPresent, 0.0f },
        },
        1,      // ...and it opens on SVT (the owner, 03.11)
    };
    return walker;
}

} // namespace orbitamp::redgroove

namespace orbitamp
{

namespace
{
    // The walker's half of the two things a product gives the processor beside its face (Product.h): CHANNEL
    // over its lineup, and the bass's own — COMP, BLEND, DI/CAB, its cuts and its effects — each declaring where the
    // walker holds it (Lineup::pinned, read into the Opening by Engine::opening).
    void addParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        channels::Engine::addParameters (layout, redgroove::lineup());
        params::Opening held;
        channels::Engine::opening (redgroove::lineup(), held);
        params::addBassParameters (layout, held);
    }

    std::unique_ptr<ProductEngine> makeEngine (AmpProcessor& p)
    {
        return std::make_unique<channels::Engine> (p, redgroove::lineup());
    }

    /** Where a fresh walker stands: on SVT, its second gear (Lineup::opensAt, Engine::opening), and with MIDDLE's frequency at 1 kHz — every
        gear opening there, INIT and a double click on MID FREQ bringing it back there (the owner, 02.11). */
    constexpr float midOpensHz = 1000.0f;

    void opening (const Product&, params::Opening& o)
    {
        channels::Engine::opening (redgroove::lineup(), o);
        o.midHz = midOpensHz;
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
        p.viewLitByDefault = true;      // the forest answers the bass from the first note
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
