// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

/** Where everything sits on the rover's dashboard, in the picture's own coordinates (1672 x 941; the
    picture itself is twice that). Measured off the picture — the cyan rings fitted as circles, the
    dark wells and slots as boxes — not guessed, so a control lands in the socket that was painted for
    it. The window scales this space as a whole. */
namespace orbitamp::redpaw::layout
{

// The stage's own space and type sizes: the picture was composed in the space every face is drawn on.
using face::stage::width;
using face::stage::height;
using face::stage::readingWidth;
using face::stage::readingScale;
using face::stage::minWindowWidth;
using face::stage::px11;
using face::stage::px13;
using face::stage::px15;
using face::stage::px16;

/** THE WINDSCREEN: the picture's cut-out, a trapezoid narrowing toward the dashboard, and the box round
    it. The view out of it fills the box and lies UNDER the picture, whose alpha is the window's frame;
    the emblem's plate rises into the bottom of it. */
inline const std::array<juce::Point<float>, 6> windscreenCorners {{
    { 122.0f, 47.5f }, { 1550.0f, 47.5f }, { 1556.0f, 60.0f }, { 1464.0f, 317.0f }, { 208.5f, 317.0f }, { 115.5f, 60.0f },
}};
inline const juce::Rectangle<float> windscreen { 115.5f, 47.5f, 1440.5f, 269.5f };

/** THE ROOF'S SIX HOLES, left to right: two, the divider, four. A key's sign stands in the gap left of
    its hole; there is no room above or under them. */
inline const std::array<juce::Point<float>, 6> holes {{
    { 683.3f, 22.7f }, { 727.2f, 22.8f }, { 809.3f, 22.7f }, { 853.3f, 23.1f }, { 897.0f, 23.0f }, { 941.5f, 22.6f },
}};
inline constexpr float holeRadius = 12.0f;

/** TWO KEYS ON THE ROOF, each in the plain panel between the toggles' plate and a grille: on the left
    the whole face full screen, on the right the landscape alone. */
inline const juce::Point<float> faceScreenKey      { 515.0f, 22.5f };
inline const juce::Point<float> landscapeScreenKey { 1125.0f, 22.5f };
inline constexpr float screenKeySize = 40.0f;
inline constexpr float signOffset = 22.0f;      // a sign's centre, left of its hole's

/** A knob's socket: the cyan ring painted round it, and how big the knob is drawn in it — so that the
    ring stays in sight all round. */
struct Socket { juce::Point<float> centre; float ring; float ringWidth; float knob; };

inline const Socket drive { { 374.3f, 538.3f }, 71.7f, 3.0f, 132.0f };
inline const std::array<Socket, 3> tones {{
    { { 1118.3f, 495.0f }, 40.6f, 2.2f, 104.0f },   // BASS
    { { 1240.5f, 495.0f }, 40.6f, 2.2f, 104.0f },   // MIDDLE
    { { 1363.3f, 494.9f }, 40.6f, 2.2f, 104.0f },   // TREBLE
}};
inline constexpr float knobNameY = 423.0f;      // DRIVE, BASS, MIDDLE, TREBLE: on the panel over the sockets

/** THE SIX SQUARE KEYS under the tones: the two filters, the divider, the pack's four layers. A name
    stands over or under its key — neighbours by turns, since at 11 px they are wider than the keys'
    pitch: the filters and FAT and DEPTH under, BRIGHT and PRESENCE over. */
inline const std::array<juce::Point<float>, 6> keys {{
    { 1093.5f, 603.5f }, { 1141.5f, 603.5f }, { 1226.5f, 603.5f }, { 1280.5f, 603.5f }, { 1333.5f, 603.5f }, { 1388.0f, 603.5f },
}};
inline constexpr float keySize      = 48.0f;    // the key's render, whose face fills the 37 x 41 hole
inline constexpr float keyNameOverY = 563.0f;
inline constexpr float keyNameUnderY = 642.0f;
inline constexpr std::array<bool, 6> keyNameOver {{ false, false, false, true, false, true }};

/** THE NAV SCREEN: along its top the gear, the channel and the rover's speed; under them a picture of
    the amp at work; along its bottom the tuner, one low line. */
inline const juce::Rectangle<float> navScreen   { 563.5f, 423.5f, 436.5f, 195.0f };
inline const juce::Rectangle<float> navHeader   { 567.0f, 426.0f, 430.0f, 32.0f };
inline const juce::Rectangle<float> navPicture  { 567.0f, 460.0f, 430.0f, 126.0f };
inline const juce::Rectangle<float> tunerScreen { 567.0f, 588.0f, 430.0f, 27.0f };

/** THE GEAR LEVER'S SLOT: straight, 785.5..811 across, its top end at 681 — PARK — and three notches on
    its right, out to 823 — CLEAN, CRUNCH, HI-GAIN, top to bottom. A position is where the lever's stem
    enters the slot: its grip stands over it, the stem goes in under the grip's collar, and in a notch
    the stem stands in the notch. */
inline const juce::Rectangle<float> gateSlot { 785.5f, 681.0f, 37.5f, 174.0f };
inline constexpr float slotX  = 798.3f;         // the stem in the slot
inline constexpr float notchX = 814.0f;         // ...and in a notch
inline constexpr float slotTopY = 694.0f;       // PARK: the stem at the slot's top end
inline const std::array<float, 3> notches {{ 733.8f, 776.0f, 817.5f }};     // the notches' middles
inline constexpr float stemIn = 11.0f;          // how far over a position the stem enters the slot...
inline constexpr float stemStub = 10.0f;        // ...and how far under it the stem is seen
inline constexpr float leverSize = 88.0f;       // the lever's render: grip on a stem to its bottom edge
inline const juce::Rectangle<float> gearPlate { 742.0f, 655.0f, 116.0f, 222.0f };

/** TURBO: the round hole under the hatch the picture shows open, and its name over the hatch. */
inline const juce::Point<float> turbo { 921.5f, 787.3f };
inline constexpr float turboSize = 52.0f;       // the button's render
inline const juce::Point<float> turboName { 921.5f, 682.0f };

/** THE THREE WELLS — REVERB over DELAY on the left, CAB on the right — and the row each one's words are
    written in: on REVERB's and DELAY's upper rims, which the picture is made taller to carry them
    (Art: `rims`), and over CAB's plate. */
inline const std::array<juce::Rectangle<float>, 3> wells {{
    { 315.0f,  700.5f, 326.5f, 21.5f },     // REVERB
    { 314.5f,  774.0f, 326.5f, 22.0f },     // DELAY
    { 1053.0f, 730.5f, 309.5f, 24.0f },     // CAB
}};
inline const std::array<float, 3> wellNameY {{ 690.0f, 762.5f, 695.0f }};

/** REVERB's and DELAY's frames, as the picture painted them: the plate's top (its shadow), where the rim's
    lit edge ends and its face begins, where the well opens under it, and how far down the end caps'
    top screws reach. Each upper rim is made `rimLift` taller, upward. */
struct Rim { float left, right, top, face, well, capScrews, capLeft, capRight; };
inline const std::array<Rim, 2> rims {{
    { 276.0f, 681.0f, 683.0f, 691.0f, 699.5f, 703.0f, 316.0f, 637.0f },     // REVERB
    { 276.0f, 681.0f, 755.5f, 763.0f, 772.5f, 777.0f, 316.0f, 637.0f },     // DELAY
}};
inline constexpr float rimLift = 10.0f;
inline constexpr float sliderCapLength = 54.0f;     // the cap's render, along its well
inline constexpr float sliderHeight    = 44.0f;     // the slider's box round its well: room for the cap and CAB's notches

// Each side column holds two wells: the narrow one a meter, the slot a fader's.
inline const juce::Rectangle<float> inMeter  { 48.0f,   444.5f, 34.5f, 244.0f };
inline const juce::Rectangle<float> inFader  { 124.5f,  447.0f, 25.0f, 235.5f };
inline const juce::Rectangle<float> outMeter { 1512.0f, 448.0f, 34.5f, 237.0f };
inline const juce::Rectangle<float> outFader { 1588.0f, 447.5f, 24.5f, 235.0f };
inline constexpr float faderWidth     = 44.0f;      // the fader's box round its slot
inline constexpr float faderCapLength = 18.0f;      // the cap's render, across its slot


/** THE LAMPS THE DASHBOARD LIGHTS ITSELF, each with room round it for its glow: the bars of three over
    the columns, the input's overload and the output's; the slanted ones in the bottom corners, the
    gate's and the limiter's. The picture paints them lit; Art puts them out. */
enum LampId { inClipLamp, outClipLamp, gateLamp, limiterLamp, numLamps };
inline const std::array<juce::Rectangle<float>, numLamps> lamps {{
    { 27.0f,   363.0f, 135.0f, 32.0f },     // the segments 37..152 x 373..385
    { 1510.0f, 363.0f, 132.0f, 32.0f },     // the segments 1520..1632 x 372..385
    { 45.0f,   824.0f, 62.0f,  63.0f },     // the slant 55..97 x 834..877
    { 1565.0f, 824.0f, 62.0f,  63.0f },     // the slant 1575..1617 x 834..877
}};

/** INPUT and OUTPUT, on the plate under each column's wells, between its screws. */
inline const juce::Point<float> inputName  { 101.8f, 721.7f };
inline const juce::Point<float> outputName { 1570.0f, 721.7f };

/** GATE and LIMITER along their own lamps in the bottom corners: each word parallel to its lamp's
    slant, on the plate beside the lamp's housing — GATE down to the right, LIMITER up to the right. */
inline const juce::Point<float> gateWord    { 110.0f, 824.0f };
inline const juce::Point<float> limiterWord { 1562.0f, 825.0f };
inline constexpr float lampSlant = 0.7854f;     // 45 degrees

/** THE PLATES ON THE BOTTOM EDGE, between their screws: PRESET alone on the long one on the left, MEM
    over CPU on the long one on the right, and the version on the short one in the middle, under the
    gate's block. */
inline const juce::Rectangle<float> leftPlate   { 340.0f, 852.0f, 262.0f, 57.0f };
inline const juce::Rectangle<float> rightPlate  { 1103.0f, 852.0f, 229.0f, 57.0f };
inline const juce::Rectangle<float> centrePlate { 755.0f, 895.0f, 195.0f, 33.0f };

/** The emblem plate under the windscreen: a flat hexagon the family's mark is cut into, clear of its six
    screws. */
inline const std::array<juce::Point<float>, 6> emblemFace {{
    { 809.0f, 295.6f }, { 865.0f, 295.6f }, { 888.0f, 335.0f }, { 865.0f, 375.0f }, { 809.0f, 375.0f }, { 787.0f, 335.0f },
}};
inline const juce::Rectangle<float> emblemMark { 813.0f, 310.0f, 50.0f, 50.0f };

} // namespace orbitamp::redpaw::layout
