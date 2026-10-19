// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

/** Where everything sits, in the background render's own coordinates (1672 x 941). Measured off the
    render — the violet rings fitted as circles, the orange frames and the dark wells as boxes — not
    guessed, so a control lands in the socket that was painted for it. The window scales this space as
    a whole; a re-render at another resolution keeps these numbers as long as it keeps the composition. */
namespace orbitamp::catpit::layout
{

// The stage's own space and type sizes: the render was composed in the space every face is drawn on.
using face::stage::width;
using face::stage::height;
using face::stage::readingWidth;
using face::stage::readingScale;
using face::stage::minWindowWidth;
using face::stage::px11;
using face::stage::px13;
using face::stage::px15;
using face::stage::px16;

inline const juce::Rectangle<float> windshield { 152.0f, 60.0f, 1368.0f, 210.0f };
/** ALL THE GLASS: the box round the whole cut-out. The overhead panel hangs into the windshield from the
    top, and on either side of it the glass runs on upward — two wedges, 31 rows tall at their outer ends
    (the cut-out's first row is 29) and closing to nothing where they meet the panel. The sky is flown
    over this; `windshield` stays what the picture is composed for — where the stars come from, how big
    they are — and what the monitor draws in. */
inline const juce::Rectangle<float> glass { 152.0f, 29.0f, 1368.0f, 241.0f };

struct Socket { juce::Point<float> centre; float radius; };

inline const Socket drive  { { 653.6f, 503.6f }, 92.4f };

/** The tone knobs stand with their top edges level with DRIVE's, higher than the render painted their
    sockets — the picture is baked with the painted rings lifted to match (tools/bake-art). */
inline const std::array<Socket, 3> tones {{
    { { 1060.3f, 476.9f }, 62.2f },    // BASS
    { { 1209.6f, 477.0f }, 62.4f },    // MIDDLE
    { { 1358.7f, 476.9f }, 61.9f },    // TREBLE
}};

/** The row that lift frees under them: six keys, their names below. Two groups — the two filters under
    BASS, the pack's four layers under MIDDLE and TREBLE — with the painted divider between them. */
inline constexpr float keyRowY     = 563.0f;
inline constexpr float keyNameY    = 588.5f;
inline constexpr float keySize     = 34.0f;
inline constexpr float keyGroupX   = 1060.3f;    // the filters' middle: BASS
inline constexpr float keyRowRight = 1418.0f;    // the last name ends here, short of the plate's corner screw

inline const juce::Rectangle<float> leverFrame { 254.0f, 376.0f, 261.0f, 217.0f };
/** The TURBO frame stands higher than the render painted it, so that its name, which is printed in its
    top line, is read in the row it always was — beside DRIVE's and the tones'. The picture is baked to
    match (tools/bake-art). */
inline const juce::Rectangle<float> turboFrame { 799.0f, 401.0f, 165.0f, 180.0f };
/** THE THROTTLE'S GRIP, rebuilt with its bar reaching further to the left of its stem than to the right
    (tools/bake-art): the stem stands this far across it, 0 its left end — as the tool reports it. */
inline constexpr float gripAxis = 0.629746854f;

inline const std::array<juce::Rectangle<float>, 3> sliderInsets {{
    { 258.0f, 660.0f, 342.0f, 53.0f },     // REVERB
    { 665.0f, 660.0f, 345.0f, 53.0f },     // DELAY AMOUNT
    { 1073.0f, 660.0f, 341.0f, 53.0f },    // CAB
}};

/** The six holes in the overhead panel, left to right: two, a divider, four. */
inline const std::array<juce::Point<float>, 6> holes {{
    { 626.0f, 60.0f }, { 726.0f, 60.0f }, { 834.0f, 60.0f }, { 901.0f, 60.0f }, { 967.0f, 60.0f }, { 1035.0f, 60.0f },
}};
inline constexpr float holeRadius = 12.0f;

// Each side column holds two wells: the narrow one a meter, the wide one a fader's slot.
inline const juce::Rectangle<float> inMeter  { 47.0f,   383.0f, 53.0f, 350.0f };
inline const juce::Rectangle<float> inFader  { 116.0f,  383.0f, 65.0f, 350.0f };
inline const juce::Rectangle<float> outMeter { 1493.0f, 383.0f, 53.0f, 350.0f };
inline const juce::Rectangle<float> outFader { 1562.0f, 383.0f, 65.0f, 350.0f };

inline const juce::Rectangle<float> tunerScreen { 635.0f, 799.0f, 402.0f, 104.0f };

/** The flat plate under CAB's right end, below the orange lamp: the version sits on it. */
inline const juce::Point<float> versionPlate { 1434.0f, 812.0f };

/** Its mirror under REVERB's left end: the presets' door. */
inline const juce::Point<float> presetPlate { width - 1434.0f, 812.0f };

/** THE LAMPS THE COCKPIT LIGHTS ITSELF, each with room round it for its glow: the bars under the columns'
    names, the input's overload and the output's; the slanted ones in the bottom corners under the
    columns, the gate's and the limiter's. The render paints them lit; Art puts them out. */
enum LampId { inClipLamp, outClipLamp, gateLamp, limiterLamp, numLamps };
inline const std::array<juce::Rectangle<float>, numLamps> lamps {{
    { 81.0f,   325.0f, 66.0f, 38.0f },    // the bar 93..134 x 334..340, and its glow on the seam below
    { 1525.0f, 325.0f, 67.0f, 38.0f },    // the bar 1537..1579 x 334..340, and its glow on the seam below
    { 62.0f,   824.0f, 54.0f, 49.0f },    // the slant 74..103 x 836..860
    { 1556.0f, 824.0f, 55.0f, 48.0f },    // the slant 1568..1598 x 836..859
}};

/** The plates over those two, their names between the screws: left of PRESET, the gate's (screws at 25
    and 125); right of the version, the limiter's (1546 and 1645). */
inline const juce::Point<float> gatePlate    { 75.0f, 805.5f };
inline const juce::Point<float> limiterPlate { 1595.5f, 805.5f };

/** INPUT's well as painted, its rim and all — a corner's slot is cut from it — and its two ends: the top
    one down past the T the slot's head makes, the bottom one up from where the slot stops. */
inline const juce::Rectangle<float> inWell { 113.0f, 381.0f, 70.0f, 354.0f };
inline constexpr float inWellTop = 32.0f, inWellBottom = 24.0f;
inline constexpr float inWellChamfer = 7.0f;     // its four corners, cut off

/** GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader in a slot cut into its corner: INPUT's
    well at `cornerSlotScale` of its size, its two ends whole and its straight run cut short to make it
    `cornerSlotLength` long, laid upright about its middle and then turned (and, for one, mirrored) to lie
    where it lies — the fader in it turned with it. Each lies along the slanted beam beside its lamp, at
    the lamp's slant, its head up toward the plate with the word on it; the beam on the right is the
    mirror of the left one, and so is the slot. -70 dB at GATE's foot to -40 at its head; -3 dB to -0.1
    at LIMITER's. */
struct CornerSlot
{
    juce::Point<float> centre;
    float angle;        // the head turned this far from straight up, clockwise, in radians
    bool mirrored;      // drawn as its mirror image, the fader's scale on its left

    /** The well, upright about the slot's middle, `wide` across. */
    juce::Rectangle<float> upright (float wide, float length) const { return juce::Rectangle<float> (wide, length).withCentre (centre); }

    /** What lays the upright well where it lies. */
    juce::AffineTransform turn() const
    {
        return (mirrored ? juce::AffineTransform::scale (-1.0f, 1.0f, centre.x, centre.y) : juce::AffineTransform())
                   .rotated (angle, centre.x, centre.y);
    }
};
inline constexpr float cornerSlotScale  = 0.3f;
inline constexpr float cornerSlotLength = 76.0f;
inline constexpr float lampSlant = 0.532f;   // 30.5 degrees off upright: the lamps' sides, and the beams'
inline const CornerSlot gateSlot    { { 155.5f, 865.0f }, -lampSlant, false };   // above the beam's screw
inline const CornerSlot limiterSlot { { 1515.5f, 865.0f }, lampSlant, true };
inline constexpr float thresholdLowDb = -70.0f, thresholdHighDb = -40.0f;

/** The flat plate under the grille, right of the tuner and down-left of the version, on the cockpit's
    bottom edge: what the engine is costing reads here. The words start at its left edge. */
inline const juce::Rectangle<float> loadPlate { 1246.0f, 908.0f, 126.0f, 26.0f };

/** Its mirror under the left grille, down-right of PRESET: what the amp is holding in memory. */
inline const juce::Rectangle<float> memoryPlate { 316.0f, 908.0f, 140.0f, 26.0f };

/** The emblem plate's inner face, a flat hexagon under the windshield, and its six screws. The render's
    own cat is painted over by a new face here, and the family's mark goes on it. */
inline const std::array<juce::Point<float>, 6> emblemFace {{
    { 773.0f, 235.0f }, { 900.0f, 235.0f }, { 930.0f, 264.0f }, { 900.0f, 350.0f }, { 774.0f, 350.0f }, { 742.0f, 264.0f },
}};
inline const std::array<juce::Point<float>, 6> emblemScrews {{
    { 776.0f, 241.0f }, { 895.0f, 241.0f }, { 746.0f, 267.0f }, { 924.0f, 267.0f }, { 781.0f, 346.0f }, { 890.0f, 346.0f },
}};
inline const juce::Rectangle<float> emblemMark { 785.0f, 242.0f, 102.0f, 100.0f };

} // namespace orbitamp::catpit::layout
