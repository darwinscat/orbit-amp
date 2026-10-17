// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PlainsWorld.h"

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <memory>
#include <vector>

namespace orbitamp::redpaw
{

/** THE PLAINS ON THE GPU. A geometry clipmap: nine concentric 128 x 128 vertex grids, level k with a
    cell of 2^k metres, each snapped to a world lattice of twice its cell so vertices never slide —
    driving only adds rows at the edges. Levels 1..8 draw a ring, their index buffer leaving out the
    cells the finer level covers. The vertex shader displaces each vertex by the height function and
    morphs the outer sixteen cells to the coarser lattice, so the ring boundaries are crack-free with no
    skirts. Per pixel, the normal comes from the analytic gradient of the height function (two gradient
    textures, five taps), lit by a low sun with haze toward the horizon's colour. Rocks are two
    instanced lumpy icospheres placed by an integer hash of a world cell, so they never move between
    frames. The sky is one full-screen triangle at depth 1, filling what the terrain left. The storm
    is attribute-less streak quads in camera space, flowing toward the eye.

    Precision: every shader position is relative to the integer metre cell under the rover, and the
    texture offsets fract (cell / period) are computed in doubles, so the picture is as steady at
    665 km as at the start.

    Draws into an offscreen frame of its own at a render scale, then blits it to the frame buffer it is
    given. GLSL 1.50 core, as JUCE's 3.2 core context on every platform. GL thread only. */
class PlainsGl final
{
public:
    explicit PlainsGl (PlainsWorld& w) : world (w) {}

    /** Builds the programs, buffers and textures. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), blitted to `targetFrameBuffer`
        over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, juce::uint32 targetFrameBuffer, int x = 0, int y = 0);

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. Timer
        queries where the driver has them, else a finished frame timed once a second. */
    float gpuMillis() const noexcept { return gpuMs; }

    /** The offscreen frame as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<juce::uint8>& rgba, int& w, int& h);

    /** The scene's last frame size. */
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }

    /** The scene drawn this many times a frame, for measuring: the timer then reads that many scenes. */
    void setRepeats (int times) noexcept { repeats = juce::jlimit (1, 16, times); }

private:
    PlainsWorld& world;

    struct Program
    {
        std::unique_ptr<juce::OpenGLShaderProgram> program;
        juce::uint32 id = 0;
        int uniform (const char* name) const;
    };
    Program terrain, rock, relic, sky, dust;

    juce::uint32 texHeight = 0, texGrad1 = 0, texGrad2 = 0, texDetail = 0;
    juce::uint32 vaoTerrain = 0, vboGrid = 0, vaoRock = 0, vboRock = 0, vaoEmpty = 0;

    // The relics: the four meshes in one buffer, and per variant the instance buffer of cells.
    juce::uint32 vaoRelic = 0, vboRelic = 0;
    struct Range { int first = 0, count = 0; };
    std::array<Range, PlainsWorld::numRelics> relicRange;
    std::array<juce::uint32, PlainsWorld::numRelics> relicInstances {};
    std::array<int, PlainsWorld::numRelics> relicCount {};
    std::array<std::vector<float>, PlainsWorld::numRelics> relicCells;
    int relicBx = 0x7fffffff, relicBz = 0x7fffffff;    // the window the lists were made for
    int locRelicCell = -1;
    bool instancing = false;                            // glVertexAttribDivisor is there

    static constexpr int N = 128, levels = 9, morphBand = 16;
    struct Level
    {
        juce::uint32 indexBuffer = 0;
        int count = 0, holeX = -1, holeY = -1;
        double cell = 1.0, ox = 0.0, oz = 0.0;
    };
    std::array<Level, levels> level;
    std::vector<juce::uint16> fullIndices, ringIndices;

    juce::uint32 fbo = 0, fboColour = 0, fboDepth = 0;
    int fbW = 0, fbH = 0;
    int repeats = 1;

    // The frame timer.
    bool queriesWork = false;
    std::array<juce::uint32, 4> query {};
    int queryNext = 0, queriesPending = 0;
    float gpuMs = -1.0f;
    int frameCounter = 0;
    double probeStart = 0.0;

    bool compile (Program&, const char* vs, const char* fs, const char* what);
    void makeTextures();
    void makeGeometry();
    void ensureFrame (int w, int h);
    void rebuildRing (Level&, int hx, int hy);
    void drawScene (int w, int h);
    void noteCost (float ms);
};

} // namespace orbitamp::redpaw
