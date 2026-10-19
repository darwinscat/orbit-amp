// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitGlSky.h"

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
    // The shader and what it draws are SkyGl's (CatpitSkyGl.cpp), the site's too.
    if (! sky.create())
    {
        DBG ("catpit: the sky's shader did not build — " << juce::String (sky.error()));
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

void GlSky::openGLContextClosing()
{
    sky.destroy();
}

} // namespace orbitamp::catpit
