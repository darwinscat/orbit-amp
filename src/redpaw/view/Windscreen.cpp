// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Windscreen.h"

#include <chrono>
#include <cmath>

namespace orbitamp::redpaw
{

namespace
{
    constexpr float tooSlowMs = 4.0f;             // a second's frames above this, and the ladder steps down
    constexpr std::array<float, 3> marsTexelsPerSecond { 2.0f, 5.0f, 10.0f };   // the 1993 drive's gears
    constexpr float marsStrafePerSecond = 6.0f;   // the 1993 view has no heading: steering slides it along u

    /** The 1993 program took its seed from the clock, 18.2 ticks a second since midnight, 15 bits kept. */
    uint16_t clockSeed()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto sinceEpoch = duration_cast<seconds> (now.time_since_epoch()).count();
        const auto secondsToday = (double) (sinceEpoch % 86400);
        return (uint16_t) ((long long) (secondsToday * 1193180 / 65536) & 0x7FFF);
    }
}

Windscreen::Windscreen()
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (false);
    gl.setContinuousRepainting (false);     // the pulse asks for each frame, on every platform: see tick()
    windowStart = lastFrame = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

Windscreen::~Windscreen()
{
    detachGl();
}

//==============================================================================
/** THE PLAYER'S CHOICE ALWAYS WINS: whatever the ladder came down to, a mode chosen starts afresh — the
    Plains at the render scale asked for, the cost forgotten. Only the ladder's own steps are one-way. */
void Windscreen::setMode (Mode m)
{
    wanted = running = m;
    note.clear();
    restartLadder();
    apply();
}

void Windscreen::restartLadder()
{
    scaleInUse.store (scaleWanted);
    gpuMsShared.store (-1.0f);
    gpuFrames.store (0);
    cpuMs = -1.0f;
    cpuFrames = 0;
    windowStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void Windscreen::setGear (int g)
{
    gearWanted.store (juce::jlimit (1, PlainsWorld::numGears, g));
}

void Windscreen::setTurbo (bool on)
{
    turboWanted.store (on);
}

void Windscreen::setParked (bool on)
{
    parkedWanted.store (on);
}

void Windscreen::steer (float amount) noexcept
{
    steering.setOutside (amount);
}

void Windscreen::setRenderScale (float scale)
{
    scaleWanted = juce::jlimit (0.25f, 1.0f, scale);
    running = wanted;
    note.clear();
    restartLadder();
    apply();
}

void Windscreen::setFrameRateCap (int fpsCap)
{
    cap = fpsCap >= 60 ? 60 : 30;
}

float Windscreen::speedKmh() const noexcept
{
    constexpr float kmh = 3.6f;
    const int g = juce::jlimit (1, PlainsWorld::numGears, gearWanted.load());
    switch (running)
    {
        case Mode::gpu:   return speedShared.load() * kmh;
        case Mode::cpu:   return (float) marsSpeed / marsTexelsPerSecond[(size_t) g - 1] * PlainsWorld::gearSpeed[(size_t) g - 1] * kmh;
        case Mode::still: break;
    }
    return parkedWanted.load() ? 0.0f : PlainsWorld::speedFor (g, turboWanted.load()) * kmh;
}

//==============================================================================
Windscreen::Status Windscreen::status() const
{
    Status st;
    st.mode = running;
    st.scale = scaleInUse.load();
    st.fps = fps;
    st.gpuBroken = gpuFailed.load();
    st.note = note;
    if (running == Mode::gpu)
    {
        st.frameMs = gpuMsShared.load();
        st.frameWidth = gpuFrameW.load(); st.frameHeight = gpuFrameH.load();
    }
    else if (running == Mode::cpu)
    {
        st.frameMs = cpuMs;
        st.frameWidth = mars.width(); st.frameHeight = mars.height();
    }
    else
    {
        st.frameMs = 0.0f;
        st.frameWidth = still.getWidth(); st.frameHeight = still.getHeight();
    }
    return st;
}

juce::String Windscreen::statusLine() const
{
    const auto st = status();
    juce::String s;
    switch (st.mode)
    {
        case Mode::gpu:   s << "GPU  Plains"; break;
        case Mode::cpu:   s << "CPU  1993"; break;
        case Mode::still: s << "STILL"; break;
    }
    if (st.mode != Mode::still)
        s << "   " << juce::String (st.fps, 0) << " fps   " << (st.frameMs < 0 ? juce::String ("?") : juce::String (st.frameMs, 2)) << " ms";
    s << "   " << st.frameWidth << "x" << st.frameHeight;
    if (st.mode == Mode::gpu)
        s << "   scale " << juce::String (st.scale, 2);
    s << "   gear " << gearWanted.load() << (turboWanted.load() ? " TURBO" : "") << (parkedWanted.load() ? " PARK" : "");
    if (st.gpuBroken)
        s << "   (no GPU: the shaders did not build)";
    if (st.note.isNotEmpty())
        s << "   " << st.note;
    return s;
}

//==============================================================================
void Windscreen::apply()
{
    stillReady.store (false);
    stillWanted.store (false);
    switch (running)
    {
        case Mode::gpu:
            if (gpuFailed.load()) { running = Mode::cpu; apply(); return; }
            attachGl();
            repaint();          // hosted, what the CPU or the still painted here must go, or it lies over the GPU's frame
            triggerFrame();
            break;
        case Mode::cpu:
            detachGl();
            drawCpuFrame (false);
            repaint();
            break;
        case Mode::still:
            takeStill();
            break;
    }
}

void Windscreen::attachGl()
{
    if (glAttached)
        return;
    if (host == nullptr)
        gl.attachTo (*this);
    glAttached = true;
}

void Windscreen::detachGl()
{
    if (! glAttached)
        return;
    if (host == nullptr)
        gl.detach();
    glAttached = false;
}

void Windscreen::triggerFrame()
{
    if (host != nullptr)
        host->triggerRepaint();
    else
        gl.triggerRepaint();
}

void Windscreen::setHost (juce::OpenGLContext* context)
{
    detachGl();
    host = context;
    setOpaque (host == nullptr);    // hosted, the GPU's frame shows through where the view paints nothing
}

void Windscreen::hostContextCreated()
{
    newOpenGLContextCreated();
}

void Windscreen::hostContextClosing()
{
    openGLContextClosing();
}

void Windscreen::hostRender (juce::Rectangle<int> glass, int frameWidth, int frameHeight)
{
    if (host == nullptr || ! glAttached)
        return;

    const auto target = host->getFrameBufferID();
    renderFrame (glass.getX(), frameHeight - glass.getBottom(), glass.getWidth(), glass.getHeight(), target);

    // What is painted over the frame next is the host's components: the whole surface its viewport,
    // nothing of the scene's state left on.
    using namespace juce::gl;
    glBindFramebuffer (GL_FRAMEBUFFER, target);
    glDisable (GL_DEPTH_TEST);
    glDisable (GL_CULL_FACE);
    glDisable (GL_SCISSOR_TEST);
    glDisable (GL_BLEND);
    glBindVertexArray (0);
    glUseProgram (0);
    glViewport (0, 0, frameWidth, frameHeight);
}

/** One frame, kept. From the GPU where it works — the GL thread reads it back and the pulse collects
    it — else from the 1993 landscape at once. */
void Windscreen::takeStill()
{
    if (! gpuFailed.load())
    {
        attachGl();
        stillWanted.store (true);
        triggerFrame();
        return;
    }
    detachGl();
    drawCpuFrame (false);
    still = cpuImage.createCopy();
    repaint();
}

void Windscreen::stepDown (const juce::String& why)
{
    if (running == Mode::gpu)
    {
        const float sc = scaleInUse.load();
        if (sc > 0.76f)      { scaleInUse.store (0.75f); note = "scale 0.75: " + why; }
        else if (sc > 0.51f) { scaleInUse.store (0.5f);  note = "scale 0.5: " + why; }
        else                 { running = Mode::cpu;      note = "CPU: " + why; apply(); }
    }
    else if (running == Mode::cpu)
    {
        running = Mode::still;
        note = "still: " + why;
        apply();
    }
    gpuMsShared.store (-1.0f);
    cpuMs = -1.0f;
}

//==============================================================================
/** The pulse: once per refresh of the screen. The frames are asked for here on every platform — on
    Linux JUCE's GL thread, told to repaint continuously, renders flat out with nothing pacing it but a
    buffer swap that through XWayland does not always hold; and the cap wants a say everywhere. */
void Windscreen::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Once a second: the count, the cost, the ladder.
    if (now - windowStart >= 1.0)
    {
        const double span = now - windowStart;
        const int frames = running == Mode::gpu ? gpuFrames.exchange (0) : cpuFrames;
        cpuFrames = 0;
        fps = (float) (frames / span);
        windowStart = now;
        if (gpuFailed.load() && running == Mode::gpu)
        {
            note = "the GPU's shaders did not build";
            running = Mode::cpu;
            apply();
        }
        else if (adaptive && frames > 0)
        {
            const float ms = running == Mode::gpu ? gpuMsShared.load() : (running == Mode::cpu ? cpuMs : -1.0f);
            if (ms > tooSlowMs)
                stepDown (juce::String (ms, 1) + " ms a frame");
        }
    }

    if (running == Mode::still)
    {
        if (stillReady.exchange (false))
        {
            {
                const juce::ScopedLock sl (stillLock);
                still = juce::Image (juce::Image::ARGB, stillW, stillH, false);
                juce::Image::BitmapData bd (still, juce::Image::BitmapData::writeOnly);
                for (int y = 0; y < stillH; ++y)
                {
                    const juce::uint8* src = stillPixels.data() + (size_t) (stillH - 1 - y) * (size_t) stillW * 4;   // GL rows run bottom-up
                    for (int x = 0; x < stillW; ++x, src += 4)
                        bd.setPixelColour (x, y, juce::Colour (src[0], src[1], src[2]));
                }
            }
            detachGl();
            repaint();
        }
        return;
    }

    // The cap: a frame no sooner than its period, less half a refresh so a 60 Hz screen keeps all its frames.
    const double period = 1.0 / cap;
    if (now - lastFrame < period - 0.004)
        return;
    lastFrame = now;

    if (running == Mode::gpu)
    {
        if (glAttached)
            triggerFrame();
    }
    else
    {
        drawCpuFrame (true);
        repaint();
    }
}

//==============================================================================
/** The 1993 landscape: the drive advances along +v at the gear's pace, the frame is drawn into the
    image at the glass's rows scaled, and the DAC is the palette. */
void Windscreen::drawCpuFrame (bool advance)
{
    if (! marsMade)
    {
        mars.makeMaps (clockSeed());
        marsMade = true;
        cpuLast = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
    const int rows = juce::jmax (50, juce::roundToInt ((float) getHeight() * scaleInUse.load())) & ~1;
    if (mars.height() != rows)
        mars.setRows (rows);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = advance ? juce::jlimit (0.0, 0.1, now - cpuLast) : 0.0;
    cpuLast = now;
    const int g = gearWanted.load();
    const bool parked = parkedWanted.load();
    const double want = parked ? 0.0 : marsTexelsPerSecond[(size_t) g - 1] * (turboWanted.load() ? PlainsWorld::turboFactor : 1.0f);
    marsSpeed += (want - marsSpeed) * (1 - std::exp (-dt / (parked ? PlainsWorld::parkSeconds : 0.28)));
    marsPos += marsSpeed * 16 * dt;
    if (! parked)
        marsX += steering.value() * marsStrafePerSecond * 16 * dt;
    marsPos = std::fmod (marsPos + 65536.0, 65536.0);
    marsX = std::fmod (marsX + 65536.0, 65536.0);

    const double t0 = juce::Time::getMillisecondCounterHiRes();
    mars.render ((uint16_t) (long long) marsX, (uint16_t) (long long) marsPos);
    const int w = mars.width(), h = mars.height();
    if (cpuImage.getWidth() != w || cpuImage.getHeight() != h)
        cpuImage = juce::Image (juce::Image::ARGB, w, h, false);
    {
        juce::Image::BitmapData bd (cpuImage, juce::Image::BitmapData::writeOnly);
        const auto* idx = mars.indices();
        const auto* pal = mars.palette();
        for (int y = 0; y < h; ++y)
        {
            auto* row = reinterpret_cast<juce::uint32*> (bd.getLinePointer (y));
            const auto* src = idx + (size_t) y * (size_t) w;
            for (int x = 0; x < w; ++x)
                row[x] = pal[src[x]];
        }
    }
    const float ms = (float) (juce::Time::getMillisecondCounterHiRes() - t0);
    cpuMs = cpuMs < 0 ? ms : cpuMs + (ms - cpuMs) * 0.1f;
    ++cpuFrames;
}

void Windscreen::paint (juce::Graphics& g)
{
    if (host != nullptr && running == Mode::gpu)
        return;     // the host's context draws the glass here, under what is painted over it
    g.fillAll (juce::Colours::black);
    const auto* image = running == Mode::still ? &still : (running == Mode::cpu ? &cpuImage : nullptr);
    if (image == nullptr || ! image->isValid())
        return;
    g.setImageResamplingQuality (running == Mode::cpu ? juce::Graphics::lowResamplingQuality : juce::Graphics::highResamplingQuality);
    g.drawImage (*image, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
}

void Windscreen::resized()
{
    if (running == Mode::still)
        takeStill();   // a still at the new size; the old one is painted stretched until it arrives
}

//==============================================================================
void Windscreen::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    steering.press ((float) e.x);
}

void Windscreen::mouseDrag (const juce::MouseEvent& e)
{
    steering.drag ((float) e.x, (float) getWidth());     // a quarter of the glass's width is full lock
}

void Windscreen::mouseUp (const juce::MouseEvent&)
{
    steering.release();
}

bool Windscreen::keyPressed (const juce::KeyPress& k)
{
    return face::Steering::steersBy (k);     // held: keyStateChanged
}

bool Windscreen::keyStateChanged (bool)
{
    return steering.keysChanged();
}

void Windscreen::focusLost (FocusChangeType)
{
    steering.keysLost();
}

//==============================================================================
void Windscreen::newOpenGLContextCreated()
{
    if (! plains.create())
        gpuFailed.store (true);
    glLast = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void Windscreen::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    juce::OpenGLHelpers::clear (juce::Colours::black);
    renderFrame (0, 0, juce::roundToInt ((float) getWidth() * k), juce::roundToInt ((float) getHeight() * k), gl.getFrameBufferID());
}

/** One frame of the drive, into `target` over (x, y, w, h) — GL's pixels, from the bottom-left. */
void Windscreen::renderFrame (int x, int y, int w, int h, juce::uint32 target)
{
    if (gpuFailed.load() || w <= 0 || h <= 0)
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.0, 0.05, now - glLast);
    glLast = now;
    world.setGear (gearWanted.load());
    world.setTurbo (turboWanted.load());
    world.setParked (parkedWanted.load());
    world.setSteer (steering.value());
    const bool wantStill = stillWanted.load();
    if (wantStill)
        world.place (world.state().x, world.state().z, world.state().heading, gearWanted.load(), turboWanted.load());   // a still is clear air, as the drive would be once settled
    else
        world.step (dt);
    speedShared.store (world.state().speed);

    plains.render (w, h, scaleInUse.load(), target, x, y);
    gpuMsShared.store (plains.gpuMillis());
    gpuFrameW.store (plains.frameWidth()); gpuFrameH.store (plains.frameHeight());
    ++gpuFrames;

    if (wantStill && stillWanted.exchange (false))
    {
        const juce::ScopedLock sl (stillLock);
        if (plains.readFrame (stillPixels, stillW, stillH))
            stillReady.store (true);
    }
}

void Windscreen::openGLContextClosing()
{
    plains.destroy();
}

} // namespace orbitamp::redpaw
