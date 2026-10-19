// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IceWorld.h"

#include "../../shaders/Gpu.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::frostpaw
{

/** THE ICE ON THE GPU. The Plains' engine on a new world: a geometry clipmap of eight concentric
    128 x 128 vertex grids (cells 1 m .. 128 m, 8 km out), each snapped to a world lattice of twice its
    cell so vertices never slide, the outer sixteen cells of each morphed to the coarser lattice so the
    rings meet without cracks. The vertex shader displaces by the height function — the plasma's four
    layers, the lineae, the chaos blocks, the drifts at the steel's feet — and lets the ground fall away
    with the moon's curvature, which puts the horizon 2.5 km out. Per pixel, the normal comes from the
    analytic gradient of all of it (two gradient textures and the chain rule), lit by a low white sun
    with a blue skylight and Jupiter's warm fill: snow, blue ice on the steep faces and the block rims,
    the reddish salt in the lineae, a sparkle on the near crust, a cold haze to the horizon.

    The STEEL is four bar meshes with flat normals — a lattice mast, a gantry, a dish on a tripod, a
    frame module — drawn instanced, one draw per variant, from the world's list of the structures in
    the 48-cell window (the cells enumerated on the CPU, so the GPU sees only real instances); the
    vertex shader widens thin bars to a pixel with distance and drops the braces beyond 900 m, the
    fragment shader reflects a procedural environment off brushed stainless and frosts the upper faces.
    The SKY is one full-screen triangle at depth 1: hashed steady stars, the sun, Jupiter as a shaded
    sphere in a fixed world direction — banded, with the Spot, lit by the same sun, turning slowly — and
    Io circling it. The SNOW is attribute-less streak quads in camera space flowing out of the vanishing
    point, so turning never drags the flakes sideways; its count follows the world's snow.

    Precision: every shader position is relative to the integer metre cell under the rover, and the
    texture and lineae offsets are computed in doubles, so the picture is as steady at 665 km as at the
    start. Draws into an offscreen frame of its own at a render scale, then blits it to the frame buffer
    it is given. GLSL 1.50 core, as JUCE's 3.2 core context on every platform — or, the same code built
    for the site, GLSL ES 3.00 over WebGL 2 (src/shaders/Gpu.h). GL thread only. */
class IceGl final
{
public:
    explicit IceGl (IceWorld& w) : world (w) {}

    /** Builds the programs, buffers and textures. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), blitted to `targetFrameBuffer`
        over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. Timer
        queries where the driver has them, else a finished frame timed once a second; on the web, -1. */
    float gpuMillis() const noexcept { return gpuMs; }

    /** The offscreen frame as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);

    /** The scene's last frame size. */
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }

    /** The scene drawn this many times a frame, for measuring: the timer then reads that many scenes. */
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }

    /** Why create() said no: the shader that would not build and the driver's words. */
    const std::string& error() const noexcept { return trouble; }

private:
    IceWorld& world;

    struct Program
    {
        std::uint32_t id = 0;
        int uniform (const char* name) const;
    };
    Program terrain, steel, sky, snow;

    std::uint32_t texHeight = 0, texGrad1 = 0, texGrad2 = 0, texDetail = 0;
    std::uint32_t vaoTerrain = 0, vboGrid = 0, vaoSteel = 0, vboSteel = 0, vaoEmpty = 0, vboEmpty = 0;

    static constexpr int N = 128, levels = 8, morphBand = 16;
    struct Level
    {
        std::uint32_t indexBuffer = 0;
        int count = 0, holeX = -1, holeY = -1;
        double cell = 1.0, ox = 0.0, oz = 0.0;
    };
    std::array<Level, levels> level;
    std::vector<std::uint16_t> fullIndices, ringIndices;

    // The steel: the four meshes in one buffer, and per variant the instance buffer of cells.
    struct Range { int first = 0, count = 0; };
    std::array<Range, IceWorld::numVariants> steelRange;
    std::array<std::uint32_t, IceWorld::numVariants> instanceBuffer {};
    std::array<int, IceWorld::numVariants> instanceCount {};
    std::array<std::vector<float>, IceWorld::numVariants> instanceCells;
    int instanceBx = 0x7fffffff, instanceBz = 0x7fffffff;    // the window the lists were made for
    int locCell = -1;
    bool instancing = false;                                  // glVertexAttribDivisor is there

    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;
    int fbW = 0, fbH = 0;
    int repeats = 1;

    // The frame timer.
    bool queriesWork = false;
    std::array<std::uint32_t, 4> query {};
    int queryNext = 0, queriesPending = 0;
    float gpuMs = -1.0f;
    int frameCounter = 0;
    double probeStart = 0.0;
    std::string trouble;

    bool compile (Program&, const std::string& vs, const std::string& fs, const char* what);
    void makeTextures();
    void makeGeometry();
    void ensureFrame (int w, int h);
    void rebuildRing (Level&, int hx, int hy);
    void drawScene (int w, int h);
    void noteCost (float ms);
};

} // namespace orbitamp::frostpaw
