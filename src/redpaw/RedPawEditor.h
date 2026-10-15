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
    them pick one of the six channels, the channel's name along the top of the nav screen, and the
    drive across Mars through the windscreen. */
class RedPawEditor final : public face::Console,
                           private juce::OpenGLRenderer
{
public:
    explicit RedPawEditor (AmpProcessor&);
    ~RedPawEditor() override;

    /** Esc leaves the landscape alone, as it leaves full screen. */
    bool keyPressed (const juce::KeyPress&) override;

private:
    // THE WINDSCREEN: the drive across Mars (view/Windscreen.h), under the dashboard. Its GPU frame is
    // drawn by this window's own context, which paints the face over it — a GL surface of the view's
    // own would lie over the picture on the Mac (Windscreen::setHost). What it shows is the machine's
    // to say: the Plains on the GPU, the 1993 landscape on the CPU, or a still; and at most 30 or 60
    // frames a second.
    Windscreen view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the windscreen in the editor's pixels
    void placeView();
    void chooseLandscape (Windscreen::Mode);
    void chooseFrameRate (int fps);
    void showWindowMenu();          // a right click on the glass: WINDOW and LANDSCAPE, as the settings menu has them

    // THE TWO SCREEN KEYS ON THE ROOF. The left one takes the whole face full screen — the WINDOW menu's
    // own — where the rover stands alone; inside a host there is no such screen, and it stands dim. The
    // right one puts the LANDSCAPE ALONE in the window: the view out of the dashboard and over all of it
    // — and in the standalone over the whole screen — the drive, the keys and the steering as they
    // were; Esc, or the small key in its corner, brings the dashboard back.
    face::PushButton faceScreen { "FULL SCREEN" }, landscapeScreen { "LANDSCAPE" }, dashboardBack { "DASHBOARD" };
    bool landscapeAlone = false;
    bool landscapeTookScreen = false;   // the landscape key took the screen, and gives it back
    void setLandscapeAlone (bool);
    void placeLandscape();

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

    // THE GEAR AND TURBO PICK THE CHANNEL: the gear the pedal, TURBO its second channel. TURBO is one
    // switch for all three gears — a gear pulled keeps it as it stands. PARK, the lever's top end, takes
    // the whole amp out of the sound (ProductEngine::parkSwitch); the channel waits, and TURBO does
    // nothing until a gear is pulled again.
    GearLever lever;
    face::PushButton turbo { "TURBO" };
    std::vector<std::unique_ptr<KnobRing>> rings;   // DRIVE's and the tones': where each knob stands
    NavHeader header;               // on the nav screen: the gear, the pack that is playing, the speed

    // THE NAV SCREEN'S PICTURE of the amp at work (face/Monitor.h), the machine's choice; a right click
    // on the screen offers the five.
    face::Monitor screen;
    GlassFilm glassFilm;            // the screen's glass, over all of it
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
    void stagePlaced() override { placeLandscape(); placeView(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedPawEditor)
};

} // namespace orbitamp::redpaw
