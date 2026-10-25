// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Windscreen.h"

#include "../../face/WebImages.h"
#include "../../shaders/Gpu.h"
#include "../../shaders/GpuLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace orbitamp::redpaw
{

namespace
{
    constexpr float tooSlowMs = 4.0f;             // a second's median frame above this is a slow second
    constexpr float climbMs = tooSlowMs * 0.75f;  // a calm second: the rung above would cost under this
    constexpr int slowSecondsToStep = 5;          // slow seconds in a row before the ladder steps down
    constexpr int calmSecondsToClimb = 30;        // calm seconds in a row before it climbs back a rung
    constexpr float lowestScale = 0.5f;           // the ladder's foot: it never leaves the mode the player chose

    /** The middle of a second's frame costs: one slow frame — the screen shared with another GL window, a
        busy moment of the system — moves it no more than one fast frame does. */
    float medianOf (std::vector<float>& ms)
    {
        if (ms.empty())
            return -1.0f;
        const auto mid = ms.begin() + (std::ptrdiff_t) (ms.size() / 2);
        std::nth_element (ms.begin(), mid, ms.end());
        return *mid;
    }
    /** The 1993 drive's pace in a gear (Windscreen::texelsPerSecond1993). */
    float marsTexels (int gear) noexcept
    {
        const auto& pace = Windscreen::texelsPerSecond1993;
        return pace[(size_t) juce::jlimit (1, (int) pace.size(), gear) - 1];
    }
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

Windscreen::Windscreen (std::unique_ptr<Drive> drive)
    : world (std::move (drive))
{
    jassert (world != nullptr);
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
    Plains at the render scale asked for, the cost forgotten. */
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
    gpuMsRead = gpuMsAt.load();
    cpuMs = -1.0f;
    cpuFrames = 0;
    slowSeconds = calmSeconds = 0;
    windowStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void Windscreen::setGear (int g)
{
    gearWanted.store (juce::jlimit (1, world->numGears(), g));
}

void Windscreen::setTurbo (bool on)
{
    turboWanted.store (on);
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

void Windscreen::setPicture (std::function<juce::Image()> p)
{
    picture = std::move (p);
    still = {};
    stillShipped = false;
    if (running == Mode::still)
        takeStill();
}

float Windscreen::speedKmh() const noexcept
{
    constexpr float kmh = 3.6f;
    const int g = juce::jlimit (1, world->numGears(), gearWanted.load());
    switch (running)
    {
        case Mode::gpu:   return speedShared.load() * kmh;
        case Mode::cpu:   return (float) marsSpeed / marsTexels (g) * world->speedFor (g, false) * kmh;
        case Mode::still: break;
    }
    return world->speedFor (g, turboWanted.load()) * kmh;
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
        case Mode::gpu:   s << "GPU  " << world->name(); break;
        case Mode::cpu:   s << "CPU  1993"; break;
        case Mode::still: s << "STILL"; break;
    }
    if (st.mode != Mode::still)
        s << "   " << juce::String (st.fps, 0) << " fps   " << (st.frameMs < 0 ? juce::String ("?") : juce::String (st.frameMs, 2)) << " ms";
    s << "   " << st.frameWidth << "x" << st.frameHeight;
    if (st.mode == Mode::gpu)
        s << "   scale " << juce::String (st.scale, 2);
    s << "   gear " << gearWanted.load() << (turboWanted.load() ? " TURBO" : "");
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
    if (running != Mode::still && stillShipped)
    {
        still = {};             // the product's picture goes with STILL
        stillShipped = false;
    }
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
    // A world that did not start drew nothing and left nothing on — and a GL that runs no shaders has none of
    // the calls below.
    if (host == nullptr || ! glAttached || gpuFailed.load())
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

void Windscreen::firstFrame (std::unique_ptr<Drive> fresh, int width, int height, std::function<void (juce::Image)> done)
{
    // The pixels come back from the GL thread; the picture is made of them here, on the message thread.
    const auto deliver = [done] (std::shared_ptr<std::vector<std::uint8_t>> rgba, int w, int h)
    {
        juce::MessageManager::callAsync ([done, rgba, w, h] { done (rgba != nullptr ? face::web::fromGlRows (*rgba, w, h) : juce::Image()); });
    };
    auto* context = host != nullptr ? host : &gl;
    if (fresh == nullptr || gpuFailed.load() || ! context->isAttached() || width <= 0 || height <= 0)
    {
        deliver (nullptr, 0, 0);
        return;
    }
    const std::shared_ptr<Drive> drive (std::move (fresh));
    const int g = gearWanted.load(), lights = lightsWanted.load(), wipers = wipersWanted.load();
    const bool turbo = turboWanted.load();
    context->executeOnGLThread ([drive, g, turbo, wipers, lights, width, height, deliver] (juce::OpenGLContext& c)
    {
        // As renderFrame draws a still: made, told where it stands, settled, drawn once, read back.
        std::shared_ptr<std::vector<std::uint8_t>> rgba;
        int w = 0, h = 0;
        if (drive->create())
        {
            drive->setGear (g);
            drive->setTurbo (turbo);
            drive->setSteer (0.0f);
            drive->setWipers (wipers);
            drive->setLights ((lights & 1) != 0, (lights & 2) != 0);
            drive->settle (g, turbo);
            drive->render (width, height, 1.0f, c.getFrameBufferID(), 0, 0);
            rgba = std::make_shared<std::vector<std::uint8_t>>();
            if (! drive->readFrame (*rgba, w, h))
                rgba.reset();
        }
        drive->destroy();

        // ...and what is painted next finds the context as hostRender leaves it.
        using namespace juce::gl;
        glBindFramebuffer (GL_FRAMEBUFFER, c.getFrameBufferID());
        glDisable (GL_DEPTH_TEST);
        glDisable (GL_CULL_FACE);
        glDisable (GL_SCISSOR_TEST);
        glDisable (GL_BLEND);
        glBindVertexArray (0);
        glUseProgram (0);
        deliver (rgba, w, h);
    }, false);
}

/** One frame, kept: the product's own picture where it brings one — else rendered, from the GPU where
    it works (the GL thread reads it back and the pulse collects it), else from the 1993 landscape at once. */
void Windscreen::takeStill()
{
    if (picture != nullptr && ! stillShipped)
        if (auto shipped = picture(); shipped.isValid())
        {
            still = shipped;
            stillShipped = true;
        }
    if (stillShipped)
    {
        detachGl();
        repaint();
        return;
    }
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

/** A RUNG DOWN: the scale only — the GPU's scene, or the 1993 landscape's rows. The landscape the player
    chose stays; at the foot the ladder only waits. */
void Windscreen::stepDown (const juce::String& why)
{
    const float sc = scaleInUse.load();
    if (sc > 0.76f)                      { scaleInUse.store (0.75f);       note = "scale 0.75: " + why; }
    else if (sc > lowestScale + 0.01f)   { scaleInUse.store (lowestScale); note = "scale 0.5: " + why; }
    else                                 return;
    forgetCosts();
}

/** A RUNG UP, after a calm half minute: back toward the scale the player asked for, never past it. */
void Windscreen::stepUp()
{
    const float sc = scaleInUse.load(), up = rungAbove (sc);
    if (up <= sc + 0.01f)
        return;
    scaleInUse.store (up);
    note = "scale " + juce::String (up, 2) + ": calm again";
    forgetCosts();
}

/** The rung above a scale: 0.5, 0.75, 1 — never past what the player asked for. */
float Windscreen::rungAbove (float sc) const noexcept
{
    return juce::jmin (scaleWanted, sc < 0.74f ? 0.75f : 1.0f);
}

/** What a frame cost at the old scale says nothing of the new one. */
void Windscreen::forgetCosts()
{
    gpuMsShared.store (-1.0f);
    gpuMsRead = gpuMsAt.load();
    cpuMs = -1.0f;
    slowSeconds = calmSeconds = 0;
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
            float ms = -1.0f;
            if (running == Mode::gpu)
            {
                // The second's frames, as the GL thread wrote them down; a ring overrun keeps the newest.
                const unsigned at = gpuMsAt.load();
                const unsigned from = at - gpuMsRead > (unsigned) gpuMsRing.size() ? at - (unsigned) gpuMsRing.size() : gpuMsRead;
                secondsMs.clear();
                for (unsigned i = from; i != at; ++i)
                    secondsMs.push_back (gpuMsRing[i % gpuMsRing.size()].load());
                gpuMsRead = at;
                ms = medianOf (secondsMs);
            }
            else if (running == Mode::cpu)
            {
                ms = cpuMs;
            }

            if (ms > tooSlowMs)
            {
                calmSeconds = 0;
                if (++slowSeconds >= slowSecondsToStep)
                    stepDown (juce::String (ms, 1) + " ms a frame for " + juce::String (slowSecondsToStep) + " s");
            }
            else if (ms >= 0.0f)
            {
                slowSeconds = 0;

                // Calm is judged at the rung above, not at this one: the GPU's cost goes with the pixels,
                // the 1993 landscape's with its rows — a climb back to a rung that was too slow would only
                // fall again five seconds later, and again half a minute after that.
                const float sc = scaleInUse.load(), up = rungAbove (sc);
                const float ratio = up / juce::jmax (0.01f, sc);
                const float costAbove = ms * (running == Mode::gpu ? ratio * ratio : ratio);
                if (up > sc + 0.01f && costAbove < climbMs)
                {
                    if (++calmSeconds >= calmSecondsToClimb)
                        stepUp();
                }
                else
                {
                    calmSeconds = 0;
                }
            }
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
        mars.makeMaps (clockSeed(), world->look1993());
        marsMade = true;
        cpuLast = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
    const int rows = juce::jmax (50, juce::roundToInt ((float) getHeight() * scaleInUse.load())) & ~1;
    const double glass = getHeight() > 0 ? (double) getWidth() / getHeight() : 0.0;     // the frame is stretched onto it
    if (mars.height() != rows || mars.shownAspect() != glass)
        mars.setRows (rows, glass);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = advance ? juce::jlimit (0.0, 0.1, now - cpuLast) : 0.0;
    cpuLast = now;
    const int g = gearWanted.load();
    const double want = marsTexels (g) * (turboWanted.load() ? PlainsWorld::turboFactor : 1.0f);
    marsSpeed += (want - marsSpeed) * (1 - std::exp (-dt / speedSeconds1993));
    marsPos += marsSpeed * 16 * dt;
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
    // A world that will not start on this GPU goes to the CPU on its own (apply, tick) — and says why in the
    // family's log, which is all a player who sees the 1993 landscape has to send. A GL that runs no shaders
    // is not asked to start it: the world's first call would have no entry point.
    const bool runs = gpu::runsShaders();
    if (! runs || ! world->create())
    {
        gpuFailed.store (true);
        const auto why = runs ? world->error() : std::string ("this GL runs no shaders");
        gpulog::refusal (logName, gpu::driver(),
                         std::string (world->name()) + ": " + (why.empty() ? std::string ("its GPU view would not start") : why));
    }
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
    world->setGear (gearWanted.load());
    world->setTurbo (turboWanted.load());
    world->setSteer (steering.value());
    world->setWipers (wipersWanted.load());
    world->setLights ((lightsWanted.load() & 1) != 0, (lightsWanted.load() & 2) != 0);
    if (hearing)
    {
        const auto heard = hearing();
        world->hear (heard.peak, heard.pick);
    }
    const bool wantStill = stillWanted.load();
    if (wantStill)
        world->settle (gearWanted.load(), turboWanted.load());   // where it stands, as the drive would be once settled
    else
        world->step (dt);
    speedShared.store (world->speed());

    world->render (w, h, scaleInUse.load(), target, x, y);
    const float ms = world->gpuMillis();
    gpuMsShared.store (ms);
    if (ms >= 0.0f)
        gpuMsRing[gpuMsAt.fetch_add (1) % gpuMsRing.size()].store (ms);
    gpuFrameW.store (world->frameWidth()); gpuFrameH.store (world->frameHeight());
    ++gpuFrames;

    if (wantStill && stillWanted.exchange (false))
    {
        const juce::ScopedLock sl (stillLock);
        if (world->readFrame (stillPixels, stillW, stillH))
            stillReady.store (true);
    }
}

void Windscreen::openGLContextClosing()
{
    world->destroy();
}

} // namespace orbitamp::redpaw
