// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "WalkerWorld.h"
#include "../../shaders/Gpu.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::redgroove
{

/** THE GLOWING FOREST ON THE GPU, as the view's prototype draws it (walker-gl.js), from the same shaders
    (resources/shaders/walker, walker-shaders.js's bodies as they are). A frame:
     1. THE LIGHT MAP — every cap's glow splatted top-down into a 512 x 512 RGBA16F map, 240 m across,
        centred on the eye, added up: the pools of light on the ground.
     2. THE SCENE — the ground (a polar lattice around the eye out to 420 m, its height from the
        heightfield, the moss's detail, the pools, the sun low on the horizon, the fog), the mushrooms
        (nine instanced draws, three shapes in three classes, from the world's instance lists, which follow
        the walker), then the sky over what is left: violet above, rust at the horizon, the dim red sun on
        it, faint stars.
     3. THE GLOW — a soft halo over each cap, added over everything.
    The legs, the drifting spores and the spore bursts are the prototype's too and come next.

    The camera's field follows the frame's aspect (fieldFor), the horizon sits where it is asked to
    (setHorizon), and the gait's heave, pitch, nod and roll are the camera's. Draws into an offscreen frame
    of its own at a render scale, then blits it to the frame buffer it is given. GLSL 1.50 core in the app,
    GLSL ES 3.00 on the web (src/shaders/Gpu.h). GL thread only. */
class WalkerGl final
{
public:
    explicit WalkerGl (WalkerWorld& w) : world (w) {}

    /** The half-tangents of the field of view (horizontal, vertical) for a frame's aspect: wide frames keep
        the horizontal field (about 100 degrees across), taller ones grow the vertical up to a limit, then
        narrow the horizontal. */
    static std::array<double, 2> fieldFor (double aspect) noexcept;

    /** Where the horizon sits, a fraction of the frame from its top: 0.5 the middle. The cabin's 0.40 puts it
        57 % down the dome's window over the whole stage. */
    void setHorizon (double fromTop) noexcept { horizon = std::clamp (fromTop, 0.05, 0.95); }

    /** Builds the programs, buffers and textures. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), blitted to `targetFrameBuffer` over
        (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. */
    float gpuMillis() const noexcept { return gpuMs; }
    /** The offscreen frame as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }
    /** The scene drawn this many times a frame, for measuring. */
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }
    /** Why create() said no. */
    const std::string& error() const noexcept { return trouble; }

private:
    WalkerWorld& world;
    double horizon = 0.5;

    struct Program
    {
        std::uint32_t id = 0;
        int uniform (const char* name) const;
    };
    Program ground, mush, splat, halo, sky;

    /** A class's instance list of one shape: its buffer, a vertex array with the mesh under it, one with
        the instances alone (the splats and the halos), and where the shape's triangles are. */
    struct List
    {
        std::uint32_t inst = 0, vaoMesh = 0, vaoQuad = 0;
        int count = 0, first = 0, indices = 0;
    };
    std::array<List, WalkerWorld::numClasses * WalkerWorld::variants> lists {};
    struct Base { bool set = false; int bx = 0, bz = 0; };
    std::array<Base, WalkerWorld::numClasses> windowBase {};

    std::uint32_t texHeight = 0, texDetail = 0, texLight = 0, fboLight = 0;
    std::uint32_t vboGround = 0, iboGround = 0, vaoGround = 0, vboMesh = 0, iboMesh = 0, vaoEmpty = 0, vboEmpty = 0;
    int groundIndices = 0;
    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;
    int fbW = 0, fbH = 0;
    int repeats = 1;

    // The frame timer, as the other worlds have it.
    bool queriesWork = false;
    std::array<std::uint32_t, 4> query {};
    int queryNext = 0, queriesPending = 0;
    float gpuMs = -1.0f;
    int frameCounter = 0;
    double probeStart = 0.0;
    std::string trouble;

    bool compile (Program&, const char* vertexPath, const char* fragmentPath, const char* what);
    bool makeTextures();
    void makeBuffers();
    bool ensureFrame (int w, int h);
    void updateWindows();
    void drawScene (int w, int h);
    void noteCost (float ms);
};

} // namespace orbitamp::redgroove
