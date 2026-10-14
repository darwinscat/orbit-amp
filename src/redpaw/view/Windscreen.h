// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Mars1993.h"
#include "PlainsGl.h"
#include "PlainsWorld.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <vector>

namespace orbitamp::redpaw
{

/** THE WINDSCREEN. An endless drive across Mars, three ways: the Plains on the GPU (a context of the
    view's own, the landscape rasterized by the shaders), the 1993 landscape on the CPU (MARS.EXE, drawn
    into an image and painted), or STILL — one frame, rendered once and kept, for a player who wants
    nothing moving. Three gears set the speed — CLEAN, CRUNCH, HI-GAIN — and TURBO doubles it, eased over
    about a second; TURBO also raises dust, graded by the gear, up to the head-on storm. PARK brings the
    rover to a stand and clears the air. Steering is held input: a drag left or right across the glass while the button is down, the
    arrow keys while the view has the focus (a host often keeps the keys, so the drag is the main way),
    or `steer()` from outside.

    Quality looks after itself: the GPU's frame is timed, and when a second's frames average more than
    4 ms the render scale comes down (1.0, 0.75, 0.5), then the CPU takes over, then the still. It never
    climbs back by itself within a session — a jump up and down again is worse than a steady lesser
    picture; `setMode` or `setRenderScale` from the player starts the ladder again. `status()` says
    where it stands, for a status line.

    A dumb view: it takes values, never a processor. The drive runs on the GL thread in GPU mode and on
    the message thread otherwise, paced by the screen's refresh and capped at 30 or 60 frames a second. */
class Windscreen final : public juce::Component,
                         private juce::OpenGLRenderer
{
public:
    enum class Mode { gpu, cpu, still };

    Windscreen();
    ~Windscreen() override;

    /** The player's choice of mode. */
    void setMode (Mode);
    /** The mode running now — the choice, or what the ladder has come down to. */
    Mode mode() const noexcept { return running; }

    /** 1..3: CLEAN, CRUNCH, HI-GAIN. */
    void setGear (int gear);
    int gear() const noexcept { return gearWanted.load(); }

    /** TURBO: the second channel of the pedal, twice the speed. */
    void setTurbo (bool on);
    bool isTurbo() const noexcept { return turboWanted.load(); }

    /** PARK: the lever's fourth position. The rover stands in about 1.5 s, the dust settles, and the
        hand only turns the view slowly in place. */
    void setParked (bool on);
    bool isParked() const noexcept { return parkedWanted.load(); }

    /** Held steering input from outside, -1..1; 0 lets go. Adds to the view's own drag and keys. */
    void steer (float amount) noexcept;

    /** The GPU's render scale, 0.25..1; the scene is drawn at this fraction of the glass's pixels and
        scaled up. The CPU view takes its rows from it too. Also restarts the ladder. */
    void setRenderScale (float scale);
    float renderScale() const noexcept { return scaleWanted; }

    /** 30 or 60 frames a second at most. */
    void setFrameRateCap (int fps);
    int frameRateCap() const noexcept { return cap; }

    /** Whether the ladder may lower the quality on its own. On by default. */
    void setAdaptive (bool shouldAdapt) noexcept { adaptive = shouldAdapt; }

    /** HOSTED: the GPU's frame drawn by a context the view does not own. A GL surface of the glass's own
        is a native view, and on the Mac a native view lies over everything painted beside it — over the
        picture whose cut-out the glass is meant to show through. So a window whose picture lies over the
        glass attaches ONE context to itself, painting its components over what that context renders (as
        the cockpit's sky is drawn), and hands the view its frame through the three calls below: the view
        attaches nothing of its own then, asks the host for its frames, and paints nothing of its own
        where the GPU draws. Set before the first setMode; null (the default) is a context of the view's
        own. */
    void setHost (juce::OpenGLContext* host);

    /** The host's context was made, is closing; and a frame of it, the glass at `glass` in its frame
        buffer's pixels counted from the top-left, of a frame `frameWidth` x `frameHeight`. GL thread. The frame
        buffer is left bound, with the whole of it the viewport and nothing enabled that would stand in
        the way of what is painted over it. */
    void hostContextCreated();
    void hostContextClosing();
    void hostRender (juce::Rectangle<int> glass, int frameWidth, int frameHeight);

    struct Status
    {
        Mode mode = Mode::gpu;
        float scale = 1.0f;          // the render scale in use
        float frameMs = -1.0f;       // what a frame costs: the GPU's time, or the CPU render; -1 until known
        float fps = 0.0f;            // frames drawn in the last second
        int frameWidth = 0, frameHeight = 0;   // the scene's pixels
        bool gpuBroken = false;      // the shaders would not build here
        juce::String note;           // why the ladder moved, if it did
    };
    Status status() const;
    /** The status in one line, for a label. */
    juce::String statusLine() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    // The GPU's side. The world and its renderer live on the GL thread while the context is attached.
    PlainsWorld world;
    PlainsGl plains { world };
    juce::OpenGLContext gl;
    juce::OpenGLContext* host = nullptr;    // the window's, when the view is hosted (setHost)
    bool glAttached = false;                // the GPU's frames are being drawn: in the view's own context, or the host's
    std::atomic<bool> gpuFailed { false };
    std::atomic<float> gpuMsShared { -1.0f };
    std::atomic<int> gpuFrames { 0 };
    std::atomic<int> gpuFrameW { 0 }, gpuFrameH { 0 };
    double glLast = 0.0;

    // The CPU's side: the 1993 landscape, on the message thread.
    Mars1993 mars;
    juce::Image cpuImage;
    double marsPos = 0x3E8 * 1.0, marsX = 0x3E8 * 1.0, marsSpeed = 2.0;   // 12.4 texels, as doubles
    bool marsMade = false;
    int cpuFrames = 0;
    float cpuMs = -1.0f;
    double cpuLast = 0.0;

    // The still.
    std::atomic<bool> stillWanted { false }, stillReady { false };
    juce::CriticalSection stillLock;
    std::vector<juce::uint8> stillPixels;
    int stillW = 0, stillH = 0;
    juce::Image still;

    // The inputs, shared with the GL thread.
    std::atomic<int> gearWanted { 1 };
    std::atomic<bool> turboWanted { false }, parkedWanted { false };
    std::atomic<float> steerOutside { 0.0f }, steerDrag { 0.0f }, steerKeys { 0.0f };
    std::atomic<float> scaleInUse { 1.0f };
    float scaleWanted = 1.0f;
    int dragStartX = 0;

    // The pacing and the ladder.
    Mode wanted = Mode::gpu, running = Mode::gpu;
    int cap = 60;
    bool adaptive = true;
    double lastFrame = 0.0, windowStart = 0.0;
    float fps = 0.0f;
    juce::String note;
    juce::VBlankAttachment pulse { this, [this] { tick(); } };

    void tick();
    void apply();
    void triggerFrame();
    void renderFrame (int x, int y, int w, int h, juce::uint32 target);
    void attachGl();
    void detachGl();
    void stepDown (const juce::String& why);
    void drawCpuFrame (bool advance);
    void takeStill();
    float steerTotal() const noexcept;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Windscreen)
};

} // namespace orbitamp::redpaw
