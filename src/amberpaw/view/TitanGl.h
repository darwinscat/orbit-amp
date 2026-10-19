// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "TitanWorld.h"

#include "../../shaders/Gpu.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::amberpaw
{

/** THE METHANE SEA ON THE GPU. No per-pixel ray march of the scene; a frame is six passes.
     1. PROFILE — a 1024 x 6 texel pass: for every screen column the far shore is marched along that
        azimuth through a procedural 2D height function (coast mask, low hills, long linear dunes), 16
        steps in each of 5 distance bands (2–40 km); each band keeps its silhouette (the elevation
        tangent, 16 bits in RG), the crest's distance (B) and the dune share (A); row 5 is the max of all.
     2. SCENE — one full-screen triangle. The sky: an amber haze gradient, a broad faint sun patch, high
        cloud streaks; SATURN, banded, its rings almost edge-on, fixed in the world and rising through
        the haze every 20–40 s; a distant STORM lit from inside by lightning, with bolts; a CRYOVOLCANO
        whose vapour column swells into an umbrella in an eruption — analytic shapes, each evaluated only
        in its own small angular box; then the shore bands. The sea: the plane y = 0, three scrolled
        ripple layers from the slope map (filtered by the pixel's footprint and the motion), reflecting
        the same sky mirrored, Fresnel for liquid methane; oily slicks with a thin-film sheen; nitrogen
        "champagne" fields boiling with popping bubbles; a rare "magic island"; the rotor's downwash
        rings, and on a TURBO dash a torn streak of foam ahead. Violet mist banks over it, integrated in
        closed form in three segments, blown thin near the craft; spray on the glass in a fizzing field.
     3. BUOYS — up to eight navigation buoys (red / green gates, amber singles), blinking lamps and their
        glitter columns on the liquid.
     4. SPRAY — 1400 particles the downwash kicks up from the liquid, ballistic in 1/7 g with drag.
     5. DRIZZLE — 2600 pea-sized methane drops drifting down like snow, world-anchored in a wrapping box
        ahead, blown aside by the downwash.
     6. GLASS — over the finished scene, fixed to the cab: the main rotor's disc edge above the canopy
        (one slow glint round its rim, no blades, so nothing strobes) and now and then its shadow; a
        brown tholin film and flakes building up with the time since a wiper last crossed each point;
        the three wipers, each seen only on its own pane; then the trails and the drops, lens sprites
        showing the world upside down in the scene's own colours.

    Every texture offset is fract (position / period) in doubles, so the shaders work in camera-
    relative floats and nothing jitters. Draws into an offscreen frame of its own at a render scale,
    then blits it to the frame buffer it is given. GLSL 1.50 core, as JUCE's 3.2 core context on every
    platform — or, the same code built for the site, GLSL ES 3.00 over WebGL 2 (src/shaders/Gpu.h). GL
    thread only. */
class TitanGl final
{
public:
    explicit TitanGl (TitanWorld& w) : world (w) {}

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
    TitanWorld& world;

    struct Program
    {
        std::uint32_t id = 0;
        int uniform (const char* name) const;
    };
    Program profile, scene, buoys, spray, drizzle, glass, glassDrops, glassTrails;

    std::uint32_t texNoise = 0, texRipple = 0, texProfile = 0, fboProfile = 0;
    std::uint32_t vaoEmpty = 0, vboEmpty = 0, vaoDrops = 0, vboDrops = 0, vaoTrails = 0, vboTrails = 0;
    std::vector<float> dropData, trailData;     // six vertices a drop / a trail, its values on each

    std::uint32_t fbo = 0, fboColour = 0;
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
    void makeBuffers();
    void ensureFrame (int w, int h);
    void drawScene (int w, int h);
    void noteCost (float ms);
};

} // namespace orbitamp::amberpaw
