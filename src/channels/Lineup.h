// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

/** WHAT A PRODUCT'S CHANNELS ARE. A product that is a few amps under one hand — the cockpit's four, the
    rover's six — is the same engine every time (Channels, Engine, Presets): what is its own is how many
    channels it has, which capture each one plays, what each opens on, and the names it keeps its memory
    under in a state. Said once, in the product's own file, and handed to the engine as it is built. */
namespace orbitamp::channels
{

struct Lineup
{
    struct Channel
    {
        const char* name;       // as a host writes it: one of CHANNEL's choices
        const char* capture;    // the `rig_id` its pack's manifest carries — what the pack IS, whatever its file is called
        const char* reverb;     // the room it opens on, until the player picks another (params::reverbCharacters)
        std::optional<float> reverbMix;     // ...how much of it — the parameter's own default when not said
        float delayMix;         // ...and how much delay
        bool  hot;              // its pack is one of the hottest, the ones that carry FAT

        /** A SWITCH OF ITS PACK THE CHANNEL HOLDS: that control at that position, whatever a record or a
            hand says — so two channels can play one pack, as the IR-X's Boost off and on are a gear and
            its TURBO. None when `control` is not said; the pack's other switches stay the hand's. */
        struct Held
        {
            const char* control = nullptr;
            const char* value   = nullptr;
        } held {};

        /** ITS CABINET'S SLOT, where the product's cabinets are drawn (drawnCabinets): the one of `cabinets` the
            channel stands at, never visited. */
        int cabinet = 0;
    };

    const char* nodeType;       // the product's node in the state tree, where the channels' memory is kept
    const char* parameterId;    // CHANNEL, the host's handle on the selector
    const char* openingCabinet; // the cabinet a channel opens on, by the stem of its file's name (drawnCabinets: its slot)

    std::vector<Channel> channels;

    /** CHANNELS IN PAIRS OVER ONE MEMORY, where the product has them: each channel and the next play
        their two packs over one set of everything else — DRIVE, the tones by their knobs, the keys, the
        cabinet, the reverb, the delay — the rover's gear and its TURBO, which is an option of the gear's
        as its tone is. What a pair opens on is the first's. */
    bool pairs = false;

    /** WHAT A CHANNEL KEEPS BEYOND WHAT EVERY ONE DOES (Channels: DRIVE, TURBO, the tones, the pack's switches,
        the cabinet's cuts, the room, the delay and the cabinet): the product's own parameters each of its
        channels remembers apart — the walker's BLEND, MIDDLE's frequency, COMP's threshold, its three
        effects and DI/CAB. A parameter neither list names is one for every channel. */
    std::vector<juce::String> kept {};

    /** WHAT THE PRODUCT HOLDS WHERE IT IS, whatever a record, a preset, an INIT or a host left it at: each of
        these parameters set to its value every time the chain is set up (Engine::setUpChain) — the walker's
        COMP on, all of the signal through it at 4:1 on OPTO, its effects in the strip and on, its cuts on,
        no delay and no room. */
    struct Pin
    {
        juce::String param;
        float value = 0.0f;
    };
    std::vector<Pin> pinned {};

    /** THE CHANNEL A FRESH INSTANCE OPENS ON (Channels::adopt, Engine::opening, CHANNEL's default) — the first unless
        the product says another: the walker opens on SVT, its second (the owner, 03.11). */
    int opensAt = 0;
    const Channel& opener() const { return channels[(size_t) juce::jlimit (0, count() - 1, opensAt)]; }

    /** CABINETS DRAWN, NOT FILES: the walker plays the bass's own rendered cabinets alone (ui/CabWindow.h, Ampeg's pile)
        — no file of the build's, none of the player's. */
    bool drawnCabinets = false;

    /** ...IN SLOTS: so many cabinets, each a box and its mics as the player sets them in the window — one set for every
        channel, kept in the state (Channels::slotFrom) — and each channel standing at one of them (Channel::cabinet).
        What each slot holds until the player says otherwise: its box, the box's own mic at its cap, the horn on or off. */
    struct Cabinet
    {
        const char* box = nullptr;
        bool horn = true;
    };
    std::vector<Cabinet> cabinets {};

    int count() const noexcept { return (int) channels.size(); }
};

} // namespace orbitamp::channels
