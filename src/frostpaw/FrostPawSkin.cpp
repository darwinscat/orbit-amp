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

/** ITS CHANNELS (redpaw/RedPawLineup.h): the Victory V4's six packs, three gears of two — CLEAN the
    Copper, CRUNCH the Sheriff, HI-GAIN the Kraken — and TURBO each pedal's second channel, each found by
    the capture it plays. What each opens on: a pedal's first channel a little of its room and as much
    delay, its TURBO twice as much of both — the Copper in a hall, the Sheriff on a plate, the Kraken in
    an ambience with no delay at all. The Kraken's two are the hot packs, the ones that would carry FAT. */
const channels::Lineup& lineup()
{
    static const channels::Lineup rover = roverLineup ({
        { "Copper",      "v4-copper-4ae0",      "Hall",     15.0f, 15.0f, false },
        { "Copper TB",   "v4-copper-tb-5841",   "Hall",     30.0f, 30.0f, false },
        { "Sheriff Ch1", "v4-sheriff-ch1-82ab", "Plate",    15.0f, 15.0f, false },
        { "Sheriff Ch2", "v4-sheriff-ch2-2f2f", "Plate",    30.0f, 30.0f, false },
        { "Kraken Ch1",  "v4-kraken-ch1-f7fc",  "Ambience", 15.0f, 0.0f,  true  },
        { "Kraken Ch2",  "v4-kraken-ch2-5f71",  "Ambience", 30.0f, 0.0f,  true  },
    });
    return rover;
}

namespace
{
    /** THE LAMPS GO OUT (face/Renders.h). On frosted steel and ice nothing is warm but the amber lamps
        and the light they throw: a lamp is what is warm — its red well over its blue — and lit, so the
        steel, the frost and the ice-blue strips stay as they were painted. */
    float lampness (juce::Colour c)
    {
        const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
        const float warm   = (r - b - 0.25f) / 0.25f;
        const float bright = (0.3f * r + 0.59f * g + 0.11f * b - 0.38f) / 0.18f;
        return juce::jmin (juce::jlimit (0.0f, 1.0f, warm), juce::jlimit (0.0f, 1.0f, bright));
    }
}

const Skin& skin()
{
    static const Skin ice {
        { FrostPawData::namedResourceList, FrostPawData::namedResourceListSize,
          FrostPawData::getNamedResourceOriginalFilename, FrostPawData::getNamedResource },
        "FROSTPAW_ART_DIR",
        lampness,
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

const std::array<juce::Point<float>, 6> windscreenCorners {{
    { 135.0f, 47.0f }, { 1539.5f, 47.0f }, { 1555.5f, 60.0f }, { 1453.0f, 317.0f }, { 227.0f, 317.0f }, { 115.5f, 60.0f },
}};
const juce::Rectangle<float> windscreen { 115.0f, 46.5f, 1441.5f, 271.0f };

const std::array<juce::Point<float>, 6> holes {{
    { 683.5f, 22.2f }, { 727.5f, 22.4f }, { 809.7f, 22.1f }, { 853.6f, 22.5f }, { 897.6f, 22.3f }, { 941.9f, 22.3f },
}};
const float holeRadius = 12.0f;
const float signOffset = 22.0f;

// The rings are painted wider than RedPawAmp's — about 4.5 across round DRIVE, 2.5 round a tone knob.
const Socket drive { { 374.2f, 538.3f }, 71.7f, 3.5f, 132.0f };
const std::array<Socket, 3> tones {{
    { { 1118.3f, 495.0f }, 40.6f, 2.4f, 104.0f },   // BASS
    { { 1240.6f, 495.1f }, 40.6f, 2.4f, 104.0f },   // MIDDLE
    { { 1363.4f, 494.8f }, 40.6f, 2.4f, 104.0f },   // TREBLE
}};
const float knobNameY  = 423.0f;
const float knobNameEm = px13;
const float driveNameY = 423.0f;

const std::array<juce::Point<float>, 6> keys {{
    { 1093.5f, 603.5f }, { 1141.5f, 603.5f }, { 1226.5f, 603.5f }, { 1280.5f, 603.5f }, { 1333.5f, 603.5f }, { 1388.0f, 603.5f },
}};
const float keySize      = 48.0f;    // the key's render, whose face fills the 37 x 41 hole
const float keyNameOverY = 563.0f;
const float keyNameUnderY = 642.0f;
const std::array<bool, 6> keyNameOver {{ false, false, false, true, false, true }};
const float keyNameRoom = 120.0f;
const bool keyNamesOnKeys = false;     // the names round the keys

const juce::Rectangle<float> navScreen   { 563.5f, 424.0f, 436.5f, 191.5f };
const juce::Rectangle<float> navHeader   { 567.0f, 426.0f, 430.0f, 32.0f };
const juce::Rectangle<float> navPicture  { 567.0f, 460.0f, 430.0f, 126.0f };
const juce::Rectangle<float> tunerScreen { 567.0f, 588.0f, 430.0f, 27.0f };
const face::TunerFace::Dial tunerDial {};      // none: the tuner is the screen's bottom line
const juce::Rectangle<float> learnArea = navPicture;     // LEARN over the picture...
const std::array<juce::Point<float>, 2> learnKeys {};
const float learnKeySize = 0.0f;                         // ...and no keys of its own
const std::array<juce::Point<float>, 2> learnNames {};
const float learnLine = 0.0f;
const std::array<juce::Point<float>, 4> wiperKeys {};    // no keys for the wipers
const float wiperKeySize = 0.0f;

// The slot is 786..810 across, its top end at 681.5, its notches out to 823.
const juce::Rectangle<float> gateSlot { 785.5f, 681.0f, 37.5f, 174.0f };
const float slotX  = 798.3f;
const float notchX = 814.0f;
const float slotTopY = 694.0f;
const std::vector<float> notches { 732.8f, 775.5f, 816.8f };
const float leverWidth = 124.0f;
const float collarSink = 3.0f;
const juce::Rectangle<float> gearPlate { 742.0f, 655.0f, 116.0f, 222.0f };
const float leverLeft = 724.0f, leverTop = 600.0f;
// Its third notch cut 804..830 down, its slot's lit edge 810..812, the straight run over it 789..803,
// the plate plain out to 848: filled as RedPawAmp's would be, were it to have fewer gears.
const NotchFill notchFill { 17.0f, 17.0f, 803.0f, 813.0f, 831.0f, 11.0f, 16.0f, 2.0f };

const juce::Point<float> turbo { 922.0f, 787.0f };
const float turboSize = 52.0f;
const juce::Point<float> turboName { 921.5f, 682.0f };
const juce::Rectangle<float> turboLid { 873.5f, 689.0f, 101.0f, 168.0f };     // where a hatch of its own would go; none yet

const std::array<juce::Rectangle<float>, 3> wells {{
    { 315.0f,  700.5f, 326.5f, 21.5f },     // REVERB
    { 314.5f,  774.0f, 326.5f, 22.0f },     // DELAY
    { 1053.0f, 730.5f, 309.5f, 24.0f },     // CAB
}};
const std::array<float, 3> wellNameY {{ 690.0f, 762.5f, 695.0f }};

const std::array<Rim, 2> rims {{
    { 276.0f, 681.0f, 683.0f, 691.0f, 699.5f, 703.0f, 316.0f, 637.0f },     // REVERB
    { 276.0f, 681.0f, 755.5f, 763.0f, 772.5f, 777.0f, 316.0f, 637.0f },     // DELAY
}};
const float rimLift = 10.0f;
const float sliderCapLength = 54.0f;
const float sliderHeight    = 44.0f;

const juce::Rectangle<float> inMeter  { 48.0f,   444.5f, 34.5f, 244.0f };
const juce::Rectangle<float> inFader  { 124.5f,  447.0f, 25.0f, 235.5f };
const juce::Rectangle<float> outMeter { 1512.0f, 448.0f, 34.5f, 237.0f };
const juce::Rectangle<float> outFader { 1588.0f, 447.5f, 24.5f, 235.0f };
const float faderWidth     = 44.0f;
const float faderCapLength = 18.0f;

const float lampPlateGrow = 24.0f;
const LampPlate gatePlate    { { 37.5f, 794.0f, 107.5f, 106.0f },
                               {{ { 37.5f, 795.0f }, { 64.5f, 795.0f }, { 140.0f, 870.5f }, { 140.0f, 900.0f }, { 37.5f, 900.0f } }},
                               lampPlateGrow, 37.5f,
                               {{ 38.0f, 146.0f, 60.0f }}, {{ 794.0f, 806.0f, 889.0f, 900.0f }},
                               { 40.5f, 808.0f, 18.0f, 80.0f } };
const LampPlate limiterPlate { { 1525.0f, 791.0f, 110.5f, 106.0f },
                               {{ { 1635.5f, 792.5f }, { 1607.5f, 792.5f }, { 1529.5f, 866.5f }, { 1529.5f, 897.0f }, { 1635.5f, 897.0f } }},
                               -lampPlateGrow, 1635.5f - lampPlateGrow,
                               {{ 1612.0f, 1502.0f, 1590.0f }}, {{ 791.0f, 803.0f, 886.0f, 897.0f }},
                               { 1614.5f, 805.0f, 18.0f, 80.0f } };

const juce::Rectangle<float> inSlotRim { 122.0f, 444.0f, 31.0f, 243.0f };
const float inSlotEnd = 16.0f;
const float slotScale = 18.0f / 31.0f;
const float slotRim = 3.0f * slotScale;

const std::array<juce::Rectangle<float>, numLamps> lamps {{
    { 27.0f,   363.0f, 135.0f, 32.0f },     // the segments 40..152 x 372..386
    { 1510.0f, 363.0f, 132.0f, 32.0f },     // the segments 1520..1631 x 372..386
    { 45.0f + lampPlateGrow,   824.0f, 62.0f, 63.0f },  // the slant 55..97 x 834..877, as painted
    { 1565.0f - lampPlateGrow, 824.0f, 62.0f, 63.0f },  // the slant 1575..1617 x 834..877, as painted
}};
const float lampDot = 0.0f;                     // none: the picture paints them lit
const face::SegmentMeter::Dots meterDots {};    // none: segments down the wells

const juce::Point<float> inputName  { 101.8f, 721.7f };
const juce::Point<float> outputName { 1570.0f, 721.7f };
const float columnNameRoom = 170.0f;

const juce::Point<float> gateWord    { 110.0f + lampPlateGrow, 824.0f };
const juce::Point<float> limiterWord { 1562.0f - lampPlateGrow, 825.0f };
const float lampSlant = 0.7854f;     // 45 degrees

const juce::Rectangle<float> leftPlate   { 340.0f, 852.0f, 262.0f, 57.0f };
const std::array<juce::Rectangle<float>, 2> readings {{ { 1132.5f, 853.0f, 210.0f, 28.0f }, { 1132.5f, 881.0f, 210.0f, 28.0f } }};     // on the right plate, 1103..1332
const juce::Rectangle<float> centrePlate { 755.0f, 895.0f, 195.0f, 33.0f };

const std::array<juce::Point<float>, 6> emblemFace {{
    { 809.0f, 295.6f }, { 865.0f, 295.6f }, { 888.0f, 335.0f }, { 865.0f, 375.0f }, { 809.0f, 375.0f }, { 787.0f, 335.0f },
}};
const juce::Rectangle<float> emblemMark { 813.0f, 310.0f, 50.0f, 50.0f };
const juce::Point<float> poweredBy {}, makerName {};      // no room beside the plate for the maker's words
const float makerRoom = 0.0f;

} // namespace layout

} // namespace orbitamp::redpaw
