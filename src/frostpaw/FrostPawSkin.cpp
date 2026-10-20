// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "../redpaw/RedPawSkin.h"
#include "../redpaw/RedPawLayout.h"
#include "../redpaw/RedPawLineup.h"
#include "view/IceDrive.h"

#include "../PluginProcessor.h"
#include "../face/Renders.h"

/** FROSTPAWAMP'S OWN (redpaw/RedPawSkin.h): the rover on an ice moon — RedPawAmp's dashboard in frosted
    stainless steel, its lamps amber, its rings and light strips ice-blue. The face and the controls are
    RedPawAmp's; what is here is its channels, the picture's measure, its light, and its renders. */
namespace orbitamp::redpaw
{

namespace
{

constexpr const char* name = "FrostPawAmp";

/** ITS CHANNELS (redpaw/RedPawLineup.h): the Two Notes ReVolt Guitar's three, a gear each — CLEAN, CRUNCH
    and HI-GAIN its Clean, Crunch and Lead, captured through its power amp — and TURBO that channel's own
    Boost: the same pack, the gear holding the switch off and its TURBO on (Lineup::Channel::held). What
    each opens on, as the V4's gears did before them: a gear a little of its room and as much delay, its
    TURBO twice as much of both — Clean in a hall, Crunch on a plate, Lead in an ambience with no delay at
    all; Lead the hot one. */
const channels::Lineup& iceLineup()
{
    static const channels::Lineup rover = roverLineup ({
        { "Clean",          "revolt-clean-poweramp-bf62",  "Hall",     15.0f, 15.0f, false, { "Boost", "off" } },
        { "Clean Turbo",    "revolt-clean-poweramp-bf62",  "Hall",     30.0f, 30.0f, false, { "Boost", "on"  } },
        { "Crunch",         "revolt-crunch-poweramp-73a7", "Plate",    15.0f, 15.0f, false, { "Boost", "off" } },
        { "Crunch Turbo",   "revolt-crunch-poweramp-73a7", "Plate",    30.0f, 30.0f, false, { "Boost", "on"  } },
        { "Hi-Gain",        "revolt-lead-poweramp-f5dc",   "Ambience", 15.0f, 0.0f,  true,  { "Boost", "off" } },
        { "Hi-Gain Turbo",  "revolt-lead-poweramp-f5dc",   "Ambience", 30.0f, 0.0f,  true,  { "Boost", "on"  } },
    });
    return rover;
}

//==============================================================================
// THE DASHBOARD, MEASURED (RedPawLayout.h) off FrostPawAmp's own picture. It was painted to RedPawAmp's
// plan and keeps it within a pixel nearly everywhere — the wells, the slots and the meters, the keys'
// holes, the sockets' middles, the lamps, the rims and the corner plates, the bottom plates and the
// emblem were each measured and found where RedPawAmp's are. What is its own: the window's cut-out,
// rounder at its corners and a pixel taller; the roof's holes, a hair right and up; the rings, the same
// circles painted wider; the lever's notches, each a pixel or less higher; TURBO's hole half a pixel
// right; and the nav screen's glass, ending two pixels sooner.
const layout::Plan plan {
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

const Skin& ice()
{
    static const Skin ice = []
    {
        Skin s {
            name,
            iceLineup(),
            plan,
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
            face::renders::text (name, "midi.txt", "FROSTPAW_ART_DIR"),
            // The ice moon out of the window: its crust, its steel and Jupiter (view/IceDrive.h).
            [] { return std::make_unique<frostpaw::IceDrive> (numGears (iceLineup())); },
            { "Ice (GPU)", "1993 (CPU)", "Still" },
        };
        // The cockpit's badge over the picture's plate, the plate a little darker than the steel round it
        // (#727a81), the cockpit's light cat cut into it, and the lamp under the cat's glass white, as the
        // snow's light.
        face::BadgeLook badge;
        badge.plateTop = juce::Colour (0xff60676e);
        badge.plateBottom = juce::Colour (0xff4c5258);
        badge.lamp = juce::Colour (0xfff2f6ff);
        s.badge = badge;
        return s;
    }();
    return ice;
}

} // namespace

} // namespace orbitamp::redpaw

/** FROSTPAWAMP (Product.h): the rover (redpaw::rover) in the skin above. */
const orbitamp::Product& orbitamp::products::frostPawAmp()
{
    static const Product rover = redpaw::rover (redpaw::ice());
    return rover;
}

// THE PLUG-IN'S ENTRY, in FrostPawAmp's own plug-in alone (CMakeLists.txt: add_amp): a host that opens it
// gets a processor of FrostPawAmp. A gate, or a binary that plays several amps, makes its own.
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::frostPawAmp());
}
#endif
