// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

/** WHERE EVERYTHING STANDS ON THE WALKER'S DASHBOARD, in the stage's own 1672 x 941 — measured off its render
    (resources/redgroove/dashboard.webp, drawn at twice the stage), which was painted after the layout's drawing
    scaled by 1.045. The knobs' sockets are where bake-art fits them to the picture (tools/bake-art/Walker.cpp), to
    a tenth of a pixel, and its check holds them there. Its dome is the picture's transparent top; under it, the dashboard's two rows: KEYS, the
    maker's plate and TUNER along the top; IN, AMP, TONE, EFFECTS, CABINET and OUT along the bottom. */
namespace orbitamp::redgroove::layout
{

using face::stage::width;
using face::stage::height;

/** THE DOME: the picture's transparent top, where the world shows; the sill under it from here down. */
inline const juce::Rectangle<float> dome { 0.0f, 0.0f, width, 525.0f };

// THE KEYS PANEL: six holes for push keys, UNDO and REDO wider, then A B C D.
inline constexpr float keyRowY = 622.5f;
inline constexpr std::array<float, 6> keyX { 87.8f, 184.5f, 267.0f, 339.0f, 409.5f, 480.0f };
inline constexpr float historyKeyWidth = 74.0f, registerKeyWidth = 51.0f, keyHeight = 36.0f;
inline const juce::Point<float> keysTitle { 48.0f, 576.0f };            // "KEYS", from its left edge

// THE MAKER'S PLATE: the six-sided plate the family's cat is cut in, and the two plates beside it.
inline constexpr std::array<juce::Point<float>, 7> emblemFace {{
    { 792.5f, 539.0f }, { 880.0f, 539.0f }, { 899.0f, 570.0f }, { 886.0f, 640.0f },
    { 837.0f, 648.0f }, { 788.0f, 640.0f }, { 774.0f, 570.0f } }};
inline const juce::Rectangle<float> emblemMark { 808.0f, 563.0f, 58.0f, 58.0f };
inline const juce::Rectangle<float> poweredByPlate { 561.5f, 611.5f, 202.5f, 34.5f };
inline const juce::Rectangle<float> makerNamePlate { 908.5f, 611.5f, 203.0f, 34.5f };

// THE TUNER PANEL: its screen, and its title over it.
inline const juce::Rectangle<float> tunerScreen { 1165.0f, 586.5f, 459.0f, 57.0f };
inline const juce::Point<float> tunerTitle { 1168.0f, 576.0f };

// THE BOTTOM ROW'S PANELS, their titles' places (each from its left edge, IN and OUT centred).
inline constexpr float panelTitleY = 682.0f;
inline const juce::Point<float> inTitle      { 79.5f, panelTitleY };
inline const juce::Point<float> ampTitle     { 175.5f, panelTitleY };
inline const juce::Point<float> toneTitle    { 623.0f, panelTitleY };
inline const juce::Point<float> effectsTitle { 955.0f, panelTitleY };
inline const juce::Point<float> cabinetTitle { 1260.0f, panelTitleY };
inline const juce::Point<float> outTitle     { 1586.0f, panelTitleY };

// IN: the trim's fader in its slot at the outer edge, the meter toward the middle; GATE's slot under them.
inline const juce::Rectangle<float> inFaderSlot { 41.5f, 712.5f, 17.5f, 121.5f };
inline const juce::Rectangle<float> inMeterSlot { 88.5f, 707.5f, 26.5f, 132.5f };
inline const juce::Rectangle<float> gateSlot    { 32.5f, 868.5f, 94.0f, 38.0f };

// OUT: its mirror — the meter toward the middle, the level's fader at the outer edge; LIMITER's slot under.
inline const juce::Rectangle<float> outMeterSlot { 1545.0f, 707.5f, 26.0f, 132.5f };
inline const juce::Rectangle<float> outFaderSlot { 1602.5f, 713.5f, 17.5f, 121.5f };
inline const juce::Rectangle<float> limiterSlot  { 1536.0f, 868.5f, 104.0f, 39.0f };

inline constexpr float faderWidth = 40.0f;          // a column fader's box round its slot
inline constexpr float faderCapLength = 16.0f;      // ...and its cap's render, across the slot

// AMP: CHANNEL's lever slot, COMP, GR's column, DRIVE (the hero) and BLEND — each a socket in the picture.
inline const juce::Point<float> drive { 418.8f, 791.5f };
inline constexpr float driveSocket = 120.0f, driveKnob = 104.0f;
inline constexpr float knobNameY = 712.0f;          // the row the knobs' names stand in, over AMP and TONE

// TONE: three sockets for as many tone knobs as the bass's standard has, and MID FREQ's slot under them.
inline constexpr std::array<float, 3> toneX { 645.5f, 753.1f, 860.5f };
inline constexpr float toneY = 765.1f, toneSocket = 77.0f, toneKnob = 64.0f;

// THE OTHER KNOBS: COMP and BLEND either side of DRIVE, DI/CAB in the cabinet's panel — each in its socket. A ring
// round every knob, in its socket's groove, `ringInset` inside the socket's edge.
inline const juce::Point<float> comp { 287.7f, 794.6f }, blend { 532.0f, 794.5f }, diCab { 1280.4f, 826.1f };
inline constexpr float smallSocket = 78.0f, smallKnob = 64.0f, diCabSocket = 73.5f, diCabKnob = 60.0f;
inline constexpr float ringInset = 3.5f, ringWidth = 2.5f, driveRingWidth = 3.0f;

// COMP's squeeze, in its slot between COMP and DRIVE.
inline const juce::Rectangle<float> grSlot { 336.5f, 753.5f, 12.5f, 85.0f };

// THE EFFECTS: three slots, OCTAVE, ENVELOPE and CHORUS, a vertical fader in each.
inline constexpr std::array<float, 3> effectX { 975.0f, 1073.8f, 1170.0f };
inline constexpr float effectTop = 734.0f, effectBottom = 864.0f, effectFaderWidth = 44.0f;
inline constexpr float effectNameRoom = 80.0f;      // each name set condensed into this, where wider: air between them
inline constexpr float effectNameTracking = 0.02f;  // ...and tracked tighter than the panel's other names

// MIDDLE's frequency: the slot lying under the tones, a fader across it; its ends' words beside it.
inline const juce::Rectangle<float> midFreqSlot { 683.5f, 865.0f, 141.5f, 16.0f };
inline constexpr float midFreqCap = 12.0f;

// THE READINGS, a row under each group of controls: the AMP's, the effects' and DI/CAB's; the tones'; MIDDLE's
// frequency over its slot; IN's and OUT's levers over their lamps' slots.
inline constexpr float valueY = 882.0f, toneValueY = 822.0f, midFreqValueY = 849.0f, levelValueY = 855.0f;
inline constexpr float diCabNameY = 778.0f, diCabValueY = 886.0f;

// PRESET, the version, MEM and CPU: on the sill over the maker's two plates — PRESET and the version over POWERED BY,
// MEM and CPU over DARWIN'S CAT.
inline constexpr float sillY = 586.0f;
inline const juce::Rectangle<float> presetPlace  { 560.0f, sillY - 14.0f, 116.0f, 28.0f };
inline const juce::Rectangle<float> versionPlace { 682.0f, sillY - 14.0f, 84.0f, 28.0f };
inline const juce::Rectangle<float> memoryPlace  { 910.0f, sillY - 14.0f, 124.0f, 28.0f };
inline const juce::Rectangle<float> loadPlace    { 1040.0f, sillY - 14.0f, 72.0f, 28.0f };

// CHANNEL: the lever's slot left in the AMP's panel, its two places from the top — B15, SVT — the first's name over
// the slot, the second's under it: beside it there is no room short of COMP's socket.
inline constexpr float leverX = 189.5f;
inline const std::vector<float> leverPlaces { 766.0f, 829.0f };
inline const std::vector<juce::Point<float>> leverNames { { leverX, 729.0f }, { leverX, 866.0f } };
inline const juce::Rectangle<float> leverBox { 158.0f, 712.0f, 64.0f, 170.0f };

// THE CUTS' SWITCHES, right of DI/CAB: the HPF's by the strings over the LPF's by the hand, each a slot the picture
// paints, its two words over it.
inline const juce::Rectangle<float> lowCutSlot  { 1389.0f, 788.5f, 52.0f, 28.0f };
inline const juce::Rectangle<float> highCutSlot { 1389.0f, 862.0f, 52.0f, 28.0f };
inline constexpr float cutWordsAbove = 14.0f, cutWordRoom = 78.0f;

// THE CABINET'S SCREEN, across the top of its panel.
inline const juce::Rectangle<float> cabinetScreen { 1246.0f, 707.5f, 251.5f, 49.0f };

} // namespace orbitamp::redgroove::layout
