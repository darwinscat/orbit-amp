// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Console.h"
#include "../face/Monitor.h"
#include "RedPawControls.h"
#include "view/Windscreen.h"

#include <juce_opengl/juce_opengl.h>

#include <atomic>

namespace orbitamp::redpaw
{

/** The dashboard itself: the picture with its windscreen cut out, the family's mark cut into the emblem
    plate, and every printed word on it. Drawn over the windscreen, under the controls, and never takes
    a click. */
class Dashboard final : public face::Picture
{
private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintLive (juce::Graphics&) const override;
    juce::Rectangle<int> historyArea() const override;

    void paintRoof (juce::Graphics&) const;
    void paintKeyNames (juce::Graphics&) const;
};

//==============================================================================
/** RedPawAmp's face over OrbitAmp's processor: the family's console (face/Console.h), laid out on the
    rover's dashboard, with what is the rover's own over it — the gear lever and TURBO, which between
    them pick one of the product's channels, the channel's name along the top of the nav screen, and the
    drive across Mars through the windscreen. Every rover wears it, each in a skin of its own
    (RedPawSkin.h). */
class RedPawEditor final : public face::Console,
                           private juce::OpenGLRenderer
{
public:
    explicit RedPawEditor (AmpProcessor&);
    ~RedPawEditor() override;

private:
    // THE WINDSCREEN: the product's world (RedPawSkin.h) — RedPawAmp's the drive across Mars
    // (view/Windscreen.h) — under the dashboard. Its GPU frame is drawn by this window's own context,
    // which paints the face over it — a GL surface of the view's own would lie over the picture on the
    // Mac (Windscreen::setHost). What it shows is the machine's to say: the landscape on the GPU, on the
    // CPU, or a still; and at most 30 or 60 frames a second.
    std::unique_ptr<Windscreen> view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the windscreen in the editor's pixels
    void placeView();
    void chooseLandscape (Windscreen::Mode);
    void chooseFrameRate (int fps);
    void showWindowMenu();          // a right click on the glass: WINDOW and LANDSCAPE, as the settings menu has them

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

    void webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject& world) override;

    // THE GEAR AND TURBO PICK THE CHANNEL: the gear the pedal, TURBO its second channel — an option of
    // the gear's, as its tone is: thrown, only the pack changes, and a gear pulled comes back with
    // TURBO as it was left (channels::Lineup::pairs). PARK, the lever's top end, takes
    // the whole amp out of the sound (ProductEngine::parkSwitch); the channel waits, and TURBO does
    // nothing until a gear is pulled again.
    GearLever lever;
    face::PushButton turbo { "TURBO" };
    TurboLid hatch;             // over it: TURBO's hatch, shut while parked

    // GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader down its lamp plate's outer leg:
    // -70 dB to -40, and -3 dB to -0.1, foot to head. While its block is off a fader stands dim, and
    // still moves — the block keeps what it was set to.
    face::ColumnFader threshold { Art::get().faderCapLight }, ceiling { Art::get().faderCapLight };
    std::vector<std::unique_ptr<KnobRing>> rings;   // DRIVE's and the tones': where each knob stands
    NavHeader header;               // on the nav screen: the gear, the pack that is playing, the speed
    SpeedHud hud;                   // ...or the speed on the glass, where the layout puts it there

    // THE NAV SCREEN'S PICTURE of the amp at work (face/Monitor.h), the machine's choice; a right click
    // on the screen offers the five.
    face::Monitor screen;
    ParkSign parkSign;              // parked: the warning, large, in the screen's middle
    GlassFilm glassFilm;            // the screen's glass, over all of it

    // LEARN's own keys, where the picture paints holes for them (RedPawLayout.h): GATE's learning and
    // HUM's, each started as from its menu, a key lit while its learning shows.
    face::PushButton learnKey { "LEARN GATE" }, humKey { "LEARN HUM" };
    int learnShowing = -1;          // whose learning the glass shows: 0 GATE's, 1 HUM's, -1 none

    // THE WIPERS' SPEED on keys of their own, where the picture paints a row for them (RedPawLayout.h):
    // each word on its key, the speed set lit — the machine's choice, as the landscape is.
    std::vector<std::unique_ptr<face::PushButton>> wiperButtons;
    int wipers = 1;
    void chooseWipers (int speed);

    void showScreenMenu();
    void chooseScreen (face::Monitor::View);
    void pullGear (int gear);
    void throwTurbo (bool on);
    void parkIt();
    bool isParked() const;

    void showSelector (int channel) override;
    bool channelKey (const juce::KeyPress&) override;
    void programChange (int program) override;
    void turboChange (bool thrown) override;
    void addWindowSections (juce::PopupMenu&) override;
    bool pictureRightClicked (juce::Point<int>) override;
    void stagePlaced() override { placeView(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedPawEditor)
};

} // namespace orbitamp::redpaw
