// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

/** Where everything sits, in the background render's own coordinates (1672 x 941). Measured off the
    render — the violet rings fitted as circles, the orange frames and the dark wells as boxes — not
    guessed, so a control lands in the socket that was painted for it. The window scales this space as
    a whole; a re-render at another resolution keeps these numbers as long as it keeps the composition. */
namespace orbitamp::catpit::layout
{

inline constexpr float width  = 1672.0f;
inline constexpr float height = 941.0f;

/** The smallest the window may get. Every label below is sized so that at THIS scale it is still 13 px —
    nothing a player reads goes smaller. */
inline constexpr int   minWindowWidth = 1024;
inline constexpr float minScale       = (float) minWindowWidth / width;
inline constexpr float px13           = 13.0f / minScale;   // a 13-px label, in this space
inline constexpr float px15           = 15.0f / minScale;
/** The one exception, asked for: the six keys under the tone knobs wear short names a size down. */
inline constexpr float px11           = 11.0f / minScale;

inline const juce::Point<float> windshieldSeed { 836.0f, 164.0f };
inline const juce::Rectangle<float> windshield { 152.0f, 60.0f, 1368.0f, 210.0f };

struct Socket { juce::Point<float> centre; float radius; };

inline const Socket drive  { { 653.6f, 503.6f }, 92.4f };

/** The tone knobs stand with their top edges level with DRIVE's, which is `toneLift` higher than the
    render painted their sockets — Art lifts the painted rings to match. The centres below are where
    they stand NOW. */
inline constexpr float toneLift = 24.5f;
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
inline const juce::Rectangle<float> turboFrame { 799.0f, 415.0f, 165.0f, 180.0f };

inline const std::array<juce::Rectangle<float>, 3> sliderInsets {{
    { 258.0f, 660.0f, 342.0f, 53.0f },     // REVERB
    { 665.0f, 660.0f, 345.0f, 53.0f },     // DELAY AMOUNT
    { 1073.0f, 660.0f, 341.0f, 53.0f },    // COMBO
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

/** The flat plate under COMBO's right end, below the orange lamp: the version sits on it. */
inline const juce::Point<float> versionPlate { 1434.0f, 812.0f };

/** Its mirror under REVERB's left end: the presets' door. */
inline const juce::Point<float> presetPlate { width - 1434.0f, 812.0f };

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
