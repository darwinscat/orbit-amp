// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitGlSky.h"

#include <array>
#include <cmath>

namespace orbitamp::catpit
{

using namespace juce::gl;

namespace
{
    // Ship speed at each detent — the same table as the CPU sky, so the two fly alike.
    constexpr std::array<float, 4> detentSpeed { 0.05f, 0.16f, 0.42f, 1.6f };

    const char* const vertexShader = R"GLSL(
attribute vec2 position;
void main()
{
    gl_Position = vec4 (position, 0.0, 1.0);
}
)GLSL";

    // Written for this sky, from scratch: value noise, a domain-warped cloud swelling toward the eye in
    // two staggered layers, and four depth layers of stars on a grid that zooms out from the centre —
    // which is what flying forward looks like — each star stretched along its own ray by the speed.
    const char* const fragmentShader = R"GLSL(
uniform vec2  uOrigin;
uniform vec2  uSize;
uniform float uTravel;
uniform float uSpeed;
uniform float uWarp;

float hash (vec2 p)
{
    p = fract (p * vec2 (123.34, 456.21));
    p += dot (p, p + 45.32);
    return fract (p.x * p.y);
}

float noise (vec2 p)
{
    vec2 i = floor (p), f = fract (p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash (i), hash (i + vec2 (1.0, 0.0)), u.x),
                mix (hash (i + vec2 (0.0, 1.0)), hash (i + vec2 (1.0, 1.0)), u.x), u.y);
}

float fbm (vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i)
    {
        s += a * noise (p);
        p = p * 2.03 + vec2 (1.7, 9.2);
        a *= 0.5;
    }
    return s;
}

void main()
{
    vec2 uv = (gl_FragCoord.xy - uOrigin - 0.5 * uSize) / uSize.y;
    float r = length (uv);
    vec3 col = mix (vec3 (0.028, 0.024, 0.086), vec3 (0.004, 0.004, 0.016), clamp (r * 0.8, 0.0, 1.0));

    // The clouds: each swells from far to past the glass while it brightens and fades, two half a
    // cycle apart, so one is always arriving as the other leaves.
    for (int i = 0; i < 2; ++i)
    {
        float ph = fract (uTravel * 0.12 + float (i) * 0.5);
        float zoom = mix (2.6, 0.35, ph * ph);
        vec2 p = uv * zoom + vec2 (float (i) * 7.3 + 3.0, float (i) * 3.1 + 1.0);
        vec2 q = vec2 (fbm (p * 1.1), fbm (p * 1.1 + vec2 (5.2, 1.3)));
        float d = fbm (p * 1.4 + 3.2 * q);
        vec3 c = mix (vec3 (0.16, 0.07, 0.38), vec3 (0.42, 0.17, 0.72), smoothstep (0.45, 0.72, d));
        c = mix (c, vec3 (0.69, 0.29, 0.66), smoothstep (0.68, 0.9, d));
        c += vec3 (1.0, 0.54, 0.24) * smoothstep (0.80, 0.95, fbm (p * 2.6 + 9.0)) * 0.45;
        col += c * smoothstep (0.42, 0.80, d) * sin (ph * 3.14159) * 0.8;
    }

    // At speed the light ahead gathers into a glow.
    col += vec3 (0.81, 0.77, 1.0) * uWarp * 0.32 * exp (-r * r * 5.0);

    // The stars.
    vec2 dir = uv / max (r, 1e-4);
    vec2 side = vec2 (-dir.y, dir.x);
    for (int l = 0; l < 4; ++l)
    {
        float z = fract (float (l) * 0.25 + uTravel * 0.45);
        float fade = smoothstep (0.0, 0.3, z) * smoothstep (1.0, 0.86, z);
        float scale = mix (16.0, 1.2, z);
        vec2 p = uv * scale + vec2 (float (l) * 17.1, float (l) * 31.7);
        vec2 cell = floor (p);
        float h = hash (cell);
        if (h < 0.62) continue;
        vec2 at = fract (p) - 0.5 - (vec2 (hash (cell + 3.1), hash (cell + 7.7)) - 0.5) * 0.7;
        float along = dot (at, dir), across = dot (at, side);
        float len = 0.004 + uSpeed * 0.18 * z * z;
        float d = length (vec2 (max (abs (along + len) - len, 0.0), across));
        float size = 0.010 + 0.035 * z * z;
        float star = smoothstep (size, 0.0, d) * fade * (0.55 + 0.45 * fract (h * 13.0));
        vec3 tint = fract (h * 7.0) < 0.12 ? vec3 (0.72, 0.61, 1.0)
                  : fract (h * 7.0) < 0.18 ? vec3 (1.0, 0.78, 0.6) : vec3 (0.91, 0.93, 1.0);
        col += tint * star * 1.4;
    }

    gl_FragColor = vec4 (col, 1.0);
}
)GLSL";
}

void GlSky::setArea (juce::Rectangle<float> w, juce::Point<float> editorSize) noexcept
{
    areaX.store (w.getX());  areaY.store (w.getY());
    areaW.store (w.getWidth()); areaH.store (w.getHeight());
    fullW.store (editorSize.x); fullH.store (editorSize.y);
}

void GlSky::newOpenGLContextCreated()
{
    auto s = std::make_unique<juce::OpenGLShaderProgram> (context);
    if (! s->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vertexShader))
        || ! s->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fragmentShader))
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
    if (shader == nullptr || fw <= 0 || fh <= 0 || ! flying.load())
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

    // Only the windshield's rectangle is shaded. GL counts rows from the bottom.
    const int x = juce::roundToInt (areaX.load() * k), w = juce::roundToInt (areaW.load() * k);
    const int h = juce::roundToInt (areaH.load() * k);
    const int y = fh - juce::roundToInt ((areaY.load() + areaH.load()) * k);

    glViewport (x, y, w, h);
    shader->use();
    shader->setUniform ("uOrigin", (GLfloat) x, (GLfloat) y);
    shader->setUniform ("uSize", (GLfloat) w, (GLfloat) h);
    shader->setUniform ("uTravel", (GLfloat) std::fmod (travel, 1000.0));
    shader->setUniform ("uSpeed", (GLfloat) speed);
    shader->setUniform ("uWarp", (GLfloat) juce::jlimit (0.0f, 1.0f, (speed - 0.5f) / 1.1f));

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
