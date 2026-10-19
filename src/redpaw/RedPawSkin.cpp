// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawSkin.h"
#include "RedPawLayout.h"
#include "RedPawLineup.h"
#include "view/PlainsDrive.h"

#include "../face/Renders.h"

/** REDPAWAMP'S OWN (RedPawSkin.h): the Mars rover — its channels, its rusted dashboard, measured, its
    cyan, and the drive across Mars through the windscreen. */
namespace orbitamp::redpaw
{

/** ITS CHANNELS (RedPawLineup.h): the Friedman IR-X's two, a gear each — CRUNCH its Ch1, HI-GAIN its
    Ch2 (the lever names them from the top) — and TURBO that channel's own Boost: the same pack, the
    gear holding the switch off and its TURBO on (Lineup::Channel::held). The pack's other switches are
    the hand's, on the keys: Ch1's three-way Bright, Ch2's Tight and Fat. A channel's pack is the capture
    it plays, known by the `rig_id` its manifest carries. What each opens on: a gear a little of its room
    and as much delay, its TURBO twice as much of both — Ch1 on a plate, Ch2 in an ambience with no
    delay at all. */
const channels::Lineup& lineup()
{
    static const channels::Lineup rover = roverLineup ({
        { "Crunch",         "ir-x-ch1-poweramp-3e66", "Plate",    15.0f, 15.0f, false, { "Boost", "off" } },
        { "Crunch Turbo",   "ir-x-ch1-poweramp-3e66", "Plate",    30.0f, 30.0f, false, { "Boost", "on"  } },
        { "Hi-Gain",        "ir-x-ch2-poweramp-67e9", "Ambience", 15.0f, 0.0f,  true,  { "Boost", "off" } },
        { "Hi-Gain Turbo",  "ir-x-ch2-poweramp-67e9", "Ambience", 30.0f, 0.0f,  true,  { "Boost", "on"  } },
    });
    return rover;
}

const Skin& skin()
{
    static const Skin mars {
        "REDPAW_ART_DIR",
        juce::Colour (0xff3fe6f2),      // the painted rings' light, a shade brighter
        face::Ink {},                   // the family's lilac on the dark rust...
        juce::Colours::black.withAlpha (0.7f),     // ...and a shadow under the large words, so they read off the dust
        face::renders::text ("midi.txt", "REDPAW_ART_DIR"),
        [] { return std::make_unique<Windscreen> (std::make_unique<PlainsDrive> (numGears())); },     // the drive across Mars
        { "Plains (GPU)", "1993 (CPU)", "Still" },
    };
    return mars;
}

//==============================================================================
// THE DASHBOARD, MEASURED (RedPawLayout.h): its picture is the one the rovers' plan is measured off, so
// what it measures is the plan as it stands.
namespace layout
{

const Plan plan {};

} // namespace layout

} // namespace orbitamp::redpaw
