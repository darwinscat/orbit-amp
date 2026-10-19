// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "../redpaw/RedPawSkin.h"
#include "../redpaw/RedPawLayout.h"
#include "../redpaw/RedPawLineup.h"
#include "view/IceDrive.h"

#include <FrostPawData.h>

/** FROSTPAWAMP'S OWN (redpaw/RedPawSkin.h): the rover on an ice moon — RedPawAmp's dashboard in frosted
    stainless steel, its lamps amber, its rings and light strips ice-blue. The face and the controls are
    RedPawAmp's; what is here is its channels, the picture's measure, its light, and its renders. */
namespace orbitamp::redpaw
{

/** ITS CHANNELS (redpaw/RedPawLineup.h): the Victory V4's six packs, three gears of two (v4Lineup). */
const channels::Lineup& lineup()
{
    static const channels::Lineup rover = v4Lineup();
    return rover;
}

const Skin& skin()
{
    static const Skin ice {
        { FrostPawData::namedResourceList, FrostPawData::namedResourceListSize,
          FrostPawData::getNamedResourceOriginalFilename, FrostPawData::getNamedResource },
        "FROSTPAW_ART_DIR",
        juce::Colour (0xff46e4ff),      // the painted rings' ice-blue (#46d9ed), a shade brighter, as RedPawAmp's
        // On the bright steel the words are written dark — a blue-black graphite, what they are set to
        // darker still, a state a deeper orange — each cut deep into the metal: strokes heavier than the
        // font's, the upper wall a shade over the word, the lower lip a light edge under it; and a dimmed
        // word keeps most of its ink. Nothing else lies under the large words.
        face::Ink { juce::Colour (0xff0b1018), juce::Colour (0xff030507), juce::Colour (0xff8f3507),
                    juce::Colour (0x8cf0f6fc), 0.72f, juce::Colour (0x73000208), 0.7f },
        juce::Colour(),
        // Its own MIDI sheet (resources/frostpaw/midi.txt): its channels are its own to list.
        juce::String::fromUTF8 (FrostPawData::midi_txt, FrostPawData::midi_txtSize),
        // The ice moon out of the window: its crust, its steel and Jupiter (view/IceDrive.h).
        [] { return std::make_unique<Windscreen> (std::make_unique<frostpaw::IceDrive> (numGears())); },
        { "Ice (GPU)", "1993 (CPU)", "Still" },
    };
    return ice;
}

//==============================================================================
// THE DASHBOARD, MEASURED (RedPawLayout.h) off FrostPawAmp's own picture. It was painted to RedPawAmp's
// plan and keeps it within a pixel nearly everywhere — the wells, the slots and the meters, the keys'
// holes, the sockets' middles, the lamps, the rims and the corner plates, the bottom plates and the
// emblem were each measured and found where RedPawAmp's are. What is its own: the window's cut-out,
// rounder at its corners and a pixel taller; the roof's holes, a hair right and up; the rings, the same
// circles painted wider; the lever's notches, each a pixel or less higher; TURBO's hole half a pixel
// right; and the nav screen's glass, ending two pixels sooner.
namespace layout
{

const Plan plan {
    .windscreenCorners {{
        { 135.0f, 47.0f }, { 1539.5f, 47.0f }, { 1555.5f, 60.0f }, { 1453.0f, 317.0f }, { 227.0f, 317.0f }, { 115.5f, 60.0f },
    }},
    .windscreen { 115.0f, 46.5f, 1441.5f, 271.0f },

    .holes {{
        { 683.5f, 22.2f }, { 727.5f, 22.4f }, { 809.7f, 22.1f }, { 853.6f, 22.5f }, { 897.6f, 22.3f }, { 941.9f, 22.3f },
    }},

    // The rings are painted wider than RedPawAmp's — about 4.5 across round DRIVE, 2.5 round a tone knob.
    .drive { { 374.2f, 538.3f }, 71.7f, 3.5f, 132.0f },
    .tones {{
        { { 1118.3f, 495.0f }, 40.6f, 2.4f, 104.0f },   // BASS
        { { 1240.6f, 495.1f }, 40.6f, 2.4f, 104.0f },   // MIDDLE
        { { 1363.4f, 494.8f }, 40.6f, 2.4f, 104.0f },   // TREBLE
    }},

    .navScreen { 563.5f, 424.0f, 436.5f, 191.5f },

    // The slot is 786..810 across, its top end at 681.5, its notches out to 823: RedPawAmp's within a
    // pixel, and its three gears in its three notches.
    .stops { 732.8f, 775.5f, 816.8f },

    // TURBO's hole half a pixel right; the hatch's box is RedPawAmp's, where a hatch of its own would go — none yet.
    .turbo { 922.0f, 787.0f },
};

} // namespace layout

} // namespace orbitamp::redpaw
