// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Console.h"
#include "../redgroove/RedGrooveControls.h"
#include "../redpaw/view/Windscreen.h"

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

namespace orbitamp::underwater
{

/** THE BATHYSCAPHE'S DASHBOARD ITSELF: the picture with its window transparent, the family's cat cut into the maker's
    plate, and every printed word on it — the panels' titles, the controls' names, the maker's words beside the plate,
    and the playing channel's tones' names over their knobs. Drawn over the sea, under the controls, and never takes a
    click.

    THE PANEL IS THE WORLD'S TO DRAW while it draws it: the sea's light moves on it (view/UnderwaterGl.h, its last
    pass), and a picture painted here would cover that. So while `worldDrawsPanel` says so, the layer on the screen is
    the words alone; the rest of the time — the CPU's landscape, the still, a GPU that will not draw — and in every
    picture taken of it (staticImage), the dashboard is painted here under them. */
class Cabin final : public face::Picture
{
public:
    std::function<bool()> worldDrawsPanel;
    /** Whether the playing gear has no BLEND of its own (CLEAN): BLEND's name stands dim then. */
    std::function<bool()> blendDark;

private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintStrips (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override;
    juce::Rectangle<int> historyArea() const override;
    int layerState() const override
    {
        return (worldDrawsPanel != nullptr && worldDrawsPanel() ? 1 : 0) | (blendDark != nullptr && blendDark() ? 2 : 0);
    }
};

//==============================================================================
/** DEEPGROOVEBASS'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the
    bathyscaphe's dashboard, the sea floor under the whole of it, its channels the bass's three gears. What it has of
    the console's own: DRIVE, the tones, the six keys of the history and the registers, IN and OUT, GATE and LIMITER, the
    tuner, the maker's plate; what it has not: the REVERB, DELAY and CAB plates and the HPF and LPF keys
    (Console::Places::plates, filterKeys). What is its own over it: the lever, PARK over the three gears; BLEND; the
    three effects' faders, OCTAVE, CHORUS and REVERB; a ring round every knob and a reading under every control; the
    cabinet's slider in its screen and the cuts' two switches. */
class UnderwaterEditor final : public face::Console,
                               private juce::OpenGLRenderer
{
public:
    explicit UnderwaterEditor (AmpProcessor&);
    ~UnderwaterEditor() override;

private:
    // THE SEA, under the whole stage: the bathyscaphe's world (view/UnderwaterDrive.h) in the windscreen the rovers look
    // through (redpaw/view/Windscreen.h), its gear the channel's, parked while the amp is, its living things answering the
    // bass. Its GPU frame is drawn by this window's own context, which paints the face over it; with the panel, the sea's
    // light on it, as its last pass.
    std::unique_ptr<redpaw::Windscreen> view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the stage in the editor's pixels
    static std::unique_ptr<redpaw::Drive> makeDrive();
    Cabin& cabin() { return static_cast<Cabin&> (*picture); }
    void placeView();
    void chooseLandscape (redpaw::Windscreen::Mode);
    void chooseFrameRate (int fps);
    void showWindowMenu();          // a right click on the glass: WINDOW and LANDSCAPE, as the settings menu has them
    void addWindowSections (juce::PopupMenu&) override;
    void stagePlaced() override { placeView(); }
    // A right click on the glass is the view's menu; a left drag across it steers.
    struct GlassClicks final : juce::MouseListener
    {
        std::function<void()> menu;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && menu) menu(); }
    };
    GlassClicks glassClicks;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // THE BASS'S OWN CONTROLS: BLEND, and the three effects' amounts, 0 the dry. A double click puts any of them back
    // where it opens.
    face::ColumnFader blend, octave, chorus, reverb;

    // COMP (the owner, 17.11): a fader that is its threshold alone, 0 at the foot to -48 dB at the head, its ratio, its
    // character and its share the cabin's to hold (channels::Lineup::pinned) — and its squeeze in a column beside it.
    face::ColumnFader comp;
    redgroove::GrColumn squeeze;
    std::vector<std::unique_ptr<face::KnobRing>> rings;     // DRIVE's, then the tones'
    static constexpr size_t firstToneRing = 1;
    std::vector<std::unique_ptr<redgroove::Readout>> readouts;
    redgroove::Readout* blendValue = nullptr;

    // THE TONES' SOCKETS: which of the playing channel's tones stands in each — its parameter and its place in the pack's
    // list, none where the socket is bare — and the reading under each, which reads the knob in its socket.
    std::array<juce::String, 3> socketParam;
    std::array<int, 3> socketTone { -1, -1, -1 };
    std::array<redgroove::Readout*, 3> toneReadings {};
    void showToneReadings();

    // A SOCKET THE CHANNEL HAS NO TONE FOR keeps a knob, frozen (the owner, 17.11): CLEAN's middle, at its noon, dim, out of
    // the hand's reach, its ring with it.
    face::ArtKnob frozenTone;
    std::unique_ptr<face::KnobRing> frozenRing;

    /** A reading of its own in `box`: under a control, a size down and quieter; else the panel's names' size. */
    redgroove::Readout& readout (juce::Rectangle<float> box, bool underControl = true);

    template <typename Control>
    void bindTravel (Control&, const juce::String& paramId, std::function<float (float)> toTravel,
                     std::function<float (float)> toPlain, std::function<void (float)> shown);

    // THE HAND ON THE GEARS AND THE CUTS. The lever: PARK, then CLEAN, DIRT and DRIVE — PARK is the amp's bypass, the
    // gears the channels (and the number keys, a Program Change, a host's CHANNEL with them); the two switches throw
    // 5 STR / 4 STR and FINGERS / SLAP, each its frequency on its right click; the cabinet's slider in its screen, a
    // notch each of the bass's dozen.
    redgroove::ChannelLever lever;
    redgroove::SlideSwitch lowCut, highCut;
    redgroove::CabinetScreen cabinet;
    juce::ParameterAttachment park;
    bool parked = false;
    void bindSwitch (redgroove::SlideSwitch&, const char* paramId);
    void hearSlot (int notch);
    void chooseSlot (int notch);
    int dragAt = -1;                       // the slot under the hand while it moves the slider; -1 when it does not

    void showSelector (int channel) override;
    void showPark (bool on);
    void showBlend (float value);
    bool channelKey (const juce::KeyPress&) override;
    void stripsLaid (const face::Strips&) override;
    void webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world) override;
    juce::Image webStill;           // the site's still: the sea's first frame, the first a poster brings

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnderwaterEditor)
};

} // namespace orbitamp::underwater
