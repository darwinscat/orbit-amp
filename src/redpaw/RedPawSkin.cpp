// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawSkin.h"
#include "RedPawLayout.h"
#include "RedPawLineup.h"
#include "view/PlainsDrive.h"

#include <RedPawData.h>

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
        { "IR-X Ch1",       "ir-x-ch1-poweramp-3e66", "Plate",    15.0f, 15.0f, false, { "Boost", "off" } },
        { "IR-X Ch1 Boost", "ir-x-ch1-poweramp-3e66", "Plate",    30.0f, 30.0f, false, { "Boost", "on"  } },
        { "IR-X Ch2",       "ir-x-ch2-poweramp-67e9", "Ambience", 15.0f, 0.0f,  true,  { "Boost", "off" } },
        { "IR-X Ch2 Boost", "ir-x-ch2-poweramp-67e9", "Ambience", 30.0f, 0.0f,  true,  { "Boost", "on"  } },
    });
    return rover;
}

namespace
{
    /** THE LAMPS GO OUT (face/Renders.h). On a rusted dashboard warm is everywhere: a lamp is what is
        both warm AND bright — its red well over its blue, and the whole of it lit — so the rust and the
        dust round it stay as they were painted. */
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
    static const Skin mars {
        { RedPawData::namedResourceList, RedPawData::namedResourceListSize,
          RedPawData::getNamedResourceOriginalFilename, RedPawData::getNamedResource },
        "REDPAW_ART_DIR",
        lampness,
        juce::Colour (0xff3fe6f2),      // the painted rings' light, a shade brighter
        face::Ink {},                   // the family's lilac on the dark rust...
        juce::Colours::black.withAlpha (0.7f),     // ...and a shadow under the large words, so they read off the dust
        juce::String::fromUTF8 (RedPawData::midi_txt, RedPawData::midi_txtSize),
        [] { return std::make_unique<Windscreen> (std::make_unique<PlainsDrive> (numGears())); },     // the drive across Mars
        { "Plains (GPU)", "1993 (CPU)", "Still" },
    };
    return mars;
}

//==============================================================================
// THE DASHBOARD, MEASURED (RedPawLayout.h).
namespace layout
{

const std::array<juce::Point<float>, 6> windscreenCorners {{
    { 122.0f, 47.5f }, { 1550.0f, 47.5f }, { 1556.0f, 60.0f }, { 1464.0f, 317.0f }, { 208.5f, 317.0f }, { 115.5f, 60.0f },
}};
const juce::Rectangle<float> windscreen { 115.5f, 47.5f, 1440.5f, 269.5f };

const std::array<juce::Point<float>, 6> holes {{
    { 683.3f, 22.7f }, { 727.2f, 22.8f }, { 809.3f, 22.7f }, { 853.3f, 23.1f }, { 897.0f, 23.0f }, { 941.5f, 22.6f },
}};
const float holeRadius = 12.0f;
const float signOffset = 22.0f;

const Socket drive { { 374.3f, 538.3f }, 71.7f, 3.0f, 132.0f };
const std::array<Socket, 3> tones {{
    { { 1118.3f, 495.0f }, 40.6f, 2.2f, 104.0f },   // BASS
    { { 1240.5f, 495.0f }, 40.6f, 2.2f, 104.0f },   // MIDDLE
    { { 1363.3f, 494.9f }, 40.6f, 2.2f, 104.0f },   // TREBLE
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

const juce::Rectangle<float> navScreen   { 563.5f, 423.5f, 436.5f, 195.0f };
const juce::Rectangle<float> navHeader   { 567.0f, 426.0f, 430.0f, 32.0f };
const juce::Rectangle<float> navPicture  { 567.0f, 460.0f, 430.0f, 126.0f };
const juce::Rectangle<float> tunerScreen { 567.0f, 588.0f, 430.0f, 27.0f };
const face::TunerFace::Dial tunerDial {};      // none: the tuner is the screen's bottom line
const juce::Rectangle<float> learnArea = navPicture;     // LEARN over the picture...
const juce::Point<float> learnKey {};
const float learnKeySize = 0.0f;                         // ...and no key of its own
const juce::Point<float> learnName {};
const float learnNameRoom = 0.0f;
const std::array<juce::Point<float>, 4> wiperKeys {};    // no keys for the wipers
const float wiperKeySize = 0.0f;

// The slot is 785.5..811 across, its top end at 681, its notches out to 823.
const juce::Rectangle<float> gateSlot { 785.5f, 681.0f, 37.5f, 174.0f };
const float slotX  = 798.3f;
const float notchX = 814.0f;
const float slotTopY = 694.0f;
const std::vector<float> notches { 733.8f, 776.0f, 817.5f };
const float leverWidth = 124.0f;
const float collarSink = 3.0f;
const juce::Rectangle<float> gearPlate { 742.0f, 655.0f, 116.0f, 222.0f };
const float leverLeft = 724.0f, leverTop = 600.0f;
// A notch is cut 805..829 down and out to 824, its bevels' corners reaching 803.5 and 831.5 on the slot's
// lit edge, 809.5..811.5; the straight run over the third is 789..803.5. The plate right of it is plain
// out to 850.
const NotchFill notchFill { 17.0f, 17.0f, 803.0f, 813.0f, 831.0f, 11.0f, 16.0f, 2.0f };

const juce::Point<float> turbo { 921.5f, 787.3f };
const float turboSize = 52.0f;
const juce::Point<float> turboName { 921.5f, 682.0f };
const juce::Rectangle<float> turboLid { 873.5f, 689.0f, 101.0f, 168.0f };

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
    { 27.0f,   363.0f, 135.0f, 32.0f },     // the segments 37..152 x 373..385
    { 1510.0f, 363.0f, 132.0f, 32.0f },     // the segments 1520..1632 x 372..385
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
