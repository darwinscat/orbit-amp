// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CaliforniaWorld.h"
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

namespace orbitamp::california
{

/** CALIFORNIA, 1967, ON THE GPU, as the view's prototype draws it (california-gl.js's createCaliforniaView), from the
    same shaders (resources/shaders/california, california-shaders.js's bodies as they are). A frame, in the
    prototype's order:
     1. THE SCENE, into an offscreen colour and depth at the render scale, only inside the windshield's box (a
        scissor from the dashboard's alpha: nothing under the panel is shaded): first what stands on the ground —
        the three flowers in their fields (none below the glass's lower edge), the oaks at three details, the poles,
        the windpumps; the signs, the fences, the banners and the flags; the butterflies (the world's and an
        attack's); the monolith; the meshes (the Feynman van, the barns, the drive-in, the giants, the walker, the
        yellow submarine, the saucers), each censored headstock's box noted; then the ground's three levels, near to
        far; then the sky where nothing was drawn. Blended over them, writing no depth: a saucer's beam, the
        sprites (the radio dishes, the water tower), the wires, the particles (pollen, fluff, an attack's petals,
        the birds, a beam's petals), and in PARK the monarchs on the glass, in the frame's own screen space.
     2. THE POST PASS, at the frame's own pixels: the sun's rays near the sun, the censorship's mosaic and its word,
        and the cabin's dashboard over it all (where it is opaque the scene is not even read).
     3. THE PEACE PENDANT, last: one quad turned about its pivot by the world's pendulum, a glint running over it.
    The cabin (the dashboard and the pendant) is drawn while it is asked for (setCabin) and its dashboard is in
    (drawsCabin()); without it the scene is drawn whole and the face paints the panel itself. What is never seen is
    not drawn, beyond the prototype's own scissor and cut: the flowers only in the bands of their window's cells whose
    squares can hold a root the shader would keep, the outer ground levels without their cells wholly inside the
    inner level's square (which they discard), and the post pass, while the dashboard's gain holds, only inside the
    windshield's box and the pendant's swing (outside them the finished frame keeps the dashboard it has) — none of
    it changing a pixel that shows. The quality ladder's
    rungs are UnderwaterWorld::Quality's, as the prototype's; the bottom one leaves out besides the oaks past about
    600 m and the flowers' outer band (fading as the ladder fades), and gives the instance buffers new storage for
    each draw. The projection — a constant half-width tangent, the
    height from the aspect, the centre sheared — is set with setLens. Draws into frames of its own, then blits the
    finished one to the frame buffer it is given. GLSL 1.50 core in the app, GLSL ES 3.00 on the web
    (src/shaders/Gpu.h). GL thread only. */
class CaliforniaGl final
{
public:
    using Picture = underwater::UnderwaterGl::Picture;

    /** THE CABIN'S ART AND THE FINDS' FILES, each read when the GPU's half is made (create(), on the GL thread, so
        nothing of it is held between). A function left empty, or what it hands back empty or invalid, leaves that
        part out, as the prototype's view does when a file does not come: no dashboard, no cabin; no atlas, the
        signs' faces and the sprites their placeholders; no mesh, its placeholder box. */
    struct Art
    {
        std::function<Picture()> dashboard, pendant;
        std::string pendantBox;                    // the pendant's box and pivot (its .txt as it is)
        /** The CENSORED word as the prototype draws it on a 512 x 112 canvas (straight RGBA); none, no word. */
        std::function<Picture()> label;
        /** A file of the finds by its path under finds/, as the prototype names it ("dish/atlas.png",
            "van3d/van.mesh", "van3d/van.txt"): a picture, a text, the bytes. */
        std::function<Picture (const std::string&)> picture;
        std::function<std::string (const std::string&)> text;
        std::function<std::vector<std::uint8_t> (const std::string&)> bytes;
        /** The optional chalks (the winks): on in the app, off on the site. */
        bool wink = true;
    };

    explicit CaliforniaGl (CaliforniaWorld& w) : world (&w) {}

    void setArt (Art a) { art = std::move (a); }
    /** The projection: the half-width's tangent (constant over any aspect) and where the view's centre sits (NDC). */
    void setLens (double tanHalfWidth, double centreShear) noexcept { tanH = tanHalfWidth; shear = centreShear; }

    /** Builds the programs, buffers and textures, and reads the art. False when a shader would not build here. */
    bool create();
    void destroy();

    /** Draws the world's state: the scene at (w * scale) x (h * scale), the post pass and the cabin at w x h,
        blitted to `targetFrameBuffer` over (x, y, w, h) — GL's own pixels, from its bottom-left corner. */
    void render (int w, int h, float scale, std::uint32_t targetFrameBuffer, int x = 0, int y = 0);

    /** Whether the cabin is drawn: asked for (setCabin), and its dashboard is on the GPU. */
    bool drawsCabin() const noexcept { return cabinOn && texDash != 0; }
    void setCabin (bool on) noexcept { cabinOn = on; }

    float gpuMillis() const noexcept { return gpuMs; }
    bool readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h);
    int frameWidth() const noexcept  { return fbW; }
    int frameHeight() const noexcept { return fbH; }
    void setRepeats (int times) noexcept { repeats = std::clamp (times, 1, 16); }
    const std::string& error() const noexcept { return trouble; }
    /** A developer's switch, the prototype's ?off=: the draws left out (a comma list of ground, flora, props, signs,
        bfly, finds, sky, particles, rays, post), to time the rest. */
    void setSkip (const std::string& list) { skip = "," + list + ","; }

private:
    CaliforniaWorld* world;
    Art art;
    double tanH = CaliforniaWorld::tanH, shear = 0;
    bool cabinOn = true;

    struct Program
    {
        std::uint32_t id = 0;
        mutable std::unordered_map<const char*, int> at;     // by the name's literal: each call site's own
        mutable int stamp = -1;                               // the scene its frame's uniforms were last set for
        int u (const char* name) const;
    };
    Program ground, groundH, prop, flora, bfly, mono, mesh, beam, sky, sprite, wire, particle, post, glass, sign, pendant;

    std::uint32_t texHeight = 0, texDetail = 0, texNoise = 0, texRoad = 0, texLand = 0, texShadow = 0;
    std::uint32_t texDash = 0, texPend = 0, texLabel = 0, texSigns = 0, texFlags = 0, texChalk = 0;
    std::optional<CaliforniaWorld::V4> glassBox;                  // the windshield's box, fractions of the picture
    std::optional<CaliforniaWorld::PendantBox> pendBox;
    struct Sprite { std::uint32_t tex = 0; CaliforniaWorld::SpriteTable t; std::array<int, 2> size { 1, 1 }; bool in = false; };
    std::map<std::string, Sprite> sprites;                        // "dish", and the sprite places without a mesh
    struct MeshGpu
    {
        std::uint32_t vao = 0, vbo = 0, ibo = 0;
        int count = 0;
        std::array<std::uint32_t, 3> tex {};
        std::vector<CaliforniaWorld::CbMesh::Part> parts;
        std::vector<CaliforniaWorld::Headstock> censor;
    };
    std::map<std::string, MeshGpu> meshes;

    // What the files told the world (laid into it again when it is made afresh).
    std::vector<CaliforniaWorld::Sign> signTable;
    int signAtlasW = 0, signAtlasH = 0;
    std::vector<CaliforniaWorld::V4> flagCells;
    std::vector<CaliforniaWorld::ChalkDrawing> chalkTable;
    std::vector<std::string> came;
    bool tablesRead = false;

    struct Grid { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; };
    Grid grid;                                                     // every level's lattice is 96 x 96
    struct Prop { std::uint32_t vao = 0, vbo = 0, ibo = 0, inst = 0; int count = 0, max = 0; std::vector<float> data; };
    Prop oakNear, oakMid, oakFar, poles, pumpTower, pumpFan;
    struct Flora { std::uint32_t vao = 0, vbo = 0, ibo = 0; int count = 0; };
    std::array<Flora, 3> floraMesh {};
    std::uint32_t vaoBfly = 0, vboBfly = 0, iboBfly = 0, instBfly = 0;
    int bflyIndices = 0;
    std::vector<float> bflyData;
    std::uint32_t vaoWire = 0, vboWire = 0, iboWire = 0, instWire = 0;
    std::vector<float> wireData;
    std::uint32_t vaoGlass = 0, instGlass = 0;
    struct SignBuf { std::uint32_t vao = 0, inst = 0; int max = 0; std::vector<float> data; };
    SignBuf signBoxes, signBanners, signFlags;
    std::uint32_t vaoEmpty = 0;

    std::uint32_t fbo = 0, fboColour = 0, fboDepth = 0;     // the scene: textures the post pass reads
    std::uint32_t fboOut = 0, fboOutColour = 0;             // the finished frame
    int fbW = 0, fbH = 0, outW = 0, outH = 0;
    double outsideGain = -1.0;                              // the finished frame's dashboard outside the glass, as last drawn
    std::uint32_t outsideTex = 0;
    int repeats = 1;
    int frameStamp = 0;
    CaliforniaWorld::Quality quality;
    double lastFrameMs = -1.0;

    bool queriesWork = false;
    std::array<std::uint32_t, 4> query {};
    int queryNext = 0, queriesPending = 0;
    float gpuMs = -1.0f;
    int frameCounter = 0;
    double probeStart = 0.0;
    std::string trouble;
    std::string skip;
    bool off (const char* what) const { return ! skip.empty() && skip.find (std::string (",") + what + ",") != std::string::npos; }

    bool compile (Program&, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written = {});
    void makeTextures();
    void makeArt();
    void loadMesh (const std::string& key, const std::string& base, const std::vector<CaliforniaWorld::V2>& parts);
    void applyTables();
    void makeBuffers();
    bool ensureFrame (int w, int h, int cw, int ch);
    void uploadShadow();
    void drawScene (int rw, int rh, int cw, int ch);
    void noteCost (float ms);
};

} // namespace orbitamp::california
