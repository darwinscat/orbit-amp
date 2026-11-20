// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MetalFound.h"
#include "MetalWorld.h"
#include "../../shaders/Gpu.h"
#include "../../shaders/Shaders.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace orbitamp::metal
{

/** THE METAL WORLD ON THE GPU, as the view's prototype draws it (metal-gl.js), from the same shaders
    (resources/shaders/metal, metal-shaders.js's bodies as they are). A frame, in the prototype's order:
     1. THE SCENE, into an offscreen colour and depth at the render scale, scissored to the cabin's window (the box
        round the plates' clear pixels, 4 stage px wider) while the cabin is drawn: the steel things (instanced, the
        procedural meshes and the hill), the owner's meshes (the skull gate, the fossil, the landmarks, the guitars of
        the groves, the warrior, the cat), the dragons (their wing-beat's two poses mixed), the cloth (the citadel's
        banners, the battle jacket's flag), the ground (three lattices, the outer two with the inner one's square left
        out), the sky where nothing was drawn; then blended, writing no depth: the vents' steam and sparks, the fire,
        the ribbons (the lightning, the Tesla arcs, the searchlights), the burning letters and the word CENSORED, the
        embers.
     2. THE POST PASS, at the frame's own pixels: the censored headstocks' mosaic, the flash's lift, the knee, the
        vignette, and THE CABIN in the same pass: the two plates (the night's and the flash's, premultiplied with their
        mips) crossfaded by the world's lightning (MetalWorld::cabinFlash), world x (1 - a) + plate; where the plate is
        opaque the scene is not read, outside a flash one plate is fetched. Drawn only while both plates are in and
        the cabin is asked for (drawsCabin()); without it the world alone, the face paints the plate.
    The quality ladder's rungs (the scale under 1) fade the aurora's finest rays (0.75) and at the foot (0.5) half the
    fire's particles and of the embers, the near lattice coarse beyond 45 m, as the prototype's do; the foot besides
    draws less than the prototype's: the near lattice only in its middle 108 m once it is coarse beyond 45 m (the middle
    level draws the same coarse lattice beyond), and no far meshes (the guitars beyond 300 m, the landmarks beyond 450 m,
    the dragons beyond 600 m, dithered out as the rung comes).
    The cabin's projection — a constant half-width tangent, the centre sheared — is set with setLens. Draws into
    frames of its own, then blits the finished one to the frame buffer it is given. GLSL 1.50 core in the app,
    GLSL ES 3.00 on the web (src/shaders/Gpu.h). GL thread only. */
class MetalGl final
{
public:
    using Picture = metal::Picture;
    using Art = metal::Art;
    using Found = metal::Found;
    /** What the art brings the world and the renderer (metal::find). */
    static Found find (const Art& a) { return metal::find (a); }

    explicit MetalGl (MetalWorld& w) : world (w) {}

    /** The art and what was found in it (find (art)), before create(). */
    void setArt (Art a, Found f) { art = std::move (a); found = std::move (f); }
    /** The projection: the half-width's tangent (constant over any aspect) and where the view's centre sits (NDC). */
    void setLens (double tanHalfWidth, double centreShear) noexcept { tanH = tanHalfWidth; shear = centreShear; }

    /** Builds the programs, buffers and textures, and uploads the art. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), the post pass and the cabin at w x h,
        blitted to `targetFrameBuffer` over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** Whether the cabin is drawn: asked for (setCabin), and its two plates are on the GPU. */
    bool drawsCabin() const noexcept { return cabinOn && texDash != 0 && texDashFlash != 0; }
    void setCabin (bool on) noexcept { cabinOn = on; }

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. */
    float gpuMillis() const noexcept { return gpuMs; }
    /** The finished frame (the cabin over the view) as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }
    /** The scene drawn this many times a frame, for measuring. */
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }
    /** Parts left out, for measuring what each costs (the prototype's `off`: props, meshes, dragons, signs, ground, sky,
        vents, fire, ribbons, letters, embers, post, beams, flash), comma-separated; and halffloat: a half-float frame
        taken as not drawable, as on a WebGL 2 without EXT_color_buffer_float, to see the fallback. */
    void setSkip (const std::string& list);
    const std::string& error() const noexcept { return trouble; }

private:
    MetalWorld& world;
    Art art;
    Found found;
    double tanH = 18.0 / 16.0, shear = 0.0;
    bool cabinOn = true;

    struct Program
    {
        std::uint32_t id = 0;
        mutable std::unordered_map<const char*, int> at;
        int u (const char* name) const;
    };
    Program ground, groundH, groundHN, sky, skySmooth, cabinPass, prop, mesh, dragon, sign, letters, fire, ribbon, spark, steam, ember, post;

    std::uint32_t texBase = 0, texDetail = 0, texNoise = 0, texCells = 0, texRoad = 0;
    std::uint32_t texDash = 0, texDashFlash = 0, texLetters = 0, texBanners = 0;
    std::array<double, 4> glassBox {};          // x0 y0 x1 y1, fractions of the stage, the top row first
    bool hasGlassBox = false;
    int dashW = 0, dashH = 0;                   // the plates' size
    bool platesWhole = false;                   // ...and whether they are read without their mips (platesFor)
    std::array<double, 2> lettersAtlas { 2048, 1024 };
    std::vector<std::array<double, 4>> bannerCells;

    struct Gpu { std::uint32_t vao = 0, vbo = 0, ibo = 0, inst = 0; int count = 0; bool wide = false; int max = 0; double r = 0; };
    std::uint32_t vaoGrid = 0, vboGrid = 0, iboGrid = 0, vaoGridFoot = 0, iboGridFoot = 0;
    int gridCount = 0, gridFootCount = 0;                // the lattice; its middle cells, the near level at the foot
    std::map<std::string, Gpu> propBufs;
    std::vector<std::pair<std::string, std::vector<int>>> byMesh;   // the props by mesh, in the prototype's order
    std::vector<int> spireOf;                   // per prop, its spire (-1: none)
    struct FileMesh { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; double zmin = 1e9, zmax = -1e9; };
    std::map<std::string, FileMesh> meshes;     // by role, the guitars by "guitar:<name>"
    struct DragonGpu { std::uint32_t vao = 0, vbo = 0; int nv = 0, nf = 0, count = 0; float unit = 0; bool ok = false; };
    DragonGpu dragonM;
    Gpu clothBuf, fireBuf, ribBuf, letBuf;
    std::uint32_t vaoEmpty = 0, vboEmpty = 0;
    std::vector<float> scratch;
    MetalWorld::Fires frameFires;               // this frame's, worked out once before its draws (render)
    MetalWorld::Ribbons frameRibbons;

    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;     // the scene: its colour a texture the post pass reads
    std::uint32_t fboOut = 0, fboOutColour = 0;             // the finished frame
    int fbW = 0, fbH = 0, outW = 0, outH = 0, subW = 0, subH = 0;   // the view's pixels, the finished frame's, the scene's frame
    std::array<int, 2> sceneAt {};              // where the scene's frame sits in the view's (the window's box)
    int repeats = 1;
    std::uint32_t fboSmooth = 0, texSmooth = 0, fboPost = 0, texPost = 0;   // the sky's smooth parts; the foot's post pass
    int smoothW = 0, smoothH = 0, postW = 0, postH = 0;
    int smoothDiv = 4;                          // the smooth parts at a quarter of the view's pixels each way
    bool smoothFloats = true;                   // their frame in half floats; else 8 bits a channel, square-rooted
    bool outerKept = false;                     // the plate outside the window's box is in the finished frame...
    double keptFlash = -1.0;                    // ...as this flash drew it

    // The quality ladder's fades, as metal-gl.js's follow() eases them.
    struct Fades { double flat = 0, thin = 0; int level = 0; bool started = false; } Q;
    double lastFrameMs = -1.0, envHot = -1.0;
    std::vector<std::string> skip;
    bool skips (const char* what) const noexcept;

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
    bool loadMesh (const std::string& key, const std::vector<std::uint8_t>& bytes);
    bool ensureFrame (int w, int h, int cw, int ch);
    bool ensureTarget (std::uint32_t& fb, std::uint32_t& tex, int& tw, int& th, int w, int h, bool floats);
    void follow (double scale, double seconds);
    void platesFor (int cw, int ch);
    void drawScene (int rw, int rh, int cw, int ch);
    void noteCost (float ms);
};

} // namespace orbitamp::metal
