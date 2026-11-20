// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <vector>

/** WHERE EVERYTHING STANDS ON THE IO ROVER'S DASHBOARD, in the stage's own 1672 x 941. The picture is catbay's render
    of the rover's cabin on Io (variant b, final), drawn at twice the stage, its windscreen transparent; every socket in
    it is the scene's own object cast through the cabin's camera — its table, resources/io/sockets.txt as catbay handed
    it, is where each centre and size here comes from. The panels are the bathyscaphe's shapes
    (underwater/UnderwaterLayout.h): TUNER, the maker's plate and KEYS along the top; IN, AMP, TONE, EFFECTS, CABINET and
    OUT along the bottom. sock_dosimeter is the world's: it draws the needle, and the face leaves it alone.

    THE PICTOGRAMS AND THE FEW WORDS (the owner, 20.11) stand where the approved mockup has them (io/cabin/drafts/face/
    io.html): each glyph at its centre and scale, each word at its anchor and baseline, in the mockup's own stage px. */
namespace orbitamp::io::layout
{

using face::stage::width;
using face::stage::height;

/** THE VIEW: under the whole stage — the picture's alpha is its windscreen. */
inline const juce::Rectangle<float> view { 0.0f, 0.0f, width, height };

inline juce::Rectangle<float> socket (float cx, float cy, float w, float h)
{
    return juce::Rectangle<float> (w, h).withCentre ({ cx, cy });
}

// THE TOP ROW.
inline const juce::Rectangle<float> tunerScreen = socket (267.0f, 718.0f, 446.0f, 38.0f);

// THE MAKER'S PLATE (plate_maker): the octagon the family's cat is cut in, between POWERED BY's and DARWIN'S CAT's.
inline constexpr std::array<juce::Point<float>, 8> emblemFace {{
    { 790.0f, 645.0f }, { 882.0f, 645.0f }, { 898.0f, 662.0f }, { 898.0f, 736.0f },
    { 881.0f, 753.0f }, { 791.0f, 753.0f }, { 774.0f, 736.0f }, { 774.0f, 662.0f } }};
inline const juce::Rectangle<float> emblemMark = socket (836.0f, 699.0f, 74.0f, 74.0f);
inline const juce::Rectangle<float> poweredByPlate = socket (648.0f, 716.0f, 196.0f, 30.0f);
inline const juce::Rectangle<float> makerNamePlate = socket (1024.0f, 716.0f, 196.0f, 30.0f);

// THE READINGS (the owner, 20.11): nothing round the maker's plate. PRESET and the playing preset's name top left and
// the version top right, on the dark rim over the window — on dashboard.webp the riveted strip, y 0..23, and the frame's
// brass under it down to the window's edge at y 73 — their middle at y 36, under the rivets (y 3..11) and, at the right,
// clear of the grey tape at x 1556..1654, y 31..49. MEM and CPU side by side in the text strip under the tone knobs
// (catbay's tone_strip), the pair centred in it as the standalone reads them.
inline const juce::Rectangle<float> rim { 0.0f, 0.0f, width, 73.0f };
inline constexpr float rimY = 36.0f;
inline const juce::Rectangle<float> toneTextStrip = juce::Rectangle<float>::leftTopRightBottom (712.0f, 878.0f, 948.0f, 912.0f);
inline const juce::Rectangle<float> presetPlace  { 64.0f, rimY - 13.0f, 180.0f, 26.0f };
inline const juce::Rectangle<float> versionPlace { 1440.0f, rimY - 13.0f, 106.0f, 26.0f };
inline const juce::Rectangle<float> memoryPlace  { 744.0f, 882.0f, 100.0f, 26.0f };
inline const juce::Rectangle<float> loadPlace    { 852.0f, 882.0f, 72.0f, 26.0f };

// KEYS: UNDO and REDO wider, then A B C D — a dark cap in each hole, its glyph or its letter on it.
inline constexpr float keyRowY = 719.0f, keyHole = 36.0f;
inline constexpr std::array<float, 6> keyX { 1232.0f, 1322.0f, 1399.0f, 1463.0f, 1527.0f, 1591.0f };
inline constexpr float historyHole = 80.0f, registerHole = 54.0f;
inline constexpr float arrowY = 718.0f;                         // UNDO's and REDO's arrows' centre
inline constexpr float letterBaseline = 725.5f;                 // A to D

// IN: the trim's fader at the outer edge, the meter toward the middle, GATE's button under them; the arrow into a box
// over them; the panel's own lamp, top right, its overload's.
inline const juce::Rectangle<float> inFaderSlot = socket (70.0f, 824.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> inMeterSlot = socket (114.0f, 824.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> gateSlot    = socket (92.0f, 907.0f, 92.0f, 26.0f);
inline const juce::Point<float> inLampAt { 143.0f, 768.0f };

// OUT: its mirror, the speaker with three arcs over it.
inline const juce::Rectangle<float> outMeterSlot = socket (1558.0f, 824.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> outFaderSlot = socket (1602.0f, 824.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> limiterSlot  = socket (1580.0f, 907.0f, 92.0f, 26.0f);
inline const juce::Point<float> outLampAt { 1527.0f, 768.0f };

inline constexpr float faderWidth = 40.0f;          // a column fader's box round its slot
inline constexpr float faderCapLength = 16.0f;      // ...its cap's render, across the slot
inline constexpr float faderCapSquash = 0.5f;       // ...at half its render's height, as the walker's

// AMP: the lever's slot with its three stops — PARK, SLOW, FAST — their signs beside them; COMP and its squeeze; DRIVE
// the hero; PRESENCE; BLEND.
inline constexpr float leverX = 214.0f;
inline const std::vector<float> leverStops { 818.0f, 852.0f, 886.0f };
inline const juce::Rectangle<float> leverBox { 188.0f, 797.0f, 56.0f, 112.0f };
inline constexpr float leverSignX = 268.0f;                         // P and the slow triangle; the fast pair either side of it
inline constexpr float parkBaseline = 826.5f, parkEm = 22.0f;       // P, B612 Bold
inline constexpr float fastLeftX = 263.0f, fastRightX = 276.0f;

inline const juce::Rectangle<float> compSlot   = socket (318.0f, 864.5f, 16.0f, 96.0f);
inline const juce::Rectangle<float> compGrSlot = socket (343.0f, 864.5f, 14.0f, 96.0f);

inline const juce::Point<float> drive { 450.0f, 850.0f };
inline constexpr float driveSocket = 112.0f, driveKnob = 97.0f;

inline const juce::Point<float> presence { 566.0f, 846.0f };
inline const juce::Rectangle<float> blendSlot = socket (646.0f, 864.5f, 16.0f, 96.0f);

// TONE: three sockets, BASS, MIDDLE and TREBLE — each the playing channel's tone of that name. PRESENCE's socket is the
// same size.
inline constexpr std::array<float, 3> toneX { 742.0f, 830.0f, 918.0f };
inline constexpr float toneY = 832.0f, toneSocket = 76.0f, toneKnob = 63.0f;

// THE EFFECTS: OCTAVE, CHORUS and REVERB, a vertical fader in each slot. Every column's slot — COMP's, its squeeze's,
// BLEND's and these — runs 816.5 to 912.5 on the re-rendered plates; the tone knobs stand 14 px higher than the first
// render had them, their pictograms with them.
inline constexpr std::array<float, 3> effectX { 1024.0f, 1086.0f, 1148.0f };
inline constexpr float effectY = 864.5f, effectSlotHeight = 96.0f, effectFaderWidth = 44.0f;

inline constexpr float ringInset = 3.5f, ringWidth = 2.5f, driveRingWidth = 3.0f;

// CABINET: its slider in the screen, the playing cabinet's name over it; and the two switches under it, 5STR | 4STR over
// FING | SLAP, their words either side (B612, the mockup's anchors: the left word ends at 1320, the right starts at 1388).
inline const juce::Rectangle<float> cabinetScreen = socket (1354.0f, 811.0f, 244.0f, 38.0f);
inline const juce::Rectangle<float> cabinetControl = cabinetScreen.reduced (12.0f, 0.0f);
inline const juce::Rectangle<float> cabinetName { cabinetScreen.getX(), cabinetScreen.getY() - 22.0f, cabinetScreen.getWidth(), 20.0f };
inline const juce::Rectangle<float> stringsSlot = socket (1354.0f, 857.7f, 52.0f, 28.0f);
inline const juce::Rectangle<float> fingersSlot = socket (1354.0f, 899.3f, 52.0f, 28.0f);
inline constexpr float switchLeftEnd = 1320.0f, switchRightStart = 1388.0f;
inline constexpr float stringsBaseline = 862.5f, fingersBaseline = 904.0f;

// THE WORDS (B612): 15 px on the buttons and by the switches, A to D Bold at 17 — the mockup's font sizes, in stage px.
inline constexpr float wordEm = 15.0f, letterEm = 17.0f;
inline constexpr float buttonBaseline = 912.0f;     // GATE and LIMITER, centred on their buttons

// THE PICTOGRAMS OVER THE KNOBS AND THE FADERS: each centre and scale, the mockup's.
struct GlyphAt { float x, y, scale; };
inline constexpr GlyphAt driveGlyph    { 450.0f, 776.0f, 1.35f };
inline constexpr GlyphAt presenceGlyph { 566.0f, 783.0f, 1.2f };
inline constexpr GlyphAt blendGlyph    { 646.0f, 783.0f, 1.2f };
inline constexpr std::array<GlyphAt, 3> toneGlyphs {{ { 742.0f, 771.0f, 1.3f }, { 830.0f, 771.0f, 1.3f }, { 918.0f, 771.0f, 1.3f } }};   // raised with the knobs
inline constexpr std::array<GlyphAt, 3> effectGlyphs {{ { 1024.0f, 785.0f, 1.1f }, { 1086.0f, 785.0f, 1.1f }, { 1148.0f, 785.0f, 1.2f } }};
inline constexpr GlyphAt compGlyph     { 330.0f, 785.0f, 1.2f };
inline constexpr GlyphAt inGlyph       { 92.0f, 772.0f, 1.1f };
inline constexpr GlyphAt outGlyph      { 1584.0f, 770.0f, 1.25f };

/** THE DOSIMETER'S FACE, for the site's cabin-dial.webp: the kit's cabin with a clear disk of this radius round the dial's hub,
    inside the bezel's black ring, so that the world's dial and its live needle show through (io-wasm's README). */
inline const juce::Point<float> dialHub { 119.2f, 564.9f };
inline constexpr float dialClearRadius = 37.8f;

/** WHERE THE CABIN'S LABELS LIE — every glyph and word on the bottom row and the keys' — for the night's glow, which is
    drawn there alone. */
inline const juce::Rectangle<float> labelsArea = juce::Rectangle<float>::leftTopRightBottom (40.0f, 690.0f, 1632.0f, 930.0f);

} // namespace orbitamp::io::layout
