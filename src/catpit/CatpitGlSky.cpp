// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitGlSky.h"

#include "../face/WebImages.h"
#include "../shaders/Gpu.h"
#include "../shaders/GpuLog.h"

namespace orbitamp::catpit
{

using namespace juce::gl;

void GlSky::setArea (juce::Rectangle<float> g, juce::Rectangle<float> w, juce::Point<float> editorSize) noexcept
{
    areaX.store (g.getX());  areaY.store (g.getY());
    areaW.store (g.getWidth()); areaH.store (g.getHeight());
    viewX.store (w.getX());  viewY.store (w.getY());
    viewW.store (w.getWidth()); viewH.store (w.getHeight());
    fullW.store (editorSize.x); fullH.store (editorSize.y);
}

void GlSky::newOpenGLContextCreated()
{
    // The shader and what it draws are SkyGl's (CatpitSkyGl.cpp), the site's too. One that will not build
    // here sends the sky to the CPU (CatpitEditor::applySky) — and says why in the family's log. So does a GL
    // that runs no shaders at all, before a call it has no entry point for.
    if (! gpu::runsShaders())
    {
        gpulog::refusal (logName, gpu::driver(), "the sky: this GL runs no shaders");
        broken.store (true);
        return;
    }
    if (! sky.create())
    {
        DBG ("catpit: the sky's shader did not build — " << juce::String (sky.error()));
        gpulog::refusal (logName, gpu::driver(), "the sky: its shader did not build — " + sky.error());
        broken.store (true);
        return;
    }
    last = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void GlSky::renderOpenGL()
{
    const float k = (float) context.getRenderingScale();
    const int fw = juce::roundToInt (fullW.load() * k), fh = juce::roundToInt (fullH.load() * k);

    juce::OpenGLHelpers::clear (juce::Colours::black);
    if (! sky.ready() || fw <= 0 || fh <= 0)
        return;

    // The flight, stepped by the time since the last frame — at most a twentieth of a second, so a stall
    // does not throw the ship forward.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) juce::jlimit (0.0, 0.05, now - last);
    last = now;
    if (hearing && lit && lit())
    {
        const auto heard = hearing();
        flight.hear (heard.peak, heard.pick);
    }
    flight.step (throttle.load(), steer.load(), dt);

    // Only the glass's rectangle is shaded. GL counts rows from the bottom.
    const int x = juce::roundToInt (areaX.load() * k), w = juce::roundToInt (areaW.load() * k);
    const int h = juce::roundToInt (areaH.load() * k);
    const int y = fh - juce::roundToInt ((areaY.load() + areaH.load()) * k);

    // ...and the picture in it is the windshield's: the shader measures from that rectangle's corner and
    // by its size, so the glass above it is more of the same sky, not the same sky stretched.
    const int vx = juce::roundToInt (viewX.load() * k), vw = juce::roundToInt (viewW.load() * k);
    const int vh = juce::roundToInt (viewH.load() * k);
    const int vy = fh - juce::roundToInt ((viewY.load() + viewH.load()) * k);

    sky.render (flight, x, y, w, h, (float) vx, (float) vy, (float) vw, (float) vh);

    glViewport (0, 0, fw, fh);   // the components are painted over the whole surface next
}

void GlSky::firstFrame (float throttleAt, juce::Rectangle<float> glass, juce::Rectangle<float> windshield, float scale,
                        std::function<void (juce::Image)> done)
{
    const auto deliver = [done] (std::shared_ptr<std::vector<std::uint8_t>> rgba, int w, int h)
    {
        juce::MessageManager::callAsync ([done, rgba, w, h] { done (rgba != nullptr ? face::web::fromGlRows (*rgba, w, h) : juce::Image()); });
    };
    const int w = juce::roundToInt (glass.getWidth() * scale), h = juce::roundToInt (glass.getHeight() * scale);
    if (broken.load() || ! context.isAttached() || w <= 0 || h <= 0)
    {
        deliver (nullptr, 0, 0);
        return;
    }
    // The windshield in the frame's own pixels, rows from the bottom, as renderOpenGL measures it inside the
    // glass (and the site's sky, tools/worlds-wasm/sky.cpp).
    const float vx = (windshield.getX() - glass.getX()) * scale, vw = windshield.getWidth() * scale;
    const float vh = windshield.getHeight() * scale;
    const float vy = (float) h - ((windshield.getY() - glass.getY()) + windshield.getHeight()) * scale;
    context.executeOnGLThread ([this, throttleAt, w, h, vx, vy, vw, vh, deliver] (juce::OpenGLContext& c)
    {
        std::shared_ptr<std::vector<std::uint8_t>> rgba;
        int fw = 0, fh = 0;
        juce::OpenGLFrameBuffer frame;
        if (sky.ready() && frame.initialise (c, w, h) && frame.makeCurrentRenderingTarget())
        {
            Flight start {};            // the flight from its start...
            start.settle (throttleAt);  // ...at the channel's speed at once
            sky.render (start, 0, 0, w, h, vx, vy, vw, vh);
            rgba = std::make_shared<std::vector<std::uint8_t>>();
            if (! sky.readFrame (*rgba, fw, fh))
                rgba.reset();
            frame.releaseAsRenderingTarget();
        }
        frame.release();
        deliver (rgba, fw, fh);
    }, false);
}

void GlSky::openGLContextClosing()
{
    sky.destroy();
}

} // namespace orbitamp::catpit
