// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MetalControls.h"

#include "../face/Console.h"
#include "../redgroove/RedGrooveControls.h"
#include "../redpaw/view/Windscreen.h"

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

namespace orbitamp::metal
{

/** THE E530'S DASHBOARD ITSELF: the night plate with its window transparent, and every word the app writes on it — the
    controls' names in their bands, the playing channel's tones' and keys' names, GATE's and LIMITER's caps — in DC
    Rocker, silver. DARWIN'S CAT and POWERED BY are the plate's own; the cat in the maker's plate is drawn live over it
    (CatLogo). Drawn over the world, under the controls, and never takes a click.

    THE PLATE IS THE WORLD'S TO DRAW while it draws it (Windscreen::drawsCabin — the metal world, the lightning's flash
    plate crossfaded in); the rest of the time, and in every picture taken of it (staticImage), the night plate is painted
    here, flat. The words and the cat are always the face's, over whichever plate. */
class Cabin final : public face::Picture
{
public:
    std::function<bool()> worldDrawsPanel;

private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintStrips (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override { return juce::Colour (0xffc4cad0); }
    juce::Rectangle<int> historyArea() const override;
    int layerState() const override { return worldDrawsPanel != nullptr && worldDrawsPanel() ? 1 : 0; }
};

//==============================================================================
/** FERROCLAWAMP'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the ENGL E530's
    dashboard in the metal world's cabin, its channels the E530's four. What it has of the console's own: GAIN (DRIVE),
    the tone strip and the key strip, as many as the playing pack has, the six keys of the history and the registers, IN
    and OUT, the tuner, the maker's plate; what it has not: the REVERB, DELAY and CAB plates and the HPF and LPF keys. What
    is its own over it: the lever, PARK over the four channels, its stops glyphs; COMP's fader and its squeeze; FX 1 (the
    delay's amount), FX 2 (the octave's mix) and REVERB; GATE's and LIMITER's buttons; the cabinet's slider in its screen,
    its name over it, and the service readings — PRESET, the version, MEM and CPU — on the picture under it; the cat in the
    maker's plate, silver, and crimson in flames under the hand. */
class MetalEditor final : public face::Console,
                          private juce::OpenGLRenderer
{
public:
    explicit MetalEditor (AmpProcessor&);
    ~MetalEditor() override;

private:
    // THE WORLD, under the whole stage, in the windscreen the rovers look through (redpaw/view/Windscreen.h): its gear the
    // channel's, parked while the amp is, answering the guitar. Its GPU frame is drawn by this window's own context,
    // which paints the face over it.
    std::unique_ptr<redpaw::Windscreen> view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the stage in the editor's pixels
    static std::unique_ptr<redpaw::Drive> makeDrive();
    Cabin& cabin() { return static_cast<Cabin&> (*picture); }
    void placeView();
    void chooseLandscape (redpaw::Windscreen::Mode);
    void chooseFrameRate (int fps);
    void showWindowMenu();
    void addWindowSections (juce::PopupMenu&) override;
    void stagePlaced() override { placeView(); }
    struct RightClicks final : juce::MouseListener
    {
        std::function<void()> menu;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && menu) menu(); }
    };
    RightClicks glassClicks;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // THE FADERS: COMP's threshold and its squeeze beside it; FX 1, the delay's amount; FX 2, the octave's mix, its name
    // OCTAVE under the hand; REVERB, the room's share. A double click puts any of them back where it opens.
    face::ColumnFader comp, fx1, fx2, reverb;
    redgroove::GrColumn squeeze;

    template <typename Control>
    void bindTravel (Control&, const juce::String& paramId, std::function<float (float)> toTravel,
                     std::function<float (float)> toPlain);

    // THE HAND ON THE CHANNELS: the lever, PARK over the four — PARK the amp's bypass, the others the channels (and the
    // number keys, a Program Change, a host's CHANNEL with them); its stop's full name under the hand and, as it lands, on
    // the tuner's screen for a moment.
    GlyphLever lever;
    TunerNote note;
    juce::ParameterAttachment park;
    bool parked = false;
    int placeShown = -1;
    juce::String placeName (int place) const;

    // GATE AND LIMITER: buttons in the plate's wells, each what the console's door was — its menu on a click.
    WordButton gateButton { "Gate" }, limiterButton { "Limiter" };

    // THE CABINET: the slider in its screen, a notch a cabinet of the TOP 12, its name in the gap over it; the picture under
    // it carries the service readings (Places: preset, version, memory, load).
    redgroove::CabinetScreen cabinet;
    void hearSlot (int notch);
    void chooseSlot (int notch);
    int dragAt = -1;

    // THE CAT in the maker's plate, live over the plate.
    CatLogo logo;

    std::vector<std::unique_ptr<redgroove::Readout>> readouts;
    redgroove::Readout& readout (juce::Rectangle<float> box);

    // THE SITE'S DEMO (Console::webExtras): what is the cabin's own, in the walker's words, and a poster a stop of the lever.
    void webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world) override;
    juce::Image webStill;           // the site's still: the world's first frame, the first a poster brings

    void showSelector (int channel) override;
    void showPark (bool on);
    void showPlace();
    bool channelKey (const juce::KeyPress&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalEditor)
};

} // namespace orbitamp::metal
