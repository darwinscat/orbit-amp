// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

/** WHERE EVERYTHING STANDS ON THE VAN'S DASHBOARD, in the stage's own 1672 x 941. The picture is catbay's render of the
    1967 van's cabin (variant C), drawn at twice the stage, its windscreen transparent; every socket in it is the scene's
    own object cast through the cabin's camera — its table, resources/california/sockets.txt as catbay handed it, is where
    each centre and size here comes from. The panels are the bathyscaphe's shapes (underwater/UnderwaterLayout.h): two
    rows, TUNER, the maker's plate and KEYS along the top; IN, AMP, TONE, EFFECTS, CABINET and OUT along the bottom.

    THE PICTOGRAMS (the owner, 20.11: no words on the panel) stand where the approved mockup has them
    (van-cabin/drafts/glyphs/van.html): each centred over its socket's top, its scale the mockup's. */
namespace orbitamp::california::layout
{

using face::stage::width;
using face::stage::height;

/** THE VIEW: under the whole stage — the picture's alpha is its windscreen. */
inline const juce::Rectangle<float> view { 0.0f, 0.0f, width, height };

inline juce::Rectangle<float> socket (float cx, float cy, float w, float h)
{
    return juce::Rectangle<float> (w, h).withCentre ({ cx, cy });
}

// THE PEACE PENDANT under the mirror (dashboard_pendant.txt): its sprite's box on the stage, hanging still until the
// world swings it.
inline const juce::Rectangle<float> pendant = juce::Rectangle<float>::leftTopRightBottom (858.5f, 145.0f, 952.0f, 409.0f);

// THE TOP ROW.
inline const juce::Rectangle<float> tunerScreen = socket (267.0f, 718.0f, 446.0f, 38.0f);
inline constexpr float forkX = 64.0f, forkY = 718.0f, forkScale = 1.15f;     // the tuning fork at the screen's left end
inline constexpr float tunerLeftInset = 34.0f;                                 // ...the tuner's strip right of it

// THE MAKER'S PLATE: the octagon the family's cat is cut in, between POWERED BY's and DARWIN'S CAT's plates.
inline constexpr std::array<juce::Point<float>, 8> emblemFace {{
    { 790.0f, 645.0f }, { 882.0f, 645.0f }, { 898.0f, 662.0f }, { 898.0f, 736.0f },
    { 881.0f, 753.0f }, { 791.0f, 753.0f }, { 774.0f, 736.0f }, { 774.0f, 662.0f } }};
inline const juce::Rectangle<float> emblemMark = socket (836.0f, 699.0f, 74.0f, 74.0f);
inline const juce::Rectangle<float> poweredByPlate = socket (648.0f, 716.0f, 196.0f, 30.0f);
inline const juce::Rectangle<float> makerNamePlate = socket (1024.0f, 716.0f, 196.0f, 30.0f);

// THE READINGS along the top of the van (the owner, 20.11): the cream band over the cab, y 0..51 on dashboard.webp, its
// lower edge at y 52 where the orange starts; "Make Love" hangs over it at x 152..638 and "Not War" at x 1034..1522,
// from y 6 down. PRESET and the preset's name in the band's free left end, MEM and CPU in its middle between the two
// plates (over the mirror's stem), the version in its right end; in the panels' ink.
inline constexpr float bandY = 26.0f;                                   // the words' middle in the band
inline const juce::Rectangle<float> band { 0.0f, 0.0f, 1672.0f, 52.0f };
inline const juce::Rectangle<float> presetPlace  { 12.0f, bandY - 13.0f, 136.0f, 26.0f };
inline const juce::Rectangle<float> memoryPlace  { 733.0f, bandY - 13.0f, 122.0f, 26.0f };
inline const juce::Rectangle<float> loadPlace    { 863.0f, bandY - 13.0f, 76.0f, 26.0f };
inline const juce::Rectangle<float> versionPlace { 1534.0f, bandY - 13.0f, 128.0f, 26.0f };

// KEYS: UNDO and REDO wider, then A B C D.
inline constexpr float keyRowY = 719.0f, keyHole = 36.0f;
inline constexpr std::array<float, 6> keyX { 1232.0f, 1322.0f, 1399.0f, 1463.0f, 1527.0f, 1591.0f };
inline constexpr float historyHole = 80.0f, registerHole = 54.0f;
inline constexpr float keyGlyphY = 718.0f;          // UNDO's and REDO's arrows
inline constexpr float letterBaseline = 726.0f, letterSize = 19.0f;     // A to D, in Shrikhand

// IN: the trim's fader at the outer edge, the meter toward the middle, GATE's button under them; the jack over them.
inline const juce::Rectangle<float> inFaderSlot = socket (70.0f, 824.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> inMeterSlot = socket (114.0f, 824.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> gateSlot    = socket (92.0f, 907.0f, 92.0f, 26.0f);
inline constexpr float inGlyphX = 92.0f, levelGlyphY = 770.0f, inGlyphScale = 1.2f;

// OUT: its mirror, the speaker with three arcs over it.
inline const juce::Rectangle<float> outMeterSlot = socket (1558.0f, 824.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> outFaderSlot = socket (1602.0f, 824.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> limiterSlot  = socket (1580.0f, 907.0f, 92.0f, 26.0f);
inline constexpr float outGlyphX = 1584.0f, outGlyphScale = 1.25f;
inline constexpr float capGlyphScale = 1.15f;       // GATE's and LIMITER's on their caps

inline constexpr float faderWidth = 40.0f;          // a column fader's box round its slot
inline constexpr float faderCapLength = 16.0f;      // ...its cap's render, across the slot
inline constexpr float faderCapSquash = 0.5f;       // ...at half its render's height

// AMP: the lever's slot with its three stops — PARK, CLEAN, DIRTY — and their pictograms beside them; COMP and its
// squeeze; GAIN the hero; the channel's VOLUME.
inline constexpr float leverX = 214.0f;
inline const std::vector<float> leverStops { 818.0f, 852.0f, 886.0f };
inline const juce::Rectangle<float> leverBox { 188.0f, 797.0f, 56.0f, 112.0f };
inline constexpr float leverGlyphX = 276.0f, leverGlyphScale = 1.2f;
inline constexpr float parkLetterX = 268.0f, parkLetterBaseline = 825.5f, parkLetterSize = 21.0f;

inline const juce::Rectangle<float> compSlot   = socket (331.0f, 850.0f, 16.0f, 67.0f);
inline const juce::Rectangle<float> compGrSlot = socket (356.0f, 850.0f, 14.0f, 67.0f);
inline constexpr float compGlyphX = 340.0f;

inline const juce::Point<float> gain { 465.0f, 850.0f };
inline constexpr float gainSocket = 112.0f, gainKnob = 97.0f;
inline constexpr float gainGlyphY = 776.0f;

inline const juce::Rectangle<float> volumeSlot = socket (564.0f, 850.0f, 16.0f, 67.0f);
inline constexpr float volumeGlyphX = 566.0f;

// TONE: three sockets, BASS, MIDDLE and TREBLE — each the playing channel's tone of that name.
inline constexpr std::array<float, 3> toneX { 694.0f, 784.0f, 874.0f };
inline constexpr float toneY = 846.0f, toneSocket = 80.0f, toneKnob = 66.0f;

// THE EFFECTS: DELAY, PHASER and REVERB, a vertical fader in each slot.
inline constexpr std::array<float, 3> effectX { 993.0f, 1068.0f, 1143.0f };
inline constexpr std::array<float, 3> effectGlyphX { 996.0f, 1071.0f, 1143.0f };
inline constexpr float effectY = 850.0f, effectSlotHeight = 67.0f, effectFaderWidth = 44.0f;

// THE PICTOGRAMS OVER THE KNOBS AND THE FADERS: their row, and how large.
inline constexpr float glyphY = 785.0f, glyphScale = 1.35f, smallGlyphScale = 1.3f;

inline constexpr float ringInset = 3.5f, ringWidth = 2.5f, gainRingWidth = 3.0f;

// CABINET: its slider in the narrow screen, and the picture of the cabinet that plays, with its name, in the wide one
// under it.
inline const juce::Rectangle<float> cabinetScreen = socket (1354.0f, 806.0f, 244.0f, 34.0f);
inline const juce::Rectangle<float> cabinetControl = cabinetScreen.reduced (12.0f, 0.0f);
inline const juce::Rectangle<float> cabinetPicture = socket (1354.0f, 875.0f, 244.0f, 88.0f);

} // namespace orbitamp::california::layout
