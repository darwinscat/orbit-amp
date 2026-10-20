// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "../redpaw/RedPawSkin.h"
#include "../redpaw/RedPawLayout.h"
#include "../redpaw/RedPawLineup.h"
#include "view/TitanDrive.h"

#include "../PluginProcessor.h"
#include "../face/Renders.h"

/** AMBERPAWAMP'S OWN (redpaw/RedPawSkin.h): a helicopter's cockpit over Titan's methane sea — dark
    gunmetal under a canopy of three panes, white rings and white ticks, no lamp painted lit. The face
    and the controls are the rovers': the GAIN knob is their DRIVE, the lever's slot runs straight
    between its ticks, the tuner is the gauge left of the centre, the meters are the dots beside the
    edge columns' slots. What is here is its channels, the picture's measure, its light and its renders. */
namespace orbitamp::redpaw
{

namespace
{

constexpr const char* name = "AmberPawAmp";

/** ITS CHANNELS (redpaw/RedPawLineup.h): the Victory V4's six packs, three gears of two, as FrostPawAmp
    has them for now (v4Lineup), until it has captures of its own. */
const channels::Lineup& titanLineup()
{
    static const channels::Lineup heli = v4Lineup();
    return heli;
}

//==============================================================================
// THE COCKPIT, MEASURED (RedPawLayout.h) off its own picture, 1672 x 941: the white rings fitted as
// circles, the dark wells, slots, holes and screens as boxes, the dial's ticks by where their strokes
// point. The rovers' plan is kept where the picture keeps it, and said otherwise where it does not:
// the canopy is the whole top; the lever's slot is straight, its five ticks to its right; the tuner is
// a gauge; the meters are dots; the corner slots lie across and are painted, so the plates grow nothing;
// the wells' words have room as painted, so no rim is lifted.
// The radio's screen's top line and the picture under it, named here so that LEARN can lie over both.
const juce::Rectangle<float> radioTopLine { 123.0f, 531.0f, 154.0f, 22.0f };
const juce::Rectangle<float> radioPicture { 123.0f, 555.0f, 154.0f, 67.0f };

const layout::Plan plan {

    // No trapezoid: the canopy's three panes and the struts between them fill the whole top, down to 417 at
    // its sides, and the picture's alpha is the frame.
    .windscreenCorners {{
        { 0.0f, 0.0f }, { 836.0f, 0.0f }, { 1672.0f, 0.0f }, { 1672.0f, 418.0f }, { 836.0f, 418.0f }, { 0.0f, 418.0f },
    }},
    .windscreen { 0.0f, 0.0f, 1672.0f, 418.0f },

    // The overhead strip over the middle pane: two holes, the divider at 772, four; each 22.5 across.
    .holes {{
        { 678.2f, 30.4f }, { 732.9f, 30.2f }, { 811.8f, 30.4f }, { 866.7f, 30.2f }, { 922.2f, 30.0f }, { 976.7f, 30.3f },
    }},
    .holeRadius = 11.5f,
    .signOffset = 27.0f,

    // GAIN's ring, white, 89..92 out from its middle; the knob's knurl inside it. The tones' rings 43.4..44.9.
    .drive { { 732.7f, 624.5f }, 90.8f, 3.0f, 160.0f },
    .tones {{
        { { 1217.1f, 589.4f }, 44.2f, 2.2f, 76.0f },    // BASS
        { { 1348.2f, 589.5f }, 44.2f, 2.2f, 76.0f },    // MIDDLE
        { { 1479.0f, 589.4f }, 44.2f, 2.2f, 76.0f },    // TREBLE
    }},
    // The tones' panel starts at 512 under its lit top edge, and their housings — moved down toward the keys
    // in the picture, which leaves room over them — at 531.5: their words stand between, a size down. GAIN's
    // on the console's own plate, between its top edge at 497 and its housing.
    .knobNameY  = 522.0f,
    .knobNameEm = 12.0f / layout::readingScale,
    .driveNameY = 508.5f,

    // The six square holes, 42 x 43, two and four. The panel leaves no room round them for a word: the
    // filters' names are on their keys, the layers' none.
    .keys {{
        { 1186.7f, 687.5f }, { 1242.8f, 687.4f }, { 1330.0f, 687.5f }, { 1387.4f, 687.4f }, { 1445.2f, 687.5f }, { 1502.5f, 687.5f },
    }},
    .keySize       = 50.0f,
    .keyNameOverY  = 654.0f,
    .keyNameUnderY = 728.0f,
    .keyNameOver {{ false, false, false, false, false, false }},
    .keyNameRoom = 52.0f,
    .keyNamesOnKeys = true,
    .lightKeys {{ 4, 5 }},      // the two under TREBLE: the searchlights, left and right

    // The radio's screen, its glass 122..277 x 530..623: the gear along its top, the picture under it — the
    // speed is a hologram in the top right corner of the canopy's middle pane, under the overhead strip and
    // clear of the post (its glass edge x = 1262 - 0.43 y).
    .navScreen   { 121.0f, 529.0f, 158.0f, 96.0f },
    .navHeader   = radioTopLine,
    .navPicture  = radioPicture,
    .speedHud    { 1050.0f, 58.0f, 140.0f, 56.0f },

    // THE TUNER, the gauge's face, 390..576 across: its seventeen ticks point at 483, 627.5, the end ones
    // 58 degrees either side of the long one straight up, from 87 to 94 out; the little window 453..512
    // x 654..676 under that.
    .tunerScreen { 390.0f, 507.0f, 186.0f, 186.0f },
    .tunerDial { { 483.0f, 627.5f }, 90.0f, 1.012f, { 453.0f, 654.0f, 59.0f, 22.0f } },
    // LEARN over the whole radio screen, its top line too: the picture under it is low for the trace and the
    // words. Its keys are the two round holes beside the screen, GATE's over HUM's, moved apart in the
    // picture for their words and to the middle of their plate — the bezels 553..602 and 637..686, 305..350
    // across — each key's LEARN over
    // its name in two lines over it, clear of the plate's screws at 515.
    .learnArea = radioTopLine.getUnion (radioPicture),
    .learnKeys {{ { 327.5f, 577.5f }, { 327.5f, 661.5f } }},
    .learnKeySize = 42.0f,
    .learnNames {{ { 327.5f, 538.0f }, { 327.5f, 620.0f } }},
    .learnLine = 11.5f,

    // The wipers' four keys: the row of square holes under the radio's screen, each 35 x 39, 44 apart.
    .wiperKeys {{ { 136.0f, 667.8f }, { 180.0f, 667.8f }, { 224.0f, 667.8f }, { 268.0f, 667.8f } }},
    .wiperKeySize = 42.0f,

    // The slot is 882..907 across, 517..724 down, and straight: the stem stays in it. Its five ticks, to its
    // right at 926..946.5, are PARK and four places below it — the grip lies along a tick, so the collar's
    // foot is 24 past it — and a fourth gear's tick, past this product's three, is painted over.
    .gateSlot { 882.0f, 517.0f, 25.0f, 207.0f },
    .slotX  = 894.5f,
    .notchX = 894.5f,
    .slotTopY = 548.9f,
    .stops { 587.5f, 627.0f, 666.3f, 704.7f },
    .leverWidth = 136.0f,
    .collarSink = 24.0f,
    .gearPlate { 867.0f, 507.0f, 53.0f, 229.0f },
    .leverLeft = 812.0f, .leverTop = 510.0f,

    // TURBO's round hole under the red cover, 992..1071 across; the cover is painted open, and has no render
    // of its own shut.
    .turbo { 1031.5f, 643.3f },
    .turboSize = 62.0f,
    .turboName { 1031.5f, 723.0f },
    .turboLid { 975.0f, 505.0f, 120.0f, 208.0f },

    // REVERB over DELAY on the left, CAB on the right; REVERB's words on the plate over the pair, DELAY's
    // between the two tracks, CAB's on the plate over its track.
    .wells {{
        { 248.0f,  802.0f, 399.0f, 20.0f },     // REVERB
        { 241.0f,  849.0f, 405.0f, 20.0f },     // DELAY
        { 1020.0f, 833.0f, 412.0f, 20.0f },     // CAB
    }},
    .wellNameY {{ 774.0f, 835.0f, 775.0f }},

    .sliderHeight = 32.0f,

    // Each edge column: its slot, 23 wide, and beside it the column of nine dots, 25.9 apart — the meter
    // the lower eight, the overload the top one.
    .inMeter  { 50.3f,   513.25f, 24.0f, 207.2f },
    .inFader  { 23.0f,   494.0f,  23.0f, 217.0f },
    .outMeter { 1596.5f, 513.25f, 24.0f, 207.2f },
    .outFader { 1627.0f, 494.0f,  22.0f, 217.0f },
    .faderCapLength = 16.0f,
    .meterDots { 8, 9.0f },

    // The corner plates' slots are painted, 59..148 and 1523..1612 x 841..857, rim and all a little more,
    // and lie across: GATE's threshold and LIMITER's ceiling go left to right in them. Nothing grows.
    .thresholdSlot { 57.0f, 839.0f, 93.0f, 21.0f },
    .ceilingSlot   { 1521.0f, 839.0f, 93.0f, 21.0f },
    .slotScale = 21.0f / 27.0f,

    // No lamp is painted lit: each is drawn, a glass as wide as the meters' dots — the overloads on the
    // columns' top dots, GATE's and LIMITER's over their slots.
    .lamps {{
        { 47.3f,   486.0f, 30.0f, 30.0f },
        { 1592.2f, 486.2f, 30.0f, 30.0f },
        { 88.5f,   799.0f, 30.0f, 30.0f },
        { 1552.5f, 799.0f, 30.0f, 30.0f },
    }},
    .lampDot = 11.0f,

    // INPUT and OUTPUT on the column's foot, under the dots, 80 wide between its edges.
    .inputName  { 36.0f, 725.0f },
    .outputName { 1636.0f, 725.0f },
    .columnNameRoom = 76.0f,

    // GATE and LIMITER under their slots, level, low on their plates toward the console's middle.
    .gateWord    { 99.2f, 882.0f },
    .limiterWord { 1572.0f, 882.0f },
    .lampSlant = 0.0f,

    // PRESET on the left of the two little screens low in the middle, MEM over CPU on the right one, the
    // version on the plate over both.
    .leftPlate   { 700.0f, 801.0f, 120.0f, 73.0f },
    .readings {{ { 857.0f, 809.0f, 110.0f, 28.0f }, { 857.0f, 837.0f, 110.0f, 28.0f } }},     // on the screen 851..971
    .centrePlate { 700.0f, 760.0f, 275.0f, 30.0f },

    // The plain metal between the vents, where the family's mark is cut: narrowing down past the vents to
    // the seam over the little screens.
    .emblemFace {{
        { 775.0f, 396.0f }, { 901.0f, 396.0f }, { 890.0f, 442.0f }, { 878.0f, 489.0f }, { 797.0f, 489.0f }, { 786.0f, 442.0f },
    }},
    .emblemMark { 799.5f, 403.0f, 76.0f, 76.0f },

    // ...and the maker's words low beside it, on the strips under the vents, 461..490 down, close in to the
    // plate's edges at 792 and 888.
    .poweredBy { 735.0f, 476.0f },
    .makerName { 947.0f, 476.0f },
    .makerRoom = 110.0f,
};

const Skin& titan()
{
    static const Skin titan {
        name,
        titanLineup(),
        plan,
        "AMBERPAW_ART_DIR",
        juce::Colour (0xfff2f6ff),      // the painted rings' white, a shade brighter
        face::Ink {},                   // the family's lilac on the dark gunmetal...
        juce::Colours::black.withAlpha (0.7f),     // ...and a shadow under the large words
        face::renders::text (name, "midi.txt", "AMBERPAW_ART_DIR"),
        // Titan's methane sea out of the canopy, flown low over (view/TitanDrive.h).
        [] { return std::make_unique<Windscreen> (std::make_unique<amberpaw::TitanDrive> (numGears (titanLineup()))); },
        { "Titan (GPU)", "1993 (CPU)", "Still" },
        "GAIN",
        true,                           // the family's cat, cut into the plate between the vents
        "IN", "OUT",                    // the edge columns, short as a cockpit labels them
        false,                          // the radio's top line the gear and the speed alone...
        face::Monitor::View::shape,     // ...over the shape the amp gives the sound
        false,                          // parked, the large warning alone
    };
    return titan;
}

} // namespace

} // namespace orbitamp::redpaw

/** AMBERPAWAMP (Product.h): the rover (redpaw::rover) in the skin above. */
const orbitamp::Product& orbitamp::products::amberPawAmp()
{
    static const Product rover = redpaw::rover (redpaw::titan());
    return rover;
}

// THE PLUG-IN'S ENTRY, in AmberPawAmp's own plug-in alone (CMakeLists.txt: add_amp): a host that opens it
// gets a processor of AmberPawAmp. A gate, or a binary that plays several amps, makes its own.
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::amberPawAmp());
}
#endif
