// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitGlSky.h"

#include "../shaders/Shaders.h"

#include <array>
#include <cmath>

namespace orbitamp::catpit
{

using namespace juce::gl;

namespace
{
    // Ship speed at each detent. CLEAN and EDGE fly as the CPU sky does; from CRUNCH on this one goes
    // faster — CRUNCH a little past where HI-GAIN used to fly, HI-GAIN nearly twice that — and since a
    // star's streak is as long as the ship is fast, the two top detents smear the stars further too.
    constexpr std::array<float, 4> detentSpeed { 0.05f, 0.16f, 1.8f, 3.0f };

    // A turn's sweep, in the stars' own cells a second at full lock: a slide at the cruise, four times
    // that at the top. A layer's cells look smaller the farther off it is, so the same sweep carries
    // the near stars across the glass much faster than the far ones. The clouds are swept the same
    // way, in their own measure, by this share of it.
    constexpr float slideCruise = 3.0f, slidePerSpeed = 3.0f;
    constexpr float cloudSlide = 0.08f;
}

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
    // THE SKY'S SHADERS are resources/shaders/catpit/sky.vert and sky.frag (src/shaders/Shaders.h), with no
    // #version: JUCE puts in front of them the one this context speaks. The fragment shader is written for
    // this sky, from scratch: value noise, a domain-warped cloud swelling toward the eye in two staggered
    // layers, and four depth layers of stars on a grid that zooms out from the centre — which is what
    // flying forward looks like — each star stretched along its own ray by the speed.
    //
    // THE HELM (CatpitHelm.h) moves that centre — the point the stars stream out of — toward the turn
    // (uFocus), turns the whole sky about it against the bank (uRoll), and slides each layer and each
    // cloud sideways by as far as the turns have swept it since it came up out of the distance (uDrift,
    // uCloudDrift); a star's streak then runs back toward where the sweep brought it from as well
    // (uSlide). Flying straight, all of it is nothing and the sky is the one it always was.
    const auto vertexShader = shaders::source ("catpit/sky.vert"), fragmentShader = shaders::source ("catpit/sky.frag");
    auto s = std::make_unique<juce::OpenGLShaderProgram> (context);
    if (! s->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader.c_str()))
        || ! s->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragmentShader.c_str()))
        || ! s->link())
    {
        DBG ("catpit: the sky's shader did not build — " << s->getLastError());
        broken.store (true);
        return;
    }
    shader = std::move (s);

    const GLfloat corners[] { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    glGenBuffers (1, &quad);
    glBindBuffer (GL_ARRAY_BUFFER, quad);
    glBufferData (GL_ARRAY_BUFFER, sizeof (corners), corners, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    start = last = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void GlSky::renderOpenGL()
{
    const float k = (float) context.getRenderingScale();
    const int fw = juce::roundToInt (fullW.load() * k), fh = juce::roundToInt (fullH.load() * k);

    juce::OpenGLHelpers::clear (juce::Colours::black);
    if (shader == nullptr || fw <= 0 || fh <= 0)
        return;

    // The ship: the speed the lever asks for, reached the way a ship reaches it — slower up than down.
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float dt = (float) juce::jlimit (0.0, 0.05, now - last);
    last = now;
    const float at = throttle.load() * 3.0f;
    const int i = juce::jlimit (0, 2, (int) at);
    const float want = detentSpeed[(size_t) i] + (detentSpeed[(size_t) i + 1] - detentSpeed[(size_t) i]) * (at - (float) i);
    speed += (want - speed) * (1.0f - std::exp (-dt / (want > speed ? 0.9f : 0.5f)));
    travel += (double) (speed * dt);

    // ...and the helm: the turn sweeps each layer and each cloud sideways, and what a layer or a cloud
    // was swept is forgotten when it comes up again out of the distance, where nothing of it shows —
    // so no sweep ever piles up. The depths are the shader's own.
    helm.step (steer.load(), dt);
    const float slide = helm.turn * (slideCruise + slidePerSpeed * speed);
    const double flown = std::fmod (travel, 1000.0);
    const auto fraction = [] (double v) { return (float) (v - std::floor (v)); };
    for (size_t l = 0; l < drift.size(); ++l)
    {
        const float z = fraction ((double) l * 0.25 + flown * 0.45);
        if (z < depth[l])
            drift[l] = 0.0f;
        depth[l] = z;
        drift[l] += slide * dt;
    }
    for (size_t c = 0; c < cloudDrift.size(); ++c)
    {
        const float phase = fraction (flown * 0.12 + (double) c * 0.5);
        if (phase < cloudPhase[c])
            cloudDrift[c] = 0.0f;
        cloudPhase[c] = phase;
        cloudDrift[c] += slide * cloudSlide * dt;
    }

    // Only the glass's rectangle is shaded. GL counts rows from the bottom.
    const int x = juce::roundToInt (areaX.load() * k), w = juce::roundToInt (areaW.load() * k);
    const int h = juce::roundToInt (areaH.load() * k);
    const int y = fh - juce::roundToInt ((areaY.load() + areaH.load()) * k);

    // ...and the picture in it is the windshield's: the shader measures from that rectangle's corner and
    // by its size, so the glass above it is more of the same sky, not the same sky stretched.
    const int vx = juce::roundToInt (viewX.load() * k), vw = juce::roundToInt (viewW.load() * k);
    const int vh = juce::roundToInt (viewH.load() * k);
    const int vy = fh - juce::roundToInt ((viewY.load() + viewH.load()) * k);

    glViewport (x, y, w, h);
    shader->use();
    shader->setUniform ("uOrigin", (GLfloat) vx, (GLfloat) vy);
    shader->setUniform ("uSize", (GLfloat) vw, (GLfloat) vh);
    shader->setUniform ("uTravel", (GLfloat) flown);
    shader->setUniform ("uSpeed", (GLfloat) speed);
    shader->setUniform ("uWarp", (GLfloat) juce::jlimit (0.0f, 1.0f, (speed - 0.5f) / 1.1f));
    shader->setUniform ("uFocus", (GLfloat) helm.focus());
    shader->setUniform ("uRoll", (GLfloat) helm.roll());
    shader->setUniform ("uSlide", (GLfloat) slide);
    shader->setUniform ("uDrift", drift[0], drift[1], drift[2], drift[3]);
    shader->setUniform ("uCloudDrift", cloudDrift[0], cloudDrift[1]);

    glBindBuffer (GL_ARRAY_BUFFER, quad);
    const auto pos = (GLuint) glGetAttribLocation (shader->getProgramID(), "position");
    glEnableVertexAttribArray (pos);
    glVertexAttribPointer (pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray (pos);
    glBindBuffer (GL_ARRAY_BUFFER, 0);

    glViewport (0, 0, fw, fh);   // the components are painted over the whole surface next
}

void GlSky::openGLContextClosing()
{
    if (quad != 0)
        glDeleteBuffers (1, &quad);
    quad = 0;
    shader.reset();
}

} // namespace orbitamp::catpit
