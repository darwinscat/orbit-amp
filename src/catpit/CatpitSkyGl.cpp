// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSkyGl.h"

#include "../shaders/Gpu.h"
#include "../shaders/Shaders.h"

namespace orbitamp::catpit
{

using namespace orbitamp::gpu;

bool SkyGl::create()
{
    // THE SKY'S SHADERS are resources/shaders/catpit/sky.vert and sky.frag (src/shaders/Shaders.h), with no
    // #version: what this context reads goes in front of them (gpu::legacyHeader). The fragment shader is
    // written for this sky, from scratch: value noise, a domain-warped cloud swelling toward the eye in two
    // staggered layers, and four depth layers of stars on a grid that zooms out from the centre — which is
    // what flying forward looks like — each star stretched along its own ray by the speed.
    //
    // THE HELM (CatpitHelm.h) moves that centre — the point the stars stream out of — toward the turn
    // (uFocus), turns the whole sky about it against the bank (uRoll), and slides each layer and each
    // cloud sideways by as far as the turns have swept it since it came up out of the distance (uDrift,
    // uCloudDrift); a star's streak then runs back toward where the sweep brought it from as well
    // (uSlide). Flying straight, all of it is nothing and the sky is the one it always was. THE GUITAR
    // brightens the stars (uShine) and the nebulae (uCloudShine): in silence nothing, as well.
    const auto head = legacyHeader();
    program = gpu::program (shaders::vertexToV3 (shaders::source ("catpit/sky.vert"), head),
                            shaders::fragmentToV3 (shaders::source ("catpit/sky.frag"), head), trouble);
    if (program == 0)
        return false;

    const auto uniform = [this] (const char* name) { return (int) glGetUniformLocation (program, name); };
    at = { uniform ("uOrigin"), uniform ("uSize"), uniform ("uTravel"), uniform ("uSpeed"), uniform ("uWarp"),
           uniform ("uFocus"), uniform ("uRoll"), uniform ("uSlide"), uniform ("uDrift"), uniform ("uCloudDrift"),
           uniform ("uShine"), uniform ("uCloudShine") };
    position = (int) glGetAttribLocation (program, "position");

    const GLfloat corners[] { -1.0f, -1.0f,  1.0f, -1.0f,  -1.0f, 1.0f,  1.0f, 1.0f };
    GLuint buffer = 0;
    glGenBuffers (1, &buffer);
    glBindBuffer (GL_ARRAY_BUFFER, buffer);
    glBufferData (GL_ARRAY_BUFFER, sizeof (corners), corners, GL_STATIC_DRAW);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    quad = buffer;
    return true;
}

void SkyGl::destroy()
{
    if (quad != 0)
    {
        const GLuint buffer = quad;
        glDeleteBuffers (1, &buffer);
    }
    if (program != 0)
        glDeleteProgram (program);
    quad = program = 0;
}

void SkyGl::render (const Flight& flight, int x, int y, int w, int h, float vx, float vy, float vw, float vh)
{
    if (program == 0)
        return;
    areaX = x; areaY = y; areaW = w; areaH = h;

    glViewport (x, y, w, h);
    glUseProgram (program);
    glUniform2f (at.origin, vx, vy);
    glUniform2f (at.size, vw, vh);
    glUniform1f (at.travel, (GLfloat) flight.travelled());
    glUniform1f (at.speed, flight.speed);
    glUniform1f (at.warp, flight.warp());
    glUniform1f (at.focus, flight.helm.focus());
    glUniform1f (at.roll, flight.helm.roll());
    glUniform1f (at.slide, flight.slide);
    glUniform4f (at.drift, flight.drift[0], flight.drift[1], flight.drift[2], flight.drift[3]);
    glUniform2f (at.cloudDrift, flight.cloudDrift[0], flight.cloudDrift[1]);
    glUniform1f (at.shine, flight.starShine());
    glUniform1f (at.cloudShine, flight.cloudShine());

    glBindBuffer (GL_ARRAY_BUFFER, quad);
    const auto pos = (GLuint) position;
    glEnableVertexAttribArray (pos);
    glVertexAttribPointer (pos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray (pos);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

bool SkyGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (program == 0 || areaW <= 0 || areaH <= 0)
        return false;
    w = areaW;
    h = areaH;
    rgba.assign ((size_t) w * (size_t) h * 4, 0);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (areaX, areaY, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return true;
}

} // namespace orbitamp::catpit
