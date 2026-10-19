// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitFlight.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::catpit
{

/** THE SKY'S SHADER AT WORK: one quad over the glass, shaded per pixel from a Flight's numbers by
    resources/shaders/catpit/sky.frag. Written against src/shaders/Gpu.h, so the same code draws the app's
    sky in JUCE's context (GlSky, which steps the flight and says where the glass is) and the site's in
    WebGL 2 (tools/worlds-wasm/sky.cpp). It draws into whatever frame buffer is bound and touches nothing
    else: no clearing, and the viewport left at the glass's.

    The shader is written in GLSL 1.10's words — `attribute`, `gl_FragColor`, no #version — and goes to
    the context in the words it reads (gpu::legacyHeader): as it is where that is 1.10, the legacy profile
    a JUCE context has on a Mac unless it asks for more; under "#version 150" with the newer words put in
    where the context is 3.2 or later, as JUCE's OpenGLHelpers put it; under GLSL ES 3.00's on the web.

    GL thread only. */
class SkyGl final
{
public:
    /** The program and the quad, with a context current. False when the shader would not build here
        (error() says why). */
    bool create();

    /** Everything the GPU holds, with the context current. */
    void destroy();

    /** True once create() has built the shader. */
    bool ready() const noexcept { return program != 0; }

    /** Draws `flight` over (x, y, w, h) — the glass, in GL's own pixels from the frame buffer's
        bottom-left corner — composed for the windshield at (vx, vy, vw, vh) in the same pixels: the
        stars fly out of its centre and it is the sky's measure, so the glass beyond it is more of the
        same sky, not the same sky stretched. */
    void render (const Flight& flight, int x, int y, int w, int h, float vx, float vy, float vw, float vh);

    /** The glass's pixels as the last render left them in the bound frame buffer, RGBA, rows bottom-up as
        GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);

    /** Why create() said no: the driver's words. */
    const std::string& error() const noexcept { return trouble; }

private:
    std::uint32_t program = 0, quad = 0;
    int position = -1;
    struct Uniforms { int origin = -1, size = -1, travel = -1, speed = -1, warp = -1, focus = -1, roll = -1, slide = -1, drift = -1, cloudDrift = -1; };
    Uniforms at;
    int areaX = 0, areaY = 0, areaW = 0, areaH = 0;     // where the last render drew
    std::string trouble;
};

} // namespace orbitamp::catpit
