// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The metal world's renderer: metal-gl.js, pass for pass, from its shaders (resources/shaders/metal). The per-frame
// numbers the GPU is fed are worked out here as the prototype works them out (in doubles, stored as floats); the
// world's own (the fires, the ribbons, the drive) come from MetalWorld.

#include "MetalGl.h"

#include <cmath>
#include <cstring>

namespace orbitamp::metal
{

using namespace orbitamp::gpu;

namespace
{
    using W = MetalWorld;
    using V3 = W::V3;
    constexpr double pi = W::pi;

    struct Level { double cell; int n; };
    constexpr std::array<Level, 3> levels { { { 2.0, 96 }, { 6.0, 96 }, { 24.0, 96 } } };
    constexpr double zNear = 0.5, zFar = 9000.0, farCut = W::farCut;
    constexpr double fadeSeconds = 1.5;
    constexpr int ventW = 8, sparks = 64, puffs = 5, embers = 1400;
    constexpr int fireP = 48, maxEmit = W::maxEmit, maxRib = W::maxRib, maxLetters = 40, maxCloth = 24;
    constexpr double bandAzTurns = -0.09, bandTilt = 34 * pi / 180;
    constexpr std::array<double, 2> stagePx { 1672, 941 };
    constexpr double cabinFlashFull = 0.75;

    // The attributes' locations, the same for every program that reads them (no program reads two that share one).
    const std::vector<gpu::Attribute> attributes {
        { "aGrid", 0 },
        { "aNrm", 1 }, { "aE0", 1 }, { "aA", 1 }, { "aL0", 1 }, { "aB0", 1 }, { "aP0", 1 },
        { "aMat", 2 }, { "aE1", 2 }, { "aB", 2 }, { "aL1", 2 }, { "aB1", 2 }, { "aN0", 2 },
        { "aI0", 3 }, { "aE2", 3 }, { "aC", 3 }, { "aL2", 3 }, { "aB2", 3 }, { "aP1", 3 },
        { "aI1", 4 }, { "aK", 4 }, { "aL3", 4 }, { "aB3", 4 }, { "aN1", 4 },
        { "aUV", 5 }, { "aCol", 6 }, { "aKind", 7 }, { "aTex", 8 } };

    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double clamp01 (double x, double lo, double hi) noexcept { return std::min (hi, std::max (lo, x)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp01 ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline const void* offset (std::size_t bytes) noexcept { return reinterpret_cast<const void*> (bytes); }
    inline V3 norm3 (const V3& v) noexcept { double l = std::sqrt (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (! (l > 0)) l = 1; return { v[0] / l, v[1] / l, v[2] / l }; }
    inline V3 cross3 (const V3& a, const V3& b) noexcept { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    using M3 = std::array<float, 9>;
    using M4 = std::array<float, 16>;

    M4 multiply (const M4& a, const M4& b) noexcept
    {
        M4 o {};
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
            {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += (double) a[(size_t) (k * 4 + r)] * b[(size_t) (c * 4 + k)];
                o[(size_t) (c * 4 + r)] = (float) s;
            }
        return o;
    }
    /** A yaw and a scale as a column-major mat3 (y up). */
    M3 yawMat (double yaw, double k = 1) noexcept
    {
        const double c = std::cos (yaw) * k, s = std::sin (yaw) * k;
        return { (float) c, 0, (float) s, 0, (float) k, 0, (float) -s, 0, (float) c };
    }
    /** A yaw, a scale and a lean (radians about the thing's own x, then its z). */
    M3 leanMat (double yaw, double k, double lx, double lz) noexcept
    {
        const double cx = std::cos (lx), sx = std::sin (lx), cz = std::cos (lz), sz = std::sin (lz), c = std::cos (yaw), s = std::sin (yaw);
        const V3 L[3] = { { cz, sz, 0 }, { -sz * cx, cz * cx, sx }, { sz * sx, -cz * sx, cx } };
        M3 o {};
        for (size_t i = 0; i < 3; ++i)
        {
            const V3& v = L[i];
            o[i * 3 + 0] = (float) ((c * v[0] - s * v[2]) * k);
            o[i * 3 + 1] = (float) (v[1] * k);
            o[i * 3 + 2] = (float) ((s * v[0] + c * v[2]) * k);
        }
        return o;
    }
    inline V3 mulM3 (const M3& M, const V3& v) noexcept
    {
        return { M[0] * v[0] + M[3] * v[1] + M[6] * v[2], M[1] * v[0] + M[4] * v[1] + M[7] * v[2], M[2] * v[0] + M[5] * v[1] + M[8] * v[2] };
    }

    /** A texture from a picture: straight or premultiplied as the prototype's browser uploads it, with mips or not. */
    GLuint pictureTexture (const MetalGl::Picture& p, bool premultiplied, bool mips)
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

    /** A picture drawn into a cell of another, scaled to it, as a canvas's drawImage puts it there: each target pixel
        the mean of the source's pixels under it (premultiplied while averaged, straight after). */
    void drawInto (MetalGl::Picture& dst, const MetalGl::Picture& src, int x0, int y0, int w, int h)
    {
        if (! src.valid() || w <= 0 || h <= 0) return;
        const double sx = (double) src.width / w, sy = (double) src.height / h;
        for (int y = 0; y < h; ++y)
        {
            const int ty = y0 + y;
            if (ty < 0 || ty >= dst.height) continue;
            const double fy0 = y * sy, fy1 = std::max (fy0 + 1e-9, (y + 1) * sy);
            for (int x = 0; x < w; ++x)
            {
                const int tx = x0 + x;
                if (tx < 0 || tx >= dst.width) continue;
                const double fx0 = x * sx, fx1 = std::max (fx0 + 1e-9, (x + 1) * sx);
                double acc[4] {}, area = 0;
                const int iy0 = (int) std::floor (fy0), iy1 = std::min (src.height, (int) std::ceil (fy1));
                const int ix0 = (int) std::floor (fx0), ix1 = std::min (src.width, (int) std::ceil (fx1));
                for (int yy = iy0; yy < iy1; ++yy)
                {
                    const double wy = std::min (fy1, yy + 1.0) - std::max (fy0, (double) yy);
                    for (int xx = ix0; xx < ix1; ++xx)
                    {
                        const double wgt = wy * (std::min (fx1, xx + 1.0) - std::max (fx0, (double) xx));
                        const std::uint8_t* p = &src.rgba[((size_t) yy * (size_t) src.width + (size_t) xx) * 4];
                        const double a = p[3] * wgt;
                        acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
                        area += wgt;
                    }
                }
                std::uint8_t* q = &dst.rgba[((size_t) ty * (size_t) dst.width + (size_t) tx) * 4];
                const double alpha = area > 0 ? acc[3] / area : 0;
                for (int k = 0; k < 3; ++k) q[k] = (std::uint8_t) (acc[3] > 0 ? std::lround (std::min (255.0, acc[k] / acc[3])) : 0);
                q[3] = (std::uint8_t) std::lround (std::min (255.0, alpha));
            }
        }
    }
}

//==============================================================================
int MetalGl::Program::u (const char* name) const
{
    const auto it = at.find (name);
    if (it != at.end()) return it->second;
    const int loc = glGetUniformLocation (id, name);
    at.emplace (name, loc);
    return loc;
}

bool MetalGl::compile (Program& p, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written)
{
    std::string log;
    p.at.clear();
    p.id = gpu::program (gpu::header() + shaders::source (vertexPath, written), gpu::header() + shaders::source (fragmentPath, written), log, attributes);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("metal: " + trouble);
        return false;
    }
    return true;
}

void MetalGl::setSkip (const std::string& list)
{
    skip.clear();
    size_t at = 0;
    while (at <= list.size())
    {
        const size_t comma = std::min (list.find (',', at), list.size());
        if (comma > at) skip.push_back (list.substr (at, comma - at));
        at = comma + 1;
    }
}

bool MetalGl::skips (const char* what) const noexcept
{
    for (const auto& s : skip) if (s == what) return true;
    return false;
}

bool MetalGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("metal: " + trouble);
        return false;
    }
    if (! world.isBuilt())
        world.build();

    if (! compile (ground, "metal/ground.vert", "metal/ground.frag", "ground", { { "hole", "#define HOLE 0\n#define FARCUT 0\n" } })
        || ! compile (groundH, "metal/ground.vert", "metal/ground.frag", "outer ground", { { "hole", "#define HOLE 1\n#define FARCUT 1\n" } })
        || ! compile (groundHN, "metal/ground.vert", "metal/ground.frag", "middle ground", { { "hole", "#define HOLE 1\n#define FARCUT 0\n" } })
        || ! compile (sky, "metal/sky.vert", "metal/sky.frag", "sky", { { "skypass", "#define SKY_SMOOTH 0\n" } })
        || ! compile (skySmooth, "metal/sky.vert", "metal/sky.frag", "sky's smooth parts", { { "skypass", "#define SKY_SMOOTH 1\n" } })
        || ! compile (cabinPass, "metal/post.vert", "metal/cabin.frag", "cabin")
        || ! compile (prop, "metal/prop.vert", "metal/prop.frag", "prop")
        || ! compile (mesh, "metal/mesh.vert", "metal/mesh.frag", "mesh")
        || ! compile (dragon, "metal/dragon.vert", "metal/mesh.frag", "dragon")
        || ! compile (sign, "metal/sign.vert", "metal/sign.frag", "sign")
        || ! compile (letters, "metal/letters.vert", "metal/letters.frag", "letters")
        || ! compile (fire, "metal/fire.vert", "metal/fire.frag", "fire")
        || ! compile (ribbon, "metal/ribbon.vert", "metal/ribbon.frag", "ribbon")
        || ! compile (spark, "metal/spark.vert", "metal/spark.frag", "spark")
        || ! compile (steam, "metal/steam.vert", "metal/steam.frag", "steam")
        || ! compile (ember, "metal/ember.vert", "metal/ember.frag", "ember")
        || ! compile (post, "metal/post.vert", "metal/post.frag", "post"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeBuffers();
    makeArt();

    while (glGetError() != GL_NO_ERROR) {}
    if (gpu::timerQueries())
    {
        glGenQueries ((GLsizei) query.size(), query.data());
        gpu::beginTimer (query[0]);
        gpu::endTimer();
        queriesWork = glGetError() == GL_NO_ERROR;
        if (queriesWork)
            gpu::timerNanoseconds (query[0]);
        else
        {
            glDeleteQueries ((GLsizei) query.size(), query.data());
            query.fill (0);
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    return true;
}

void MetalGl::destroy()
{
    const auto delBuf = [] (std::uint32_t& id) { if (id != 0) glDeleteBuffers (1, &id); id = 0; };
    const auto delVao = [] (std::uint32_t& id) { if (id != 0) glDeleteVertexArrays (1, &id); id = 0; };
    const auto delTex = [] (std::uint32_t& id) { if (id != 0) glDeleteTextures (1, &id); id = 0; };
    const auto delGpu = [&] (Gpu& g) { delVao (g.vao); delBuf (g.vbo); delBuf (g.ibo); delBuf (g.inst); g = {}; };
    for (auto& [n, g] : propBufs) delGpu (g);
    propBufs.clear();
    byMesh.clear(); spireOf.clear();
    for (auto& [n, m] : meshes) { delVao (m.vao); delBuf (m.vbo); delBuf (m.ibo); }
    meshes.clear();
    delVao (dragonM.vao); delBuf (dragonM.vbo); dragonM = {};
    delGpu (clothBuf); delGpu (fireBuf); delGpu (ribBuf); delGpu (letBuf);
    delVao (vaoGrid); delBuf (vboGrid); delBuf (iboGrid); gridCount = 0;
    delVao (vaoEmpty); delBuf (vboEmpty);
    for (auto* t : { &texBase, &texDetail, &texNoise, &texCells, &texRoad, &texLetters, &texBanners, &fboColour, &texSmooth, &texPost })
        delTex (*t);
    for (auto* f : { &fboSmooth, &fboPost })
        if (*f != 0) { glDeleteFramebuffers (1, f); *f = 0; }
    smoothW = smoothH = postW = postH = 0;
    if (texDashFlash != texDash) delTex (texDashFlash);
    texDashFlash = 0;
    delTex (texDash);
    hasGlassBox = false;
    bannerCells.clear();
    if (fboDepth != 0) glDeleteRenderbuffers (1, &fboDepth);
    if (fboOutColour != 0) glDeleteRenderbuffers (1, &fboOutColour);
    fboDepth = fboOutColour = 0;
    if (fbo != 0) glDeleteFramebuffers (1, &fbo);
    if (fboOut != 0) glDeleteFramebuffers (1, &fboOut);
    fbo = fboOut = 0;
    fbW = fbH = outW = outH = subW = subH = 0;
    outerKept = false;
    Q = {};
    lastFrameMs = -1.0;
    envHot = -1.0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &ground, &groundH, &groundHN, &sky, &skySmooth, &cabinPass, &prop, &mesh, &dragon, &sign, &letters, &fire, &ribbon, &spark, &steam, &ember, &post })
    {
        if (p->id != 0) glDeleteProgram (p->id);
        p->id = 0;
        p->at.clear();
    }
}

void MetalGl::makeTextures()
{
    const auto tex = [] (std::uint32_t& id, int size, GLenum ifmt, GLenum type, const void* data, GLint filter, GLint wrap, bool mips)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D (GL_TEXTURE_2D, 0, (GLint) ifmt, size, size, 0, GL_RGBA, type, data);
        if (mips) glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    };
    // The base and the road map in half floats (their values already rounded to them), the rest RGBA8.
    tex (texBase, W::texSize, GL_RGBA16F, GL_FLOAT, world.base().data(), GL_LINEAR, GL_REPEAT, false);
    tex (texDetail, W::detailSize, GL_RGBA8, GL_UNSIGNED_BYTE, world.detail().data(), GL_LINEAR, GL_REPEAT, true);
    tex (texNoise, W::noiseSize, GL_RGBA8, GL_UNSIGNED_BYTE, world.noise().data(), GL_LINEAR, GL_REPEAT, false);
    tex (texCells, W::cellN, GL_RGBA8, GL_UNSIGNED_BYTE, world.cells().data(), GL_NEAREST, GL_REPEAT, false);
    tex (texRoad, W::roadN, GL_RGBA16F, GL_FLOAT, world.roadMap().data(), GL_LINEAR, GL_CLAMP_TO_EDGE, false);
    glBindTexture (GL_TEXTURE_2D, 0);
}

bool MetalGl::loadMesh (const std::string& key, const std::vector<std::uint8_t>& buf)
{
    const auto m = W::readMesh (buf.data(), buf.size());
    if (! m.ok) return false;
    FileMesh fm;
    fm.count = (int) m.ni; fm.zmin = m.zmin; fm.zmax = m.zmax;
    glGenVertexArrays (1, &fm.vao);
    glBindVertexArray (fm.vao);
    glGenBuffers (1, &fm.vbo);
    glBindBuffer (GL_ARRAY_BUFFER, fm.vbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) m.nv * 40, buf.data() + m.vertexAt, GL_STATIC_DRAW);
    struct A { GLuint loc; GLint size; GLenum type; GLboolean nrm; std::size_t at; };
    for (const A& a : { A { 0, 3, GL_FLOAT, GL_FALSE, 0 }, A { 1, 3, GL_FLOAT, GL_FALSE, 12 }, A { 5, 2, GL_FLOAT, GL_FALSE, 24 },
                        A { 6, 3, GL_UNSIGNED_BYTE, GL_TRUE, 32 }, A { 7, 1, GL_UNSIGNED_BYTE, GL_FALSE, 35 }, A { 8, 1, GL_UNSIGNED_BYTE, GL_FALSE, 36 } })
    {
        glEnableVertexAttribArray (a.loc);
        glVertexAttribPointer (a.loc, a.size, a.type, a.nrm, 40, offset (a.at));
    }
    glGenBuffers (1, &fm.ibo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, fm.ibo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) m.ni * 4, buf.data() + m.indexAt, GL_STATIC_DRAW);
    glBindVertexArray (0);
    meshes[key] = fm;
    return true;
}

void MetalGl::makeArt()
{
    // THE CABIN'S PLATES: premultiplied with their mips; the window's box the union of both plates' clear pixels.
    if (art.picture)
    {
        const auto night = art.picture ("dashboard.webp");
        const auto flash = art.picture ("dashboard_flash.webp");
        if (night.valid())
        {
            std::array<double, 4> box {};
            bool any = false;
            for (const Picture* p : { &night, &flash })
            {
                if (! p->valid()) continue;
                const auto b = W::glassBounds (p->rgba.data(), p->width, p->height, (int) std::ceil (4.0 * p->width / stagePx[0]));
                if (! b) continue;
                box = any ? std::array<double, 4> { std::min (box[0], (*b)[0]), std::min (box[1], (*b)[1]), std::max (box[2], (*b)[2]), std::max (box[3], (*b)[3]) } : *b;
                any = true;
            }
            texDash = pictureTexture (night, true, true);
            dashW = night.width; dashH = night.height; platesWhole = false;
            texDashFlash = flash.valid() ? pictureTexture (flash, true, true) : texDash;
            if (texDashFlash == 0) texDashFlash = texDash;
            glassBox = box; hasGlassBox = any && texDash != 0;
        }
    }
    // THE BURNING LETTERS: the owner's pictures packed into the atlas found (find), the word CENSORED among them.
    if (art.picture && found.letters.width > 0 && ! found.letters.pieces.empty())
    {
        Picture atlas;
        atlas.width = found.letters.width; atlas.height = found.letters.height;
        atlas.rgba.assign ((size_t) atlas.width * (size_t) atlas.height * 4, 0);
        for (const auto& piece : found.letters.pieces)
        {
            Picture p;
            if (piece.file.empty()) { if (art.censored) p = art.censored(); }
            else p = art.picture ("world/letters/" + piece.file.substr (0, piece.file.size() - 4) + ".webp");
            drawInto (atlas, p, (int) piece.cell[0], (int) piece.cell[1], (int) piece.cell[2], (int) piece.cell[3]);
        }
        texLetters = pictureTexture (atlas, false, true);
        lettersAtlas = { (double) atlas.width, (double) atlas.height };
    }
    // THE BANNERS' ART: the atlas and its cells.
    if (art.picture && ! found.bannersTxt.empty())
    {
        const auto atlas = art.picture ("world/banners/atlas.webp");
        if (atlas.valid())
        {
            bannerCells = W::readBanners (found.bannersTxt, atlas.width, atlas.height);
            if (! bannerCells.empty()) texBanners = pictureTexture (atlas, false, true);
        }
    }
    // THE MESHES: the owner's, by role; the groves' guitars; the dragon with its wing-beat.
    if (art.bytes)
    {
        for (const auto& [key, path] : found.meshOf)
            loadMesh (key, art.bytes (path));
        const auto meshBytes = art.bytes ("world/dragon3d/dragon.mesh");
        const auto anim = art.bytes ("world/dragon3d/dragon.anim");
        const auto m = W::readMesh (meshBytes.data(), meshBytes.size());
        if (m.ok && anim.size() >= 24 && std::memcmp (anim.data(), "CBANIM1", 7) == 0)
        {
            std::uint32_t nv = 0, nf = 0; float unit = 0;
            std::memcpy (&nv, anim.data() + 8, 4); std::memcpy (&nf, anim.data() + 12, 4); std::memcpy (&unit, anim.data() + 20, 4);
            if (nv == m.nv && nf > 0 && anim.size() >= 24 + (size_t) nf * nv * 10)
            {
                DragonGpu& D = dragonM;
                D.nv = (int) nv; D.nf = (int) nf; D.unit = unit; D.count = (int) m.ni;
                glGenVertexArrays (1, &D.vao);
                glBindVertexArray (D.vao);
                GLuint meshVbo = 0, ibo = 0;
                glGenBuffers (1, &meshVbo);
                glBindBuffer (GL_ARRAY_BUFFER, meshVbo);
                glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) m.nv * 40, meshBytes.data() + m.vertexAt, GL_STATIC_DRAW);
                glEnableVertexAttribArray (6); glVertexAttribPointer (6, 3, GL_UNSIGNED_BYTE, GL_TRUE, 40, offset (32));
                glEnableVertexAttribArray (7); glVertexAttribPointer (7, 1, GL_UNSIGNED_BYTE, GL_FALSE, 40, offset (35));
                glGenBuffers (1, &D.vbo);
                glBindBuffer (GL_ARRAY_BUFFER, D.vbo);
                glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) ((size_t) nf * nv * 10), anim.data() + 24, GL_STATIC_DRAW);
                for (GLuint loc = 1; loc <= 4; ++loc) glEnableVertexAttribArray (loc);
                glGenBuffers (1, &ibo);
                glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ibo);
                glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) m.ni * 4, meshBytes.data() + m.indexAt, GL_STATIC_DRAW);
                glBindVertexArray (0);
                // the vertex array holds both buffers; the names go with the dragon's own (meshes["dragon"])
                meshes["dragon"] = FileMesh { 0, meshVbo, ibo, (int) m.ni, m.zmin, m.zmax };
                D.ok = true;
            }
        }
    }
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void MetalGl::makeBuffers()
{
    vaoEmpty = gpu::attributeless (vboEmpty);

    // THE GROUND'S LATTICE (one for the three levels: each 96 x 96 cells).
    {
        const int NL = levels[0].n;
        std::vector<float> grid ((size_t) (NL + 1) * (NL + 1) * 2);
        for (int j = 0; j <= NL; ++j) for (int i = 0; i <= NL; ++i) { grid[(size_t) (j * (NL + 1) + i) * 2] = (float) i; grid[(size_t) (j * (NL + 1) + i) * 2 + 1] = (float) j; }
        std::vector<std::uint16_t> idx ((size_t) NL * NL * 6);
        for (int j = 0; j < NL; ++j)
            for (int i = 0; i < NL; ++i)
            {
                const size_t o = (size_t) (j * NL + i) * 6; const int v = j * (NL + 1) + i;
                idx[o] = (std::uint16_t) v; idx[o + 1] = (std::uint16_t) (v + 1); idx[o + 2] = (std::uint16_t) (v + NL + 2);
                idx[o + 3] = (std::uint16_t) v; idx[o + 4] = (std::uint16_t) (v + NL + 2); idx[o + 5] = (std::uint16_t) (v + NL + 1);
            }
        glGenVertexArrays (1, &vaoGrid);
        glBindVertexArray (vaoGrid);
        glGenBuffers (1, &vboGrid);
        glBindBuffer (GL_ARRAY_BUFFER, vboGrid);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (grid.size() * 4), grid.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (0); glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glGenBuffers (1, &iboGrid);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboGrid);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (idx.size() * 2), idx.data(), GL_STATIC_DRAW);
        glBindVertexArray (0);
        gridCount = (int) idx.size();
    }

    // THE PROPS: a vertex array per procedural mesh with its instance buffer (8 floats); the hill a mesh of its own.
    const auto propVao = [] (const W::Mesh& m, int maxInst)
    {
        Gpu g;
        glGenVertexArrays (1, &g.vao);
        glBindVertexArray (g.vao);
        glGenBuffers (1, &g.vbo);
        glBindBuffer (GL_ARRAY_BUFFER, g.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (m.v.size() * 4), m.v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray (0); glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 28, offset (0));
        glEnableVertexAttribArray (1); glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, 28, offset (12));
        glEnableVertexAttribArray (2); glVertexAttribPointer (2, 1, GL_FLOAT, GL_FALSE, 28, offset (24));
        glGenBuffers (1, &g.inst);
        glBindBuffer (GL_ARRAY_BUFFER, g.inst);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) maxInst * 32, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray (3); glVertexAttribPointer (3, 4, GL_FLOAT, GL_FALSE, 32, offset (0)); glVertexAttribDivisor (3, 1);
        glEnableVertexAttribArray (4); glVertexAttribPointer (4, 4, GL_FLOAT, GL_FALSE, 32, offset (16)); glVertexAttribDivisor (4, 1);
        glGenBuffers (1, &g.ibo);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, g.ibo);
        g.wide = m.wide();
        if (g.wide)
            glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (m.idx.size() * 4), m.idx.data(), GL_STATIC_DRAW);
        else
        {
            const std::vector<std::uint16_t> narrow (m.idx.begin(), m.idx.end());
            glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (narrow.size() * 2), narrow.data(), GL_STATIC_DRAW);
        }
        glBindVertexArray (0);
        g.count = (int) m.idx.size(); g.max = maxInst;
        for (size_t i = 0; i + 2 < m.v.size(); i += 7)
            g.r = std::max (g.r, std::sqrt ((double) m.v[i] * m.v[i] + (double) m.v[i + 1] * m.v[i + 1] + (double) m.v[i + 2] * m.v[i + 2]));
        return g;
    };
    auto PM = W::propMeshes();
    PM["hill"] = world.hillMesh();
    const auto& props = world.props();
    for (int i = 0; i < (int) props.size(); ++i)
    {
        auto it = std::find_if (byMesh.begin(), byMesh.end(), [&] (const auto& q) { return q.first == props[(size_t) i].mesh; });
        if (it == byMesh.end()) { byMesh.push_back ({ props[(size_t) i].mesh, {} }); it = byMesh.end() - 1; }
        it->second.push_back (i);
    }
    byMesh.push_back ({ "hill", { -1 } });
    for (const auto& [name, list] : byMesh)
        if (PM.count (name)) propBufs[name] = propVao (PM[name], (int) list.size());
    for (int k = 0; k < 5; ++k)
        propBufs["find" + std::to_string (k)] = propVao (PM["find" + std::to_string (k)], 3);
    spireOf.assign (props.size(), -1);
    for (size_t i = 0; i < world.spires().size(); ++i)
        if (world.spires()[i].prop >= 0 && world.spires()[i].prop < (int) props.size()) spireOf[(size_t) world.spires()[i].prop] = (int) i;

    // THE CLOTH, THE FIRE, THE RIBBONS, THE LETTERS: instance buffers, the quads from gl_VertexID.
    const auto instVao = [] (int vectors, int max)
    {
        Gpu g;
        glGenVertexArrays (1, &g.vao);
        glBindVertexArray (g.vao);
        glGenBuffers (1, &g.inst);
        glBindBuffer (GL_ARRAY_BUFFER, g.inst);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) max * vectors * 16, nullptr, GL_DYNAMIC_DRAW);
        for (int i = 0; i < vectors; ++i)
        {
            glEnableVertexAttribArray ((GLuint) (1 + i));
            glVertexAttribPointer ((GLuint) (1 + i), 4, GL_FLOAT, GL_FALSE, vectors * 16, offset ((size_t) i * 16));
            glVertexAttribDivisor ((GLuint) (1 + i), 1);
        }
        glBindVertexArray (0);
        g.max = max;
        return g;
    };
    clothBuf = instVao (4, maxCloth);
    fireBuf = instVao (3, maxEmit);
    ribBuf = instVao (4, maxRib);
    letBuf = instVao (4, maxLetters);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

bool MetalGl::ensureFrame (int w, int h, int cw, int ch)
{
    // The scene: its colour a texture, its depth a renderbuffer thrown away after the scene. While the cabin is drawn
    // only the window's box of it can show (the plate is opaque round it): the frame is that box and a pixel round it
    // (left clear, as the whole frame was), drawn through a viewport that is the whole view's, moved, so that every
    // fragment is the one the whole frame would have had.
    int bx0 = 0, by0 = 0, bx1 = w, by1 = h;
    if (drawsCabin() && hasGlassBox)
    {
        // its corner on even pixels, so that the 2 x 2 quads a fragment's derivatives come from are the whole frame's
        bx0 = std::max (0, ((int) std::floor (glassBox[0] * w) - 1) & ~1); bx1 = std::min (w, (int) std::ceil (glassBox[2] * w) + 1);
        by0 = std::max (0, ((int) std::floor ((1 - glassBox[3]) * h) - 1) & ~1); by1 = std::min (h, (int) std::ceil ((1 - glassBox[1]) * h) + 1);
    }
    sceneAt = { bx0, by0 };
    fbW = w; fbH = h;
    w = bx1 - bx0; h = by1 - by0;
    if (fbo == 0 || w != subW || h != subH)
    {
        if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); glGenRenderbuffers (1, &fboDepth); }
        glBindTexture (GL_TEXTURE_2D, fboColour);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);
        glBindRenderbuffer (GL_RENDERBUFFER, fboDepth);
        glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer (GL_RENDERBUFFER, 0);
        glBindFramebuffer (GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
        glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
        if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fbW = fbH = subW = subH = 0; return false; }
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
        if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { outW = outH = 0; return false; }
        outW = cw; outH = ch;
        outerKept = false;
    }
    return true;
}

bool MetalGl::ensureTarget (std::uint32_t& fb, std::uint32_t& tex, int& tw, int& th, int w, int h, bool floats)
{
    // A frame of its own with one colour texture (filtered, clamped), for a pass another pass reads scaled.
    if (fb != 0 && w == tw && h == th)
        return true;
    if (fb == 0) { glGenFramebuffers (1, &fb); glGenTextures (1, &tex); }
    glBindTexture (GL_TEXTURE_2D, tex);
    glTexImage2D (GL_TEXTURE_2D, 0, floats ? (GLint) GL_RGBA16F : (GLint) GL_RGBA8, w, h, 0, GL_RGBA, floats ? (GLenum) GL_FLOAT : (GLenum) GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { tw = th = 0; return false; }
    tw = w; th = h;
    return true;
}

void MetalGl::follow (double scale, double seconds)
{
    // A rung down (0.75) the aurora's finest rays; at the foot (0.5) half the fire's particles, half the embers, the
    // near lattice coarse beyond 45 m — each faded in over 1.5 s.
    Q.level = scale > 0.99 ? 0 : scale > 0.74 ? 1 : 2;
    const double d = Q.started ? std::max (0.0, seconds) / fadeSeconds : 1;
    Q.started = true;
    Q.flat += clamp01 ((Q.level >= 1 ? 1 : 0) - Q.flat, -d, d);
    Q.thin += clamp01 ((Q.level >= 2 ? 1 : 0) - Q.thin, -d, d);
}

void MetalGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void MetalGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (post.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, (int) std::lround ((double) w * scale)), rh = std::max (2, (int) std::lround ((double) h * scale));
    if (! ensureFrame (rw, rh, w, h))
        return;
    platesFor (w, h);
    const double now = gpu::milliseconds();
    follow (scale, lastFrameMs < 0 ? 0.0 : (now - lastFrameMs) * 0.001);
    lastFrameMs = now;
    // The light the world is in, eased over two seconds of the drive (metal-gl.js's envHot), once a frame.
    {
        const auto& s = world.state();
        const double hot = world.probe (s.x, s.z).hot;
        envHot = envHot < 0 ? hot : envHot + (hot - envHot) * std::min (1.0, s.dt / 2);
    }

    // The world's fires and ribbons for this frame, worked out before the timer starts: the CPU's part, not the GPU's.
    frameFires = world.fires();
    frameRibbons = world.ribbons (! skips ("beams"));

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

void MetalGl::platesFor (int cw, int ch)
{
    // The plates are read through their mips only where the frame is smaller than they are: at their own size or over
    // it a pixel never covers more than a texel, the mips weigh nothing — and a trilinear read there still fetches two.
    const bool whole = cw >= dashW && ch >= dashH;
    if (texDash == 0 || whole == platesWhole)
        return;
    platesWhole = whole;
    for (const auto id : { texDash, texDashFlash })
    {
        glBindTexture (GL_TEXTURE_2D, id);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, whole ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    }
    glBindTexture (GL_TEXTURE_2D, 0);
}

bool MetalGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
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
void MetalGl::drawScene (int rw, int rh, int cw, int ch)
{
    const auto& s = world.state();
    const bool cabin = drawsCabin();
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const std::array<float, 2> camFrac { (float) (s.x - ccx), (float) (s.z - ccz) };
    const std::array<float, 3> camRel { camFrac[0], (float) s.camY, camFrac[1] };
    const auto rel = [&] (double x, double y, double z) { return std::array<float, 3> { (float) (x - ccx), (float) y, (float) (z - ccz) }; };
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);
    const double aspect = (double) rw / rh, th = tanH, tv = th / aspect;
    M4 Pm {};
    Pm[0] = (float) (1 / th); Pm[5] = (float) (1 / tv); Pm[10] = (float) (-(zFar + zNear) / (zFar - zNear)); Pm[11] = -1;
    Pm[14] = (float) (-2 * zFar * zNear / (zFar - zNear)); Pm[9] = (float) -shear;
    const auto basis = W::cameraBasis (s.heading, s.pitch);
    M4 Vm {};
    {
        const double cx = camRel[0], cy = camRel[1], cz = camRel[2];
        const auto& r = basis.r; const auto& u = basis.u; const auto& f = basis.f;
        Vm[0] = (float) r[0]; Vm[4] = (float) r[1]; Vm[8] = (float) r[2];    Vm[12] = (float) -(r[0] * cx + r[1] * cy + r[2] * cz);
        Vm[1] = (float) u[0]; Vm[5] = (float) u[1]; Vm[9] = (float) u[2];    Vm[13] = (float) -(u[0] * cx + u[1] * cy + u[2] * cz);
        Vm[2] = (float) -f[0]; Vm[6] = (float) -f[1]; Vm[10] = (float) -f[2]; Vm[14] = (float) (f[0] * cx + f[1] * cy + f[2] * cz);
        Vm[15] = 1;
    }
    const M4 VP = multiply (Pm, Vm);
    const std::array<double, 2> off0 { fract (ccx / W::L0), fract (ccz / W::L0) },
                                off1 { fract (ccx / W::L1) + W::off1[0], fract (ccz / W::L1) + W::off1[1] },
                                off2 { fract (ccx / W::L2) + W::off2[0], fract (ccz / W::L2) + W::off2[1] };
    const double time = std::fmod (s.time, 100000.0);
    const double pxAng = 2 * tv / rh;
    const double storm = s.storm, calm = s.calm;
    const double aurK = (1 + 1.3 * storm) * (0.85 + 0.15 * std::sin (s.aurora * 0.13));
    const std::array<float, 3> aurAmb { (float) (0.05 * aurK), (float) (0.16 * aurK), (float) (0.09 * aurK) },
                               starAmb { 0.030f, 0.034f, 0.048f }, farCol { 0.030f, 0.014f, 0.010f };
    std::array<float, 24> crA {}, crB {};
    {
        const auto& cr = world.crags();
        for (size_t i = 0; i < std::min<size_t> (cr.size(), 6); ++i)
        {
            const auto& c = cr[i];
            crA[i * 4] = (float) (c.a[0] - ccx); crA[i * 4 + 1] = (float) (c.a[1] - ccz); crA[i * 4 + 2] = (float) (c.b[0] - ccx); crA[i * 4 + 3] = (float) (c.b[1] - ccz);
            crB[i * 4] = (float) c.r; crB[i * 4 + 1] = (float) c.h;
        }
    }
    const double hitAge = std::min (s.hitT, 1e4);
    const double flash = skips ("flash") ? 0 : s.flash;

    // THE FIRES this frame and the two that light what is near; THE RIBBONS.
    const auto& fires = frameFires;
    const auto& ribbons = frameRibbons;

    const auto bandH = V3 { std::sin (bandAzTurns * 2 * pi), 0, -std::cos (bandAzTurns * 2 * pi) };
    const auto bandTan = V3 { std::cos (bandAzTurns * 2 * pi), 0, std::sin (bandAzTurns * 2 * pi) };
    const auto bandT = norm3 ({ bandTan[0] * std::cos (bandTilt), std::sin (bandTilt), bandTan[2] * std::cos (bandTilt) });
    const auto bandN = norm3 (cross3 (bandH, bandT));
    const auto& tun = world.tunnel();
    const std::array<float, 4> tunnelU { (float) W::thetaAt (tun.from), (float) W::thetaAt (tun.to), (float) tun.hw, 1 };

    const auto u3 = [] (int loc, const std::array<float, 3>& v) { glUniform3fv (loc, 1, v.data()); };
    const auto u3d = [] (int loc, const V3& v) { glUniform3f (loc, (float) v[0], (float) v[1], (float) v[2]); };
    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.u ("uBase"), 0); glUniform1i (p.u ("uRoad"), 7);
        glUniform2f (p.u ("uCamCell"), (float) ccx, (float) ccz); glUniform2f (p.u ("uCamFrac"), camFrac[0], camFrac[1]);
        glUniform2f (p.u ("uOff0"), (float) off0[0], (float) off0[1]); glUniform2f (p.u ("uOff1"), (float) off1[0], (float) off1[1]);
        glUniform2f (p.u ("uOff2"), (float) off2[0], (float) off2[1]);
        u3 (p.u ("uCamRel"), camRel);
        glUniform1f (p.u ("uTime"), (float) time);
        glUniform2f (p.u ("uLoopC"), (float) (W::loopC[0] - ccx), (float) (W::loopC[1] - ccz));
        glUniform2f (p.u ("uRoadOrg"), (float) (W::roadOrg[0] - ccx), (float) (W::roadOrg[1] - ccz));
        glUniform4fv (p.u ("uCragA[0]"), 6, crA.data()); glUniform4fv (p.u ("uCragB[0]"), 6, crB.data());
        glUniformMatrix4fv (p.u ("uViewProj"), 1, GL_FALSE, VP.data());
        u3 (p.u ("uAurAmb"), aurAmb); u3 (p.u ("uStarAmb"), starAmb); u3 (p.u ("uFar"), farCol);
        glUniform1f (p.u ("uStorm"), (float) storm); glUniform1f (p.u ("uFlash"), (float) flash);
        u3d (p.u ("uFlashCol"), { s.flashCol[0], s.flashCol[1], s.flashCol[2] });
        glUniform1f (p.u ("uEnvHot"), (float) envHot); glUniform2f (p.u ("uHitT"), (float) hitAge, (float) s.hitS);
        glUniform4fv (p.u ("uPL[0]"), 2, fires.lights.data());
        glUniform4f (p.u ("uLamps"), camFrac[0], camFrac[1], (float) fx, (float) fz);
        glUniform4fv (p.u ("uTunnel"), 1, tunnelU.data());
        glUniform2f (p.u ("uFragOff"), (float) sceneAt[0], (float) sceneAt[1]);
    };

    // THE SKY'S SMOOTH PARTS (the galaxy's band and the aurora), first, into a frame of their own at half the view's pixels
    // each way over the scene's frame: the sky reads them scaled up (sky.frag, SKY_SMOOTH).
    const auto setSky = [&] (const Program& p)
    {
            glUniform1i (p.u ("uNoise"), 2);
            u3d (p.u ("uCamFwd"), basis.f); u3d (p.u ("uCamRight"), basis.r); u3d (p.u ("uCamUp"), basis.u);
            glUniform2f (p.u ("uTanHalf"), (float) th, (float) tv); glUniform1f (p.u ("uShear"), (float) shear); glUniform1f (p.u ("uPxAng"), (float) pxAng);
            glUniform1f (p.u ("uAurT"), (float) std::fmod (s.aurora, 100000.0)); glUniform1f (p.u ("uFlat"), (float) Q.flat);
            {
                // the curtains' terms that vary only with the aurora's clock, in floats as the shader had them
                const float T = (float) std::fmod (s.aurora, 100000.0);
                std::array<float, 16> A {}, B {};
                for (int k = 0; k < 4; ++k)
                {
                    const float fk = (float) k;
                    A[(size_t) k * 4]     = 0.004f * std::sin (T * 0.05f + fk * 1.7f);
                    A[(size_t) k * 4 + 1] = T * 0.9f * std::sin (T * 0.031f + fk);
                    A[(size_t) k * 4 + 2] = 0.010f * std::sin (T * 0.11f + fk * 2.3f);
                    A[(size_t) k * 4 + 3] = T * 0.45f * std::sin (T * 0.023f + fk * 0.7f);
                    B[(size_t) k * 4]     = 0.8f + 0.2f * std::sin (T * 0.13f + fk * 1.9f);
                }
                glUniform4fv (p.u ("uCurtA[0]"), 4, A.data()); glUniform4fv (p.u ("uCurtB[0]"), 4, B.data());
            }
            u3d (p.u ("uBandH"), bandH); u3d (p.u ("uBandT"), bandT); u3d (p.u ("uBandN"), bandN);
    };
    std::array<float, 4> smoothMap { 1, 1, 0, 0 };
    if (! skips ("sky"))
    {
        const int f = smoothDiv;
        const int vw = std::max (1, rw / f), vh = std::max (1, rh / f), hx = sceneAt[0] / f, hy = sceneAt[1] / f;
        const int tw = std::max (1, (subW + f - 1) / f + 1), tht = std::max (1, (subH + f - 1) / f + 1);
        if (ensureTarget (fboSmooth, texSmooth, smoothW, smoothH, tw, tht, true))
        {
            glBindFramebuffer (GL_FRAMEBUFFER, fboSmooth);
            glViewport (-hx, -hy, vw, vh);
            glDisable (GL_DEPTH_TEST); glDisable (GL_BLEND); glDisable (GL_SCISSOR_TEST);
            glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
            glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texNoise);
            glUseProgram (skySmooth.id); glBindVertexArray (vaoEmpty); setCommon (skySmooth); setSky (skySmooth);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            smoothMap = { (float) vw / (float) tw, (float) vh / (float) tht, (float) -hx / (float) tw, (float) -hy / (float) tht };
        }
    }

    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (-sceneAt[0], -sceneAt[1], rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE); glDepthMask (GL_TRUE);
    glDisable (GL_SCISSOR_TEST);
    glClearColor (0, 0, 0, 1);
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const auto bindTex = [] (int unit, std::uint32_t id) { glActiveTexture ((GLenum) GL_TEXTURE0 + (GLenum) unit); glBindTexture (GL_TEXTURE_2D, id); };
    bindTex (0, texBase); bindTex (1, texDetail); bindTex (2, texNoise); bindTex (3, texCells); bindTex (7, texRoad);
    if (cabin && hasGlassBox)
    {
        const int x0 = (int) std::floor (glassBox[0] * rw), x1 = (int) std::ceil (glassBox[2] * rw);
        const int y0 = (int) std::floor ((1 - glassBox[3]) * rh), y1 = (int) std::ceil ((1 - glassBox[1]) * rh);
        glEnable (GL_SCISSOR_TEST); glScissor (x0 - sceneAt[0], y0 - sceneAt[1], x1 - x0, y1 - y0);
    }

    // THE CENSORED HEADSTOCKS: each star's box projected; the nearest four get the mosaic, each grove one CENSORED.
    struct Cens { double x, y, z, R; int side; double D, fade, top; int grove; };
    struct Word { double x, y, z, R, fade; };
    std::vector<Cens> censList;
    std::vector<Word> censWords;
    const auto& slots = world.guitars();
    const auto guitarOf = [&] (size_t i) -> const W::GuitarEntry* { return i < found.deal.chosen.size() ? &found.guitarSet[(size_t) found.deal.chosen[i]] : nullptr; };
    const auto meshFor = [&] (size_t i) -> const FileMesh* { const auto* g = guitarOf (i); if (! g) return nullptr; const auto it = meshes.find ("guitar:" + g->name); return it == meshes.end() ? nullptr : &it->second; };
    {
        struct Cand { V3 at; M3 M; double k; W::Headstock hb; int grove; };
        std::vector<Cand> cand;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto* g = guitarOf (i);
            const auto& sl = slots[i];
            if (g && g->box && meshFor (i)) cand.push_back ({ { sl.x, sl.y, sl.z }, leanMat (sl.yaw, sl.k, sl.lean[0], sl.lean[1]), sl.k, *g->box, sl.grove });
        }
        for (const auto& q : cand)
        {
            const V3 w0 = mulM3 (q.M, { q.hb.c[0], q.hb.c[2], -q.hb.c[1] });
            const double wx = q.at[0] + w0[0], wy = q.at[1] + w0[1], wz = q.at[2] + w0[2];
            const double dx = wx - s.x, dz = wz - s.z, D = std::hypot (dx, dz);
            if (D > 700 || D < 4 || dx * fx + dz * fz < 0.35 * D) continue;
            const double R = 0.5 * std::sqrt (q.hb.s[0] * q.hb.s[0] + q.hb.s[1] * q.hb.s[1] + q.hb.s[2] * q.hb.s[2]) * q.k * 1.05;
            const double hx = wx - ccx, hz = wz - ccz;
            const double Wc = VP[3] * hx + VP[7] * wy + VP[11] * hz + VP[15];
            const double px = Wc > 0.5 ? q.hb.s[2] * q.k * Pm[5] / Wc * 0.5 * stagePx[1] : 0;
            const double fade = smoothstep (12, 18, px);
            if (fade <= 0.001) continue;
            censList.push_back ({ wx, wy, wz, R, q.hb.c[0] < 0 ? -1 : 1, D, fade, wy + 0.5 * q.hb.s[2] * q.k, q.grove });
        }
        std::map<int, double> fadeOf;
        for (const auto& hc : censList) fadeOf[hc.grove] = std::max (fadeOf[hc.grove], hc.fade);
        std::vector<std::pair<int, Word>> by;
        for (const auto& q : cand)
        {
            if (! fadeOf.count (q.grove)) continue;
            if (std::find_if (by.begin(), by.end(), [&] (const auto& b) { return b.first == q.grove; }) != by.end()) continue;
            const V3 w0 = mulM3 (q.M, { q.hb.c[0], q.hb.c[2], -q.hb.c[1] });
            const double R = 0.5 * std::sqrt (q.hb.s[0] * q.hb.s[0] + q.hb.s[1] * q.hb.s[1] + q.hb.s[2] * q.hb.s[2]) * q.k * 1.05;
            by.push_back ({ q.grove, { q.at[0] + w0[0], q.at[1] + w0[1], q.at[2] + w0[2], R, 0 } });
        }
        for (auto& [k, g] : by) { g.fade = fadeOf[k]; censWords.push_back (g); }
        std::stable_sort (censList.begin(), censList.end(), [] (const Cens& a, const Cens& b) { return a.D < b.D; });
        if (censList.size() > 4) censList.resize (4);
    }
    const auto inView = [&] (double x, double z, double r)
    {
        const double dx = x - s.x, dz = z - s.z, D = std::hypot (dx, dz);
        return D - r < farCut && (D < r + 30 || dx * fx + dz * fz > 0.45 * D - r);
    };
    const auto& props = world.props();
    const auto findsNow = world.findsNow();

    // THE STEEL THINGS (procedural), then the owner's meshes, the dragons, the signs: what stands first, the ground after.
    if (! skips ("props"))
    {
        const Program& p = prop;
        glUseProgram (p.id); setCommon (p);
        const double beatPulse = std::pow (0.5 + 0.5 * std::cos (fract (s.beat) * 2 * pi), 6);
        glUniform1f (p.u ("uStageK"), (float) (0.25 + 0.35 * beatPulse + 1.2 * s.hitS * std::exp (-hitAge / 0.6)));
        glUniform1f (p.u ("uHumK"), (float) (0.8 + 0.2 * std::sin (time * 2.3)));
        { const auto& a = world.amps(); glUniform4f (p.u ("uFlood"), (float) (a.fx - ccx), (float) a.fy, (float) (a.fz - ccz), 26); }
        const auto replaced = [&] (const std::string& name)
        {
            return (name == "gate" && meshes.count ("gate")) || (name == "cat" && meshes.count ("cat")) || (name == "anvil" && meshes.count ("anvil"))
                || (name.rfind ("find", 0) == 0 && meshes.count (name));
        };
        const auto drawGroup = [&] (Gpu& B, int n)
        {
            glBindBuffer (GL_ARRAY_BUFFER, B.inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) n * 32, scratch.data());
            glBindVertexArray (B.vao);
            glDrawElementsInstanced (GL_TRIANGLES, B.count, B.wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr, n);
        };
        for (const auto& [name, list] : byMesh)
        {
            const auto bit = propBufs.find (name);
            if (bit == propBufs.end() || replaced (name)) continue;
            Gpu& B = bit->second;
            scratch.assign ((size_t) B.max * 8, 0.0f);
            int n = 0;
            for (const int idx : list)
            {
                if (idx < 0)        // the hill, at the gate's foot
                {
                    const auto& g = world.gate();
                    if (! inView (g.x, g.z, B.r)) continue;
                    const float v[8] { (float) (g.x - ccx), (float) g.y, (float) (g.z - ccz), 0, 1, 0, 0, 0 };
                    std::memcpy (&scratch[(size_t) n * 8], v, sizeof (v));
                    if (++n >= B.max) break;
                    continue;
                }
                const auto& pr = props[(size_t) idx];
                const double ps = pr.s > 0 || pr.s < 0 ? pr.s : 1;     // the JS's `pr.s || 1`
                if (! inView (pr.x, pr.z, B.r * ps)) continue;
                double lx = 0, lz = 0;
                const int si = idx < (int) spireOf.size() ? spireOf[(size_t) idx] : -1;
                if (si >= 0 && si < (int) world.shakes().size())
                {
                    const double t = world.shakes()[(size_t) si];
                    if (t < 4) { const double a = 0.006 * std::exp (-t / 0.9) * std::sin (t * 19); lx = a * std::cos (si * 2.1); lz = a * std::sin (si * 2.1); }
                }
                const float v[8] { (float) (pr.x - ccx), (float) pr.y, (float) (pr.z - ccz), (float) pr.yaw, (float) ps, (float) pr.p, (float) lx, (float) lz };
                std::memcpy (&scratch[(size_t) n * 8], v, sizeof (v));
                if (++n >= B.max) break;
            }
            if (n) drawGroup (B, n);
        }
        for (size_t i = 0; i < findsNow.size(); ++i)
        {
            const auto& f = findsNow[i];
            const std::string name = "find" + std::to_string (f.kind);
            const auto bit = propBufs.find (name);
            if (bit == propBufs.end() || replaced (name)) continue;
            Gpu& B = bit->second;
            if (! inView (f.x, f.z, B.r) || s.findFade < 0.5) continue;
            scratch.assign (8, 0.0f);
            const float v[8] { (float) (f.x - ccx), (float) f.y, (float) (f.z - ccz), (float) f.yaw, 1, (float) i, 0, 0 };
            std::memcpy (scratch.data(), v, sizeof (v));
            drawGroup (B, 1);
        }
    }
    if (! skips ("meshes"))
    {
        const Program& p = mesh;
        glUseProgram (p.id); setCommon (p);
        glUniform1f (p.u ("uMeshSteel"), 0); glUniform4f (p.u ("uMeshPaint"), 0, 0, 0, 0);
        const std::array<float, 8> noCensor {};
        glUniform4fv (p.u ("uCensorC[0]"), 2, noCensor.data()); glUniform4fv (p.u ("uCensorS[0]"), 2, noCensor.data());
        const auto drawMesh = [&] (const FileMesh& m, const std::array<float, 3>& at, const M3& rot, std::array<float, 4> cave = {}, float fade = 1)
        {
            glBindVertexArray (m.vao);
            glUniform3fv (p.u ("uMeshAt"), 1, at.data()); glUniformMatrix3fv (p.u ("uMeshRot"), 1, GL_FALSE, rot.data());
            glUniform4fv (p.u ("uMeshCave"), 1, cave.data()); glUniform1f (p.u ("uMeshFade"), fade);
            glDrawElements (GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
        };
        const auto has = [&] (const char* role) { const auto it = meshes.find (role); return it != meshes.end() && it->second.vao != 0 ? &it->second : nullptr; };
        const auto& g = world.gate();
        if (const auto* m = has ("gate"); m && inView (g.x, g.z, 40))
            drawMesh (*m, rel (g.x, g.y, g.z), yawMat (props[(size_t) g.prop].yaw), { 6.5f, -15.0f, 10.0f, 0.85f });
        if (const auto* m = has ("fossil"))
        {
            const auto& F = W::fossilSpec;
            const double L = W::loopLength();
            const auto xz = W::roadPointAt (F.at * L, F.d);
            const auto t = W::tangentAt (W::thetaAt (F.at * L));
            if (inView (xz[0], xz[1], 15))
                drawMesh (*m, rel (xz[0], world.floorHeight (xz[0], xz[1]) + F.raise * F.scale, xz[1]), yawMat (std::atan2 (t[0], -t[1]) + 2.2, F.scale));
        }
        for (const auto& lm : world.landmarks())
        {
            const auto* m = has (lm.role.c_str());
            if (! m || ! inView (lm.x, lm.z, 30) || std::hypot (lm.x - s.x, lm.z - s.z) > 900) continue;
            const double k = lm.height > 0 ? lm.height / (m->zmax - std::max (m->zmin, 0.0)) : 1;
            glUniform1f (p.u ("uMeshSteel"), lm.paint ? 0.0f : 1.0f);
            drawMesh (*m, rel (lm.x, lm.y, lm.z), yawMat (lm.yaw, k));
        }
        // the guitars round the loop: steel, the Fender-types in their metallic paint, the stars' headstocks censored
        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto* gm = meshFor (i);
            const auto* ge = guitarOf (i);
            const auto& sl = slots[i];
            if (! gm || ! inView (sl.x, sl.z, 16) || std::hypot (sl.x - s.x, sl.z - s.z) > 700) continue;
            glUniform1f (p.u ("uMeshSteel"), 1);
            if (ge->paint) glUniform4f (p.u ("uMeshPaint"), (float) (*ge->paint)[0], (float) (*ge->paint)[1], (float) (*ge->paint)[2], 1);
            else glUniform4f (p.u ("uMeshPaint"), 0, 0, 0, 0);
            if (ge->box)
            {
                const std::array<float, 8> C { (float) ge->box->c[0], (float) ge->box->c[1], (float) ge->box->c[2], 0, 0, 0, 0, 0 };
                const std::array<float, 8> S { (float) (ge->box->s[0] / 2), (float) (ge->box->s[1] / 2), (float) (ge->box->s[2] / 2), 1, 0, 0, 0, 0 };
                glUniform4fv (p.u ("uCensorC[0]"), 2, C.data()); glUniform4fv (p.u ("uCensorS[0]"), 2, S.data());
            }
            drawMesh (*gm, rel (sl.x, sl.y, sl.z), leanMat (sl.yaw, sl.k, sl.lean[0], sl.lean[1]));
            if (ge->box) { glUniform4fv (p.u ("uCensorC[0]"), 2, noCensor.data()); glUniform4fv (p.u ("uCensorS[0]"), 2, noCensor.data()); }
        }
        glUniform4f (p.u ("uMeshPaint"), 0, 0, 0, 0);
        glUniform1f (p.u ("uMeshSteel"), 0);
        if (const auto* m = has ("warrior")) { const auto& w = world.warrior(); if (inView (w.x, w.z, 30)) drawMesh (*m, rel (w.x, w.y, w.z), yawMat (w.yaw)); }
        if (const auto* m = has ("cat"))
        {
            const auto& c = world.cat();
            const auto it = std::find_if (props.begin(), props.end(), [] (const W::Prop& q) { return q.mesh == "cat"; });
            if (it != props.end() && inView (c.x, c.z, 40)) drawMesh (*m, rel (c.x, c.y, c.z), yawMat (it->yaw));
        }
        if (const auto* m = has ("anvil"))
        {
            const auto it = std::find_if (props.begin(), props.end(), [] (const W::Prop& q) { return q.mesh == "anvil"; });
            if (it != props.end() && inView (it->x, it->z, 20)) drawMesh (*m, rel (it->x, it->y, it->z), yawMat (it->yaw));
        }
        for (const auto& f : findsNow)
            if (const auto* m = has (("find" + std::to_string (f.kind)).c_str()); m && inView (f.x, f.z, 20))
                drawMesh (*m, rel (f.x, f.y, f.z), yawMat (f.yaw), {}, (float) s.findFade);
    }
    if (dragonM.ok && ! skips ("dragons"))
    {
        const Program& p = dragon;
        const DragonGpu& D = dragonM;
        glUseProgram (p.id); setCommon (p);
        glBindVertexArray (D.vao);
        glUniform1f (p.u ("uUnit"), D.unit); glUniform4f (p.u ("uMeshCave"), 0, 0, 0, 0); glUniform1f (p.u ("uMeshFade"), 1);
        glBindBuffer (GL_ARRAY_BUFFER, D.vbo);
        for (const auto& u : world.dragons())
        {
            const double dx = u.x - s.x, dz = u.z - s.z, dd = std::hypot (dx, dz);
            if (dd > farCut || (dd > 80 && dx * fx + dz * fz < 0.3 * dd)) continue;
            const double f = u.beat * D.nf;
            const int k0 = (((int) std::floor (f)) % D.nf + D.nf) % D.nf, k1 = (k0 + 1) % D.nf;
            const size_t a0 = (size_t) k0 * (size_t) D.nv * 10, a1 = (size_t) k1 * (size_t) D.nv * 10;
            glVertexAttribPointer (1, 3, GL_SHORT, GL_FALSE, 10, offset (a0));
            glVertexAttribPointer (2, 3, GL_BYTE, GL_TRUE, 10, offset (a0 + 6));
            glVertexAttribPointer (3, 3, GL_SHORT, GL_FALSE, 10, offset (a1));
            glVertexAttribPointer (4, 3, GL_BYTE, GL_TRUE, 10, offset (a1 + 6));
            const double k = W::dragonSpec.scale;
            const M3 rot { (float) (u.L[0] * k), (float) (u.L[1] * k), (float) (u.L[2] * k), (float) (u.U[0] * k), (float) (u.U[1] * k), (float) (u.U[2] * k),
                           (float) (u.F[0] * k), (float) (u.F[1] * k), (float) (u.F[2] * k) };
            glUniformMatrix3fv (p.u ("uMeshRot"), 1, GL_FALSE, rot.data());
            const auto at = rel (u.x, u.y, u.z);
            glUniform3fv (p.u ("uMeshAt"), 1, at.data()); glUniform1f (p.u ("uMix"), (float) (f - std::floor (f)));
            glUniform2f (p.u ("uHead"), (float) u.g.hy, (float) u.g.hp);
            glDrawElements (GL_TRIANGLES, D.count, GL_UNSIGNED_INT, nullptr);
        }
    }
    if (! skips ("signs"))
    {
        const Program& p = sign;
        glUseProgram (p.id); setCommon (p);
        bindTex (6, texBanners);
        glUniform1i (p.u ("uAtlas"), 6); glUniform1i (p.u ("uBanners"), 6);
        glUniform1f (p.u ("uAtlasOn"), 0); glUniform1f (p.u ("uBannersOn"), texBanners != 0 ? 1.0f : 0.0f);
        glUniform2f (p.u ("uWind"), 1.5f, 0.6f);
        // the cloth: the citadel's banners, the battle jacket's flag
        scratch.assign ((size_t) clothBuf.max * 16, 0.0f);
        int nc = 0;
        for (const auto& b : world.banners())
        {
            if (nc >= clothBuf.max || ! inView (b.x, b.z, 20)) continue;
            const std::array<double, 4> rect = texBanners != 0 && ! bannerCells.empty() ? bannerCells[(size_t) b.cell % bannerCells.size()] : std::array<double, 4> { -1, b.seed, 0, 0 };
            const float v[16] { (float) (b.x - ccx), (float) b.y, (float) (b.z - ccz), (float) b.yaw, (float) (b.w / 2), (float) (b.h / 2), 0.05f, 2,
                                (float) rect[0], (float) rect[1], (float) rect[2], (float) rect[3], 0, (float) b.seed, 2, 0 };
            std::memcpy (&scratch[(size_t) nc++ * 16], v, sizeof (v));
        }
        const auto jk = std::find_if (findsNow.begin(), findsNow.end(), [] (const W::Find& f) { return f.kind == 2; });
        if (jk != findsNow.end() && ! meshes.count ("find2") && s.findFade > 0.5 && inView (jk->x, jk->z, 10) && nc < clothBuf.max)
        {
            const double yaw = jk->yaw + 0.9;
            const float v[16] { (float) (jk->x - ccx + std::cos (yaw) * 1.35), (float) (jk->y + 8.3), (float) (jk->z - ccz - std::sin (yaw) * 1.35), (float) yaw,
                                1.3f, 1.4f, 0.05f, 2, -2, 0, 0, 0, 0, 0.3f, 1, 0 };
            std::memcpy (&scratch[(size_t) nc++ * 16], v, sizeof (v));
        }
        if (nc)
        {
            glUniform2f (p.u ("uClothGrid"), 6, 10);
            glBindBuffer (GL_ARRAY_BUFFER, clothBuf.inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) nc * 64, scratch.data());
            glBindVertexArray (clothBuf.vao);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 6 * 10 * 6, nc);
        }
        bindTex (6, 0);
    }
    // THE GROUND: the near level, then the outer two with the inner one's square left out.
    if (! skips ("ground"))
    {
        struct Outer { double ox, oz, size, cenX, cenZ, half, cell, coarse; };
        std::array<Outer, levels.size()> outerAt {};
        for (size_t L = 0; L < levels.size(); ++L)
        {
            const double cell = levels[L].cell, half = levels[L].n * cell / 2;
            const double coarse = L + 1 < levels.size() ? levels[L + 1].cell : cell * 4;
            const double cenX = s.x + fx * 0.35 * half, cenZ = s.z + fz * 0.35 * half;
            outerAt[L] = { std::floor ((cenX - half) / coarse) * coarse, std::floor ((cenZ - half) / coarse) * coarse, levels[L].n * cell, cenX, cenZ, half, cell, coarse };
        }
        const double CP = W::crackPeriod;
        const std::array<double, 2> crackOff { std::fmod (std::fmod (ccx, CP) + CP, CP), std::fmod (std::fmod (ccz, CP) + CP, CP) };
        glBindVertexArray (vaoGrid);
        for (size_t L = 0; L < levels.size(); ++L)
        {
            const auto& g = outerAt[L];
            // the near level never has a fragment in a hole or past the far cut; a level whose square lies wholly within
            // the far cut (its farthest corner, with room for the heights) never has one past it
            double far2 = 0;
            for (const double cx : { g.ox, g.ox + g.size }) for (const double cz : { g.oz, g.oz + g.size })
                far2 = std::max (far2, (cx - s.x) * (cx - s.x) + (cz - s.z) * (cz - s.z));
            const bool pastCut = std::sqrt (far2) + 200 >= farCut;
            const Program& p = L == 0 ? ground : pastCut ? groundH : groundHN;
            glUseProgram (p.id);
            setCommon (p);
            glUniform1i (p.u ("uDetail"), 1); glUniform1i (p.u ("uCells"), 3);
            glUniform2f (p.u ("uDetOff0"), (float) fract (ccx / 1.5), (float) fract (ccz / 1.5)); glUniform2f (p.u ("uDetOff1"), (float) fract (ccx / 0.9), (float) fract (ccz / 0.9));
            glUniform2f (p.u ("uCrackOff"), (float) crackOff[0], (float) crackOff[1]);
            glUniform2f (p.u ("uLvlOrigin"), (float) (g.ox - ccx), (float) (g.oz - ccz)); glUniform1f (p.u ("uCellSize"), (float) g.cell);
            const bool last = L + 1 == levels.size();
            const double mEnd = last ? 1e9 : g.half - g.coarse - 0.5, mStart = last ? 1e9 : mEnd - 0.32 * g.half;
            const double thinEnd = L == 0 ? lerp (mEnd, 45, Q.thin) : mEnd, thinStart = L == 0 ? lerp (mStart, 30, Q.thin) : mStart;
            glUniform4f (p.u ("uMorph"), (float) (g.cenX - ccx), (float) (g.cenZ - ccz), (float) thinStart, (float) thinEnd);
            glUniform2f (p.u ("uCoarseOff"), (float) (fract (ccx / g.coarse) * g.coarse), (float) (fract (ccz / g.coarse) * g.coarse)); glUniform1f (p.u ("uCoarse"), (float) g.coarse);
            if (L > 0)
            {
                const auto& inner = outerAt[L - 1];
                const double e = 0.05;
                glUniform4f (p.u ("uHole"), (float) (inner.ox - ccx + e), (float) (inner.oz - ccz + e), (float) (inner.ox + inner.size - ccx - e), (float) (inner.oz + inner.size - ccz - e));
                glUniform1f (p.u ("uHoleOn"), 1);
            }
            else glUniform1f (p.u ("uHoleOn"), 0);
            glUniform1f (p.u ("uFarCut"), (float) farCut); glUniform1f (p.u ("uStuds"), (float) W::studs()); glUniform1f (p.u ("uBreath"), (float) (0.6 + 0.4 * calm));
            glUniform2f (p.u ("uFwdXZ"), (float) fx, (float) fz);
            glDrawElements (GL_TRIANGLES, gridCount, GL_UNSIGNED_SHORT, nullptr);
        }
    }
    // THE SKY where nothing was drawn, its smooth parts read from the smooth pass.
    if (! skips ("sky"))
    {
        const Program& p = sky;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty); setCommon (p); setSky (p);
        glActiveTexture (GL_TEXTURE9); glBindTexture (GL_TEXTURE_2D, texSmooth); glUniform1i (p.u ("uSmooth"), 9);
        glUniform4fv (p.u ("uSmoothMap"), 1, smoothMap.data());
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glActiveTexture (GL_TEXTURE9); glBindTexture (GL_TEXTURE_2D, 0);
    }

    // WHAT IS BLENDED (premultiplied; the fire added): the vents' steam and sparks, the fire, the ribbons, the embers.
    glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glDepthMask (GL_FALSE);
    if (! skips ("vents"))
    {
        const double ventShift = ventW * W::ventCell * 0.3;
        const int vbx = (int) std::floor ((s.x + fx * ventShift) / W::ventCell) - ventW / 2, vbz = (int) std::floor ((s.z + fz * ventShift) / W::ventCell) - ventW / 2;
        for (const auto& [pp, n] : { std::pair<const Program*, int> { &steam, puffs }, std::pair<const Program*, int> { &spark, sparks } })
        {
            const Program& p = *pp;
            glUseProgram (p.id); glBindVertexArray (vaoEmpty); setCommon (p);
            u3d (p.u ("uCamR"), basis.r); u3d (p.u ("uCamU"), basis.u);
            glUniform2i (p.u ("uVentBase"), vbx, vbz); glUniform1i (p.u ("uVentW"), ventW);
            glUniform1f (p.u ("uPxAng"), (float) pxAng); glUniform1f (p.u ("uRate"), (float) (0.45 + 0.55 * storm + 0.25 * calm)); glUniform1f (p.u ("uBreath"), (float) (0.6 + 0.4 * calm));
            glUniform1i (p.u ("uDetail"), 1);
            glDrawArraysInstanced (GL_TRIANGLES, 0, n * 6, ventW * ventW);
        }
    }
    if (fires.n > 0 && ! skips ("fire"))
    {
        const Program& p = fire;
        glUseProgram (p.id); setCommon (p);
        u3d (p.u ("uCamR"), basis.r); u3d (p.u ("uCamU"), basis.u); glUniform1f (p.u ("uThinF"), (float) Q.thin);
        glBindBuffer (GL_ARRAY_BUFFER, fireBuf.inst);
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) fires.n * 48, fires.em.data());
        glBindVertexArray (fireBuf.vao);
        glDrawArraysInstanced (GL_TRIANGLES, 0, fireP * 6, fires.n);
    }
    if (ribbons.n > 0 && ! skips ("ribbons"))
    {
        const Program& p = ribbon;
        glUseProgram (p.id); setCommon (p);
        glUniformMatrix4fv (p.u ("uView"), 1, GL_FALSE, Vm.data()); glUniformMatrix4fv (p.u ("uProj"), 1, GL_FALSE, Pm.data()); glUniform2f (p.u ("uRes"), (float) rw, (float) rh);
        glBlendFunc (GL_ONE, GL_ONE);
        glBindBuffer (GL_ARRAY_BUFFER, ribBuf.inst);
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) std::min (ribbons.n, maxRib) * 64, ribbons.rb.data());
        glBindVertexArray (ribBuf.vao);
        glDrawArraysInstanced (GL_TRIANGLES, 0, 6, std::min (ribbons.n, maxRib));
        glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    // THE BURNING LETTERS, the word CENSORED beside each grove's star first.
    if (! skips ("letters") && texLetters != 0)
    {
        scratch.assign ((size_t) letBuf.max * 16, 0.0f);
        int nl = 0;
        const double aw = lettersAtlas[0], ah = lettersAtlas[1];
        if (found.letters.censored)
            for (const auto& hw : censWords)
            {
                if (nl >= letBuf.max) break;
                const auto& cc = *found.letters.censored;
                const double dx = s.x - hw.x, dz = s.z - hw.z;
                const double Wd = 4.6 * hw.R, H = Wd * cc[3] / cc[2];
                const float v[16] { (float) (hw.x - ccx), (float) (hw.y + hw.R + 0.35 + H / 2), (float) (hw.z - ccz), (float) std::atan2 (dx, dz), (float) (Wd / 2), (float) (H / 2),
                                    (float) (0.37 + nl * 0.1), 0.8f, (float) (cc[0] / aw), (float) (cc[1] / ah), (float) ((cc[0] + cc[2]) / aw), (float) ((cc[1] + cc[3]) / ah),
                                    -1, 0, (float) (1 - hw.fade), 0 };
                std::memcpy (&scratch[(size_t) nl++ * 16], v, sizeof (v));
            }
        for (const auto& L : world.letters())
        {
            if (nl >= letBuf.max || ! inView (L.x, L.z, L.hw + 10)) continue;
            const std::array<double, 4> cr = L.colour ? std::array<double, 4> { (*L.colour)[0] / aw, (*L.colour)[1] / ah, ((*L.colour)[0] + (*L.colour)[2]) / aw, ((*L.colour)[1] + (*L.colour)[3]) / ah }
                                                      : std::array<double, 4> { -1, 0, 0, 0 };
            const float v[16] { (float) (L.x - ccx), (float) L.y, (float) (L.z - ccz), (float) L.yaw, (float) L.hw, (float) L.hh, (float) std::fmod (L.seed * 13.7, 1.0),
                                (float) clamp01 (3.2 / L.hh, 0.25, 1.4), (float) (L.cell[0] / aw), (float) (L.cell[1] / ah), (float) ((L.cell[0] + L.cell[2]) / aw), (float) ((L.cell[1] + L.cell[3]) / ah),
                                (float) cr[0], (float) cr[1], (float) cr[2], (float) cr[3] };
            std::memcpy (&scratch[(size_t) nl++ * 16], v, sizeof (v));
        }
        if (nl)
        {
            const Program& p = letters;
            glUseProgram (p.id); setCommon (p);
            bindTex (5, texLetters);
            glUniform1i (p.u ("uLetters"), 5); glUniform1i (p.u ("uNoise"), 2); glUniform2f (p.u ("uLettersSize"), (float) aw, (float) ah);
            glBlendFunc (GL_ONE, GL_ONE);
            glBindBuffer (GL_ARRAY_BUFFER, letBuf.inst);
            glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) nl * 64, scratch.data());
            glBindVertexArray (letBuf.vao);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 6, nl);
            glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            bindTex (5, 0);
        }
    }
    const double emberK = (0.02 * (1.0 - 0.6 * calm) + 0.98 * storm * storm) * (1 - 0.5 * Q.thin);
    if (emberK > 0.005 && ! skips ("embers"))
    {
        const Program& p = ember;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty); setCommon (p);
        glUniformMatrix4fv (p.u ("uProj"), 1, GL_FALSE, Pm.data()); glUniform2f (p.u ("uRes"), (float) rw, (float) rh);
        glUniform1f (p.u ("uTravel"), (float) std::fmod (s.travel, 10000.0)); glUniform1f (p.u ("uSpeed"), (float) s.speed); glUniform1f (p.u ("uEmber"), (float) emberK);
        glDrawArrays (GL_TRIANGLES, 0, embers * 6);
    }
    glDisable (GL_BLEND); glDepthMask (GL_TRUE);
    glDisable (GL_SCISSOR_TEST);
    if (gpu::loaded (glInvalidateFramebuffer))      // GL 4.3 and ES 3.0's; a Mac's 4.1 has none, and keeps the depth
    {
        const GLenum depth = GL_DEPTH_ATTACHMENT;
        glInvalidateFramebuffer (GL_FRAMEBUFFER, 1, &depth);
    }

    // THE CENSORED HEADSTOCKS' SQUARES: each one's bounding sphere projected, 8 x 8 blocks of at least 2 px.
    std::array<float, 16> cens {}, censB {};
    for (size_t i = 0; i < censList.size(); ++i)
    {
        const auto& hc = censList[i];
        const double wx = hc.x - ccx, wy = hc.y, wz = hc.z - ccz, R = hc.R;
        const double X = VP[0] * wx + VP[4] * wy + VP[8] * wz + VP[12], Y = VP[1] * wx + VP[5] * wy + VP[9] * wz + VP[13], Wc = VP[3] * wx + VP[7] * wy + VP[11] * wz + VP[15];
        if (Wc < R + 0.5) continue;
        const double u = X / Wc * 0.5 + 0.5, v = Y / Wc * 0.5 + 0.5, hu = R * Pm[0] / Wc * 0.5, hv = R * Pm[5] / Wc * 0.5;
        if (u + hu < 0 || u - hu > 1 || v + hv < 0 || v - hv > 1) continue;
        const double bPx = std::max (2.0, hv * 2 * ch / 8);
        cens[i * 4] = (float) (u - hu); cens[i * 4 + 1] = (float) (v - hv); cens[i * 4 + 2] = (float) (u + hu); cens[i * 4 + 3] = (float) (v + hv);
        censB[i * 4] = (float) (bPx / cw); censB[i * 4 + 1] = (float) (bPx / ch); censB[i * 4 + 2] = 1; censB[i * 4 + 3] = (float) hc.fade;
    }

    // THE POST PASS, the cabin's plates in it. Outside the window's box the plate is opaque: what is there is the plate
    // alone (with the flash's crossfade and lift), the same every frame until the flash or the frame's size changes — so
    // it is drawn once and kept, and each frame after it the pass is scissored to the box (skip "outer": all of it every
    // frame, cleared first). Kept, it is read back by the pass, and that still costs less than drawing it.
    const auto runPost = [&] (bool plates)
    {
        const Program& p = post;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        bindTex (4, fboColour); glUniform1i (p.u ("uColour"), 4);
        glUniform4f (p.u ("uSceneBox"), (float) sceneAt[0] / (float) rw, (float) sceneAt[1] / (float) rh, (float) subW / (float) rw, (float) subH / (float) rh);
        bindTex (6, plates ? texDash : 0); glUniform1i (p.u ("uDash"), 6); glUniform1f (p.u ("uDashOn"), plates ? 1.0f : 0.0f);
        bindTex (8, plates ? texDashFlash : 0); glUniform1i (p.u ("uDashFlash"), 8);
        glUniform1f (p.u ("uDashMix"), plates && ! skips ("flash") ? (float) clamp01 (s.flash / cabinFlashFull, 0, 1) : 0.0f);
        glUniform1f (p.u ("uLift"), (float) (0.035 * flash));
        glUniform4fv (p.u ("uCens[0]"), 4, cens.data()); glUniform4fv (p.u ("uCensB[0]"), 4, censB.data());
        if (! skips ("post")) glDrawArrays (GL_TRIANGLES, 0, 3);
        bindTex (4, 0); bindTex (6, 0); bindTex (8, 0);
    };
    // At the ladder's foot (a rung of 0.5) with the cabin drawn: the post pass at the scene's pixels, over its frame, and
    // the cabin's pass scaling it up under the plates, as UnderwaterGl does below its top rung. Above it, one pass.
    const bool foot = cabin && Q.level >= 2 && ensureTarget (fboPost, texPost, postW, postH, subW, subH, false);
    if (foot)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, fboPost);
        glViewport (-sceneAt[0], -sceneAt[1], rw, rh);
        glDisable (GL_DEPTH_TEST);
        glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
        runPost (false);
    }
    glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
    glViewport (0, 0, cw, ch);
    glDisable (GL_DEPTH_TEST);
    const double outerKey = cabin && hasGlassBox && ! skips ("outer") ? flash : -1.0;
    if (outerKey >= 0 && outerKept && ! (outerKey < keptFlash || outerKey > keptFlash))
    {
        const int x0 = std::max (0, (int) std::floor (glassBox[0] * cw)), x1 = std::min (cw, (int) std::ceil (glassBox[2] * cw));
        const int y0 = std::max (0, (int) std::floor ((1 - glassBox[3]) * ch)), y1 = std::min (ch, (int) std::ceil ((1 - glassBox[1]) * ch));
        glEnable (GL_SCISSOR_TEST); glScissor (x0, y0, x1 - x0, y1 - y0);
    }
    else
    {
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT);
    }
    outerKept = outerKey >= 0 && ! skips ("post");
    keptFlash = outerKey;
    if (foot)
    {
        const Program& p = cabinPass;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        bindTex (4, texPost); glUniform1i (p.u ("uView"), 4);
        glUniform4f (p.u ("uSceneBox"), (float) sceneAt[0] / (float) rw, (float) sceneAt[1] / (float) rh, (float) subW / (float) rw, (float) subH / (float) rh);
        bindTex (6, texDash); glUniform1i (p.u ("uDash"), 6);
        bindTex (8, texDashFlash); glUniform1i (p.u ("uDashFlash"), 8);
        glUniform1f (p.u ("uDashMix"), ! skips ("flash") ? (float) clamp01 (s.flash / cabinFlashFull, 0, 1) : 0.0f);
        if (! skips ("post")) glDrawArrays (GL_TRIANGLES, 0, 3);
        bindTex (4, 0); bindTex (6, 0); bindTex (8, 0);
    }
    else
    {
        runPost (cabin);
    }
    glDisable (GL_SCISSOR_TEST);
    glActiveTexture (GL_TEXTURE0);
    glEnable (GL_DEPTH_TEST);
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::metal
