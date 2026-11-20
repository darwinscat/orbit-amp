// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Matched to its prototype (io-gl.js's createIoView and drawScene) uniform for uniform: what drawScene works out on the
// CPU is IoWorld::frame(); the matrices and the light frames here are the JS's doubles where it works in doubles, f32
// where it stores into a Float32Array, the walker's Js for its Math. Built with -ffp-contract=off.

#include "IoGl.h"

#include "../../redgroove/view/WalkerWorld.h"     // Js
#include "../../shaders/Shaders.h"

#include <cmath>
#include <initializer_list>
#include <utility>

namespace orbitamp::io
{

using namespace orbitamp::gpu;

namespace
{
    using W = IoWorld;
    using Js = redgroove::WalkerWorld::Js;
    using V3 = W::V3;
    constexpr double PI = W::pi;

    // The attributes' locations: each program's own names on locations of their own; programs share a location only for
    // names that never meet in one program. aPos is 0 (gpu::program binds it there).
    constexpr GLuint locPos = 0, locGrid = 0, locNrm = 1, locUV = 2, locAcross = 2, locCol = 3, locMat = 3, locKind = 4, locFlag = 4,
                     locTex = 5, locI0 = 6, locI1 = 7, locR0 = 6, locR1 = 7, locE0 = 1, locE1 = 2, locE2 = 3, locV0 = 1, locV1 = 2, locV2 = 3;
    const std::vector<gpu::Attribute> attributes {
        { "aGrid", locGrid }, { "aNrm", locNrm }, { "aUV", locUV }, { "aAcross", locAcross }, { "aCol", locCol }, { "aMat", locMat },
        { "aKind", locKind }, { "aFlag", locFlag }, { "aTex", locTex }, { "aI0", locI0 }, { "aI1", locI1 }, { "aR0", locR0 },
        { "aR1", locR1 }, { "aE0", locE0 }, { "aE1", locE1 }, { "aE2", locE2 }, { "aV0", locV0 }, { "aV1", locV1 }, { "aV2", locV2 } };

    // io-gl.js's constants
    struct Level { double cell; int n; };
    constexpr std::array<Level, 4> levels { { { 1.6, 96 }, { 4.8, 96 }, { 14.4, 96 }, { 43.2, 112 } } };
    constexpr double zn = 0.5, zf = 20000.0, farCut = W::farCut;
    constexpr int sh0N = 1536, sh1N = 1024;
    constexpr double sh0Half = W::sh0Half, sh0Ahead = W::sh0Ahead, sh1Half = 1750.0, sh1Ahead = 1600.0;
    constexpr int plumeP = 176, fountP = 180, puffP = 260, snowN = 6000, maxEmit = W::maxEmit, maxRocks = W::maxRocks, maxStruct = 256;
    constexpr double bandAzTurns = 0.31, bandTilt = 58 * PI / 180;

    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double clamp (double v, double lo, double hi) noexcept { return std::min (hi, std::max (lo, v)); }
    inline float f32 (double v) noexcept { return (float) v; }
    inline const void* offset (int bytes) noexcept { return reinterpret_cast<const void*> ((std::uintptr_t) bytes); }
    V3 norm3 (const V3& v) noexcept { const double h = Js::hypot (v[0], v[1], v[2]), l = h > 0 ? h : 1; return { v[0] / l, v[1] / l, v[2] / l }; }
    V3 cross3 (const V3& a, const V3& b) noexcept { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    double dot3 (const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    double lum3 (const V3& c) noexcept { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }

    /** a * b, column-major, as the prototype multiplies its Float32Arrays: in doubles, kept as f32. */
    std::array<float, 16> multiply (const std::array<float, 16>& a, const std::array<float, 16>& b) noexcept
    {
        std::array<float, 16> o {};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += (double) a[(size_t) (k * 4 + r)] * (double) b[(size_t) (c * 4 + k)];
                o[(size_t) (c * 4 + r)] = (float) s;
            }
        return o;
    }

    /** A yaw, a scale and a lean (per metre up, about x and z) as a column-major mat3 (y up): the hardware's frame. */
    std::array<float, 9> frameMat (double yaw, double k, double lx, double lz) noexcept
    {
        const double c = Js::cos (yaw), s = Js::sin (yaw);
        return { f32 (c * k), 0, f32 (s * k), f32 (lx * k), f32 (k), f32 (lz * k), f32 (-s * k), 0, f32 (c * k) };
    }

    /** A texture from a picture: straight or premultiplied as the prototype's browser uploads it, with mips or not. */
    GLuint pictureTexture (const IoGl::Picture& p, bool premultiplied, bool mips)
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

    void attrib (GLuint loc, int size, GLenum type, bool norm, int stride, int at, bool instanced = false)
    {
        glEnableVertexAttribArray (loc);
        glVertexAttribPointer (loc, size, type, norm ? GL_TRUE : GL_FALSE, stride, offset (at));
        if (instanced)
            glVertexAttribDivisor (loc, 1);
    }

    template <size_t N> void uniform (int loc, const std::array<float, N>& v)
    {
        if constexpr (N == 2) glUniform2f (loc, v[0], v[1]);
        else if constexpr (N == 3) glUniform3f (loc, v[0], v[1], v[2]);
        else glUniform4f (loc, v[0], v[1], v[2], v[3]);
    }
    void uniform3 (int loc, const V3& v) { glUniform3f (loc, f32 (v[0]), f32 (v[1]), f32 (v[2])); }
}

//==============================================================================
int IoGl::Program::u (const char* name) const
{
    if (const auto it = at.find (name); it != at.end())
        return it->second;
    return at[name] = glGetUniformLocation (id, name);
}

bool IoGl::compile (Program& p, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written)
{
    std::string log;
    p.at.clear();
    p.id = gpu::program (gpu::header() + shaders::source (vertexPath), gpu::header() + shaders::source (fragmentPath, written), log, attributes);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("io: " + trouble);
        return false;
    }
    return true;
}

void IoGl::setSkip (const std::string& list)
{
    skip.clear();
    size_t at = 0;
    while (at <= list.size())
    {
        const size_t comma = list.find (',', at);
        const auto item = list.substr (at, comma == std::string::npos ? std::string::npos : comma - at);
        if (! item.empty()) skip.push_back (item);
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
}

bool IoGl::skips (const char* what) const noexcept
{
    return std::find (skip.begin(), skip.end(), what) != skip.end();
}

bool IoGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("io: " + trouble);
        return false;
    }
    if (! world.isBuilt())          // made by its drive, settled and looking down already: as it is
        world.build();

    if (! compile (ground, "io/ground.vert", "io/ground.frag", "near ground", { { "hole", "" } })
        || ! compile (groundH, "io/ground.vert", "io/ground.frag", "ground", { { "hole", "#define HOLE 1\n" } })
        || ! compile (groundF, "io/ground.vert", "io/ground.frag", "far ground", { { "hole", "#define HOLE 1\n#define FARCUT 1\n" } })
        || ! compile (groundSh, "io/ground.vert", "io/depthhole.frag", "ground's shadow")
        || ! compile (rock, "io/rock.vert", "io/rock.frag", "rock")
        || ! compile (rockSh, "io/rock.vert", "io/depth.frag", "rock's shadow")
        || ! compile (structP, "io/struct.vert", "io/struct.frag", "hardware")
        || ! compile (structSh, "io/struct.vert", "io/depth.frag", "hardware's shadow")
        || ! compile (mesh, "io/mesh.vert", "io/mesh.frag", "mesh")
        || ! compile (meshSh, "io/mesh.vert", "io/depth.frag", "mesh's shadow")
        || ! compile (sky, "io/sky.vert", "io/sky.frag", "sky")
        || ! compile (plume, "io/plume.vert", "io/plume.frag", "plume")
        || ! compile (pvol, "io/pvol.vert", "io/pvol.frag", "plume's body")
        || ! compile (snow, "io/snow.vert", "io/snow.frag", "snow")
        || ! compile (post, "io/post.vert", "io/post.frag", "post"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeBuffers();
    makeArt();
    world.frame();      // the hardware round the rover listed, as the prototype's first frames leave its lists

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

void IoGl::destroy()
{
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    for (auto& [n, g] : grids) { del (g.vbo, glDeleteBuffers); del (g.ibo, glDeleteBuffers); del (g.vao, glDeleteVertexArrays); }
    grids.clear();
    for (auto* b : { &rocks[0], &rocks[1] }) { del (b->vbo, glDeleteBuffers); del (b->inst, glDeleteBuffers); del (b->vao, glDeleteVertexArrays); }
    for (auto& b : structs) { del (b.vbo, glDeleteBuffers); del (b.inst, glDeleteBuffers); del (b.vao, glDeleteVertexArrays); }
    for (auto& [v, m] : meshes) { del (m.vbo, glDeleteBuffers); del (m.ibo, glDeleteBuffers); del (m.vao, glDeleteVertexArrays); }
    meshes.clear();
    for (auto* e : { &emitBuf[0], &emitBuf[1], &emitBuf[2], &volBuf }) { del (e->inst, glDeleteBuffers); del (e->vao, glDeleteVertexArrays); }
    del (vboEmpty, glDeleteBuffers); del (vaoEmpty, glDeleteVertexArrays);
    for (auto* t : { &texBase, &texDetail, &texNoise, &texCells, &texRoad, &texFeat, &texHist, &texDash, &texDashDark, &fboColour, &sh0.tex, &sh1.tex })
        del (*t, glDeleteTextures);
    del (fboDepth, glDeleteRenderbuffers); del (fboOutColour, glDeleteRenderbuffers);
    del (fbo, glDeleteFramebuffers); del (fboOut, glDeleteFramebuffers); del (sh0.fbo, glDeleteFramebuffers); del (sh1.fbo, glDeleteFramebuffers);
    sh0 = {}; sh1 = {};
    sh0Frame.reset(); sh1Frame.reset(); sh1Sun.reset(); sh0Age = sh1Age = 99;
    fbW = fbH = outW = outH = subW = subH = 0;
    outerKept = false;
    glassBox.reset();
    Q = {};
    lastFrameMs = -1.0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &ground, &groundH, &groundF, &groundSh, &rock, &rockSh, &structP, &structSh, &mesh, &meshSh, &sky, &plume, &pvol, &snow, &post })
    {
        if (p->id != 0) glDeleteProgram (p->id);
        p->id = 0;
        p->at.clear();
    }
}

void IoGl::makeTextures()
{
    const auto tex = [] (std::uint32_t& id, int w, int h, GLint ifmt, GLenum fmt, GLenum type, const void* data, GLint filter, GLint wrap, bool mips)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glTexImage2D (GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, data);
        if (mips) glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    };
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    tex (texBase, W::texSize, W::texSize, GL_RGBA16F, GL_RGBA, GL_FLOAT, world.base().data(), GL_LINEAR, GL_REPEAT, false);
    tex (texDetail, W::detailSize, W::detailSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, world.detail().data(), GL_LINEAR, GL_REPEAT, true);
    tex (texNoise, W::noiseSize, W::noiseSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, world.noise().data(), GL_LINEAR, GL_REPEAT, false);
    tex (texCells, W::cellN, W::cellN, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, world.cells().data(), GL_NEAREST, GL_REPEAT, false);
    tex (texRoad, W::roadN, W::roadN, GL_RGBA16F, GL_RGBA, GL_FLOAT, world.roadMap().data(), GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    tex (texFeat, W::featN, W::featN, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, world.featureMap().data(), GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    tex (texHist, W::histN, W::histRows, GL_R8, GL_RED, GL_UNSIGNED_BYTE, world.history().data(), GL_NEAREST, GL_CLAMP_TO_EDGE, false);
    glBindTexture (GL_TEXTURE_2D, 0);

    // THE SUN'S SHADOW MAPS: two depth textures, compared on lookup (bilinear: a sharp edge, no stair)
    const auto shadowMap = [] (Shadow& s, int N)
    {
        s.N = N;
        glGenTextures (1, &s.tex);
        glBindTexture (GL_TEXTURE_2D, s.tex);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, N, N, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindTexture (GL_TEXTURE_2D, 0);
        glGenFramebuffers (1, &s.fbo);
        glBindFramebuffer (GL_FRAMEBUFFER, s.fbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s.tex, 0);
        const GLenum none = GL_NONE;
        glDrawBuffers (1, &none);
        glReadBuffer (GL_NONE);
        s.ok = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer (GL_FRAMEBUFFER, 0);
    };
    shadowMap (sh0, sh0N);
    shadowMap (sh1, sh1N);
}

IoGl::Grid& IoGl::gridFor (int NL)
{
    if (const auto it = grids.find (NL); it != grids.end())
        return it->second;
    Grid g;
    std::vector<float> grid ((size_t) ((NL + 1) * (NL + 1) * 2));
    for (int j = 0; j <= NL; ++j)
        for (int i = 0; i <= NL; ++i) { grid[(size_t) ((j * (NL + 1) + i) * 2)] = (float) i; grid[(size_t) ((j * (NL + 1) + i) * 2 + 1)] = (float) j; }
    std::vector<std::uint16_t> idx ((size_t) (NL * NL * 6));
    for (int j = 0; j < NL; ++j)
        for (int i = 0; i < NL; ++i)
        {
            const size_t o = (size_t) ((j * NL + i) * 6);
            const int v = j * (NL + 1) + i;
            idx[o] = (std::uint16_t) v; idx[o + 1] = (std::uint16_t) (v + 1); idx[o + 2] = (std::uint16_t) (v + NL + 2);
            idx[o + 3] = (std::uint16_t) v; idx[o + 4] = (std::uint16_t) (v + NL + 2); idx[o + 5] = (std::uint16_t) (v + NL + 1);
        }
    glGenVertexArrays (1, &g.vao);
    glBindVertexArray (g.vao);
    glGenBuffers (1, &g.vbo);
    glBindBuffer (GL_ARRAY_BUFFER, g.vbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (grid.size() * 4), grid.data(), GL_STATIC_DRAW);
    attrib (locGrid, 2, GL_FLOAT, false, 0, 0);
    glGenBuffers (1, &g.ibo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, g.ibo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (idx.size() * 2), idx.data(), GL_STATIC_DRAW);
    glBindVertexArray (0);
    g.count = (int) idx.size();
    return grids[NL] = g;
}

void IoGl::makeBuffers()
{
    // THE GROUND LATTICES
    for (const auto& L : levels)
        gridFor (L.n);

    // THE ROCKS: one mesh, instanced (8 floats: place, yaw, the scale along x, y, z, the tint)
    for (int k = 0; k < 2; ++k)
    {
        auto& R = rocks[(size_t) k];
        const auto v = W::rockMesh (k == 0);
        glGenVertexArrays (1, &R.vao);
        glBindVertexArray (R.vao);
        glGenBuffers (1, &R.vbo);
        glBindBuffer (GL_ARRAY_BUFFER, R.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (v.size() * 4), v.data(), GL_STATIC_DRAW);
        attrib (locPos, 3, GL_FLOAT, false, 24, 0);
        attrib (locNrm, 3, GL_FLOAT, false, 24, 12);
        glGenBuffers (1, &R.inst);
        glBindBuffer (GL_ARRAY_BUFFER, R.inst);
        glBufferData (GL_ARRAY_BUFFER, maxRocks * 32, nullptr, GL_DYNAMIC_DRAW);
        attrib (locR0, 4, GL_FLOAT, false, 32, 0, true);
        attrib (locR1, 4, GL_FLOAT, false, 32, 16, true);
        R.count = (int) (v.size() / 6);
    }

    // THE HARDWARE: a vertex array per variant with its instance buffer (8 floats: place, yaw, scale, lean, seed)
    const auto meshesOf = W::structureMeshes();
    for (size_t vi = 0; vi < structs.size() && vi < meshesOf.size(); ++vi)
    {
        auto& b = structs[vi];
        const auto& v = meshesOf[vi];
        glGenVertexArrays (1, &b.vao);
        glBindVertexArray (b.vao);
        glGenBuffers (1, &b.vbo);
        glBindBuffer (GL_ARRAY_BUFFER, b.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (v.size() * 4), v.data(), GL_STATIC_DRAW);
        attrib (locPos, 3, GL_FLOAT, false, 44, 0);
        attrib (locNrm, 3, GL_FLOAT, false, 44, 12);
        attrib (locAcross, 3, GL_FLOAT, false, 44, 24);
        attrib (locMat, 1, GL_FLOAT, false, 44, 36);
        attrib (locFlag, 1, GL_FLOAT, false, 44, 40);
        glGenBuffers (1, &b.inst);
        glBindBuffer (GL_ARRAY_BUFFER, b.inst);
        glBufferData (GL_ARRAY_BUFFER, maxStruct * 32, nullptr, GL_DYNAMIC_DRAW);
        attrib (locI0, 4, GL_FLOAT, false, 32, 0, true);
        attrib (locI1, 4, GL_FLOAT, false, 32, 16, true);
        b.count = (int) (v.size() / 11);
    }

    // THE EMITTERS, THE PLUMES' BODIES: instance buffers (12 floats), the quads from gl_VertexID
    const auto instVao = [] (Emit& e, GLuint l0, GLuint l1, GLuint l2)
    {
        glGenVertexArrays (1, &e.vao);
        glBindVertexArray (e.vao);
        glGenBuffers (1, &e.inst);
        glBindBuffer (GL_ARRAY_BUFFER, e.inst);
        glBufferData (GL_ARRAY_BUFFER, maxEmit * 48, nullptr, GL_DYNAMIC_DRAW);
        attrib (l0, 4, GL_FLOAT, false, 48, 0, true);
        attrib (l1, 4, GL_FLOAT, false, 48, 16, true);
        attrib (l2, 4, GL_FLOAT, false, 48, 32, true);
    };
    for (auto& e : emitBuf)
        instVao (e, locE0, locE1, locE2);
    instVao (volBuf, locV0, locV1, locV2);
    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    vaoEmpty = gpu::attributeless (vboEmpty);   // the sky, the post pass and the snow draw with no attributes
}

void IoGl::loadMesh (int variant, const std::string& base)
{
    if (! art.bytes)
        return;
    const auto bytes = art.bytes (base + ".mesh");
    const auto info = W::readMesh (bytes.data(), bytes.size());
    if (! info.ok)
        return;
    FileMesh m;
    glGenVertexArrays (1, &m.vao);
    glBindVertexArray (m.vao);
    glGenBuffers (1, &m.vbo);
    glBindBuffer (GL_ARRAY_BUFFER, m.vbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) info.nv * 40, bytes.data() + info.vertexAt, GL_STATIC_DRAW);
    attrib (locPos, 3, GL_FLOAT, false, 40, 0);
    attrib (locNrm, 3, GL_FLOAT, false, 40, 12);
    attrib (locUV, 2, GL_FLOAT, false, 40, 24);
    attrib (locCol, 3, GL_UNSIGNED_BYTE, true, 40, 32);
    attrib (locKind, 1, GL_UNSIGNED_BYTE, false, 40, 35);
    attrib (locTex, 1, GL_UNSIGNED_BYTE, false, 40, 36);
    glGenBuffers (1, &m.ibo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) info.ni * 4, bytes.data() + info.indexAt, GL_STATIC_DRAW);
    glBindVertexArray (0);
    m.count = (int) info.ni;
    if (const auto old = meshes.find (variant); old != meshes.end())
    {
        glDeleteBuffers (1, &old->second.vbo); glDeleteBuffers (1, &old->second.ibo); glDeleteVertexArrays (1, &old->second.vao);
    }
    meshes[variant] = m;
}

void IoGl::makeArt()
{
    // THE CABIN'S PLATES: premultiplied with their mips; the window's box the union of both plates' clear pixels, 4 stage
    // px wider all round; the dosimeter's hub ink as each plate paints it.
    if (art.day)
    {
        const auto day = art.day();
        const auto dark = art.dark ? art.dark() : Picture {};
        if (day.valid())
        {
            std::optional<std::array<double, 4>> box;
            const auto read = [&] (const Picture& p, std::array<double, 3>& ink)
            {
                const double k = p.width / stage[0];
                if (const auto b = W::glassBounds (p.rgba.data(), p.width, p.height, (int) std::ceil (4 * k)))
                    box = box ? std::array<double, 4> { std::min ((*box)[0], (*b)[0]), std::min ((*box)[1], (*b)[1]),
                                                        std::max ((*box)[2], (*b)[2]), std::max ((*box)[3], (*b)[3]) } : *b;
                if (const auto c = W::inkAt (p.rgba.data(), p.width, p.height, dial.x * k, dial.y * k, std::max (1.0, 0.8 * k)))
                    ink = *c;
            };
            read (day, inkDay);
            if (dark.valid()) read (dark, inkDark); else inkDark = inkDay;
            texDash = pictureTexture (day, true, true);
            texDashDark = dark.valid() ? pictureTexture (dark, true, true) : texDash;
            if (texDashDark == 0) texDashDark = texDash;
            glassBox = texDash != 0 ? box : std::nullopt;
        }
        if (texDash == 0)
            gpu::note ("io: the cabin's plates did not come; the face paints the panel");
    }
    // THE HARDWARE'S SLOTS (finds/<variant>3d/<name>.mesh + .txt): a file takes its variant's place, by its folder's
    // name; the lava-tube base's table gives the skylight's pit its radius.
    if (art.finds)
        for (const auto& base : art.finds())
        {
            const auto folder = base.substr (0, base.find ('/'));
            const int role = W::roleOf (folder);
            if (role < 0)
                continue;
            if (art.text && role == 7)
                if (const auto r = W::pitRadiusOf (art.text (base + ".txt")))
                    world.setPitRadius (*r);
            loadMesh (role, base);
        }
}

bool IoGl::ensureFrame (int w, int h, int cw, int ch)
{
    // THE SCENE'S FRAME: colour 32 bits a pixel with light above 1 kept (R11F_G11F_B10F) until the post pass's knee,
    // RGBA8 where that does not render; depth a renderbuffer. While the cabin is drawn only the window's box of it can
    // show (the plates are opaque round it): the frame is that box and a pixel round it (left clear, as the whole frame
    // was), drawn through a viewport that is the whole view's, moved, so that every fragment is the one the whole frame
    // would have had — its corner on even pixels, so the 2 x 2 quads a fragment's derivatives come from are the whole
    // frame's (as MetalGl's).
    int bx0 = 0, by0 = 0, bx1 = w, by1 = h;
    if (drawsCabin() && glassBox)
    {
        const auto& g = *glassBox;
        bx0 = std::max (0, ((int) std::floor (g[0] * w) - 1) & ~1); bx1 = std::min (w, (int) std::ceil (g[2] * w) + 1);
        by0 = std::max (0, ((int) std::floor ((1 - g[3]) * h) - 1) & ~1); by1 = std::min (h, (int) std::ceil ((1 - g[1]) * h) + 1);
    }
    sceneAt = { bx0, by0 };
    fbW = w; fbH = h;
    w = bx1 - bx0; h = by1 - by0;
    if (fbo == 0 || w != subW || h != subH)
    {
        if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); glGenRenderbuffers (1, &fboDepth); }
        const auto colour = [&] (bool asFloat)
        {
            glBindTexture (GL_TEXTURE_2D, fboColour);
            if (asFloat) glTexImage2D (GL_TEXTURE_2D, 0, GL_R11F_G11F_B10F, w, h, 0, GL_RGB, GL_HALF_FLOAT, nullptr);
            else glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture (GL_TEXTURE_2D, 0);
            glBindFramebuffer (GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
            return glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        };
        glBindRenderbuffer (GL_RENDERBUFFER, fboDepth);
        glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer (GL_RENDERBUFFER, 0);
        while (glGetError() != GL_NO_ERROR) {}
        if (! (floatTarget && colour (true)))
        {
            floatTarget = false;
            if (! colour (false))
            {
                fbW = fbH = subW = subH = 0;
                return false;
            }
        }
        subW = w; subH = h;
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
        outerKept = false;          // a new frame: nothing of it drawn yet
    }
    return true;
}

void IoGl::follow (double scale, double seconds)
{
    Q.level = scale > 0.99 ? 0 : scale > 0.74 ? 1 : 2;
    const double d = Q.started ? std::max (0.0, seconds) / 1.5 : 1.0;
    Q.started = true;
    Q.thin += clamp ((Q.level >= 2 ? 1.0 : 0.0) - Q.thin, -d, d);
}

void IoGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void IoGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (post.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, (int) Js::round ((double) w * scale)), rh = std::max (2, (int) Js::round ((double) h * scale));
    if (! ensureFrame (rw, rh, w, h))
        return;
    // The rung: what is drawn less of below the top, eased by the time since the last frame.
    const double now = gpu::milliseconds();
    follow (scale, lastFrameMs < 0 ? 0.0 : (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

    // The frame's CPU part (IoWorld::frame) worked out before the timer starts: the timer is the GPU's.
    prepared = world.frame();

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

bool IoGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
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
// THE LIGHT FRAMES (io-gl.js lightFrame, relFrame): an orthographic frame round a box (absolute xz centre, half size,
// absolute y range), its texels snapped, for the frame's cell; a frame kept from an earlier one only re-expressed.
IoGl::LightFrame IoGl::lightFrame (const V3& sun, double cxAbs, double czAbs, double half, double yLo, double yHi, int N,
                                   double ccx, double ccz, const std::optional<LightBox>& keep)
{
    if (keep)
        return relFrame (*keep, ccx, ccz);
    LightBox f;
    f.lz = sun; f.lx = norm3 (cross3 ({ 0, 1, 0 }, f.lz)); f.ly = cross3 (f.lz, f.lx);
    V3 mn { 1e18, 1e18, 1e18 }, mx { -1e18, -1e18, -1e18 };
    for (const double sx : { -1.0, 1.0 })
        for (const double sz : { -1.0, 1.0 })
            for (const double y : { yLo, yHi })
            {
                const V3 p { cxAbs + sx * half, y, czAbs + sz * half }, q { dot3 (p, f.lx), dot3 (p, f.ly), dot3 (p, f.lz) };
                for (size_t k = 0; k < 3; ++k) { mn[k] = std::min (mn[k], q[k]); mx[k] = std::max (mx[k], q[k]); }
            }
    f.hx = (mx[0] - mn[0]) / 2 * (1 + 2.0 / N) + 0.5; f.hy = (mx[1] - mn[1]) / 2 * (1 + 2.0 / N) + 0.5;
    const double tx = 2 * f.hx / N, ty = 2 * f.hy / N;
    f.cx = Js::round ((mn[0] + mx[0]) / 2 / tx) * tx; f.cy = Js::round ((mn[1] + mx[1]) / 2 / ty) * ty;
    f.hz = (mx[2] - mn[2]) / 2 + 300; f.cz = (mn[2] + mx[2]) / 2 + 270;     // reaching 570 m toward the Sun: the casters outside the box
    f.N = N;
    return relFrame (f, ccx, ccz);
}

IoGl::LightFrame IoGl::relFrame (const LightBox& f, double ccx, double ccz)
{
    LightFrame o;
    o.f = f;
    const V3 cell { ccx, 0, ccz };
    const double ox = dot3 (cell, f.lx) - f.cx, oy = dot3 (cell, f.ly) - f.cy, oz = dot3 (cell, f.lz) - f.cz;
    o.VP = { f32 (f.lx[0] / f.hx), f32 (f.ly[0] / f.hy), f32 (-f.lz[0] / f.hz), 0, f32 (f.lx[1] / f.hx), f32 (f.ly[1] / f.hy), f32 (-f.lz[1] / f.hz), 0,
             f32 (f.lx[2] / f.hx), f32 (f.ly[2] / f.hy), f32 (-f.lz[2] / f.hz), 0, f32 (ox / f.hx), f32 (oy / f.hy), f32 (-oz / f.hz), 1 };
    for (size_t c = 0; c < 4; ++c)
        for (size_t r = 0; r < 3; ++r)
            o.M[c * 4 + r] = f32 (0.5 * (double) o.VP[c * 4 + r] + (c == 3 ? 0.5 : 0));
    o.M[3] = 0; o.M[7] = 0; o.M[11] = 0; o.M[15] = 1;
    o.texel = std::max (2 * f.hx, 2 * f.hy) / f.N;
    o.hz = f.hz;
    return o;
}

//==============================================================================
void IoGl::drawScene (int rw, int rh, int cw, int ch)
{
    // what drawScene works out on the CPU, in its order (the hardware's lists rebuilt): worked out before the timer for
    // the first of the repeats
    W::Frame F = prepared ? std::move (*prepared) : world.frame();
    prepared.reset();
    const auto& s = world.state();
    const bool cabin = drawsCabin();
    const std::optional<std::array<double, 4>> box = cabin ? glassBox : std::nullopt;
    const double ccx = F.ccx, ccz = F.ccz;
    const auto camRel = F.camRel;
    const double fx = F.fwd[0], fz = F.fwd[1];
    const double aspect = (double) rw / rh, th = tanH, tv = th / aspect;
    std::array<float, 16> Pm {};
    Pm[0] = f32 (1 / th); Pm[5] = f32 (1 / tv); Pm[10] = f32 (-(zf + zn) / (zf - zn)); Pm[11] = -1; Pm[14] = f32 (-2 * zf * zn / (zf - zn)); Pm[9] = f32 (-shear);
    const W::Basis basis = W::cameraBasis (s.heading, s.pitch);
    std::array<float, 16> Vm {};
    {
        const double cx = camRel[0], cy = camRel[1], cz = camRel[2];
        const auto& r = basis.r; const auto& u = basis.u; const auto& f = basis.f;
        Vm[0] = f32 (r[0]); Vm[4] = f32 (r[1]); Vm[8] = f32 (r[2]);    Vm[12] = f32 (-(r[0] * cx + r[1] * cy + r[2] * cz));
        Vm[1] = f32 (u[0]); Vm[5] = f32 (u[1]); Vm[9] = f32 (u[2]);    Vm[13] = f32 (-(u[0] * cx + u[1] * cy + u[2] * cz));
        Vm[2] = f32 (-f[0]); Vm[6] = f32 (-f[1]); Vm[10] = f32 (-f[2]); Vm[14] = f32 (f[0] * cx + f[1] * cy + f[2] * cz);
        Vm[15] = 1;
    }
    const auto VP = multiply (Pm, Vm);
    const double pxAng = 2 * tv / rh;
    const auto& G = F.ground;
    if (skips ("sites"))
    {
        for (size_t i = 0; i < 6; ++i) { F.sl[i * 4] = 0; F.sl[i * 4 + 1] = -1e6f; F.sl[i * 4 + 2] = 0; F.sl[i * 4 + 3] = 1; }
        F.slN = 0; F.slBox = { 1e9, 1e9, -1e9, -1e9 };
    }

    // THE SHADOW CASCADES: the near one round what is ahead, the far one round the loop (when the Sun moved)
    const double groundY = s.camY - 2.6;
    const V3 key = G.dir;
    const auto moved = [&] { return Js::hypot (key[0] - (*sh1Sun)[0], key[1] - (*sh1Sun)[1], key[2] - (*sh1Sun)[2]) > 1e-5; };
    const bool redrawNear = sh0Age >= 2 || ! sh0Frame || ! sh1Sun || moved();
    const auto L0f = lightFrame (key, s.x + fx * sh0Ahead, s.z + fz * sh0Ahead, sh0Half, groundY - 40, groundY + 70, sh0N, ccx, ccz,
                                 redrawNear ? std::nullopt : sh0Frame);
    const bool sunMoved = ! sh1Sun || moved();
    const bool redrawFar = sunMoved || sh1Age >= 3 || ! sh1Frame;
    const auto L1f = lightFrame (key, s.x + fx * sh1Ahead, s.z + fz * sh1Ahead, sh1Half, groundY - 60, groundY + 110, sh1N, ccx, ccz,
                                 redrawFar ? std::nullopt : sh1Frame);
    const bool shOn = sh0.ok && sh1.ok && ! skips ("shadows") && lum3 (G.key) > 1e-4;

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.u ("uBase"), 0); glUniform1i (p.u ("uRoad"), 7); glUniform1i (p.u ("uFeat"), 5);
        glUniform1i (p.u ("uSh0"), 8); glUniform1i (p.u ("uSh1"), 9);
        glUniform2f (p.u ("uCamCell"), f32 (ccx), f32 (ccz)); glUniform2f (p.u ("uCamFrac"), F.camFrac[0], F.camFrac[1]);
        glUniform2f (p.u ("uOff0"), f32 (F.off0[0]), f32 (F.off0[1])); glUniform2f (p.u ("uOff1"), f32 (F.off1[0]), f32 (F.off1[1]));
        glUniform2f (p.u ("uOff2"), f32 (F.off2[0]), f32 (F.off2[1]));
        glUniform3f (p.u ("uCamRel"), camRel[0], camRel[1], camRel[2]);
        glUniform1f (p.u ("uTime"), f32 (F.time));
        glUniform2f (p.u ("uLoopC"), f32 (W::loopC[0] - ccx), f32 (W::loopC[1] - ccz));
        glUniform2f (p.u ("uRoadOrg"), f32 (W::roadOrg[0] - ccx), f32 (W::roadOrg[1] - ccz));
        glUniform2f (p.u ("uFeatOrg"), f32 (W::featOrg[0] - ccx), f32 (W::featOrg[1] - ccz));
        glUniform4fv (p.u ("uCaldA[0]"), W::maxCald, F.caldA.data()); glUniform4fv (p.u ("uCaldB[0]"), W::maxCald, F.caldB.data());
        glUniform4fv (p.u ("uCaldC[0]"), W::maxCald, F.caldC.data()); glUniform1i (p.u ("uCaldN"), F.caldN);
        glUniformMatrix4fv (p.u ("uViewProj"), 1, GL_FALSE, VP.data());
        uniform3 (p.u ("uSun"), F.sun); uniform3 (p.u ("uSunCol"), F.sunCol); glUniform1f (p.u ("uSunK"), f32 (F.sunK));
        uniform3 (p.u ("uJup"), W::jupFrame().J); uniform3 (p.u ("uJupShine"), F.jupShine); glUniform1f (p.u ("uDark"), f32 (F.dark));
        glUniform4f (p.u ("uTerm"), f32 (F.term[0]), f32 (F.term[1]), f32 (F.term[2]), f32 (F.term[3])); uniform3 (p.u ("uAirglow"), F.airglow);
        uniform3 (p.u ("uJupSunCol"), F.jupSunCol); glUniform1f (p.u ("uFlat"), f32 (F.flat)); glUniform1f (p.u ("uAir"), f32 (F.air));
        glUniform4fv (p.u ("uPL[0]"), 4, F.pl.data()); glUniform4fv (p.u ("uPLC[0]"), 4, F.plc.data()); glUniform1i (p.u ("uPLN"), F.plN);
        glUniform4f (p.u ("uLamps"), F.camFrac[0], F.camFrac[1], f32 (fx), f32 (fz));
        glUniform4fv (p.u ("uSL[0]"), 6, F.sl.data()); glUniform1i (p.u ("uSLN"), F.slN); uniform3 (p.u ("uSLC"), F.slc);
        glUniform4f (p.u ("uSLBox"), f32 (F.slBox[0]), f32 (F.slBox[1]), f32 (F.slBox[2]), f32 (F.slBox[3]));
        glUniform2f (p.u ("uHitT"), f32 (F.hitAge), f32 (s.hitS));
        glUniformMatrix4fv (p.u ("uShM0"), 1, GL_FALSE, L0f.M.data()); glUniformMatrix4fv (p.u ("uShM1"), 1, GL_FALSE, L1f.M.data());
        glUniform4f (p.u ("uShK"), f32 (L0f.texel * 1.5), f32 (L1f.texel * 1.0), f32 (0.08 / (2 * L0f.hz)), f32 (0.6 / (2 * L1f.hz)));
        glUniform1f (p.u ("uShOn"), shOn ? 1.0f : 0.0f);
        glUniform1f (p.u ("uPxAng"), f32 (pxAng)); glUniform1f (p.u ("uShadowPass"), 0);
        // the surface's light (the floor in the dark); the sky's massifs take it as their own uniforms
        if (&p == &ground || &p == &groundH || &p == &groundF || &p == &rock || &p == &structP || &p == &mesh)
        {
            uniform3 (p.u ("uSun"), G.dir); uniform3 (p.u ("uSunCol"), G.key); glUniform1f (p.u ("uSunK"), f32 (G.k));
            uniform3 (p.u ("uJup"), G.adir); uniform3 (p.u ("uJupShine"), G.amb);
        }
        else
        {
            uniform3 (p.u ("uGKey"), G.dir);
            glUniform3f (p.u ("uGKeyCol"), f32 (G.key[0] * G.k), f32 (G.key[1] * G.k), f32 (G.key[2] * G.k));
            uniform3 (p.u ("uGAmb"), G.amb);
        }
    };
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texBase);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texNoise);
    glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, texCells);
    glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, texFeat);
    glActiveTexture (GL_TEXTURE7); glBindTexture (GL_TEXTURE_2D, texRoad);
    glActiveTexture (GL_TEXTURE10); glBindTexture (GL_TEXTURE_2D, texHist);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, W::histN, W::histRows, GL_RED, GL_UNSIGNED_BYTE, world.history().data());
    glActiveTexture (GL_TEXTURE8); glBindTexture (GL_TEXTURE_2D, 0);
    glActiveTexture (GL_TEXTURE9); glBindTexture (GL_TEXTURE_2D, 0);
    const auto inView = [&] (double x, double z, double r)
    {
        const double dx = x - s.x, dz = z - s.z, D = Js::hypot (dx, dz);
        return D - r < farCut && (D < r + 30 || dx * fx + dz * fz > 0.55 * D - r);
    };
    // the hardware's instances this frame (the view's first, then those only the near cascade needs), the rocks
    const double c0x = s.x + fx * sh0Ahead, c0z = s.z + fz * sh0Ahead;
    for (size_t vi = 0; vi < structs.size(); ++vi)
        if (F.hardware[vi].nSh > 0)
        {
            glBindBuffer (GL_ARRAY_BUFFER, structs[vi].inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) F.hardware[vi].nSh * 32, F.hardware[vi].data.data());
        }
    for (size_t k = 0; k < 2; ++k)
        if (! F.rocks[k].empty())
        {
            glBindBuffer (GL_ARRAY_BUFFER, rocks[k].inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (F.rocks[k].size() * 4), F.rocks[k].data());
        }
    const auto& near = world.hardwareNear();
    const auto drawHardware = [&] (bool shadow, const std::array<float, 16>* LV)
    {
        const Program& pS = shadow ? structSh : structP;
        const Program& pM = shadow ? meshSh : mesh;
        glUseProgram (pS.id); setCommon (pS);
        if (shadow) { glUniformMatrix4fv (pS.u ("uViewProj"), 1, GL_FALSE, LV->data()); glUniform1f (pS.u ("uShadowPass"), 1); }
        for (size_t vi = 0; vi < structs.size(); ++vi)
        {
            const int n = shadow ? F.hardware[vi].nSh : F.hardware[vi].n;
            if (n == 0 || meshes.count ((int) vi) != 0 || skips (("v" + std::to_string (vi)).c_str())) continue;
            glBindVertexArray (structs[vi].vao);
            glDrawArraysInstanced (GL_TRIANGLES, 0, structs[vi].count, n);
        }
        // a variant whose file came: drawn one by one
        if (meshes.empty())
            return;
        glUseProgram (pM.id); setCommon (pM);
        if (shadow) { glUniformMatrix4fv (pM.u ("uViewProj"), 1, GL_FALSE, LV->data()); glUniform1f (pM.u ("uShadowPass"), 1); }
        for (size_t vi = 0; vi < structs.size(); ++vi)
        {
            const auto it = meshes.find ((int) vi);
            if (it == meshes.end()) continue;
            glBindVertexArray (it->second.vao);
            for (const auto& st : near[vi])
            {
                if (shadow ? std::abs (st.x - c0x) > sh0Half + 70 || std::abs (st.z - c0z) > sh0Half + 70
                           : ! inView (st.x, st.z, 70) || Js::hypot (st.x - s.x, st.z - s.z) > 2400) continue;
                glUniform3f (pM.u ("uMeshAt"), f32 (st.x - ccx), f32 (st.y), f32 (st.z - ccz));
                glUniform1f (pM.u ("uMeshSeed"), f32 (st.seed));
                const auto R = frameMat (st.yaw, st.k, st.lean[0], st.lean[1]);
                glUniformMatrix3fv (pM.u ("uMeshRot"), 1, GL_FALSE, R.data());
                glDrawElements (GL_TRIANGLES, it->second.count, GL_UNSIGNED_INT, nullptr);
            }
        }
    };
    const auto drawRocks = [&] (bool shadow, const std::array<float, 16>* LV)
    {
        const Program& p = shadow ? rockSh : rock;
        glUseProgram (p.id); setCommon (p);
        if (shadow) { glUniformMatrix4fv (p.u ("uViewProj"), 1, GL_FALSE, LV->data()); glUniform1f (p.u ("uShadowPass"), 1); }
        for (size_t k = 0; k < 2; ++k)
        {
            const int n = (int) (F.rocks[k].size() / 8);
            if (n == 0) continue;
            glBindVertexArray (rocks[k].vao);
            glDrawArraysInstanced (GL_TRIANGLES, 0, rocks[k].count, n);
        }
    };

    // the ground's levels this frame (the view's and the shadows' alike)
    struct Outer { double ox, oz, size, cenX, cenZ, half, cell, coarse; };
    std::array<Outer, levels.size()> outerAt {};
    for (size_t L = 0; L < levels.size(); ++L)
    {
        const double cell = levels[L].cell, half = levels[L].n * cell / 2;
        const double coarse = L + 1 < levels.size() ? levels[L + 1].cell : cell * 3;
        const double cenX = s.x + fx * 0.35 * half, cenZ = s.z + fz * 0.35 * half;
        const double ox = std::floor ((cenX - half) / coarse) * coarse, oz = std::floor ((cenZ - half) / coarse) * coarse;
        outerAt[L] = { ox, oz, levels[L].n * cell, cenX, cenZ, half, cell, coarse };
    }
    const auto levelUniforms = [&] (const Program& p, size_t L)
    {
        const auto& g = outerAt[L];
        glUniform2f (p.u ("uLvlOrigin"), f32 (g.ox - ccx), f32 (g.oz - ccz)); glUniform1f (p.u ("uCellSize"), f32 (g.cell));
        const bool last = L + 1 == levels.size();
        const double mEnd = last ? 1e9 : g.half - g.coarse - 0.5, mStart = last ? 1e9 : mEnd - 0.32 * g.half;
        glUniform4f (p.u ("uMorph"), f32 (g.cenX - ccx), f32 (g.cenZ - ccz), f32 (mStart), f32 (mEnd));
        glUniform2f (p.u ("uCoarseOff"), f32 (fract (ccx / g.coarse) * g.coarse), f32 (fract (ccz / g.coarse) * g.coarse)); glUniform1f (p.u ("uCoarse"), f32 (g.coarse));
        if (L > 0)
        {
            const auto& inner = outerAt[L - 1];
            const double e = 0.05;
            glUniform4f (p.u ("uHole"), f32 (inner.ox - ccx + e), f32 (inner.oz - ccz + e), f32 (inner.ox + inner.size - ccx - e), f32 (inner.oz + inner.size - ccz - e));
            glUniform1f (p.u ("uHoleOn"), 1);
        }
        else
            glUniform1f (p.u ("uHoleOn"), 0);
        glUniform2f (p.u ("uFwdXZ"), f32 (fx), f32 (fz));
    };

    glDisable (GL_SCISSOR_TEST); glDisable (GL_CULL_FACE); glDisable (GL_BLEND);
    if (shOn)
    {
        glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDepthMask (GL_TRUE);
        glEnable (GL_POLYGON_OFFSET_FILL);
        // the casters: the view's own lattices (identical vertices), the rocks and the hardware near
        const auto groundShadow = [&] (const std::array<float, 16>& LV, std::initializer_list<size_t> which)
        {
            glUseProgram (groundSh.id); setCommon (groundSh);
            glUniformMatrix4fv (groundSh.u ("uViewProj"), 1, GL_FALSE, LV.data()); glUniform1f (groundSh.u ("uShadowPass"), 1);
            for (const size_t L : which)
            {
                const auto& gd = gridFor (levels[L].n);
                glBindVertexArray (gd.vao);
                levelUniforms (groundSh, L);
                glDrawElements (GL_TRIANGLES, gd.count, GL_UNSIGNED_SHORT, nullptr);
            }
        };
        if (redrawFar)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, sh1.fbo); glViewport (0, 0, sh1N, sh1N);
            glClear (GL_DEPTH_BUFFER_BIT);
            glPolygonOffset (2.0f, 4.0f);
            groundShadow (L1f.VP, { 1, 2, 3 });
            sh1Frame = L1f.f; sh1Age = 0; sh1Sun = key;
        }
        else
            ++sh1Age;
        // the near cascade: every third frame (what casts into it stands still; between, its frame is kept)
        if (redrawNear)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, sh0.fbo); glViewport (0, 0, sh0N, sh0N);
            glClear (GL_DEPTH_BUFFER_BIT);
            glPolygonOffset (1.5f, 3.0f);
            groundShadow (L0f.VP, { 0, 1, 2 });
            if (! skips ("rocks")) drawRocks (true, &L0f.VP);
            if (! skips ("hardware")) drawHardware (true, &L0f.VP);
            sh0Frame = L0f.f; sh0Age = 0;
        }
        else
            ++sh0Age;
        glDisable (GL_POLYGON_OFFSET_FILL);
    }
    glActiveTexture (GL_TEXTURE8); glBindTexture (GL_TEXTURE_2D, sh0.tex);
    glActiveTexture (GL_TEXTURE9); glBindTexture (GL_TEXTURE_2D, sh1.tex);

    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (-sceneAt[0], -sceneAt[1], rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE); glDepthMask (GL_TRUE);
    glClearColor (0, 0, 0, 1);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (box)
    {
        const int x0 = (int) std::floor ((*box)[0] * rw), x1 = (int) std::ceil ((*box)[2] * rw);
        const int y0 = (int) std::floor ((1 - (*box)[3]) * rh), y1 = (int) std::ceil ((1 - (*box)[1]) * rh);
        glEnable (GL_SCISSOR_TEST); glScissor (x0 - sceneAt[0], y0 - sceneAt[1], x1 - x0, y1 - y0);
    }
    // WHAT STANDS: the hardware, the rocks; the ground after
    if (! skips ("hardware")) drawHardware (false, nullptr);
    if (! skips ("rocks")) drawRocks (false, nullptr);
    // THE GROUND: the near level, then the outer ones with the inner one's square left out
    if (! skips ("ground"))
    {
        const auto tr = [&] (double a, double sc) -> std::array<float, 2>
        {
            const double c = Js::cos (a), sn = Js::sin (a);
            return { f32 (fract ((c * ccx - sn * ccz) / sc)), f32 (fract ((sn * ccx + c * ccz) / sc)) };
        };
        for (size_t L = 0; L < levels.size(); ++L)
        {
            // the near level with no discard, the outermost alone with the far cut (the prototype's two programs'
            // pixels: neither discard can fire where it is left out)
            const Program& p = L == 0 ? ground : L + 1 < levels.size() ? groundH : groundF;
            const auto& gd = gridFor (levels[L].n);
            glUseProgram (p.id); glBindVertexArray (gd.vao);
            setCommon (p);
            glUniform1i (p.u ("uDetail"), 1); glUniform1i (p.u ("uCells"), 3);
            // the detail layers turned (0.61 and 1.83 rad): their offsets are the turned cell's, from doubles
            uniform (p.u ("uDetOff0"), tr (0.61, 4.3)); uniform (p.u ("uDetOff1"), tr (1.83, 1.13));
            glUniform2f (p.u ("uDetOff2"), f32 (fract (ccx / 0.13)), f32 (fract (ccz / 0.13)));
            glUniform4f (p.u ("uDetRot"), f32 (Js::cos (0.61)), f32 (Js::sin (0.61)), f32 (Js::cos (1.83)), f32 (Js::sin (1.83)));
            levelUniforms (p, L);
            glUniform1f (p.u ("uFarCut"), f32 (farCut)); glUniform1f (p.u ("uAct"), f32 (s.act)); glUniform1f (p.u ("uLakeT"), f32 (F.time));
            glUniform2f (p.u ("uCrackOff"), f32 (std::fmod (std::fmod (ccx, 819.2) + 819.2, 819.2)), f32 (std::fmod (std::fmod (ccz, 819.2) + 819.2, 819.2)));
            glDrawElements (GL_TRIANGLES, gd.count, GL_UNSIGNED_SHORT, nullptr);
        }
    }
    // THE SKY where nothing was drawn
    if (! skips ("sky"))
    {
        const auto& J = W::jupFrame();
        glUseProgram (sky.id); glBindVertexArray (vaoEmpty); setCommon (sky);
        glUniform1i (sky.u ("uNoise"), 2);
        uniform3 (sky.u ("uCamFwd"), basis.f); uniform3 (sky.u ("uCamRight"), basis.r); uniform3 (sky.u ("uCamUp"), basis.u);
        glUniform2f (sky.u ("uTanHalf"), f32 (th), f32 (tv)); glUniform1f (sky.u ("uShear"), f32 (shear));
        uniform3 (sky.u ("uJupR"), J.R); uniform3 (sky.u ("uJupU"), J.U); uniform3 (sky.u ("uJupAxis"), J.axis);
        glUniform1f (sky.u ("uJupSin"), f32 (Js::sin (W::jupRho()))); glUniform1f (sky.u ("uJupCos"), f32 (Js::cos (W::jupRho())));
        glUniform1f (sky.u ("uJupT"), f32 (F.jupT)); glUniform1f (sky.u ("uSep"), f32 (F.sep)); glUniform1f (sky.u ("uRho"), f32 (W::jupRho()));
        uniform3 (sky.u ("uSunUp"), F.sunUp); glUniform1f (sky.u ("uJupK"), f32 (F.jupK));
        glUniform4fv (sky.u ("uMoon[0]"), 2, F.moon.data()); glUniform3fv (sky.u ("uMoonAlb[0]"), 2, F.moonAlb.data());
        // the attack's aurora: a short flash and a slower glow, on the beat
        glUniform1f (sky.u ("uAurora"), skips ("aurora") ? 0.0f : f32 (F.aurora)); glUniform1f (sky.u ("uFlux"), f32 (F.flux));
        // the Milky Way's band, fixed in the sky
        static const std::array<V3, 3> band = []
        {
            const double ba = bandAzTurns * 2 * PI;
            const V3 bandH { Js::sin (ba), 0, -Js::cos (ba) }, bandTan { Js::cos (ba), 0, Js::sin (ba) };
            const V3 bandT = norm3 ({ bandTan[0] * Js::cos (bandTilt), Js::sin (bandTilt), bandTan[2] * Js::cos (bandTilt) });
            return std::array<V3, 3> { bandH, bandT, norm3 (cross3 (bandH, bandT)) };
        }();
        uniform3 (sky.u ("uBandH"), band[0]); uniform3 (sky.u ("uBandT"), band[1]); uniform3 (sky.u ("uBandN"), band[2]);
        uniform3 (sky.u ("uFar"), F.far);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // WHAT IS BLENDED (premultiplied): the plumes, the fountains and the puffs
    glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glDepthMask (GL_FALSE);
    // THE PLUMES' BODY: solved per pixel, at full resolution, depth-tested at its nearest crossing; the far ones first
    if (! skips ("plumes") && ! skips ("volumes") && ! F.volumes.empty())
    {
        const int n = (int) F.volumes.size(), nIn = F.volumesInside;
        glUseProgram (pvol.id); setCommon (pvol);
        glUniform1i (pvol.u ("uNoise"), 2); glUniform1i (pvol.u ("uHist"), 10);
        glBindBuffer (GL_ARRAY_BUFFER, volBuf.inst);
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) n * 48, F.volumes.data());
        glBindVertexArray (volBuf.vao);
        glEnable (GL_CULL_FACE);
        // those round the eye first (their back faces, no depth test), then the rest far to near (front faces, tested)
        if (nIn > 0) { glCullFace (GL_FRONT); glDisable (GL_DEPTH_TEST); glDrawArraysInstanced (GL_TRIANGLES, 0, 36, nIn); glEnable (GL_DEPTH_TEST); }
        if (n > nIn)
        {
            glCullFace (GL_BACK);
            // the instance offset: the attributes re-pointed past the insiders
            const GLuint loc[3] { locV0, locV1, locV2 };
            for (int i = 0; i < 3; ++i) glVertexAttribPointer (loc[i], 4, GL_FLOAT, GL_FALSE, 48, offset (nIn * 48 + i * 16));
            glDrawArraysInstanced (GL_TRIANGLES, 0, 36, n - nIn);
            for (int i = 0; i < 3; ++i) glVertexAttribPointer (loc[i], 4, GL_FLOAT, GL_FALSE, 48, offset (i * 16));
        }
        glDisable (GL_CULL_FACE);
    }
    if (! skips ("plumes"))
    {
        glUseProgram (plume.id); setCommon (plume);
        uniform3 (plume.u ("uCamR"), basis.r); uniform3 (plume.u ("uCamU"), basis.u); glUniform1f (plume.u ("uThinF"), f32 (Q.thin));
        glUniform1i (plume.u ("uHist"), 10);
        const std::array<int, 3> counts { plumeP, fountP, puffP };
        for (size_t k = 0; k < 3; ++k)
        {
            const auto& list = F.emitters[k];
            if (list.empty()) continue;
            glBindBuffer (GL_ARRAY_BUFFER, emitBuf[k].inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (list.size() * 48), list.data());
            glBindVertexArray (emitBuf[k].vao);
            glDrawArraysInstanced (GL_TRIANGLES, 0, counts[k] * 6, (GLsizei) list.size());
        }
    }
    glDisable (GL_BLEND); glDepthMask (GL_TRUE);
    glDisable (GL_SCISSOR_TEST);
   #if defined (__EMSCRIPTEN__)
    {
        const GLenum depth = GL_DEPTH_ATTACHMENT;      // thrown away after the scene, as the prototype's
        glInvalidateFramebuffer (GL_FRAMEBUFFER, 1, &depth);
    }
   #endif

    // THE POST PASS (the cabin's plates in it, the dosimeter's needle on its dial), into the finished frame
    glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
    glViewport (0, 0, cw, ch);
    glDisable (GL_DEPTH_TEST);
    // Outside the window's box and the dosimeter's dial the frame is the plates alone (opaque: the scene is not read
    // there), so it is drawn once and kept until the dark share or the frame's size changes; between, the pass runs only
    // over the box round the window and the dial.
    const double darkShare = cabin ? 1 - s.cabinDay : 0;
    const double outerKey = cabin && box && ! skips ("outer") ? darkShare : -1.0;
    if (outerKey >= 0 && outerKept && ! (outerKey < keptDark || outerKey > keptDark))
    {
        // the box round the window and the dial (fractions of the stage, y down), a pixel wider
        const double fx0 = std::min ((*box)[0], (dial.x - dial.r) / stage[0]), fx1 = std::max ((*box)[2], (dial.x + dial.r) / stage[0]);
        const double fy0 = std::min ((*box)[1], (dial.y - dial.r) / stage[1]), fy1 = std::max ((*box)[3], (dial.y + dial.r) / stage[1]);
        const int x0 = std::max (0, (int) std::floor (fx0 * cw) - 1), x1 = std::min (cw, (int) std::ceil (fx1 * cw) + 1);
        const int y0 = std::max (0, (int) std::floor ((1 - fy1) * ch) - 1), y1 = std::min (ch, (int) std::ceil ((1 - fy0) * ch) + 1);
        glEnable (GL_SCISSOR_TEST); glScissor (x0, y0, x1 - x0, y1 - y0);
    }
    else
    {
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT);
    }
    outerKept = outerKey >= 0 && ! skips ("post");
    keptDark = outerKey;
    {
        glUseProgram (post.id); glBindVertexArray (vaoEmpty);
        glActiveTexture (GL_TEXTURE4); glBindTexture (GL_TEXTURE_2D, fboColour); glUniform1i (post.u ("uColour"), 4);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, cabin ? texDash : 0); glUniform1i (post.u ("uDash"), 6);
        glUniform1f (post.u ("uDashOn"), cabin ? 1.0f : 0.0f);
        glActiveTexture (GL_TEXTURE11); glBindTexture (GL_TEXTURE_2D, cabin ? texDashDark : 0); glUniform1i (post.u ("uDashDark"), 11);
        // the plates' crossfade: the dark's share, as the sunlight goes (premultiplied, in the shader)
        const double darkK = darkShare;
        glUniform1f (post.u ("uDashMix"), f32 (darkK));
        glUniform4f (post.u ("uSceneBox"), (float) sceneAt[0] / (float) rw, (float) sceneAt[1] / (float) rh, (float) subW / (float) rw, (float) subH / (float) rh);
        glUniform1f (post.u ("uExpo"), f32 (s.sun.expo));
        // the dosimeter's needle: its pivot and length (stage px), the canvas px a stage px, its way, its colour
        if (cabin)
        {
            const auto way = W::needle (s.dose, dial.from, dial.to);
            glUniform4f (post.u ("uDial"), f32 (dial.x), f32 (dial.y), f32 (dial.r), f32 (cw / stage[0]));
            glUniform2f (post.u ("uStage"), f32 (stage[0]), f32 (stage[1]));
            glUniform2f (post.u ("uNeedle"), f32 (way[0]), f32 (way[1]));
            glUniform3f (post.u ("uNeedleCol"), f32 (inkDay[0] + (inkDark[0] - inkDay[0]) * darkK), f32 (inkDay[1] + (inkDark[1] - inkDay[1]) * darkK),
                         f32 (inkDay[2] + (inkDark[2] - inkDay[2]) * darkK));
        }
        else
            glUniform4f (post.u ("uDial"), 0, 0, 0, 1);
        if (! skips ("post")) glDrawArrays (GL_TRIANGLES, 0, 3);
        glDisable (GL_SCISSOR_TEST);
        glActiveTexture (GL_TEXTURE4); glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE11); glBindTexture (GL_TEXTURE_2D, 0);
    }
    // THE SULPHUR SNOW (over the post pass, in the window's box: near and small, it needs no scene)
    if (! skips ("snow") && F.snow > 0.004)
    {
        glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (box)
        {
            const int x0 = (int) std::floor ((*box)[0] * cw), x1 = (int) std::ceil ((*box)[2] * cw);
            const int y0 = (int) std::floor ((1 - (*box)[3]) * ch), y1 = (int) std::ceil ((1 - (*box)[1]) * ch);
            glEnable (GL_SCISSOR_TEST); glScissor (x0, y0, x1 - x0, y1 - y0);
        }
        glUseProgram (snow.id); glBindVertexArray (vaoEmpty); setCommon (snow);
        glUniformMatrix4fv (snow.u ("uProj"), 1, GL_FALSE, Pm.data()); glUniform2f (snow.u ("uRes"), (float) cw, (float) ch);
        glUniform1f (snow.u ("uExpo"), f32 (s.sun.expo));
        glUniform1f (snow.u ("uTravel"), f32 (F.travel)); glUniform1f (snow.u ("uSpeed"), f32 (F.speed)); glUniform1f (snow.u ("uSnow"), f32 (F.snow));
        glUniform1f (snow.u ("uCount"), f32 (snowN * (1 - 0.5 * Q.thin)));
        uniform3 (snow.u ("uSunV"), F.sunV); uniform3 (snow.u ("uLavaEnv"), F.lavaEnv);
        glDrawArrays (GL_TRIANGLES, 0, (GLsizei) std::ceil (snowN * F.snow * 1.07) * 6);
        glDisable (GL_BLEND); glDisable (GL_SCISSOR_TEST);
    }

    for (const GLenum unit : { GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE5, GL_TEXTURE7, GL_TEXTURE8, GL_TEXTURE9, GL_TEXTURE10 })
    {
        glActiveTexture (unit); glBindTexture (GL_TEXTURE_2D, 0);
    }
    glActiveTexture (GL_TEXTURE0);
    glDisable (GL_DEPTH_TEST);
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::io
