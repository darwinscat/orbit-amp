// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

/** WHERE EVERYTHING STANDS ON THE BATHYSCAPHE'S DASHBOARD, in the stage's own 1672 x 941. The picture is a render of
    the sphere's inside (catbay, blender/cabins/sub_cabin.py, variant C), drawn at twice the stage, and every socket in
    it is the scene's own object cast through the cabin's camera — its table, resources/underwater/sockets.txt as catbay
    handed it, is where each centre and size here comes from. The panels'
    edges are read off the picture. The window is the sphere: everything above the brow, and the world lies under the
    whole stage. Two rows: TUNER, the maker's plate and KEYS along the top; IN, AMP, TONE, EFFECTS, CABINET and OUT along
    the bottom. */
namespace orbitamp::underwater::layout
{

using face::stage::width;
using face::stage::height;

/** THE VIEW: under the whole stage — the picture's alpha is its window. */
inline const juce::Rectangle<float> view { 0.0f, 0.0f, width, height };

inline juce::Rectangle<float> socket (float cx, float cy, float w, float h)
{
    return juce::Rectangle<float> (w, h).withCentre ({ cx, cy });
}

// THE TOP ROW. Its titles stand in the strip over each panel's sockets.
inline constexpr float topTitleY = 684.0f;

// TUNER, on the left: the screen across the panel.
inline const juce::Rectangle<float> tunerScreen = socket (267.0f, 717.5f, 446.0f, 38.0f);
inline const juce::Point<float> tunerTitle { 48.0f, topTitleY };

// THE MAKER'S PLATE: the hexagon the family's cat is cut in, between POWERED BY and DARWIN'S CAT's plates.
inline constexpr std::array<juce::Point<float>, 8> emblemFace {{
    { 790.0f, 645.0f }, { 882.0f, 645.0f }, { 897.0f, 662.0f }, { 897.0f, 735.0f },
    { 880.0f, 752.0f }, { 792.0f, 752.0f }, { 775.0f, 735.0f }, { 775.0f, 662.0f } }};
inline const juce::Rectangle<float> emblemMark = socket (836.0f, 698.5f, 74.0f, 74.0f);
inline const juce::Rectangle<float> poweredByPlate = socket (648.0f, 715.5f, 196.0f, 30.0f);
inline const juce::Rectangle<float> makerNamePlate = socket (1024.0f, 715.5f, 196.0f, 30.0f);

// PRESET, the version, MEM and CPU: over the two plates, in the middle panel's strip.
inline const juce::Rectangle<float> presetPlace  { 552.0f, topTitleY - 14.0f, 116.0f, 28.0f };
inline const juce::Rectangle<float> versionPlace { 670.0f, topTitleY - 14.0f, 76.0f, 28.0f };
inline const juce::Rectangle<float> memoryPlace  { 926.0f, topTitleY - 14.0f, 120.0f, 28.0f };
inline const juce::Rectangle<float> loadPlace    { 1050.0f, topTitleY - 14.0f, 72.0f, 28.0f };

// KEYS, on the right: six holes, UNDO and REDO wider, then A B C D.
inline constexpr float keyRowY = 718.5f, keyHole = 36.0f;
inline constexpr std::array<float, 6> keyX { 1232.0f, 1322.0f, 1399.0f, 1463.0f, 1527.0f, 1591.0f };
inline constexpr float historyHole = 80.0f, registerHole = 54.0f;
inline constexpr float keyInset = 3.0f;                 // a key's cap inside its hole's edge
inline const juce::Point<float> keysTitle { 1192.0f, topTitleY };

// THE BOTTOM ROW: each panel's title in the strip over its sockets, the controls' names in the row under it. DRIVE's
// socket reaches into that row, so its name stands in the titles' row.
inline constexpr float titleY = 764.0f, nameY = 786.5f;
inline const juce::Point<float> inTitle      { 92.0f, titleY };     // centred
inline const juce::Point<float> ampTitle     { 185.0f, titleY };
inline const juce::Point<float> toneTitle    { 556.0f, titleY };
inline const juce::Point<float> effectsTitle { 907.0f, titleY };
inline const juce::Point<float> cabinetTitle { 1220.0f, titleY };
inline const juce::Point<float> outTitle     { 1580.0f, titleY };   // centred

// IN: the trim's fader at the outer edge, the meter toward the middle, GATE's slot under them.
inline const juce::Rectangle<float> inFaderSlot = socket (70.0f, 838.8f, 16.0f, 78.5f);
inline const juce::Rectangle<float> inMeterSlot = socket (114.0f, 838.8f, 14.0f, 78.5f);
inline const juce::Rectangle<float> gateSlot    = socket (92.0f, 907.0f, 92.0f, 26.0f);

// OUT: its mirror.
inline const juce::Rectangle<float> outMeterSlot = socket (1558.0f, 838.8f, 14.0f, 78.5f);
inline const juce::Rectangle<float> outFaderSlot = socket (1602.0f, 838.8f, 16.0f, 78.5f);
inline const juce::Rectangle<float> limiterSlot  = socket (1580.0f, 907.0f, 92.0f, 26.0f);

inline constexpr float faderWidth = 40.0f;          // a column fader's box round its slot
inline constexpr float faderCapLength = 16.0f;      // ...its cap's render, across the slot
inline constexpr float faderCapSquash = 0.5f;       // ...at half its render's height, as the walker's

// AMP: the lever's slot with its four stops, DRIVE the hero, BLEND's fader.
inline constexpr float leverX = 214.0f;
inline const juce::Rectangle<float> leverSlot = socket (leverX, 847.5f, 24.0f, 110.0f);
inline const std::vector<float> leverStops { 804.5f, 833.2f, 861.9f, 890.5f };      // PARK, CLEAN, DIRT, DRIVE
inline constexpr float leverStopX = 240.0f;
inline constexpr float leverNameLeft = 247.0f, leverNameRoom = 39.0f;               // the names beside the stops, short of DRIVE
inline const juce::Rectangle<float> leverBox { 188.0f, 786.0f, 104.0f, 122.0f };

inline const juce::Point<float> drive { 350.0f, 838.8f };
inline constexpr float driveSocket = 124.0f, driveKnob = 107.0f;
inline const juce::Rectangle<float> blendSlot = socket (484.0f, 838.8f, 16.0f, 78.5f);

// TONE: three sockets, BASS, MIDDLE and TREBLE.
inline constexpr std::array<float, 3> toneX { 602.0f, 710.0f, 818.0f };
inline constexpr float toneY = 834.8f, toneSocket = 80.0f, toneKnob = 66.0f;

// THE EFFECTS: OCTAVE, CHORUS and REVERB, a vertical fader in each slot.
inline constexpr std::array<float, 3> effectX { 948.0f, 1042.0f, 1136.0f };
inline constexpr float effectY = 838.8f, effectSlotHeight = 78.5f, effectFaderWidth = 44.0f;

inline constexpr float ringInset = 3.5f, ringWidth = 2.5f, driveRingWidth = 3.0f;

// CABINET: its screen — the slider across it, the playing cabinet's name in the names' row over it — and the two
// switches under it, 5 STR / 4 STR over FINGERS / SLAP.
inline const juce::Rectangle<float> cabinetScreen = socket (1354.0f, 810.5f, 244.0f, 38.0f);
inline const juce::Rectangle<float> cabinetControl = cabinetScreen.reduced (12.0f, 0.0f);
inline const juce::Rectangle<float> cabinetName { cabinetScreen.getX(), nameY - 10.0f, cabinetScreen.getWidth(), 20.0f };
inline const juce::Rectangle<float> stringsSlot = socket (1354.0f, 852.8f, 52.0f, 28.0f);
inline const juce::Rectangle<float> fingersSlot = socket (1354.0f, 887.6f, 52.0f, 28.0f);

// THE READINGS, a row under each group of controls.
inline constexpr float valueY = 892.0f, driveValueY = 912.0f, levelValueY = 886.0f;

} // namespace orbitamp::underwater::layout
