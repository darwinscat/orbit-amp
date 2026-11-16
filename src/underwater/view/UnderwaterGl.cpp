// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of sea-gl.js's drawing, call for call: the same numbers handed to the same shaders in the same order of
// passes, the matrices made as the prototype makes them (in doubles from the f32 values it keeps them in), the
// trigonometry V8's (WalkerWorld::Js), so a frame here is the prototype's frame at the same state.

#include "UnderwaterGl.h"

#include "../../redgroove/view/WalkerWorld.h"     // Js
#include "../../shaders/Shaders.h"

#include <cmath>
#include <initializer_list>
#include <tuple>
#include <utility>
#include <string>
#include <vector>

namespace orbitamp::underwater
{

using namespace orbitamp::gpu;

namespace
{
    using W = UnderwaterWorld;
    using Js = redgroove::WalkerWorld::Js;
    using V3 = W::V3;
    constexpr double pi = W::pi;

    constexpr int N = 160;                  // the floor lattice: 240 m square, snapped to the world
    constexpr double cellSize = 1.5;
    constexpr double zNear = 0.3, zFar = 600.0;
    struct Field { double cell; int w; double prob, smin, smax; };
    constexpr Field rocks { 3.0, 36, 0.16, 0.25, 1.3 }, spires { 14.0, 16, 0.22, 2.5, 7.0 };
    constexpr double wormCell = 1.1;
    constexpr int wormW = 56;
    // The flora: kind, the cell and window, how much of a meadow's cells are taken, the height range in metres.
    struct FloraKind { int kind; double cell; int w; double density, smin, smax; };
    constexpr std::array<FloraKind, 5> floraKinds { {
        { 0, 2.2, 36, 0.50, 0.4,  1.1 },    // fan
        { 1, 2.6, 30, 0.40, 0.5,  1.3 },    // bush
        { 2, 1.6, 40, 0.55, 0.35, 0.9 },    // anemone
        { 3, 1.0, 56, 0.40, 1.2,  3.0 },    // kelp
        { 4, 0.7, 64, 0.50, 0.15, 0.4 },    // polyp
    } };
    constexpr int snowCount = 2200, siltCount = 2800, bubbleCount = 650;
    constexpr double cau1 = 2.1, cau2 = 1.44, cauW = 18.9;

    // The attributes' locations, shared by every program that reads them. aPos is 0 (gpu::program binds it there);
    // the floor's lattice, the only attribute of its program, takes 0 too.
    constexpr GLuint locPos = 0, locGrid = 0, locNrm = 1, locCol = 2, locUV = 3, locFlag = 4, locTSI = 5,
                     locScale = 6, locPhase = 7, locAxis = 8, locAlpha = 9, locGlow = 10, locTend = 11;
    const std::vector<gpu::Attribute> attributes {
        { "aGrid", locGrid }, { "aNrm", locNrm }, { "aCol", locCol }, { "aUV", locUV }, { "aFlag", locFlag }, { "aTSI", locTSI },
        { "aScale", locScale }, { "aPhase", locPhase }, { "aAxis", locAxis }, { "aAlpha", locAlpha }, { "aGlow", locGlow }, { "aTend", locTend } };

    /** JS's Math.round of a positive size: halves up. */
    inline int jsRoundInt (double x) noexcept { return (int) Js::round (x); }
    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline V3 mix3 (const V3& a, const V3& b, double t) noexcept { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
    inline const void* offset (int bytes) noexcept { return reinterpret_cast<const void*> ((std::uintptr_t) bytes); }

    /** a * b, column-major, as the prototype multiplies its Float32Arrays: in doubles, kept as f32. */
    std::array<float, 16> multiply (const std::array<float, 16>& a, const std::array<float, 16>& b) noexcept
    {
        std::array<float, 16> o {};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                o[(size_t) (c * 4 + r)] = (float) ((double) a[(size_t) r] * b[(size_t) (c * 4)] + (double) a[(size_t) (4 + r)] * b[(size_t) (c * 4 + 1)]
                                                   + (double) a[(size_t) (8 + r)] * b[(size_t) (c * 4 + 2)] + (double) a[(size_t) (12 + r)] * b[(size_t) (c * 4 + 3)]);
        return o;
    }

    // The light: from above, slanted; the shafts' plane basis is perpendicular to it.
    struct Light { V3 dir, a, b; };
    const Light& light() noexcept
    {
        static const Light l = []
        {
            const V3 L = W::normalised ({ 0.45, 0.8, -0.35 });
            const V3 A = W::normalised ({ L[1], -L[0], 0 });
            const V3 B = W::normalised ({ L[1] * A[2] - L[2] * A[1], L[2] * A[0] - L[0] * A[2], L[0] * A[1] - L[1] * A[0] });
            return Light { L, A, B };
        }();
        return l;
    }

    /** A texture from a picture: straight or premultiplied as the prototype's browser uploads it, with mips or not. */
    GLuint pictureTexture (const UnderwaterGl::Picture& p, bool premultiplied, bool mips)
    {
        GLint largest = 0;
        glGetIntegerv (GL_MAX_TEXTURE_SIZE, &largest);
        if (! p.valid() || p.width > largest || p.height > largest)
            return 0;
        std::vector<std::uint8_t> px;
        const std::uint8_t* data = p.rgba.data();
        if (premultiplied)
        {
            px = p.rgba;
            for (size_t i = 0; i < px.size(); i += 4)
                for (size_t k = 0; k < 3; ++k)
                    px[i + k] = (std::uint8_t) ((px[i + k] * px[i + 3] + 127) / 255);
            data = px.data();
        }
        GLuint id = 0;
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, p.width, p.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        if (mips)
            glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
        return id;
    }
}

//==============================================================================
int UnderwaterGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool UnderwaterGl::compile (Program& p, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written)
{
    std::string log;
    p.id = gpu::program (gpu::header() + shaders::source (vertexPath), gpu::header() + shaders::source (fragmentPath, written), log, attributes);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("underwater: " + trouble);
        return false;
    }
    return true;
}

bool UnderwaterGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("underwater: " + trouble);
        return false;
    }
    world.build();

    if (! compile (floor, "underwater/floor.vert", "underwater/floor.frag", "floor")
        || ! compile (rock, "underwater/rock.vert", "underwater/rock.frag", "rock")
        || ! compile (worm, "underwater/worm.vert", "underwater/worm.frag", "worm")
        || ! compile (flora, "underwater/flora.vert", "underwater/flora.frag", "flora")
        || ! compile (bell, "underwater/bell.vert", "underwater/bell.frag", "bell")
        || ! compile (tendril, "underwater/tendril.vert", "underwater/tendril.frag", "tendril")
        || ! compile (particle, "underwater/particle.vert", "underwater/particle.frag", "particle")
        || ! compile (water, "underwater/water.vert", "underwater/water.frag", "water")
        || ! compile (post, "underwater/post.vert", "underwater/post.frag", "post")
        || ! compile (sprite, "underwater/sprite.vert", "underwater/sprite.frag", "sprite")
        || ! compile (glow, "underwater/sprite.vert", "underwater/glow.frag", "glow")
        || ! compile (panel, "underwater/post.vert", "underwater/panel.frag", "panel", { { "panel", "" } })
        || ! compile (panelOver, "underwater/post.vert", "underwater/panel.frag", "panel over the view", { { "panel", "#define OVER 1\n" } })
        || ! compile (panelOverFlat, "underwater/post.vert", "underwater/panel.frag", "flat panel over the view", { { "panel", "#define OVER 1\n#define FLAT 1\n" } })
        || ! compile (cover, "underwater/cover.vert", "underwater/cover.frag", "cover"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeArt();
    makeBuffers();

    // The frame timer: a timer query where the driver has one. Asked once; an error means no.
    while (glGetError() != GL_NO_ERROR) {}
    if (gpu::timerQueries())
    {
        glGenQueries ((GLsizei) query.size(), query.data());
        gpu::beginTimer (query[0]);
        gpu::endTimer();
        queriesWork = glGetError() == GL_NO_ERROR;
        if (queriesWork)
            gpu::timerNanoseconds (query[0]);   // drains the probe
        else
        {
            glDeleteQueries ((GLsizei) query.size(), query.data());
            query.fill (0);
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    return true;
}

void UnderwaterGl::destroy()
{
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    for (auto& f : floraMesh) { del (f.vbo, glDeleteBuffers); del (f.ibo, glDeleteBuffers); del (f.vao, glDeleteVertexArrays); f.count = 0; }
    del (vboGrid, glDeleteBuffers); del (iboFloor, glDeleteBuffers); del (vaoFloor, glDeleteVertexArrays);
    del (vboRock, glDeleteBuffers); del (vaoRock, glDeleteVertexArrays);
    del (vboWorm, glDeleteBuffers); del (iboWorm, glDeleteBuffers); del (vaoWorm, glDeleteVertexArrays);
    del (vboJelly, glDeleteBuffers); del (vboBell, glDeleteBuffers); del (iboBell, glDeleteBuffers); del (vaoBell, glDeleteVertexArrays);
    del (vboTend, glDeleteBuffers); del (iboTend, glDeleteBuffers); del (vaoTendril, glDeleteVertexArrays);
    del (vaoEmpty, glDeleteVertexArrays); del (vboEmpty, glDeleteBuffers);
    del (vboCover, glDeleteBuffers); del (vaoCover, glDeleteVertexArrays); coverVertices = 0;
    for (auto* t : { &texHeight, &texDetail, &texCaustic, &texDash, &texSea, &texAtlas, &texMask, &fboColour, &fboDepth, &fboPostColour })
        del (*t, glDeleteTextures);
    del (fboOutColour, glDeleteRenderbuffers);
    del (fbo, glDeleteFramebuffers); del (fboOut, glDeleteFramebuffers); del (fboPost, glDeleteFramebuffers);
    fbW = fbH = outW = outH = postW = postH = 0;
    mask = {};
    quality = {};
    lastFrameMs = -1.0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &floor, &rock, &worm, &flora, &bell, &tendril, &particle, &water, &post, &sprite, &glow, &panel, &panelOver, &panelOverFlat, &cover })
    {
        if (p->id != 0) glDeleteProgram (p->id);
        p->id = 0;
    }
}

void UnderwaterGl::makeTextures()
{
    // The heights: RGBA float, linear, wrapping, no mips (the shaders read level 0).
    glGenTextures (1, &texHeight);
    glBindTexture (GL_TEXTURE_2D, texHeight);
    glTexImage2D (GL_TEXTURE_2D, 0, (GLint) gpu::filteredFloatFormat(), W::texSize, W::texSize, 0, GL_RGBA, GL_FLOAT, world.height().data());
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // The detail and the caustics: RGBA8, their mips made by GL.
    const auto rgba8 = [] (std::uint32_t& id, int size, const std::vector<std::uint8_t>& px)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    rgba8 (texDetail, W::detailSize, world.detail());
    rgba8 (texCaustic, W::causticSize, world.caustic());
    glBindTexture (GL_TEXTURE_2D, 0);
}

void UnderwaterGl::makeArt()
{
    // The cabin's panel and its sea layer in straight alpha, no mips (drawn at the stage's own size); the passing
    // bathyscaphe's atlas premultiplied, with mips (seen small and far). Each picture is made here and let go.
    Picture dash;
    if (art.dashboard && art.sea)
    {
        dash = art.dashboard();
        texDash = pictureTexture (dash, false, false);
        texSea = pictureTexture (art.sea(), false, false);
        if (texDash == 0 || texSea == 0)
        {
            const auto del = [] (std::uint32_t& id) { if (id != 0) glDeleteTextures (1, &id); id = 0; };
            del (texDash); del (texSea);
            dash = {};
            gpu::note ("underwater: the cabin's pictures did not come; the face paints the panel");
        }
    }
    makeMask (dash);
    if (art.atlas && sprites.scale > 0 && ! sprites.cells.empty())
        texAtlas = pictureTexture (art.atlas(), true, true);
}

void UnderwaterGl::makeMask (const Picture& dash)
{
    // Blocks of 8 of the picture's pixels; covered with a margin of 24 round them (three blocks: a scene pixel at a
    // quarter of the frame's, and the filters' reach), bare with one (the panel's own filter).
    constexpr int block = 8, coverMargin = 24, openMargin = 1;
    mask = dash.valid() ? W::PanelMask::of (dash.rgba.data(), dash.width, dash.height, block, coverMargin, openMargin) : W::PanelMask {};
    const int mw = std::max (1, mask.width), mh = std::max (1, mask.height);
    std::vector<std::uint8_t> rg ((size_t) mw * (size_t) mh * 2, 0);
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width; ++x)
        {
            const auto c = mask.at (x, y);
            rg[(size_t) (y * mw + x) * 2] = c == W::PanelMask::covered ? 255 : 0;
            rg[(size_t) (y * mw + x) * 2 + 1] = c == W::PanelMask::open ? 255 : 0;
        }
    glGenTextures (1, &texMask);
    glBindTexture (GL_TEXTURE_2D, texMask);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RG8, mw, mh, 0, GL_RG, GL_UNSIGNED_BYTE, rg.data());
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    // The covered blocks as rectangles in NDC, a run of them along a row each (the picture's top row at y = 1).
    std::vector<float> v;
    const double pw = mask.pictureWidth, ph = mask.pictureHeight;
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width;)
        {
            if (mask.at (x, y) != W::PanelMask::covered) { ++x; continue; }
            int e = x;
            while (e < mask.width && mask.at (e, y) == W::PanelMask::covered) ++e;
            const auto ndcX = [pw] (int px) { return (float) (std::min ((double) px, pw) / pw * 2 - 1); };
            const auto ndcY = [ph] (int py) { return (float) (1 - std::min ((double) py, ph) / ph * 2); };
            const float x0 = ndcX (x * block), x1 = ndcX (e * block), y0 = ndcY (y * block), y1 = ndcY ((y + 1) * block);
            v.insert (v.end(), { x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1 });
            x = e;
        }
    coverVertices = (int) v.size() / 2;
    if (coverVertices > 0)
    {
        glGenVertexArrays (1, &vaoCover);
        glBindVertexArray (vaoCover);
        glGenBuffers (1, &vboCover);
        glBindBuffer (GL_ARRAY_BUFFER, vboCover);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (v.size() * sizeof (float)), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (locPos); glVertexAttribPointer (locPos, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindVertexArray (0);
        glBindBuffer (GL_ARRAY_BUFFER, 0);
    }
}

bool UnderwaterGl::maskHolds (int w, int h) const noexcept
{
    // A pixel that shows reads the scene within one of its pixels, and below the top the post pass's frame within one
    // more: two scene pixels and the panel's filter must lie inside the margin.
    if (coverVertices == 0 || w <= 0 || h <= 0)
        return false;
    const double reach = 2 * std::max ((double) mask.pictureWidth / w, (double) mask.pictureHeight / h) + 2;
    return reach <= mask.coverMargin;
}

void UnderwaterGl::makeBuffers()
{
    // The floor lattice.
    {
        std::vector<float> grid ((size_t) ((N + 1) * (N + 1) * 2));
        for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i) { grid[(size_t) ((j * (N + 1) + i) * 2)] = (float) i; grid[(size_t) ((j * (N + 1) + i) * 2 + 1)] = (float) j; }
        std::vector<std::uint32_t> idx ((size_t) (N * N * 6));
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
            {
                const auto o = (size_t) ((j * N + i) * 6);
                const auto v = (std::uint32_t) (j * (N + 1) + i);
                idx[o] = v; idx[o + 1] = v + 1; idx[o + 2] = v + N + 2;
                idx[o + 3] = v; idx[o + 4] = v + N + 2; idx[o + 5] = v + N + 1;
            }
        floorIndices = (int) idx.size();
        glGenVertexArrays (1, &vaoFloor);
        glBindVertexArray (vaoFloor);
        glGenBuffers (1, &vboGrid);
        glBindBuffer (GL_ARRAY_BUFFER, vboGrid);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (grid.size() * sizeof (float)), grid.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (locGrid); glVertexAttribPointer (locGrid, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glGenBuffers (1, &iboFloor);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboFloor);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (idx.size() * sizeof (std::uint32_t)), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray (0);
    }

    // The rocks: two lumpy variants in one buffer.
    {
        auto v = W::icosphere (77);
        const auto v2 = W::icosphere (78);
        v.insert (v.end(), v2.begin(), v2.end());
        glGenVertexArrays (1, &vaoRock);
        glBindVertexArray (vaoRock);
        glGenBuffers (1, &vboRock);
        glBindBuffer (GL_ARRAY_BUFFER, vboRock);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (v.size() * sizeof (float)), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (locPos); glVertexAttribPointer (locPos, 3, GL_FLOAT, GL_FALSE, 24, offset (0));
        glEnableVertexAttribArray (locNrm); glVertexAttribPointer (locNrm, 3, GL_FLOAT, GL_FALSE, 24, offset (12));
        glBindVertexArray (0);
    }

    // An indexed mesh in a vertex array of its own.
    const auto mesh = [] (const W::Mesh& m, std::uint32_t& vao, std::uint32_t& vbo, std::uint32_t& ibo, const auto& attribs)
    {
        glGenVertexArrays (1, &vao);
        glBindVertexArray (vao);
        glGenBuffers (1, &vbo);
        glBindBuffer (GL_ARRAY_BUFFER, vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (m.v.size() * sizeof (float)), m.v.data(), GL_STATIC_DRAW);
        attribs();
        glGenBuffers (1, &ibo);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (m.idx.size() * sizeof (std::uint16_t)), m.idx.data(), GL_STATIC_DRAW);
        return (int) m.idx.size();
    };

    // The worms: one tube.
    wormIndices = mesh (W::tubeMesh (6, 9), vaoWorm, vboWorm, iboWorm, []
    {
        glEnableVertexAttribArray (locPos); glVertexAttribPointer (locPos, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    });
    glBindVertexArray (0);

    // The flora: five meshes, one vertex array each.
    const auto floraMeshes = W::floraMeshes();
    for (size_t k = 0; k < floraMeshes.size(); ++k)
    {
        auto& f = floraMesh[k];
        f.count = mesh (floraMeshes[k], f.vao, f.vbo, f.ibo, []
        {
            for (const auto& [loc, size, at] : { std::array<int, 3> { (int) locPos, 3, 0 }, { (int) locNrm, 3, 12 }, { (int) locCol, 3, 24 },
                                                 { (int) locUV, 2, 36 }, { (int) locFlag, 1, 44 } })
            {
                glEnableVertexAttribArray ((GLuint) loc);
                glVertexAttribPointer ((GLuint) loc, size, GL_FLOAT, GL_FALSE, 48, offset (at));
            }
        });
        glBindVertexArray (0);
    }

    // The jellyfish: the bell grid and the tendril ribbons, each with the shared instance buffer (16 floats).
    jellyData.assign ((size_t) W::jellyCount * 16, 0.0f);
    glGenBuffers (1, &vboJelly);
    glBindBuffer (GL_ARRAY_BUFFER, vboJelly);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (jellyData.size() * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
    const auto instances = [this]
    {
        glBindBuffer (GL_ARRAY_BUFFER, vboJelly);
        for (const auto& [loc, size, at] : { std::array<int, 3> { (int) locPos, 3, 0 }, { (int) locScale, 1, 12 }, { (int) locCol, 3, 16 },
                                             { (int) locPhase, 1, 28 }, { (int) locAxis, 3, 32 }, { (int) locAlpha, 1, 44 },
                                             { (int) locGlow, 3, 48 }, { (int) locTend, 1, 60 } })
        {
            glEnableVertexAttribArray ((GLuint) loc);
            glVertexAttribPointer ((GLuint) loc, size, GL_FLOAT, GL_FALSE, 64, offset (at));
            glVertexAttribDivisor ((GLuint) loc, 1);
        }
    };
    bellIndices = mesh (W::bellMesh (28, 12), vaoBell, vboBell, iboBell, [&]
    {
        glEnableVertexAttribArray (locUV); glVertexAttribPointer (locUV, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        instances();
    });
    glBindVertexArray (0);
    tendrilIndices = mesh (W::tendrilMesh (10, 14), vaoTendril, vboTend, iboTend, [&]
    {
        glEnableVertexAttribArray (locTSI); glVertexAttribPointer (locTSI, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        instances();
    });
    glBindVertexArray (0);

    // The passes made of the vertex's index alone: the water, the post pass, the cabin, the sprite, the particles.
    vaoEmpty = gpu::attributeless (vboEmpty);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

bool UnderwaterGl::ensureFrame (int w, int h, int cw, int ch)
{
    // The scene's colour and depth as textures, read by the post pass.
    if (fbo == 0 || w != fbW || h != fbH)
    {
        if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); glGenTextures (1, &fboDepth); }
        glBindTexture (GL_TEXTURE_2D, fboColour);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, fboDepth);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
        glBindFramebuffer (GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fboDepth, 0);
        if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            fbW = fbH = 0;
            return false;
        }
        fbW = w; fbH = h;
    }
    // The finished frame, at the frame's own pixels: what is blitted out and read back.
    if (fboOut == 0 || cw != outW || ch != outH)
    {
        if (fboOut == 0) { glGenFramebuffers (1, &fboOut); glGenRenderbuffers (1, &fboOutColour); }
        glBindRenderbuffer (GL_RENDERBUFFER, fboOutColour); glRenderbufferStorage (GL_RENDERBUFFER, GL_RGBA8, cw, ch);
        glBindRenderbuffer (GL_RENDERBUFFER, 0);
        glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
        glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fboOutColour);
        if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            outW = outH = 0;
            return false;
        }
        outW = cw; outH = ch;
    }
    return true;
}

bool UnderwaterGl::ensurePostFrame (int w, int h)
{
    // Below the top rung: the post pass's frame at the scene's pixels, scaled up to the finished one (filtered).
    if (fboPost != 0 && w == postW && h == postH)
        return true;
    if (fboPost == 0) { glGenFramebuffers (1, &fboPost); glGenTextures (1, &fboPostColour); }
    glBindTexture (GL_TEXTURE_2D, fboPostColour);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fboPost);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboPostColour, 0);
    if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        postW = postH = 0;
        return false;
    }
    postW = w; postH = h;
    return true;
}

void UnderwaterGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void UnderwaterGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (post.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, jsRoundInt ((double) w * scale)), rh = std::max (2, jsRoundInt ((double) h * scale));
    if (! ensureFrame (rw, rh, w, h))
        return;
    // The rung: what is drawn less of below the top, eased by the time since the last frame.
    const double now = gpu::milliseconds();
    quality.follow (scale, lastFrameMs < 0 ? 0.0 : (now - lastFrameMs) * 0.001);
    lastFrameMs = now;
    if (quality.level >= 1 && ! ensurePostFrame (rw, rh))
        return;

    // The timer: queries read back a few frames late so nothing waits — or, without them, one frame in sixty
    // finished and clocked. On the web, neither.
    const bool probe = ! gpu::web && ! queriesWork && (frameCounter % 60) == 0;
    ++frameCounter;
    if (queriesWork)
    {
        while (queriesPending > 0)
        {
            const auto oldest = query[(size_t) ((queryNext - queriesPending + (int) query.size()) % (int) query.size())];
            GLuint available = 0;
            glGetQueryObjectuiv (oldest, GL_QUERY_RESULT_AVAILABLE, &available);
            if (! available) break;
            noteCost ((float) gpu::timerNanoseconds (oldest) * 1.0e-6f);
            --queriesPending;
        }
        if (queriesPending < (int) query.size())
        {
            gpu::beginTimer (query[(size_t) queryNext]);
            queryNext = (queryNext + 1) % (int) query.size();
            ++queriesPending;
        }
    }
    else if (probe)
    {
        glFinish();
        probeStart = gpu::milliseconds();
    }

    for (int i = 0; i < repeats; ++i)
        drawScene (rw, rh, w, h);

    if (queriesWork)
        gpu::endTimer();
    else if (probe)
    {
        glFinish();
        noteCost ((float) (gpu::milliseconds() - probeStart));
    }

    glBindFramebuffer (GL_READ_FRAMEBUFFER, fboOut);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer (0, 0, w, h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer (GL_FRAMEBUFFER, target);
}

bool UnderwaterGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (fboOut == 0 || outW == 0)
        return false;
    rgba.resize ((size_t) outW * (size_t) outH * 4);
    glBindFramebuffer (GL_READ_FRAMEBUFFER, fboOut);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    w = outW; h = outH;
    return true;
}

//==============================================================================
void UnderwaterGl::drawScene (int rw, int rh, int cw, int ch)
{
    const auto& s = world.state();
    const auto& L = light();
    const double silt = s.silt;
    const V3 fogLevel = mix3 ({ 0.06, 0.40, 0.44 }, { 0.08, 0.38, 0.41 }, silt);
    const V3 fogDeep { 0.02, 0.07, 0.16 }, fogUp { 0.46, 0.84, 0.80 }, sunGlow { 0.65, 0.55, 0.35 }, shaftCol { 0.55, 0.85, 0.80 };
    const double fogK = 1.0 / 26;

    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const std::array<float, 2> camFrac { (float) (s.x - ccx), (float) (s.z - ccz) };
    const std::array<float, 3> camRel { camFrac[0], (float) s.camY, camFrac[1] };
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double aspect = (double) rw / rh;
    // a constant width of view (the lens's), the height from the aspect; the centre where the window wants it
    const double th = tanH, tv = th / aspect, zn = zNear, zf = zFar, sh = shear;
    std::array<float, 16> Pm {};
    Pm[0] = (float) (1 / th); Pm[5] = (float) (1 / tv); Pm[10] = (float) (-(zf + zn) / (zf - zn)); Pm[11] = -1; Pm[14] = (float) (-2 * zf * zn / (zf - zn));
    Pm[9] = (float) -sh;
    const auto basis = W::cameraBasis (s.heading, s.pitch);
    std::array<float, 16> V {};
    {
        const double cx = camRel[0], cy = camRel[1], cz = camRel[2];
        const auto& r = basis.r; const auto& u = basis.u; const auto& f = basis.f;
        V[0] = (float) r[0]; V[4] = (float) r[1]; V[8] = (float) r[2];     V[12] = (float) -(r[0] * cx + r[1] * cy + r[2] * cz);
        V[1] = (float) u[0]; V[5] = (float) u[1]; V[9] = (float) u[2];     V[13] = (float) -(u[0] * cx + u[1] * cy + u[2] * cz);
        V[2] = (float) -f[0]; V[6] = (float) -f[1]; V[10] = (float) -f[2]; V[14] = (float) (f[0] * cx + f[1] * cy + f[2] * cz);
        V[15] = 1;
    }
    const auto VP = multiply (Pm, V);
    const double off0x = fract (ccx / 420), off0z = fract (ccz / 420), off1x = fract (ccx / 96), off1z = fract (ccz / 96);
    const double off2x = fract (ccx / 27), off2z = fract (ccz / 27);
    const double det0x = fract (ccx / 2.6), det0z = fract (ccz / 2.6), det1x = fract (ccx / 0.7), det1z = fract (ccz / 0.7);
    const double cau1x = fract (ccx / cau1), cau1z = fract (ccz / cau1), cau2x = fract (ccx / cau2), cau2z = fract (ccz / cau2);
    const double cauWx = fract (ccx / cauW), cauWz = fract (ccz / cauW);
    // The shafts' offsets: the cell projected on the plane's basis (the cell has no y).
    const double sa = ccx * L.a[0] + ccz * L.a[2], sb = ccx * L.b[0] + ccz * L.b[2];
    const double shaft1a = fract (sa / 16), shaft1b = fract (sb / 16), shaft2a = fract (sa / 6.5), shaft2b = fract (sb / 6.5);
    const double rayWay = s.x * L.a[0] + s.z * L.a[2] + 0.4 * (s.x * L.b[0] + s.z * L.b[2]);
    const double rayDrift = fract (rayWay / 60), rayDrift2 = fract (rayWay / 120);   // each wraps whole on its own
    const double time = s.time;                // never wrapped: the sines are not periodic over a wrap
    // the bathyscaphe's two boom lamps: 1.3 m out, 0.55 m over and 1.06 m ahead of the pilot's eye, aimed ahead
    // and 24 degrees down
    const V3 lampDir = W::normalised ({ basis.f[0] - basis.u[0] * 0.45, basis.f[1] - basis.u[1] * 0.45, basis.f[2] - basis.u[2] * 0.45 });
    const auto lampAt = [&basis] (double side)
    {
        V3 p {};
        for (size_t i = 0; i < 3; ++i) p[i] = basis.r[i] * side * 1.3 + basis.u[i] * 0.55 + basis.f[i] * 1.06;
        return p;
    };
    const V3 lamp0 = lampAt (-1), lamp1 = lampAt (1);

    const auto set3 = [] (const Program& p, const char* name, const V3& v) { glUniform3f (p.uniform (name), (float) v[0], (float) v[1], (float) v[2]); };
    const auto set2 = [] (const Program& p, const char* name, double a, double b) { glUniform2f (p.uniform (name), (float) a, (float) b); };
    const auto set1 = [] (const Program& p, const char* name, double a) { glUniform1f (p.uniform (name), (float) a); };
    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uHeight"), 0);
        set2 (p, "uCamCell", ccx, ccz);
        glUniform2f (p.uniform ("uCamFrac"), camFrac[0], camFrac[1]);
        set2 (p, "uOff0", off0x, off0z); set2 (p, "uOff1", off1x, off1z); set2 (p, "uOff2", off2x, off2z);
        glUniformMatrix4fv (p.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    };
    const auto setLight = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uCaustic"), 2);
        set3 (p, "uLight", L.dir); set3 (p, "uShaftA", L.a); set3 (p, "uShaftB", L.b);
        set3 (p, "uFogLevel", fogLevel); set3 (p, "uFogDeep", fogDeep); set3 (p, "uFogUp", fogUp);
        set3 (p, "uSunGlow", sunGlow); set3 (p, "uShaftCol", shaftCol);
        set1 (p, "uFogK", fogK); set1 (p, "uSilt", silt); set1 (p, "uTime", time); set1 (p, "uCauGain", world.causticGain());
        set2 (p, "uCauOff1", cau1x, cau1z); set2 (p, "uCauOff2", cau2x, cau2z); set2 (p, "uCauOffW", cauWx, cauWz);
        set2 (p, "uShaftOff1", shaft1a, shaft1b); set2 (p, "uShaftOff2", shaft2a, shaft2b);
        set1 (p, "uRayDrift", rayDrift); set1 (p, "uRayDrift2", rayDrift2);
        glUniform3f (p.uniform ("uCamRel"), camRel[0], camRel[1], camRel[2]);
        set3 (p, "uLampPos0", lamp0); set3 (p, "uLampPos1", lamp1); set3 (p, "uLampDir", lampDir);
        set1 (p, "uLampOn", 1.0);
        set2 (p, "uHitT", std::min (s.hitT, 1e4), s.hitS);
    };

    // The jellyfish moved, and those that can be seen packed in their order (far to near), each with its fade: one
    // wholly outside the view — the frustum's planes from VP's rows, its reach round it — draws nothing, nor one
    // faded out. Uploaded before the scene's pass begins, into a buffer let go of first, so nothing waits for the GPU.
    world.updateJellies (s.dt);
    int jellyDrawn = 0;
    {
        std::array<std::array<double, 4>, 6> planes {};
        for (int k = 0; k < 3; ++k)
            for (int c = 0; c < 4; ++c)
            {
                planes[(size_t) (2 * k)][(size_t) c] = (double) VP[(size_t) (c * 4 + 3)] + VP[(size_t) (c * 4 + k)];
                planes[(size_t) (2 * k + 1)][(size_t) c] = (double) VP[(size_t) (c * 4 + 3)] - VP[(size_t) (c * 4 + k)];
            }
        const auto seen = [&planes] (const float* p, double reach)
        {
            for (const auto& q : planes)
                if (q[0] * p[0] + q[1] * p[1] + q[2] * p[2] + q[3] < -reach * std::sqrt (q[0] * q[0] + q[1] * q[1] + q[2] * q[2]))
                    return false;
            return true;
        };
        for (const auto& j : world.jellies())
        {
            const float alpha = W::Quality::thinned (j.id) ? (float) (1 - quality.thin) : 1.0f;
            float* o = jellyData.data() + (size_t) jellyDrawn * 16;
            o[0] = (float) (camRel[0] + j.rel[0]); o[1] = (float) (camRel[1] + j.rel[1]); o[2] = (float) (camRel[2] + j.rel[2]);
            if (alpha <= 0.0f || ! seen (o, W::jellyReach (j)))
                continue;
            o[3] = (float) j.scale;
            o[4] = (float) j.col[0]; o[5] = (float) j.col[1]; o[6] = (float) j.col[2];
            o[7] = (float) std::fmod (j.phase, 2 * pi);
            o[8] = (float) j.outAxis[0]; o[9] = (float) j.outAxis[1]; o[10] = (float) j.outAxis[2];
            o[11] = alpha;
            o[12] = (float) j.glow[0]; o[13] = (float) j.glow[1]; o[14] = (float) j.glow[2];
            o[15] = (float) j.tend;
            ++jellyDrawn;
        }
        glBindBuffer (GL_ARRAY_BUFFER, vboJelly);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (jellyData.size() * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
        if (jellyDrawn > 0)
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) ((size_t) jellyDrawn * 16 * sizeof (float)), jellyData.data());
        glBindBuffer (GL_ARRAY_BUFFER, 0);
    }

    // What the cabin's panel covers is never seen: while it is drawn, laid into the depth at its nearest before
    // anything, so nothing of the scene is shaded under it, and skipped by the post pass.
    const bool masked = drawsCabin() && maskHolds (rw, rh);
    const bool postLow = quality.level >= 1 && fboPost != 0;

    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (0, 0, rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE);
    glDepthMask (GL_TRUE);
    glClearColor (0, 0, 0, 1);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (masked)
    {
        glUseProgram (cover.id); glBindVertexArray (vaoCover);
        glColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthFunc (GL_ALWAYS);
        glDrawArrays (GL_TRIANGLES, 0, coverVertices);
        glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthFunc (GL_LEQUAL);
    }
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texCaustic);

    // The floor.
    {
        glUseProgram (floor.id); glBindVertexArray (vaoFloor);
        setCommon (floor); setLight (floor);
        glUniform1i (floor.uniform ("uDetail"), 1);
        set2 (floor, "uDetOff0", det0x, det0z); set2 (floor, "uDetOff1", det1x, det1z);
        const double half = N / 2 * cellSize;
        const double ox = std::floor ((s.x + fx * 0.35 * half) / cellSize) * cellSize - half, oz = std::floor ((s.z + fz * 0.35 * half) / cellSize) * cellSize - half;
        set2 (floor, "uLvlOrigin", ox - ccx, oz - ccz);
        set1 (floor, "uCellSize", cellSize);
        glDrawElements (GL_TRIANGLES, floorIndices, GL_UNSIGNED_INT, nullptr);
    }
    // The rocks and the spires.
    {
        glUseProgram (rock.id); glBindVertexArray (vaoRock);
        setCommon (rock); setLight (rock);
        for (const auto& [R, tall] : { std::pair<Field, double> { rocks, 0.0 }, { spires, 1.0 } })
        {
            const double shift = R.w * R.cell * 0.3;
            const int bx = (int) (std::floor ((s.x + fx * shift) / R.cell) - R.w / 2), bz = (int) (std::floor ((s.z + fz * shift) / R.cell) - R.w / 2);
            glUniform2i (rock.uniform ("uRockBase"), bx, bz); glUniform1i (rock.uniform ("uRockW"), R.w);
            glUniform1i (rock.uniform ("uRockM"), jsRoundInt (W::worldPeriod / R.cell));
            set1 (rock, "uRockCell", R.cell); set1 (rock, "uRockProb", R.prob); set2 (rock, "uRockSize", R.smin, R.smax);
            set1 (rock, "uTall", tall);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 480, R.w * R.w);
        }
    }
    // The worms.
    {
        glUseProgram (worm.id); glBindVertexArray (vaoWorm);
        setCommon (worm); setLight (worm);
        const double shift = wormW * wormCell * 0.3;
        const int bx = (int) (std::floor ((s.x + fx * shift) / wormCell) - wormW / 2), bz = (int) (std::floor ((s.z + fz * shift) / wormCell) - wormW / 2);
        glUniform2i (worm.uniform ("uWormBase"), bx, bz); glUniform1i (worm.uniform ("uWormW"), wormW);
        glUniform1i (worm.uniform ("uWormM"), jsRoundInt (W::worldPeriod / wormCell));
        set1 (worm, "uWormCell", wormCell);
        glDrawElementsInstanced (GL_TRIANGLES, wormIndices, GL_UNSIGNED_SHORT, nullptr, wormW * wormW);
    }
    // The flora: five instanced kinds in their meadows.
    {
        glUseProgram (flora.id);
        setCommon (flora); setLight (flora);
        set1 (flora, "uSpeed", s.speed);
        for (const auto& F : floraKinds)
        {
            const double shift = F.w * F.cell * 0.3;
            const int bx = (int) (std::floor ((s.x + fx * shift) / F.cell) - F.w / 2), bz = (int) (std::floor ((s.z + fz * shift) / F.cell) - F.w / 2);
            glBindVertexArray (floraMesh[(size_t) F.kind].vao);
            glUniform2i (flora.uniform ("uFloraBase"), bx, bz); glUniform1i (flora.uniform ("uFloraW"), F.w);
            glUniform1i (flora.uniform ("uFloraM"), jsRoundInt (W::worldPeriod / F.cell));
            set1 (flora, "uFloraCell", F.cell); set1 (flora, "uFloraKind", F.kind); set1 (flora, "uFloraDensity", F.density);
            set2 (flora, "uFloraSize", F.smin, F.smax);
            glDrawElementsInstanced (GL_TRIANGLES, floraMesh[(size_t) F.kind].count, GL_UNSIGNED_SHORT, nullptr, F.w * F.w);
        }
    }

    // The water, where nothing was drawn (depth == 1): before anything is blended, so nothing blended is lost.
    {
        glUseProgram (water.id); glBindVertexArray (vaoEmpty);
        setLight (water);
        set3 (water, "uCamFwd", basis.f); set3 (water, "uCamRight", basis.r); set3 (water, "uCamUp", basis.u);
        set2 (water, "uTanHalf", th, tv); set1 (water, "uShear", sh);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // The jellyfish, far to near, blended (premultiplied), over the floor and the water, writing no depth.
    glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glDepthMask (GL_FALSE);
    // ...but first another bathyscaphe, if one is passing: the atlas cell for the heading it shows the viewer, an
    // upright quad facing the camera, fogged; then its lamps' halos and beams, added.
    const auto& ps = s.sub;
    const int cells = (int) sprites.cells.size();
    if (ps.on && ps.vis > 0.003 && texAtlas != 0 && cells > 0 && sprites.scale > 0)
    {
        double dx = ps.x - s.x, dz = ps.z - s.z;
        dx -= Js::round (dx / W::worldPeriod) * W::worldPeriod; dz -= Js::round (dz / W::worldPeriod) * W::worldPeriod;
        double tl = Js::hypot (dx, dz);
        if (! (tl > 0)) tl = 1;                                           // the prototype's `|| 1`
        const double vx = dx / tl, vz = dz / tl;                          // the view toward it, level
        const V3 right { -vz, 0, vx }, up { 0, 1, 0 };
        const double hDeg = Js::atan2 (ps.fx * right[0] + ps.fz * right[2], -(ps.fx * vx + ps.fz * vz)) * 180 / pi;
        // the two nearest headings, blended by how far between them: no jump from one picture to the next
        const double fh = std::fmod (std::fmod (hDeg, 360) + 360, 360) / (360.0 / cells);
        const int k0 = (int) std::floor (fh) % cells, k1 = (k0 + 1) % cells;
        const double mixK = fh - std::floor (fh);
        const double hw = sprites.cell[0] / sprites.scale / 2, hh = sprites.cell[1] / sprites.scale / 2;
        const V3 c { camRel[0] + dx, ps.y, camRel[2] + dz };
        const int gc = sprites.grid[0], gr = sprites.grid[1];
        const auto rect = [gc, gr] (const Program& p, const char* name, int k)
        {
            glUniform4f (p.uniform (name), (float) ((double) (k % gc) / gc), (float) (std::floor ((double) k / gc) / gr),
                         (float) ((double) (k % gc + 1) / gc), (float) ((std::floor ((double) k / gc) + 1) / gr));
        };
        glUseProgram (sprite.id); glBindVertexArray (vaoEmpty);
        setCommon (sprite); setLight (sprite);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, texAtlas);
        glUniform1i (sprite.uniform ("uAtlas"), 5); set1 (sprite, "uLightK", 1.35); set1 (sprite, "uFade", ps.vis);
        set3 (sprite, "uCenter", c);
        set3 (sprite, "uAxisA", { right[0] * hw, right[1] * hw, right[2] * hw }); set3 (sprite, "uAxisB", { up[0] * hh, up[1] * hh, up[2] * hh });
        rect (sprite, "uRect0", k0); rect (sprite, "uRect1", k1); set1 (sprite, "uMix", mixK);
        glDrawArrays (GL_TRIANGLES, 0, 6);
        // the lamps: a halo at each lens (larger and brighter when it looks at the viewer), a beam along it
        glUseProgram (glow.id);
        setCommon (glow); setLight (glow);
        glBlendFunc (GL_ONE, GL_ONE);
        const V3 warm { 1.0, 0.88, 0.7 };
        for (const auto& [k, wk] : { std::pair<int, double> { k0, 1 - mixK }, { k1, mixK } })
            for (const auto& lamp : sprites.cells[(size_t) k])
            {
                const double ox = (lamp.x - sprites.cell[0] / 2) / sprites.scale, oy = (sprites.cell[1] / 2 - lamp.y) / sprites.scale;
                const V3 at { c[0] + right[0] * ox - vx * 0.4, c[1] + oy, c[2] + right[2] * ox - vz * 0.4 };
                const double tw = std::max (lamp.toward, 0.0);
                const double hr = 0.35 + 0.55 * tw;
                set1 (glow, "uMode", 0);
                set3 (glow, "uCenter", at);
                set3 (glow, "uAxisA", { right[0] * hr, right[1] * hr, right[2] * hr }); set3 (glow, "uAxisB", { up[0] * hr, up[1] * hr, up[2] * hr });
                set3 (glow, "uColor", { warm[0] * (0.25 + 0.75 * tw) * wk * ps.vis, warm[1] * (0.25 + 0.75 * tw) * wk * ps.vis, warm[2] * (0.25 + 0.75 * tw) * wk * ps.vis });
                glDrawArrays (GL_TRIANGLES, 0, 6);
                const double len = 5.5 * (1 - 0.8 * tw), bx = lamp.dx, by = -lamp.dy;     // the beam in the quad's plane
                const V3 d3 { right[0] * bx, by, right[2] * bx }, q3 { -right[0] * by, bx, -right[2] * by };
                set1 (glow, "uMode", 1);
                set3 (glow, "uCenter", { at[0] + d3[0] * len / 2, at[1] + d3[1] * len / 2, at[2] + d3[2] * len / 2 });
                set3 (glow, "uAxisA", { d3[0] * len / 2, d3[1] * len / 2, d3[2] * len / 2 }); set3 (glow, "uAxisB", { q3[0] * 0.5, q3[1] * 0.5, q3[2] * 0.5 });
                set3 (glow, "uColor", { warm[0] * 0.22 * wk * ps.vis, warm[1] * 0.22 * wk * ps.vis, warm[2] * 0.22 * wk * ps.vis });
                glDrawArrays (GL_TRIANGLES, 0, 6);
            }
        glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, 0);
    }
    {
        for (const auto& [p, vao, count] : { std::tuple<const Program*, std::uint32_t, int> { &tendril, vaoTendril, tendrilIndices }, { &bell, vaoBell, bellIndices } })
        {
            glUseProgram (p->id); glBindVertexArray (vao);
            setCommon (*p); setLight (*p);
            set3 (*p, "uCamR", basis.r); set3 (*p, "uCamU", basis.u);
            if (jellyDrawn > 0)
                glDrawElementsInstanced (GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr, jellyDrawn);
        }
    }
    // The particles: the snow, the silt, the bubbles.
    {
        glUseProgram (particle.id); glBindVertexArray (vaoEmpty);
        setCommon (particle); setLight (particle);
        set3 (particle, "uCamR", basis.r); set3 (particle, "uCamU", basis.u); set3 (particle, "uCamF", basis.f);
        const auto draw = [&] (int kind, int count, double amount, const std::array<double, 3>& box, const V3& wrapOff, const std::array<double, 3>& flow, double sizeK)
        {
            if (amount < 0.005)
                return;
            set1 (particle, "uKind", kind); set1 (particle, "uAmount", amount); set1 (particle, "uSizeK", sizeK);
            set1 (particle, "uSpeed", s.speed); set1 (particle, "uTanH", th);
            set2 (particle, "uFwdLevel", fx, fz); set2 (particle, "uRightLevel", -fz, fx);
            set3 (particle, "uBox", box); set3 (particle, "uWrapOff", wrapOff); set3 (particle, "uFlowAcc", flow);
            glDrawArrays (GL_TRIANGLES, 0, count * 6);
        };
        const V3 snowWrap { fract (s.x / W::snowBox[0]), fract (s.camY / W::snowBox[1]), fract (s.z / W::snowBox[2]) };
        draw (0, snowCount, 1.0, W::snowBox, snowWrap, s.snowFlow, 0.075);
        draw (1, siltCount, silt, W::siltBox, { 0, 0, 0 }, s.siltFlow, 0.30);
        draw (2, bubbleCount, s.thrust, W::bubbleBox, { 0, 0, 0 }, s.bubbleFlow, 0.045);
    }
    glDisable (GL_BLEND); glDepthMask (GL_TRUE);

    // The post pass: to the finished frame, at its own pixels — or below the top rung at the scene's, scaled up to it.
    glBindFramebuffer (GL_FRAMEBUFFER, postLow ? fboPost : fboOut);
    glViewport (0, 0, postLow ? rw : cw, postLow ? rh : ch);
    glDisable (GL_DEPTH_TEST);
    glActiveTexture (GL_TEXTURE7); glBindTexture (GL_TEXTURE_2D, texMask);
    const double maskScaleX = mask.block > 0 ? (double) mask.pictureWidth / mask.block : 1, maskScaleY = mask.block > 0 ? (double) mask.pictureHeight / mask.block : 1;
    {
        glUseProgram (post.id); glBindVertexArray (vaoEmpty);
        setLight (post);
        glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, fboColour);
        glActiveTexture (GL_TEXTURE4); glBindTexture (GL_TEXTURE_2D, fboDepth);
        glUniform1i (post.uniform ("uColour"), 3); glUniform1i (post.uniform ("uDepth"), 4);
        set3 (post, "uCamFwd", basis.f); set3 (post, "uCamRight", basis.r); set3 (post, "uCamUp", basis.u);
        set2 (post, "uTanHalf", th, tv); set1 (post, "uShear", sh);
        set1 (post, "uNear", zn); set1 (post, "uFar", zf); set1 (post, "uShaftGain", 3.3);   // a little softer (Oleh, 16.11)
        glUniform1i (post.uniform ("uMask"), 7); set2 (post, "uMaskScale", maskScaleX, maskScaleY); set1 (post, "uMaskOn", masked ? 1 : 0);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE4); glBindTexture (GL_TEXTURE_2D, 0);
    }
    // ...scaled up: by the cabin's pass where it is drawn, else as it is.
    const bool cabin = drawsCabin();
    if (postLow && ! cabin)
    {
        glBindFramebuffer (GL_READ_FRAMEBUFFER, fboPost);
        glBindFramebuffer (GL_DRAW_FRAMEBUFFER, fboOut);
        glBlitFramebuffer (0, 0, rw, rh, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
        glViewport (0, 0, cw, ch);
    }
    // The cabin's panel over it all: the dashboard, its sea layer moved by the caustics and dimmed by the silt.
    // Below the top rung it is drawn flat, faded to that — the dashboard alone, with no sea and no caustics — over the
    // view scaled up from the scene's pixels, in one pass that writes every pixel of the frame.
    if (cabin)
    {
        const Program& pp = ! postLow ? panel : quality.flat >= 1 ? panelOverFlat : panelOver;
        if (postLow)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
            glViewport (0, 0, cw, ch);
            glClear (GL_COLOR_BUFFER_BIT);      // every pixel is written: what was there need not be read back
        }
        glUseProgram (pp.id); glBindVertexArray (vaoEmpty);
        setLight (pp);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, texDash);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, texSea);
        glUniform1i (pp.uniform ("uDash"), 5); glUniform1i (pp.uniform ("uSea"), 6);
        glUniform1i (pp.uniform ("uMask"), 7); set2 (pp, "uMaskScale", maskScaleX, maskScaleY); set1 (pp, "uFlat", quality.flat);
        set1 (pp, "uPanelGain", 1.0 - 0.35 * silt);
        set2 (pp, "uCauScale", 6.0, 3.4);
        // the net slides with the way travelled, slower than on the floor and never in steps
        const double tr = s.travel * 0.08;
        set2 (pp, "uPanelOff1", fract (tr / 2.1), fract (tr * 0.37 / 2.1));
        set2 (pp, "uPanelOff2", fract (-tr * 0.6 / 1.44), fract (tr * 0.8 / 1.44));
        set2 (pp, "uPanelOffW", fract (tr / 19.0), fract (tr * 0.5 / 19.0));
        if (postLow)
        {
            glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, fboPostColour);
            glUniform1i (pp.uniform ("uPost"), 3);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, 0);
        }
        else
        {
            glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            glDisable (GL_BLEND);
        }
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, 0);
    }
    glActiveTexture (GL_TEXTURE7); glBindTexture (GL_TEXTURE_2D, 0);
    glDepthMask (GL_TRUE);
    glBindVertexArray (0);
    glUseProgram (0);
    glActiveTexture (GL_TEXTURE0);
}

} // namespace orbitamp::underwater
