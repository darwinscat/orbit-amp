// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "UnderwaterWorld.h"
#include "../../shaders/Gpu.h"
#include "../../shaders/Shaders.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace orbitamp::underwater
{

/** THE SEA FLOOR ON THE GPU, as the view's prototype draws it (sea-gl.js), from the same shaders
    (resources/shaders/underwater, sea-shaders.js's bodies as they are). A frame, in the prototype's order:
     1. THE SCENE, into an offscreen colour and depth at the render scale: the floor (a 160 x 160 lattice of
        1.5 m cells snapped to the world, its heights from the fbm texture, ripples, caustics, the boom lamps),
        the rocks and the spires, the tube-worms, the five flora in their meadows; then the water where nothing
        was drawn (depth 1); then, blended over them and writing no depth, the passing bathyscaphe's sprite and
        its lamps' halos and beams, the jellyfish (tendrils, then bells, far to near), and the particles (the
        marine snow, the silt, the bubbles).
     2. THE POST PASS, at the frame's own pixels: the scene with the light shafts marched through it.
     3. THE CABIN, last: the bathyscaphe's dashboard over the whole stage, straight alpha, its sea layer moved by
        the soft caustic — rgb = dashboard + (k - 1) * sea — blended over the view. Drawn only while its two
        pictures are in (drawsCabin()); without them the face paints the panel itself.
    What is never seen is not drawn. While the cabin is drawn, where its panel is opaque (UnderwaterWorld::PanelMask's
    covered blocks: a third of the stage) is laid into the scene's depth at its nearest first, so nothing of the scene
    is shaded there, and the post pass skips it; the panel skips where it is bare glass; a jellyfish wholly outside the
    view, or under the panel's covered band, is not drawn; of the things on the floor only the cells that can hold one
    (the world's FieldWindow) and whose thing can show (each frame, against the view's planes) are drawn, an instance
    each, in the window's order; of the floor only its rows' runs of cells that can show, in the lattice's order. None
    of it changes a pixel that shows. A rung below the top of the windscreen's ladder (the scale under 1) draws less
    besides — UnderwaterWorld::Quality says what, and fades it — draws the floor after what stands on it, and with the
    post pass at the scene's pixels, scales the view up in the cabin's pass: one pass at the frame's pixels instead of
    two. At the foot the floor beyond the morph is the coarse lattice's (2 x 2 cells a cell, the lattice snapped to
    them), except on the web, where each run would be a draw of its own.
    The attack of the playing (uHitT), the silt and the drive's state are the world's; the cabin's projection —
    a constant half-width tangent, the height from the aspect, the centre sheared — is set with setLens.
    Draws into frames of its own, then blits the finished one to the frame buffer it is given. GLSL 1.50 core in
    the app, GLSL ES 3.00 on the web (src/shaders/Gpu.h). GL thread only. */
class UnderwaterGl final
{
public:
    /** A picture for a texture: straight RGBA bytes, the top row first, as a decoder hands them over. */
    struct Picture
    {
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
        bool valid() const noexcept { return width > 0 && height > 0 && rgba.size() == (size_t) width * (size_t) height * 4; }
    };
    /** THE CABIN'S ART: each picture made when the GPU's half is (create(), on the GL thread, so nothing of it is
        held between), and the passing bathyscaphe's table. A function left empty, or a picture that comes back
        invalid, leaves that part out: no cabin pass, no passing bathyscaphe. */
    struct Art
    {
        std::function<Picture()> dashboard, sea, atlas;
        std::string sprites;        // sprites.txt as it is
    };

    explicit UnderwaterGl (UnderwaterWorld& w) : world (w) {}

    /** The cabin's art, before create(). */
    void setArt (Art a) { art = std::move (a); sprites = UnderwaterWorld::parseSprites (art.sprites); }
    /** The passing bathyscaphe's atlas and table (sprites.txt as it is) after create(), on the GL thread, between two
        frames — for a page that has them only once its first frame is drawn; the app hands them in its Art. Taken as
        create() takes them, kept until destroy(); a table with no cells or an atlas the GPU cannot hold leaves the
        bathyscaphe out. */
    void setPassing (const Picture& atlas, const std::string& table);
    /** The projection: the half-width's tangent (constant over any aspect) and where the view's centre sits (NDC). */
    void setLens (double tanHalfWidth, double centreShear) noexcept { tanH = tanHalfWidth; shear = centreShear; }

    /** Builds the programs, buffers and textures. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), the post pass and the cabin at w x h,
        blitted to `targetFrameBuffer` over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** Whether the cabin pass is drawn: asked for (setCabin), and its two pictures are on the GPU. */
    bool drawsCabin() const noexcept { return cabinOn && texDash != 0 && texSea != 0; }
    /** The cabin pass on (the live frames) or off (a still, a poster: the world alone, the face paints the panel). */
    void setCabin (bool on) noexcept { cabinOn = on; }

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. */
    float gpuMillis() const noexcept { return gpuMs; }
    /** The finished frame (the cabin over the view) as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);
    /** The scene's pixels (the render scale's). */
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }
    /** The scene drawn this many times a frame, for measuring. */
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }
    /** Why create() said no. */
    const std::string& error() const noexcept { return trouble; }

private:
    UnderwaterWorld& world;
    Art art;
    UnderwaterWorld::Sprites sprites;
    double tanH = 18.0 / 13.0, shear = -0.2339;
    bool cabinOn = true;

    struct Program
    {
        std::uint32_t id = 0;
        int uniform (const char* name) const;
    };
    Program floor, rock, worm, flora, bell, tendril, particle, water, post, sprite, glow, panel, panelOver, panelOverFlat, cover;

    std::uint32_t texHeight = 0, texDetail = 0, texCaustic = 0, texDash = 0, texSea = 0, texAtlas = 0;
    // Where the panel covers the view and where it is bare (R, G: PanelMask's blocks), and its covered blocks as
    // rectangles for the scene's depth; a 1 x 1 of neither when there is no dashboard.
    std::uint32_t texMask = 0, vboCover = 0, vaoCover = 0;
    int coverVertices = 0;
    UnderwaterWorld::PanelMask mask;
    UnderwaterWorld::Quality quality;
    double lastFrameMs = -1.0;
    std::uint32_t vboGrid = 0, iboFloor = 0, vaoFloor = 0, iboCoarse = 0, vaoFloorCoarse = 0, vboRock = 0, vaoRock = 0, vboWorm = 0, iboWorm = 0, vaoWorm = 0;
    struct Flora { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; };
    std::array<Flora, 5> floraMesh {};
    std::uint32_t vboJelly = 0, vboBell = 0, iboBell = 0, vaoBell = 0, vboTend = 0, iboTend = 0, vaoTendril = 0;
    std::uint32_t vaoEmpty = 0, vboEmpty = 0;
    // The cells the things on the floor are drawn in: each field's in one buffer, its run of it [first, count).
    std::uint32_t vboCells = 0;
    std::array<UnderwaterWorld::FieldWindow, UnderwaterWorld::numFields> windows;
    std::vector<std::int32_t> cellData;
    std::array<std::array<int, 2>, UnderwaterWorld::numFields> cellRuns {};
    // The floor's runs of indices in view, and the heights it can reach.
    std::vector<int> floorFirsts, floorCounts, coarseFirsts, coarseCounts;
    std::vector<const void*> floorOffsets;
    std::array<double, 2> floorRange { -20.0, 20.0 };
    double coverTop = -1.0;     // NDC: under it, across the whole stage, the panel covers the view (-1: nowhere)
    int floorIndices = 0, wormIndices = 0, bellIndices = 0, tendrilIndices = 0;
    std::vector<float> jellyData;

    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;     // the scene: textures the post pass reads
    std::uint32_t fboOut = 0, fboOutColour = 0;             // the finished frame
    std::uint32_t fboPost = 0, fboPostColour = 0;           // below the top rung: the post pass at the scene's pixels (a texture)
    int fbW = 0, fbH = 0, outW = 0, outH = 0, postW = 0, postH = 0;
    int repeats = 1;

    // The frame timer, as the other worlds have it.
    bool queriesWork = false;
    std::array<std::uint32_t, 4> query {};
    int queryNext = 0, queriesPending = 0;
    float gpuMs = -1.0f;
    int frameCounter = 0;
    double probeStart = 0.0;
    std::string trouble;

    bool compile (Program&, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written = {});
    void makeTextures();
    void makeArt();
    void makeBuffers();
    bool ensureFrame (int w, int h, int cw, int ch);
    bool ensurePostFrame (int w, int h);
    void makeMask (const Picture& dashboard);
    /** Whether the scene at w x h is fine enough for the covered blocks' margin: a pixel that shows never reads one. */
    bool maskHolds (int w, int h) const noexcept;
    void drawScene (int rw, int rh, int cw, int ch);
    void noteCost (float ms);
};

} // namespace orbitamp::underwater
