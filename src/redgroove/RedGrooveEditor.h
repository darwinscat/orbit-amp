// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "RedGrooveControls.h"

#include "../face/Console.h"
#include "../redpaw/view/Windscreen.h"

#include <juce_opengl/juce_opengl.h>

#include <atomic>

namespace orbitamp::redgroove
{

/** The walker's dashboard itself: the picture with its dome transparent, the family's cat cut into the maker's
    plate, and every printed word on it — the panels' titles, DRIVE's name, the maker's words beside the plate,
    and the playing channel's tones' names over their knobs. Drawn over the dome, under the controls, and never
    takes a click. */
class Cabin final : public face::Picture
{
private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintStrips (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override;
    juce::Rectangle<int> historyArea() const override;
};

//==============================================================================
/** REDGROOVEBASS'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the
    walker's dashboard, under its glass dome over the mushroom forest, its channels the bass's two amps. What
    the walker has of the console's own: DRIVE, the tones, the six keys of the history and the registers, IN
    and OUT, GATE and LIMITER, the tuner, the maker's plate; what it has not: REVERB, DELAY and CAB's plates,
    HPF and LPF (Console::Places::plates, filterKeys). What is the walker's own over it: COMP's knob, its threshold,
    and its squeeze beside it, BLEND, the three effects' faders, MIDDLE's frequency across under the tones, DI/CAB,
    a ring round every knob and a reading under every control; the channel lever, the cuts' two switches and the
    cabinet's screen; LEARN's glass over the tuner. */
class RedGrooveEditor final : public face::Console,
                              private juce::OpenGLRenderer
{
public:
    explicit RedGrooveEditor (AmpProcessor&);
    ~RedGrooveEditor() override;

private:
    // THE DOME: the walker's world (view/WalkerDrive.h) in the windscreen the rovers look through (redpaw/view/
    // Windscreen.h), under the dashboard, in the dome's box: its gear the channel's, its pace the host's tempo where
    // there is one, its forest breathing with the bass. Its GPU frame is drawn by this window's own context, which
    // paints the face over it. What it shows is the machine's to say: the forest on the GPU, the 1993 landscape on
    // the CPU, or the still the walker ships.
    std::unique_ptr<redpaw::Windscreen> view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the dome in the editor's pixels
    /** A walk of the walker's own, at its world's start, in the product's two gears, the horizon 57 % down the dome. */
    static std::unique_ptr<redpaw::Drive> makeDrive();
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

    // THE BASS'S OWN CONTROLS. COMP's knob is its threshold, clockwise the deeper (0 to -48 dB); its ratio, its
    // character and its share are the walker's to hold (channels::Lineup::pinned). BLEND and DI/CAB are their
    // parameters'; each effect's fader its amount, 0 the dry; MIDDLE's frequency lies across, 200 Hz to 3 kHz on a
    // logarithmic travel. A double click puts any of them back where it opens.
    face::ArtKnob comp;
    face::ColumnFader octave, envelope, chorus, midFreq, diCab, blend;
    GrColumn squeeze;
    std::vector<std::unique_ptr<face::KnobRing>> rings;     // DRIVE's, COMP's, BLEND's, DI/CAB's, then the tones'
    static constexpr size_t firstToneRing = 2;
    std::vector<std::unique_ptr<Readout>> readouts;

    /** A reading of its own under a control, in `box`. */
    Readout& readout (juce::Rectangle<float> box);

    /** A control onto a parameter through its own travel: where a value stands on it, and the value a travel is. */
    template <typename Control>
    void bindTravel (Control&, const juce::String& paramId, std::function<float (float)> toTravel,
                     std::function<float (float)> toPlain, std::function<void (float)> shown);

    // THE WALKER'S HAND ON THE CHANNELS AND THE CUTS. The lever picks the gear — and the number keys, a Program
    // Change, a host's CHANNEL with it; the two switches throw 5 STR / 4 STR and FINGERS / SLAP, each its frequency
    // on its right click; the cabinet's screen names the cabinet that plays, and a click on it opens the cabinets'
    // window — Ampeg's, drawn.
    ChannelLever lever;
    SlideSwitch lowCut, highCut, ultraLo, ultraHi;
    CabinetScreen cabinet;
    void bindSwitch (SlideSwitch&, const char* paramId);
    void showCabinets();

    void showSelector (int channel) override;
    bool channelKey (const juce::KeyPress&) override;
    void stripsLaid (const face::Strips&) override;     // the tones' rings go round their knobs where they stand now
    void webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedGrooveEditor)
};

} // namespace orbitamp::redgroove
