// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Stage.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

/** WHERE EVERYTHING STANDS ON THE E530'S DASHBOARD, in the stage's own 1672 x 941. The plates are catbay's render of the
    metal world's cabin (variant c6, the owner's pick of 20.11), drawn at twice the stage, and every socket is the scene's
    own object cast through the cabin's camera: resources/metal/sockets.txt is that table as catbay handed it, and the
    label bands, where the code writes a control's name, resources/metal/labels.txt. DARWIN'S CAT and POWERED BY are baked
    into the plates: nothing is drawn there. The window is the plate's alpha, and the world lies under the whole stage. */
namespace orbitamp::metal::layout
{

using face::stage::width;
using face::stage::height;

inline juce::Rectangle<float> socket (float cx, float cy, float w, float h)
{
    return juce::Rectangle<float> (w, h).withCentre ({ cx, cy });
}

/** THE VIEW: under the whole stage. */
inline const juce::Rectangle<float> view { 0.0f, 0.0f, width, height };

// THE LABEL BANDS (labels.txt): 20 px tall, a control's name centred in each.
inline constexpr float bandY = 773.0f;
inline constexpr float inBandX = 88.0f, compBandX = 338.0f, gainBandX = 452.0f, toneBandX = 760.0f;
inline constexpr float fx1BandX = 1036.0f, fx2BandX = 1096.0f, reverbBandX = 1156.0f, outBandX = 1584.0f;
inline constexpr float keyBandY = 873.0f;

// THE TOP ROW: the tuner's screen on the left, the maker's plate in the middle, the keys on the right.
inline const juce::Rectangle<float> tunerScreen = socket (267.0f, 718.0f, 446.0f, 38.0f);

/** THE MAKER'S PLATE (plate_maker) and the octagon its seat is painted as — its outer rim, the hover's area and where the
    flames rise from, and its inside, which they never cover (catbay, metal/cabin-logo.js). */
inline const juce::Rectangle<float> makerPlate = socket (836.0f, 699.0f, 124.0f, 108.0f);
inline constexpr std::array<juce::Point<float>, 8> seatRim {{
    { 788.0f, 641.0f }, { 884.0f, 641.0f }, { 905.0f, 662.0f }, { 905.0f, 730.0f },
    { 879.0f, 756.0f }, { 793.0f, 756.0f }, { 767.0f, 730.0f }, { 767.0f, 662.0f } }};
inline constexpr std::array<juce::Point<float>, 8> seatInside {{
    { 795.0f, 655.0f }, { 877.0f, 655.0f }, { 891.0f, 669.0f }, { 891.0f, 724.0f },
    { 873.0f, 742.0f }, { 799.0f, 742.0f }, { 781.0f, 724.0f }, { 781.0f, 669.0f } }};
/** ...the logo inside 86 % of the plate, and the box the flames are drawn in over it. */
inline const juce::Rectangle<float> logoBox = makerPlate.withSizeKeepingCentre (124.0f * 0.86f, 108.0f * 0.86f);
inline const juce::Rectangle<float> flameBox { 726.0f, 540.0f, 220.0f, 228.0f };

// PRESET and the version left, MEM and CPU right, on the frame's rail over the middle panel.
inline const juce::Rectangle<float> presetPlace  { 552.0f, 658.0f, 116.0f, 26.0f };
inline const juce::Rectangle<float> versionPlace { 670.0f, 658.0f, 76.0f, 26.0f };
inline const juce::Rectangle<float> memoryPlace  { 912.0f, 658.0f, 124.0f, 26.0f };
inline const juce::Rectangle<float> loadPlace    { 1050.0f, 658.0f, 80.0f, 26.0f };

// THE KEYS: UNDO and REDO wider, then A B C D; each cap 3 px inside its well.
inline constexpr float keyRowY = 719.0f, keyHeight = 36.0f, keyInset = 3.0f;
inline constexpr std::array<float, 6> keyX { 1232.0f, 1322.0f, 1399.0f, 1463.0f, 1527.0f, 1591.0f };
inline constexpr float historyKeyWidth = 80.0f, registerKeyWidth = 54.0f;

// IN: its fader and its meter, GATE's button under them.
inline const juce::Rectangle<float> inFaderSlot = socket (72.0f, 833.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> inMeterSlot = socket (104.0f, 833.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> gateButton  = socket (88.0f, 907.0f, 92.0f, 26.0f);

// OUT: its mirror.
inline const juce::Rectangle<float> outMeterSlot  = socket (1568.0f, 833.0f, 14.0f, 88.0f);
inline const juce::Rectangle<float> outFaderSlot  = socket (1600.0f, 833.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> limiterButton = socket (1584.0f, 907.0f, 92.0f, 26.0f);

inline constexpr float faderWidth = 40.0f;          // a column fader's box round its slot
inline constexpr float faderCapLength = 16.0f;      // ...its cap's render, across the slot
inline constexpr float faderCapSquash = 0.5f;

/** THE LEVER: its slot, and its five stops top to bottom — PARK, CLEAN GAIN LO and HI, LEAD GAIN LO and HI — each a dot the
    plate paints at x 216; their glyphs from x 228. */
inline const juce::Rectangle<float> leverSlot = socket (190.0f, 842.0f, 24.0f, 132.0f);
inline constexpr std::array<float, 5> leverStops { 790.0f, 816.0f, 842.0f, 868.0f, 894.0f };
inline constexpr float glyphLeft = 228.0f;
inline const juce::Rectangle<float> leverBox { 164.0f, 772.0f, 96.0f, 140.0f };

// COMP: its fader, the threshold, and its squeeze's column beside it.
inline const juce::Rectangle<float> compSlot   = socket (326.0f, 833.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> compGrSlot = socket (350.0f, 833.0f, 14.0f, 88.0f);

// GAIN, the hero, in its socket.
inline const juce::Point<float> gain { 452.0f, 843.0f };
inline constexpr float gainSocket = 112.0f, gainKnob = 96.0f;

/** THE TONE STRIP (sock_tone_strip, 432 x 94): the playing channel's tone knobs, as many as its pack has, its names in
    the band along the strip's top. A row of five stands at 72 apart; fewer spread to its ends — three at 616, 760 and 904,
    where the approved mockup writes CLEAN's BASS, MIDDLE and TREBLE; six shrink to fit. */
inline const juce::Rectangle<float> toneStripBox = socket (760.0f, 810.0f, 432.0f, 94.0f);
inline constexpr float toneY = 822.0f, tonePitch = 72.0f, toneSocket = 72.0f, toneKnob = 60.0f;

/** THE KEY STRIP (sock_key_strip, 212 x 60): BRIGHT and CONTOUR, the E530's two keys, 106 apart where the mockup writes
    them, each its name in the band over it — dark while the playing pack has no switch of that name (MetalEditor.cpp). */
inline const juce::Rectangle<float> keyStripBox = socket (760.0f, 893.0f, 212.0f, 60.0f);
inline constexpr float keyY = 903.0f, keyPitch = 106.0f, keySize = 30.0f;

// THE EFFECTS: FX 1, DELAY's amount; FX 2, nothing yet; the family's REVERB.
inline const juce::Rectangle<float> fx1Slot    = socket (1036.0f, 833.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> fx2Slot    = socket (1096.0f, 833.0f, 16.0f, 88.0f);
inline const juce::Rectangle<float> reverbSlot = socket (1156.0f, 833.0f, 16.0f, 88.0f);

// THE CABINET: its screen, the slider across it a notch a cabinet; under it the picture, the playing cabinet's name along
// its top and the amp at work under that (face/Monitor.h).
inline const juce::Rectangle<float> cabinetScreen  = socket (1354.0f, 806.0f, 244.0f, 34.0f);
inline const juce::Rectangle<float> cabinetControl = cabinetScreen.reduced (12.0f, 0.0f);
inline const juce::Rectangle<float> cabinetPicture = socket (1354.0f, 875.0f, 244.0f, 88.0f);
inline const juce::Rectangle<float> cabinetName    = cabinetPicture.withHeight (24.0f).translated (0.0f, 2.0f);
inline const juce::Rectangle<float> monitorBand    = cabinetPicture.withTrimmedTop (26.0f).reduced (6.0f, 4.0f);

} // namespace orbitamp::metal::layout
