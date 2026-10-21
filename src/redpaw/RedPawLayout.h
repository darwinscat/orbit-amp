// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"
#include "../face/Stage.h"
#include "../face/Strip.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

/** Where everything sits on the rover's dashboard, in the picture's own coordinates (1672 x 941; the
    picture itself is twice that). Measured off the picture — the cyan rings fitted as circles, the
    dark wells and slots as boxes — not guessed, so a control lands in the socket that was painted for
    it. The window scales this space as a whole.

    EVERY ROVER'S PICTURE IS MEASURED FOR ITSELF, AGAINST ONE PLAN. The dashboard is worn by more than
    one product, each over a picture of its own painted to the same plan (RedPawSkin.h). The plan is said
    once, here (Plan), as RedPawAmp's picture — the first, the one the others were painted to — measures
    it; each product's skin's file says only where its own picture measures otherwise:
    redpaw/RedPawSkin.cpp nowhere, frostpaw/FrostPawSkin.cpp in a few places, amberpaw/AmberPawSkin.cpp,
    a cockpit of its own, nearly everywhere. Each face reads its own skin's (Skin::plan). */
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

/** A knob's socket: the cyan ring painted round it, and how big the knob is drawn in it — so that the
    ring stays in sight all round. */
struct Socket { juce::Point<float> centre; float ring; float ringWidth; float knob; };

/** The lamps the dashboard lights itself (Plan::lamps). */
enum LampId { inClipLamp, outClipLamp, gateLamp, limiterLamp, numLamps };

inline constexpr float thresholdLowDb = -70.0f, thresholdHighDb = -40.0f;

/** THE PLAN, AS REDPAWAMP'S PICTURE MEASURES IT. A skin's own is this with the values its picture
    measures otherwise, each named (`.navScreen { ... }`), in the order they stand here; a value written
    in terms of another — LEARN over the picture, the corner slots' rims — follows that other where a
    skin moves it. */
struct Plan
{
    /** THE WINDSCREEN: the picture's cut-out, a trapezoid narrowing toward the dashboard, and the box
        round it. The view out of it fills the box and lies UNDER the picture, whose alpha is the window's
        frame; the emblem's plate rises into the bottom of it. */
    std::array<juce::Point<float>, 6> windscreenCorners {{
        { 122.0f, 47.5f }, { 1550.0f, 47.5f }, { 1556.0f, 60.0f }, { 1464.0f, 317.0f }, { 208.5f, 317.0f }, { 115.5f, 60.0f },
    }};
    juce::Rectangle<float> windscreen { 115.5f, 47.5f, 1440.5f, 269.5f };

    /** THE ROOF'S SIX HOLES, left to right: two, the divider, four. A key's sign stands in the gap left
        of its hole; there is no room above or under them. */
    std::array<juce::Point<float>, 6> holes {{
        { 683.3f, 22.7f }, { 727.2f, 22.8f }, { 809.3f, 22.7f }, { 853.3f, 23.1f }, { 897.0f, 23.0f }, { 941.5f, 22.6f },
    }};
    float holeRadius = 12.0f;
    float signOffset = 22.0f;       // a sign's centre, left of its hole's

    /** DRIVE's socket and the tones' (Socket), and the words over them. */
    Socket drive { { 374.3f, 538.3f }, 71.7f, 3.0f, 132.0f };
    std::array<Socket, 3> tones {{
        { { 1118.3f, 495.0f }, 40.6f, 2.2f, 104.0f },   // BASS
        { { 1240.5f, 495.0f }, 40.6f, 2.2f, 104.0f },   // MIDDLE
        { { 1363.3f, 494.9f }, 40.6f, 2.2f, 104.0f },   // TREBLE
    }};
    float knobNameY  = 423.0f;      // BASS, MIDDLE, TREBLE: on the panel over the sockets
    float knobNameEm = px13;        // ...how big they are written
    float driveNameY = 423.0f;      // ...and DRIVE's own word, over its socket

    /** THE SIX SQUARE KEYS under the tones: the two filters, the divider, the pack's four layers. A name
        stands over or under its key — neighbours by turns, since at 11 px they are wider than the keys'
        pitch: the filters and FAT and DEPTH under, BRIGHT and PRESENCE over. */
    std::array<juce::Point<float>, 6> keys {{
        { 1093.5f, 603.5f }, { 1141.5f, 603.5f }, { 1226.5f, 603.5f }, { 1280.5f, 603.5f }, { 1333.5f, 603.5f }, { 1388.0f, 603.5f },
    }};
    float keySize       = 48.0f;    // the key's render, whose face fills the 37 x 41 hole
    float keyNameOverY  = 563.0f;
    float keyNameUnderY = 642.0f;
    std::array<bool, 6> keyNameOver {{ false, false, false, true, false, true }};
    float keyNameRoom = 120.0f;     // how wide a name may be: a wider one is set condensed into it, as tall
    /** ...OR THE NAMES ON THE KEYS' FACES, where the picture leaves them no room round the keys: the
        filters' a size down on their keys, the pack's layers' none until the packs carry them. */
    bool keyNamesOnKeys = false;    // the names round the keys

    /** THE SEARCHLIGHTS' KEYS, where the face has lights to throw (Drive::setLights): which of the six
        are the left lamp's and the right's, each a lamp's sign on its face; -1 for none. */
    std::array<int, 2> lightKeys {{ -1, -1 }};      // no searchlights

    /** THE NAV SCREEN: along its top the gear, the channel and the rover's speed; under them a picture of
        the amp at work; along its bottom the tuner, one low line. */
    juce::Rectangle<float> navScreen  { 563.5f, 423.5f, 436.5f, 195.0f };
    juce::Rectangle<float> navHeader  { 567.0f, 426.0f, 430.0f, 32.0f };
    juce::Rectangle<float> navPicture { 567.0f, 460.0f, 430.0f, 126.0f };

    /** ...OR THE SPEED AS A HOLOGRAM ON THE GLASS (SpeedHud), where this is not empty: the nav screen's
        top line is then the gear alone. */
    juce::Rectangle<float> speedHud {};             // none: the speed is on the nav screen

    juce::Rectangle<float> tunerScreen { 567.0f, 588.0f, 430.0f, 27.0f };     // the tuner, the screen's bottom line

    /** ...OR THE TUNER IS A GAUGE THE PICTURE PAINTS (face::TunerFace::Dial), where this has a reach: a
        needle over its dial, `tunerScreen` the dial's face, which a click opens the tuner's settings on.
        The nav screen is then the top line and the picture alone. */
    face::TunerFace::Dial tunerDial {};             // none: the tuner is the screen's bottom line

    /** LEARN: where GATE's learning or HUM's shows while it runs — over the nav screen's picture, or over
        the whole screen, its top line too, where the picture is too small to hold it — and a round key
        for each where the picture paints holes for them, GATE's over HUM's: lit while its learning shows,
        LEARN over its name in two lines over it. A size of nought: no keys, LEARN only from the menus and
        GATE's lamp. */
    juce::Rectangle<float> learnArea = navPicture;  // LEARN over the picture...
    std::array<juce::Point<float>, 2> learnKeys {}; // GATE's, HUM's
    float learnKeySize = 0.0f;                      // ...and no keys of its own (the key's render, whose button fills the hole)
    std::array<juce::Point<float>, 2> learnNames {};    // the middle of each key's two lines
    float learnLine = 0.0f;                         // ...the two lines apart

    /** ...AND HUM'S HOLE WHILE THE BUILD HAS NO HUM (Features.h): plate, laid over it once, as the picture
        is loaded (Art::of) — a stretch of the same plate, `learnPlate`, with the same stretch upside down
        under it, the two centred on HUM's key and cut to a round `learnHoleSize` across, its edge soft
        for the last pixel or two. The plate reads on as if the key had never been there. Empty: nothing
        to cover. */
    juce::Rectangle<float> learnPlate {};
    float learnHoleSize = 0.0f;

    /** THE WIPERS' SPEED: four keys where the picture paints a row of them — OFF, INT, LO, HI, each word
        on its key, the speed set lit. A size of nought: none, the wipers keep their own time. */
    std::array<juce::Point<float>, 4> wiperKeys {};     // no keys for the wipers
    float wiperKeySize = 0.0f;                      // the key's render, whose face fills the hole

    /** THE GEAR LEVER'S SLOT: straight, and its notches on its right — the product's gears, CLEAN,
        CRUNCH, HI-GAIN, stand at its `stops` from the top, as many as it has
        (RedPawLineup.h): in the notches the picture paints, or, where a product spreads fewer gears down a
        slot painted for more, at places of its own, the picture baked with its notches there and none
        where no gear stands (tools/bake-art). A position is where the lever's stem stands: seen from
        above, as the whole dashboard is, the lever is its T grip lying across the slot, the stem's collar
        under the grip's middle standing on the position — in a notch, in the notch.

        The slot is 785.5..811 across, its top end at 681, its notches out to 823. */
    juce::Rectangle<float> gateSlot { 785.5f, 681.0f, 37.5f, 174.0f };
    float slotX  = 798.3f;          // the stem in the slot
    float notchX = 814.0f;          // ...and in a notch
    // Where the gears stand, top to bottom. Two gears down a slot painted for three notches, 733.8, 776
    // and 817.5, spread evenly from the slot's top end at 694: a step of 61.75 to the middle and another
    // to the third notch, so the slot is the lever's to its end — the picture baked with the first two
    // filled in and the middle one the second, copied up (tools/bake-art).
    std::vector<float> stops { 755.75f, 817.5f };
    float leverWidth = 124.0f;      // the lever's render, grip and collar, this wide: over the plate's edges
    float collarSink = 3.0f;        // how far the collar's foot goes past the position, into the slot
    juce::Rectangle<float> gearPlate { 742.0f, 655.0f, 116.0f, 222.0f };
    float leverLeft = 724.0f, leverTop = 600.0f;    // the lever's box: the plate, and the room over it the grip stands up into

    /** TURBO: the round hole under the hatch the picture shows open, and its name over the hatch. */
    juce::Point<float> turbo { 921.5f, 787.3f };
    float turboSize = 52.0f;        // the button's render
    juce::Point<float> turboName { 921.5f, 682.0f };

    /** THE THREE WELLS — REVERB over DELAY on the left, CAB on the right — and the row each one's words
        are written in: on REVERB's and DELAY's upper rims, which the picture is baked taller to carry them
        where it was painted without room for them (tools/bake-art), and over CAB's plate. */
    std::array<juce::Rectangle<float>, 3> wells {{
        { 315.0f,  700.5f, 326.5f, 21.5f },     // REVERB
        { 314.5f,  774.0f, 326.5f, 22.0f },     // DELAY
        { 1053.0f, 730.5f, 309.5f, 24.0f },     // CAB
    }};
    std::array<float, 3> wellNameY {{ 690.0f, 762.5f, 695.0f }};

    float sliderCapLength = 54.0f;  // the cap's render, along its well
    float sliderHeight    = 44.0f;  // the slider's box round its well: room for the cap and CAB's notches

    // Each side column holds two wells: the narrow one a meter, the slot a fader's.
    juce::Rectangle<float> inMeter  { 48.0f,   444.5f, 34.5f, 244.0f };
    juce::Rectangle<float> inFader  { 124.5f,  447.0f, 25.0f, 235.5f };
    juce::Rectangle<float> outMeter { 1512.0f, 448.0f, 34.5f, 237.0f };
    juce::Rectangle<float> outFader { 1588.0f, 447.5f, 24.5f, 235.0f };
    float faderWidth     = 44.0f;   // the fader's box round its slot
    float faderCapLength = 18.0f;   // the cap's render, across its slot

    /** THE METERS AS DOTS THE PICTURE PAINTS (face::SegmentMeter::Dots), where there is a count: the dots
        down the middle of inMeter's and outMeter's boxes, a pitch apart. None: segments down their wells. */
    face::SegmentMeter::Dots meterDots {};          // none: segments down the wells

    /** THE CORNER LAMPS' PLATES, each with a fader's slot down its outer leg, its rim included — painted,
        or the plate baked grown on that leg with a slot of INPUT's kind cut down it (tools/bake-art):
        GATE's threshold in the left one, -70 dB at its foot to -40 at its head; LIMITER's ceiling in the
        right one, -3 dB to -0.1. A slot lying across takes a fader lying across, its foot on the left.

        Each corner plate grown 24 on its outer leg, a slot cut down the room (tools/bake-art). */
    juce::Rectangle<float> thresholdSlot { 40.5f, 808.0f, 18.0f, 80.0f };
    juce::Rectangle<float> ceilingSlot   { 1614.5f, 805.0f, 18.0f, 80.0f };
    float slotScale = 18.0f / 31.0f;                // the corner slots' size to INPUT's
    float slotRim = 3.0f * slotScale;

    /** THE LAMPS THE DASHBOARD LIGHTS ITSELF, each with room round it for its glow: the bars of three
        over the columns, the input's overload and the output's; the slanted ones in the bottom corners,
        the gate's and the limiter's. The picture paints them lit; it is baked with them out and each cut
        out of it (tools/bake-art), to be laid back over its box. A picture that paints none lit has them
        drawn, each a round glass `lampDot` across in the middle of its box (face::Lamp::dot); none where
        that is nothing. */
    std::array<juce::Rectangle<float>, numLamps> lamps {{
        { 27.0f,   363.0f, 135.0f, 32.0f },     // the segments 37..152 x 373..385
        { 1510.0f, 363.0f, 132.0f, 32.0f },     // the segments 1520..1632 x 372..385
        { 69.0f,   824.0f, 62.0f, 63.0f },      // the slant 55..97 x 834..877 as painted, moved in with its plate
        { 1541.0f, 824.0f, 62.0f, 63.0f },      // the slant 1575..1617 x 834..877 as painted, moved in with its plate
    }};
    float lampDot = 0.0f;                           // none: the picture paints them lit

    /** INPUT and OUTPUT, on the plate under each column's wells, between its screws; a word wider than
        the room it has there is set condensed into it, as tall. */
    juce::Point<float> inputName  { 101.8f, 721.7f };
    juce::Point<float> outputName { 1570.0f, 721.7f };
    float columnNameRoom = 170.0f;

    /** GATE and LIMITER along their own lamps in the bottom corners: each word parallel to its lamp's
        slant, on the plate beside the lamp's housing — GATE down to the right, LIMITER up to the right. */
    juce::Point<float> gateWord    { 134.0f, 824.0f };
    juce::Point<float> limiterWord { 1538.0f, 825.0f };
    float lampSlant = 0.7854f;      // 45 degrees

    /** THE PLATES ON THE BOTTOM EDGE, between their screws: PRESET alone on the long one on the left, MEM
        over CPU on the long one on the right, and the version on the short one in the middle, under the
        gate's block. MEM's and CPU's rows each start where MEM of its usual width would be centred, so
        that a number that changes width moves nothing else. */
    juce::Rectangle<float> leftPlate { 340.0f, 852.0f, 262.0f, 57.0f };
    std::array<juce::Rectangle<float>, 2> readings {{ { 1132.5f, 853.0f, 210.0f, 28.0f }, { 1132.5f, 881.0f, 210.0f, 28.0f } }};     // MEM over CPU, on the right plate, 1103..1332
    juce::Rectangle<float> centrePlate { 755.0f, 895.0f, 195.0f, 33.0f };

    /** The emblem plate under the windscreen: a flat hexagon the family's mark is cut into, clear of its
        six screws. */
    std::array<juce::Point<float>, 6> emblemFace {{
        { 809.0f, 295.6f }, { 865.0f, 295.6f }, { 888.0f, 335.0f }, { 865.0f, 375.0f }, { 809.0f, 375.0f }, { 787.0f, 335.0f },
    }};
    juce::Rectangle<float> emblemMark { 813.0f, 310.0f, 50.0f, 50.0f };

    /** THE BADGE'S PLACE, where the skin wears the family's badge (Skin::badge) over that plate: the
        cockpit's plate (catpit/CatpitLayout.h: emblemFace) as it is — its wide flat top, its sides slanting out
        and then in to the narrower bottom, the shallow nose its bottom edge falls to in the middle — scaled by
        0.715 about x = 835.5. Bounded by the frame's raised step it sits on: its top 3.5 under the step's top
        (y 289, x 789..881) and its upper sides as far in from the step's shoulders, so that the step's edge
        shows all round the top; its nose on the rail under it (y 380), the bottom edge's two ends 5 above
        the rail, a small gap at either side; and over the picture's hexagon and its groove (783..889 x
        294..379) but for the groove's bottom line, which shows in those two gaps as their shadow. Its six
        screws where the cockpit has its own, scaled the same; the mark a little larger than the cockpit's
        share of its plate, as the wide top leaves its ears room. */
    std::array<juce::Point<float>, 7> badgeFace {{
        { 790.1f, 292.5f }, { 880.9f, 292.5f }, { 902.7f, 313.2f }, { 880.5f, 374.7f }, { 835.5f, 379.0f },
        { 790.5f, 374.7f }, { 768.3f, 313.2f },
    }};
    std::array<juce::Point<float>, 6> badgeScrews {{
        { 793.0f, 296.8f }, { 878.0f, 296.8f }, { 771.9f, 315.4f }, { 899.1f, 315.4f }, { 796.5f, 371.9f }, { 874.5f, 371.9f },
    }};
    float badgeScrew = 5.7f;    // the screws' heads, across: the cockpit's 8, scaled
    juce::Rectangle<float> badgeMark { 797.0f, 295.8f, 77.0f, 75.0f };

    /** THE MAKER'S WORDS beside the mark, where the picture has room for them: POWERED BY low on its
        left, DARWIN'S CAT low on its right, each set condensed into its room where it is wider. A room of
        nought: none. */
    juce::Point<float> poweredBy {}, makerName {};  // no room beside the plate for the maker's words
    float makerRoom = 0.0f;

    /** THE ROWS THE PICTURE NO LONGER PAINTS: the tone knobs' sockets and the frame round the keys right
        of the filters are drawn by code, as many as the playing pack has (face::Console::layStrips), in
        the room the three sockets and the four-key frame stood in — `tones` and the last four `keys`
        measure it. The socket's render (socket.webp) is `socketSize` across at the nominal size, its
        ring `tones`' ring; the frame is a left cap, a cell for each key, a right cap (key-frame-*.webp),
        `keyFrameHeight` tall. Either row shrinks to fit, to `toneMinScale` and `keyMinScale` of it. */
    float socketSize = 124.0f;
    float toneMinScale = 0.6f;
    float keyCapLeft = 5.5f, keyCapRight = 11.0f, keyFrameHeight = 70.0f;
    float keyMinScale = 0.75f;
    /** Each tone knob's sign (face::drawToneSign): this far under the names' row, nearer its knob, and
        this tall. */
    float toneSignDrop = 3.0f, toneSignHeight = 12.0f;

    face::Strip toneStrip() const
    {
        const float pitch = 0.5f * (tones[2].centre.x - tones[0].centre.x);
        const float left = tones[0].centre.x - 0.5f * socketSize, right = tones[2].centre.x + 0.5f * socketSize;
        return { { left, tones[1].centre.y - 0.5f * socketSize, right - left, socketSize }, pitch, socketSize, toneMinScale };
    }

    face::Strip keyStrip() const
    {
        const float pitch = (keys[5].x - keys[2].x) / 3.0f;
        const float left = keys[2].x - 0.5f * pitch - keyCapLeft, right = keys[5].x + 0.5f * pitch + keyCapRight;
        return { { left, keys[2].y - 0.5f * keyFrameHeight, right - left, keyFrameHeight }, pitch, pitch + keyCapLeft + keyCapRight, keyMinScale };
    }
};

/** The product's own plan is in its skin's file, and its face reads it there (Skin::plan): every rover's
    sources link into one binary, so no plan is THE plan. */

} // namespace orbitamp::redpaw::layout
