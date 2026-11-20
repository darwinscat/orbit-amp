// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IoWorld.h"
#include "../../underwater/view/UnderwaterGl.h"     // Picture
#include "../../shaders/Gpu.h"
#include "../../shaders/Shaders.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace orbitamp::io
{

/** IO ON THE GPU, as the view's prototype draws it (io-gl.js's createIoView), from the same shaders (resources/shaders/io,
    io-shaders.js's bodies as they are). A frame, in the prototype's order:
     1. THE SUN'S SHADOW CASCADES (only while the surface has a key): the far one round the loop ahead (the outer three
        ground levels; every fourth frame, or whenever the Sun moved), the near one round what is ahead (the inner three
        levels, the rocks and the hardware; every third frame).
     2. THE SCENE, into an offscreen colour (R11F_G11F_B10F where it renders, RGBA8 otherwise) and depth at the render
        scale, scissored to the cabin's window (the box round the plates' clear pixels, 4 stage px wider) while the cabin
        is drawn: the hardware (the stand-ins instanced, a find's mesh one by one), the rocks, the ground's four levels
        (each outer one with the inner one's square left out), the sky where nothing was drawn; then blended
        (premultiplied), writing no depth: the plumes' bodies (those round the eye first, on their back faces), the
        plumes', the fountains' and the puffs' particles. (The flux tube is off in the prototype: not drawn.)
     3. THE POST PASS, at the frame's own pixels: the knee, the vignette, THE CABIN — the day's and the dark's plates,
        premultiplied with their mips, crossfaded by the dark's share 1 - cabinDay, world x (1 - a) + plate, the scene
        not read where the plate is opaque, one plate fetched outside the crossfade — and the dosimeter's needle, inside
        its dial's box only, in the plates' own ink.
     4. THE SULPHUR SNOW, over it, inside the window's box.
    The cabin is drawn while it is asked for (setCabin) and both plates are in (drawsCabin()); without it the scene is
    drawn whole, no needle, and the face paints the plate. The quality ladder's rung (the scale) thins, at its foot
    (0.5), half the plumes' particles and the snow, eased as the prototype's follow() eases them. The projection — a
    constant half-width tangent, the centre sheared — is set with setLens. Draws into frames of its own, then blits
    the finished one to the frame buffer it is given. GLSL 1.50 core in the app, GLSL ES 3.00 on the web
    (src/shaders/Gpu.h). GL thread only. */
class IoGl final
{
public:
    using Picture = underwater::UnderwaterGl::Picture;

    /** THE CABIN'S PLATES AND THE FINDS' FILES, each read when the GPU's half is made (create(), on the GL thread, so
        nothing of it is held between). A function left empty, or what it hands back empty or invalid, leaves that part
        out, as the prototype's view does when a file does not come: no plates, no cabin; no mesh, its variant's
        stand-in. */
    struct Art
    {
        std::function<Picture()> day, dark;                 // dashboard.webp, dashboard_dark.webp (3344 x 1882)
        /** The finds' meshes, as the prototype's view lists its finds/ folder: each "<folder>3d/<name>", a mesh
            (<name>.mesh) and its table (<name>.txt) under finds/, the folders in their names' order. */
        std::function<std::vector<std::string>()> finds;
        std::function<std::string (const std::string&)> text;                 // a file under finds/, by its path
        std::function<std::vector<std::uint8_t> (const std::string&)> bytes;
    };

    explicit IoGl (IoWorld& w) : world (w) {}

    void setArt (Art a) { art = std::move (a); }

    /** The projection: the half-width's tangent (constant over any aspect) and where the view's centre sits (NDC). */
    void setLens (double tanHalfWidth, double centreShear) noexcept { tanH = tanHalfWidth; shear = centreShear; }

    /** Builds the programs, buffers and textures, and uploads the art. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), the post pass, the cabin and the snow at w x h,
        blitted to `targetFrameBuffer` over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** Whether the cabin is drawn: asked for (setCabin), and its two plates are on the GPU. */
    bool drawsCabin() const noexcept { return cabinOn && texDash != 0 && texDashDark != 0; }
    void setCabin (bool on) noexcept { cabinOn = on; }

    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. */
    float gpuMillis() const noexcept { return gpuMs; }

    /** The finished frame (the cabin over the view) as RGBA bytes, rows bottom-up as GL has them. */
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }

    /** The scene drawn this many times a frame, for measuring. */
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }

    /** Parts left out, for measuring what each costs (the prototype's `off`: ground, sky, rocks, hardware, plumes,
        volumes, snow, shadows, aurora, post, sites, v0..v9), comma-separated. */
    void setSkip (const std::string& list);

    const std::string& error() const noexcept { return trouble; }

    /** The dosimeter's dial on the plates (stage px: its hub, the scale's radius, its start and end, degrees
        counter-clockwise from 3 o'clock), as cabin.html hands it to the prototype, and the stage. */
    struct Dial { double x, y, r, from, to; };
    static constexpr Dial dial { 119.2, 564.9, 36.8, 210, -30 };
    static constexpr std::array<double, 2> stage { 1672, 941 };

private:
    IoWorld& world;
    Art art;
    double tanH = 18.0 / 16.0, shear = 0.0;
    bool cabinOn = true;

    struct Program
    {
        std::uint32_t id = 0;
        mutable std::unordered_map<const char*, int> at;
        int u (const char* name) const;
    };
    Program half, ground, groundH, groundF, groundSh, rock, rockSh, structP, structSh, mesh, meshSh, sky, plume, pvol, snow, post;

    std::uint32_t texBase = 0, texDetail = 0, texNoise = 0, texCells = 0, texRoad = 0, texFeat = 0, texHist = 0;
    std::uint32_t texDash = 0, texDashDark = 0;
    std::optional<std::array<double, 4>> glassBox;      // x0 y0 x1 y1, fractions of the stage, the top row first
    std::array<double, 3> inkDay { 0.05, 0.04, 0.02 }, inkDark { 0.05, 0.04, 0.02 };

    struct Grid { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; };
    std::map<int, Grid> grids;                          // by cells across (io-gl.js gridFor)
    struct Instanced { std::uint32_t vao = 0, vbo = 0, inst = 0; int count = 0; };
    std::array<Instanced, 2> rocks;                     // the stones and the boulders; the pebbles
    std::array<Instanced, IoWorld::numVariants> structs;
    struct FileMesh { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; };
    std::map<int, FileMesh> meshes;                     // by variant
    struct Emit { std::uint32_t vao = 0, inst = 0; };
    std::array<Emit, 3> emitBuf;
    Emit volBuf;
    std::uint32_t vaoEmpty = 0, vboEmpty = 0;

    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;     // the scene: its colour a texture the post pass reads
    std::uint32_t fboOut = 0, fboOutColour = 0;             // the finished frame
    int fbW = 0, fbH = 0, outW = 0, outH = 0, subW = 0, subH = 0;   // the view's pixels, the finished frame's, the scene's frame
    std::array<int, 2> sceneAt {};              // where the scene's frame sits in the view's (the window's box)
    std::uint32_t fboHalf = 0, texHalf = 0, rbHalf = 0;   // at the ladder's foot: the plumes' bodies at half the scene's pixels
    int halfW = 0, halfH = 0;
    bool outerKept = false;                     // the plates outside the window's box are in the finished frame...
    double keptDark = -1.0;                     // ...as this dark share drew them
    bool floatTarget = true;
    int repeats = 1;

    // THE SHADOW MAPS and the light frames kept between their redraws (io-gl.js lightFrame / relFrame)
    struct Shadow { std::uint32_t tex = 0, fbo = 0; int N = 0; bool ok = false; };
    Shadow sh0, sh1;
    struct LightBox { IoWorld::V3 lx {}, ly {}, lz {}; double cx = 0, cy = 0, cz = 0, hx = 0, hy = 0, hz = 0; int N = 0; };
    struct LightFrame { std::array<float, 16> VP {}, M {}; double texel = 0, hz = 0; LightBox f; };
    std::optional<LightBox> sh0Frame, sh1Frame;
    std::optional<IoWorld::V3> sh1Sun;
    int sh0Age = 99, sh1Age = 99;
    static LightFrame lightFrame (const IoWorld::V3& sun, double cxAbs, double czAbs, double half, double yLo, double yHi, int N,
                                  double ccx, double ccz, const std::optional<LightBox>& keep);
    static LightFrame relFrame (const LightBox& f, double ccx, double ccz);

    // The quality ladder's fade, as io-gl.js's follow() eases it: at the foot (0.5) half the plumes' particles and the snow.
    struct Fades { double thin = 0; int level = 0; bool started = false; } Q;
    void follow (double scale, double seconds);
    double lastFrameMs = -1.0;
    std::optional<IoWorld::Frame> prepared;     // the next drawScene's CPU part, worked out ahead of the timer

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
    void makeBuffers();
    void makeArt();
    void loadMesh (int variant, const std::string& base);
    Grid& gridFor (int cells);
    bool ensureFrame (int w, int h, int cw, int ch);
    void drawScene (int rw, int rh, int cw, int ch);
    void noteCost (float ms);
};

} // namespace orbitamp::io
