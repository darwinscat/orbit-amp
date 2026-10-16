// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

/** Where everything sits on the rover's dashboard, in the picture's own coordinates (1672 x 941; the
    picture itself is twice that). Measured off the picture — the cyan rings fitted as circles, the
    dark wells and slots as boxes — not guessed, so a control lands in the socket that was painted for
    it. The window scales this space as a whole.

    EVERY ROVER'S PICTURE IS MEASURED FOR ITSELF. The dashboard is worn by more than one product, each
    over a picture of its own painted to the same plan (RedPawSkin.h): what is said here is what there
    is to measure; the numbers are the product's own, in its skin's file — redpaw/RedPawSkin.cpp for
    RedPawAmp, frostpaw/FrostPawSkin.cpp for FrostPawAmp — and a target compiles exactly one. */
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
extern const std::array<juce::Point<float>, 6> windscreenCorners;
extern const juce::Rectangle<float> windscreen;

/** THE ROOF'S SIX HOLES, left to right: two, the divider, four. A key's sign stands in the gap left of
    its hole; there is no room above or under them. */
extern const std::array<juce::Point<float>, 6> holes;
extern const float holeRadius;
extern const float signOffset;      // a sign's centre, left of its hole's

/** A knob's socket: the cyan ring painted round it, and how big the knob is drawn in it — so that the
    ring stays in sight all round. */
struct Socket { juce::Point<float> centre; float ring; float ringWidth; float knob; };

extern const Socket drive;
extern const std::array<Socket, 3> tones;       // BASS, MIDDLE, TREBLE
extern const float knobNameY;       // DRIVE, BASS, MIDDLE, TREBLE: on the panel over the sockets

/** THE SIX SQUARE KEYS under the tones: the two filters, the divider, the pack's four layers. A name
    stands over or under its key — neighbours by turns, since at 11 px they are wider than the keys'
    pitch: the filters and FAT and DEPTH under, BRIGHT and PRESENCE over. */
extern const std::array<juce::Point<float>, 6> keys;
extern const float keySize;         // the key's render, whose face fills its hole
extern const float keyNameOverY;
extern const float keyNameUnderY;
extern const std::array<bool, 6> keyNameOver;

/** THE NAV SCREEN: along its top the gear, the channel and the rover's speed; under them a picture of
    the amp at work; along its bottom the tuner, one low line. */
extern const juce::Rectangle<float> navScreen;
extern const juce::Rectangle<float> navHeader;
extern const juce::Rectangle<float> navPicture;
extern const juce::Rectangle<float> tunerScreen;

/** THE GEAR LEVER'S SLOT: straight, its top end PARK, and three notches painted on its right — the
    product's gears, CLEAN, CRUNCH, HI-GAIN, stand in them from the top, as many as it has (RedPawLineup.h).
    A position is where the lever's stem stands: seen from above, as the whole
    dashboard is, the lever is its T grip lying across the slot, the stem's collar under the grip's
    middle standing on the position — in a notch, in the notch. */
extern const juce::Rectangle<float> gateSlot;
extern const float slotX;           // the stem in the slot
extern const float notchX;          // ...and in a notch
extern const float slotTopY;        // PARK: the stem at the slot's top end
extern const std::array<float, 3> notches;      // the notches' middles, as painted, top to bottom
extern const float leverWidth;      // the lever's render, grip and collar, this wide: over the plate's edges
extern const float collarSink;      // how far the collar's foot goes past the position, into the slot
extern const juce::Rectangle<float> gearPlate;

/** A NOTCH PAINTED PAST THE PRODUCT'S LAST GEAR IS FILLED IN (Art: `fillSpareNotches`), so the slot runs
    straight there and the lever has nowhere to stand that is not a gear. From `above` the notch's middle
    to `below` it: the slot's edge, from `left` in the slot's dark to `seam`, is the straight run just
    over the fill, `run` tall, mirrored down and back, so the edge's lit line goes on unbroken; the plate
    beside it, from `seam` to `right`, is the plate `plateShift` further right on the same rows, so its
    grain and its light are the rows' own. The fill fades into the picture over `feather` at its top, its
    bottom and its right, and the edge into the plate across the seam. */
struct NotchFill { float above, below, left, seam, right, run, plateShift, feather; };
extern const NotchFill notchFill;

/** TURBO: the round hole under the hatch the picture shows open, and its name over the hatch. */
extern const juce::Point<float> turbo;
extern const float turboSize;       // the button's render
extern const juce::Point<float> turboName;

/** THE THREE WELLS — REVERB over DELAY on the left, CAB on the right — and the row each one's words are
    written in: on REVERB's and DELAY's upper rims, which the picture is made taller to carry them
    (Art: `rims`), and over CAB's plate. */
extern const std::array<juce::Rectangle<float>, 3> wells;      // REVERB, DELAY, CAB
extern const std::array<float, 3> wellNameY;

/** REVERB's and DELAY's frames, as the picture painted them: the plate's top (its shadow), where the rim's
    lit edge ends and its face begins, where the well opens under it, and how far down the end caps'
    top screws reach. Each upper rim is made `rimLift` taller, upward. */
struct Rim { float left, right, top, face, well, capScrews, capLeft, capRight; };
extern const std::array<Rim, 2> rims;           // REVERB, DELAY
extern const float rimLift;
extern const float sliderCapLength;     // the cap's render, along its well
extern const float sliderHeight;        // the slider's box round its well: room for the cap and CAB's notches

// Each side column holds two wells: the narrow one a meter, the slot a fader's.
extern const juce::Rectangle<float> inMeter;
extern const juce::Rectangle<float> inFader;
extern const juce::Rectangle<float> outMeter;
extern const juce::Rectangle<float> outFader;
extern const float faderWidth;          // the fader's box round its slot
extern const float faderCapLength;      // the cap's render, across its slot

/** THE CORNER LAMPS' PLATES GROW ON THEIR OUTER LEGS, each for a fader's slot (Art: `widenLampPlates`):
    the plate as painted is cut along its outer leg, between its edge and its screws; all of it inward
    of the cut — the screws, the lamp, the slanted edge — moves `lampPlateGrow` inward, and the room left
    is filled with the plate's own top and bottom edges and plain metal between, a slot of INPUT's kind
    cut down it. GATE's threshold in the left one, -70 dB at its foot to -40 at its head; LIMITER's
    ceiling in the right one, -3 dB to -0.1. */
extern const float lampPlateGrow;
struct LampPlate
{
    juce::Rectangle<float> moved;       // what moves inward: the plate inward of the cut, with its shadow...
    std::array<juce::Point<float>, 5> outline;  // ...cut to the plate's own outline, so the panel round it stays put
    float shift;                        // ...this far, to the right for plus
    float room;                         // the new room's left edge; it is lampPlateGrow wide
    std::array<float, 3> from;          // where its pieces are taken from, across: the top edge, the plain metal, the bottom edge
    std::array<float, 4> rows;          // the pieces' rows, the same where they are taken and where they go
    juce::Rectangle<float> slot;        // the slot cut down the room, its rim included
};
extern const LampPlate gatePlate;
extern const LampPlate limiterPlate;

/** The slot both are cut from: INPUT's, with its rim, as painted — its round ends kept whole, its straight
    run cut short. */
extern const juce::Rectangle<float> inSlotRim;
extern const float inSlotEnd;           // each round end, rim and all
extern const float slotScale;           // the corner slots' size to INPUT's
extern const float slotRim;
inline constexpr float thresholdLowDb = -70.0f, thresholdHighDb = -40.0f;

/** THE LAMPS THE DASHBOARD LIGHTS ITSELF, each with room round it for its glow: the bars of three over
    the columns, the input's overload and the output's; the slanted ones in the bottom corners, the
    gate's and the limiter's. The picture paints them lit; Art puts them out. */
enum LampId { inClipLamp, outClipLamp, gateLamp, limiterLamp, numLamps };
extern const std::array<juce::Rectangle<float>, numLamps> lamps;

/** INPUT and OUTPUT, on the plate under each column's wells, between its screws. */
extern const juce::Point<float> inputName;
extern const juce::Point<float> outputName;

/** GATE and LIMITER along their own lamps in the bottom corners: each word parallel to its lamp's
    slant, on the plate beside the lamp's housing — GATE down to the right, LIMITER up to the right. */
extern const juce::Point<float> gateWord;
extern const juce::Point<float> limiterWord;
extern const float lampSlant;

/** THE PLATES ON THE BOTTOM EDGE, between their screws: PRESET alone on the long one on the left, MEM
    over CPU on the long one on the right, and the version on the short one in the middle, under the
    gate's block. */
extern const juce::Rectangle<float> leftPlate;
extern const juce::Rectangle<float> rightPlate;
extern const juce::Rectangle<float> centrePlate;

/** The emblem plate under the windscreen: a flat hexagon the family's mark is cut into, clear of its six
    screws. */
extern const std::array<juce::Point<float>, 6> emblemFace;
extern const juce::Rectangle<float> emblemMark;

} // namespace orbitamp::redpaw::layout
