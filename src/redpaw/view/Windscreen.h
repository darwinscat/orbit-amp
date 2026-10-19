// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Drive.h"
#include "Mars1993.h"
#include "PlainsWorld.h"
#include "../../face/Steering.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace orbitamp::redpaw
{

/** THE WINDSCREEN. An endless drive across Mars, three ways: the Plains on the GPU (a context of the
    view's own, the landscape rasterized by the shaders), the 1993 landscape on the CPU (MARS.EXE, drawn
    into an image and painted), or STILL — one frame, rendered once and kept, for a player who wants
    nothing moving. The gears set the speed — as many as the product has, CLEAN first — and TURBO doubles
    it, eased over about a second; TURBO also raises dust, graded by the gear, the top one's the head-on
    storm. Steering is held input: a drag left or right across the glass while the button is down, the
    arrow keys while the view has the focus (a host often keeps the keys, so the drag is the main way),
    or `steer()` from outside.

    The world it looks out on is a Drive handed in at construction, made for the product's gears — the
    Plains of Mars, the ice moon's drive; the 1993 landscape wears that world's colours. The glass, the
    modes, the ladder and the hand are the same over any of them.

    Quality looks after itself: every frame is timed, and when the middle of a second's frames costs
    more than 4 ms five seconds running, the render scale comes down a rung (1.0, 0.75, 0.5); after half a
    minute in which the rung above would have cost under 3 ms, it climbs back one, never past the scale
    asked for. The ladder
    never changes the landscape: the 1993 one and the still are the player's to choose — only a GPU
    whose shaders will not build is left for the CPU by itself. `setMode` or `setRenderScale` from the
    player starts the ladder again. `status()` says where it stands, for a status line.

    A dumb view: it takes values, never a processor. The drive runs on the GL thread in GPU mode and on
    the message thread otherwise, paced by the screen's refresh and capped at 30 or 60 frames a second. */
class Windscreen final : public juce::Component,
                         private juce::OpenGLRenderer
{
public:
    enum class Mode { gpu, cpu, still };

    /** With the world to drive across, in the product's gears. */
    explicit Windscreen (std::unique_ptr<Drive> drive);
    ~Windscreen() override;

    /** The world, to be placed or clocked before the first frame. The GL thread's while the GPU draws. */
    Drive& drive() noexcept { return *world; }

    /** The player's choice of mode. */
    void setMode (Mode);
    /** The mode running now — the choice, or what the ladder has come down to. */
    Mode mode() const noexcept { return running; }

    /** 1..numGears(): CLEAN, CRUNCH, HI-GAIN, as far as the product has them. */
    void setGear (int gear);
    int gear() const noexcept { return gearWanted.load(); }
    int numGears() const noexcept { return world->numGears(); }

    /** TURBO: the second channel of the pedal, twice the speed. */
    void setTurbo (bool on);
    bool isTurbo() const noexcept { return turboWanted.load(); }

    /** The wipers' speed (Drive::setWipers), where the face has keys for it. */
    void setWipers (int speed) { wipersWanted.store (speed); }
    /** ...and the searchlights (Drive::setLights). */
    void setLights (bool left, bool right) { lightsWanted.store ((left ? 1 : 0) | (right ? 2 : 0)); }

    /** THE GUITAR'S PEAK, as the world hears it (Drive::hear): asked once a frame, on the thread that drives,
        for the input's peak since it was last asked. Set before the first setMode; none is silence. */
    void setHearing (std::function<float()> peakSinceAsked) { hearing = std::move (peakSinceAsked); }

    /** THE ROVER'S SPEED, km/h, as the view is driving it: eased toward what the gear and TURBO ask for.
        In the 1993 landscape its own drive, counted in the Plains' measure; in
        the still, what the gear asks for — nothing moves there to ease. Message thread. */
    float speedKmh() const noexcept;

    /** THE 1993 DRIVE'S PACE: texels a second in each gear, the first first — a lever longer than the
        table runs its top gears at the last — twice that with TURBO; eased to it with this time constant. */
    static constexpr std::array<float, 4> texelsPerSecond1993 { 2.0f, 5.0f, 10.0f, 16.0f };
    static constexpr double speedSeconds1993 = 0.28;

    /** Held steering input from outside, -1..1; 0 lets go. Adds to the view's own drag and keys. */
    void steer (float amount) noexcept;

    /** The GPU's render scale, 0.25..1; the scene is drawn at this fraction of the glass's pixels and
        scaled up. The CPU view takes its rows from it too. Also restarts the ladder. */
    void setRenderScale (float scale);
    float renderScale() const noexcept { return scaleWanted; }

    /** 30 or 60 frames a second at most. */
    void setFrameRateCap (int fps);
    int frameRateCap() const noexcept { return cap; }

    /** How a GPU that will not draw the world names this window in the family's log (shaders/GpuLog.h): the
        product, the format it was loaded as, the host (AmpProcessor::loadedAs). Said before the first
        setMode or setHost — the GL thread reads it as its context is made. */
    void setLogName (juce::String name) { logName = std::move (name); }

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
    /** The still as kept, when the mode is STILL and it has arrived; else invalid. Message thread. */
    juce::Image stillImage() const { return still; }

    /** THE VIEW'S FIRST FRAME, for a poster of the site's: `fresh` — a drive of the same kind, made just now
        and so at its start — standing where this view's gear and TURBO put it, settled there as STILL
        settles a frame (its speed and air reached, nothing stepped), with the wipers and the lights as this
        view has them and no hand on the helm, drawn on the GPU at `width` x `height` in the context this
        view draws in, and read back. What a page's copy of the view, started there, shows first. Handed to
        `done` on the message thread; invalid where the GPU will not draw here or no context is attached.
        Message thread. */
    void firstFrame (std::unique_ptr<Drive> fresh, int width, int height, std::function<void (juce::Image)> done);

    /** THE STILL AS SHIPPED: where the product brings a picture of its world, STILL shows that — asked
        for when STILL comes on, let go when it goes off — and renders nothing; else, or when it gives an
        invalid one, STILL renders a frame of its own. Message thread. */
    void setPicture (std::function<juce::Image()> picture);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

private:
    // The GPU's side. The drive — the world and its renderer — lives on the GL thread while the context is attached.
    std::unique_ptr<Drive> world;
    juce::OpenGLContext gl;
    juce::OpenGLContext* host = nullptr;    // the window's, when the view is hosted (setHost)
    bool glAttached = false;                // the GPU's frames are being drawn: in the view's own context, or the host's
    std::atomic<bool> gpuFailed { false };
    juce::String logName { "a windscreen of no amp" };    // setLogName
    std::atomic<float> gpuMsShared { -1.0f };
    std::atomic<float> speedShared { PlainsWorld::gearSpeed[0] };     // m/s, the GPU's drive, as last stepped
    std::atomic<int> gpuFrames { 0 };
    std::array<std::atomic<float>, 256> gpuMsRing {};    // every frame's cost, the GL thread writing, the ladder reading
    std::atomic<unsigned> gpuMsAt { 0 };
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
    std::function<juce::Image()> picture;
    bool stillShipped = false;           // `still` is the product's picture

    // The inputs, shared with the GL thread.
    std::atomic<int> gearWanted { 1 };
    std::atomic<bool> turboWanted { false };
    std::atomic<int> wipersWanted { 1 }, lightsWanted { 0 };
    std::function<float()> hearing;
    face::Steering steering;        // the drag, the keys, and steer() from outside, as one value
    std::atomic<float> scaleInUse { 1.0f };
    float scaleWanted = 1.0f;

    // The pacing and the ladder.
    Mode wanted = Mode::gpu, running = Mode::gpu;
    int cap = 60;
    bool adaptive = true;
    double lastFrame = 0.0, windowStart = 0.0;
    float fps = 0.0f;
    juce::String note;
    unsigned gpuMsRead = 0;                  // the ring read up to here
    std::vector<float> secondsMs;            // a second's frame costs, for the median
    int slowSeconds = 0, calmSeconds = 0;    // in a row
    juce::VBlankAttachment pulse { this, [this] { tick(); } };

    void tick();
    void apply();
    void restartLadder();
    void triggerFrame();
    void renderFrame (int x, int y, int w, int h, juce::uint32 target);
    void attachGl();
    void detachGl();
    void stepDown (const juce::String& why);
    void stepUp();
    void forgetCosts();
    float rungAbove (float scale) const noexcept;
    void drawCpuFrame (bool advance);
    void takeStill();

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Windscreen)
};

} // namespace orbitamp::redpaw
