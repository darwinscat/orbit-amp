// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

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
    };

    const char* nodeType;       // the product's node in the state tree, where the channels' memory is kept
    const char* parameterId;    // CHANNEL, the host's handle on the selector
    const char* openingCabinet; // the cabinet a channel opens on, by the stem of its file's name

    std::vector<Channel> channels;

    /** CHANNELS IN PAIRS OVER ONE MEMORY, where the product has them: each channel and the next play
        their two packs over one set of everything else — DRIVE, the tones by their knobs, the keys, the
        cabinet, the reverb, the delay — the rover's gear and its TURBO, which is an option of the gear's
        as its tone is. What a pair opens on is the first's. */
    bool pairs = false;

    int count() const noexcept { return (int) channels.size(); }
};

} // namespace orbitamp::channels
