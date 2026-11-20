// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Matched to its prototype (california-gl.js's drawScene) uniform for uniform: the JS's doubles where it works in
// doubles, f32 where it stores into a Float32Array, the walker's Js for its Math. Built with -ffp-contract=off.

#include "CaliforniaGl.h"

#include "../../redgroove/view/WalkerWorld.h"     // Js
#include "../../shaders/Shaders.h"

#include <cmath>
#include <utility>

namespace orbitamp::california
{

using namespace orbitamp::gpu;

namespace
{
    using W = CaliforniaWorld;
    using Js = redgroove::WalkerWorld::Js;
    using V2 = W::V2;
    using V3 = W::V3;
    using V4 = W::V4;
    constexpr double pi = W::pi;

    // The attributes' locations: each program's own names on locations of their own; programs share a location
    // only for names that never meet in one program. aPos is 0 (gpu::program binds it there).
    constexpr GLuint locPos = 0, locNrm = 1, locCol = 2, locUV = 3, locFlag = 4, locKind = 4, locSway = 5, locTex = 5,
                     locPart = 5, locI0 = 6, locI1 = 7, locGrid = 0, locTS = 0, locA = 1, locB = 2, locSag = 3,
                     locB0 = 0, locB1 = 1, locB2 = 2, locB3 = 3, locG0 = 0, locG1 = 1;
    const std::vector<gpu::Attribute> attributes {
        { "aGrid", locGrid }, { "aNrm", locNrm }, { "aCol", locCol }, { "aUV", locUV }, { "aFlag", locFlag }, { "aKind", locKind },
        { "aSway", locSway }, { "aTex", locTex }, { "aPart", locPart }, { "aI0", locI0 }, { "aI1", locI1 }, { "aTS", locTS },
        { "aA", locA }, { "aB", locB }, { "aSag", locSag }, { "aB0", locB0 }, { "aB1", locB1 }, { "aB2", locB2 }, { "aB3", locB3 },
        { "aG0", locG0 }, { "aG1", locG1 } };

    constexpr int petalSlots = W::petalSlots, bfBurst = W::bfBurst, bfMax = W::bfCount + W::petalSlots * W::bfBurst;
    constexpr int wireSegs = 24, wireMax = 64 * 3, glassCount = (int) W::glassSpots.size();
    constexpr double monoThick = W::monoThick, monoWide = W::monoWide, monoHigh = W::monoHigh;
    struct Sky { V3 zenith, mid, horSun, horAway, glow, sunCol, ambUp, ambDown, rays; };
    constexpr Sky skyCol { { 0.38, 0.44, 0.72 }, { 0.78, 0.57, 0.70 }, { 1.0, 0.72, 0.42 }, { 0.86, 0.60, 0.62 }, { 1.0, 0.55, 0.22 },
                        { 1.28, 0.90, 0.56 }, { 0.30, 0.31, 0.45 }, { 0.24, 0.17, 0.10 }, { 1.0, 0.70, 0.36 } };
    constexpr std::array<float, 9> tiltNone { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    constexpr std::array<float, 4> lampNone { 0, 0, 0, 0 }, lampPaint { 0, 0, 0, 1 };
    // What the ladder's bottom rung (UnderwaterWorld::Quality's thin) leaves out besides the prototype's own: the oaks
    // past about 600 m (shrinking into the ground from footOakFrom, gone by footOakGone, metres from the camera) and
    // the flowers' outer band (the half the ladder keeps gone footFloraBeyond metres past the half it thins).
    constexpr double footOakFrom = 600.0, footOakGone = 750.0, footFloraBeyond = 4.0;

    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double clamp (double v, double lo, double hi) noexcept { return std::min (hi, std::max (lo, v)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline double f32 (double v) noexcept { return (double) (float) v; }
    /** The shortest way round the world (the prototype's nearest). */
    inline double nearest (double d) noexcept { return d - Js::round (d / W::worldPeriod) * W::worldPeriod; }
    inline const void* offset (int bytes) noexcept { return reinterpret_cast<const void*> ((std::uintptr_t) bytes); }
    /** An instance buffer's first `bytes` for this draw. Written over in place, or — `renew` — into new storage of the
        buffer's whole `capacity` (the buffer orphaned): writing over a buffer that draws not yet sent still read makes
        the driver send the frame's commands so far first (a command buffer cut mid-scene, the scene's targets stored
        and loaded back), as each scene drawn again under --stress does; new storage leaves the earlier draws theirs.
        The same pixels either way. */
    void upload (GLuint buf, int bytes, int capacity, const void* data, bool renew)
    {
        glBindBuffer (GL_ARRAY_BUFFER, buf);
        if (renew) glBufferData (GL_ARRAY_BUFFER, capacity, nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData (GL_ARRAY_BUFFER, 0, bytes, data);
    }

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

    /** A texture from a picture: straight or premultiplied as the prototype's browser uploads it, with mips or not. */
    GLuint pictureTexture (const CaliforniaGl::Picture& p, bool premultiplied, bool mips)
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

    /** The JS's Float32Array.set of doubles: each kept as f32. */
    template <size_t N>
    void put (std::vector<float>& to, size_t at, const std::array<double, N>& v) noexcept
    {
        for (size_t i = 0; i < N; ++i)
            to[at + i] = (float) v[i];
    }
}

//==============================================================================
int CaliforniaGl::Program::u (const char* name) const
{
    if (const auto it = at.find (name); it != at.end())
        return it->second;
    return at[name] = glGetUniformLocation (id, name);
}

bool CaliforniaGl::compile (Program& p, const char* vertexPath, const char* fragmentPath, const char* what, const std::vector<shaders::Written>& written)
{
    std::string log;
    p.at.clear();
    p.id = gpu::program (gpu::header() + shaders::source (vertexPath), gpu::header() + shaders::source (fragmentPath, written), log, attributes);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("california: " + trouble);
        return false;
    }
    return true;
}

bool CaliforniaGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("california: " + trouble);
        return false;
    }
    if (! world->isBuilt())          // made by its drive, settled and looking down already: as it is
        world->build();

    if (! compile (ground, "california/ground.vert", "california/ground.frag", "near ground", { { "level", "#define NEAR 1\n" } })
        || ! compile (groundH, "california/ground.vert", "california/ground.frag", "ground", { { "level", "" } })
        || ! compile (prop, "california/prop.vert", "california/prop.frag", "prop")
        || ! compile (flora, "california/flora.vert", "california/flora.frag", "flora")
        || ! compile (bfly, "california/bfly.vert", "california/bfly.frag", "butterfly")
        || ! compile (mono, "california/mono.vert", "california/mono.frag", "monolith")
        || ! compile (mesh, "california/mesh.vert", "california/mesh.frag", "mesh")
        || ! compile (beam, "california/beam.vert", "california/beam.frag", "beam")
        || ! compile (sky, "california/sky.vert", "california/sky.frag", "sky")
        || ! compile (sprite, "california/sprite.vert", "california/sprite.frag", "sprite")
        || ! compile (wire, "california/wire.vert", "california/wire.frag", "wire")
        || ! compile (particle, "california/particle.vert", "california/particle.frag", "particle")
        || ! compile (post, "california/post.vert", "california/post.frag", "post")
        || ! compile (glass, "california/glass.vert", "california/glass.frag", "glass")
        || ! compile (sign, "california/sign.vert", "california/sign.frag", "sign")
        || ! compile (pendant, "california/pendant.vert", "california/pendant.frag", "pendant"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeBuffers();
    makeArt (true);

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

void CaliforniaGl::destroy()
{
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (grid.vbo, glDeleteBuffers); del (grid.ibo, glDeleteBuffers); del (grid.vao, glDeleteVertexArrays);
    for (auto* p : { &oakNear, &oakMid, &oakFar, &poles, &pumpTower, &pumpFan })
    { del (p->vbo, glDeleteBuffers); del (p->ibo, glDeleteBuffers); del (p->inst, glDeleteBuffers); del (p->vao, glDeleteVertexArrays); }
    for (auto& f : floraMesh) { del (f.vbo, glDeleteBuffers); del (f.ibo, glDeleteBuffers); del (f.vao, glDeleteVertexArrays); }
    for (auto* b : { &vboBfly, &iboBfly, &instBfly, &vboWire, &iboWire, &instWire, &instGlass })
        del (*b, glDeleteBuffers);
    for (auto* v : { &vaoBfly, &vaoWire, &vaoGlass, &vaoEmpty })
        del (*v, glDeleteVertexArrays);
    for (auto* b : { &signBoxes, &signBanners, &signFlags }) { del (b->inst, glDeleteBuffers); del (b->vao, glDeleteVertexArrays); }
    for (auto& [key, m] : meshes)
    {
        del (m.vbo, glDeleteBuffers); del (m.ibo, glDeleteBuffers); del (m.vao, glDeleteVertexArrays);
        for (auto& t : m.tex) del (t, glDeleteTextures);
    }
    meshes.clear();
    for (auto& [key, sp] : sprites) del (sp.tex, glDeleteTextures);
    sprites.clear();
    for (auto* t : { &texHeight, &texDetail, &texNoise, &texRoad, &texLand, &texShadow, &texDash, &texPend, &texLabel, &texSigns,
                     &texFlags, &texChalk, &fboColour, &fboDepth })
        del (*t, glDeleteTextures);
    del (fboOutColour, glDeleteRenderbuffers);
    del (fbo, glDeleteFramebuffers); del (fboOut, glDeleteFramebuffers);
    fbW = fbH = outW = outH = 0;
    glassBox.reset(); pendBox.reset();
    labelIn = false;
    quality = {};
    lastFrameMs = -1.0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &ground, &groundH, &prop, &flora, &bfly, &mono, &mesh, &beam, &sky, &sprite, &wire, &particle, &post, &glass, &sign, &pendant })
    {
        if (p->id != 0) glDeleteProgram (p->id);
        p->id = 0;
        p->at.clear();
    }
}

void CaliforniaGl::makeTextures()
{
    // The height, the road and land maps: RGBA16F (their values already half floats), linear, no mips.
    const auto half = [] (std::uint32_t& id, int size, const std::vector<float>& px, GLint wrap)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, px.data());
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    };
    half (texHeight, W::texSize, world->height(), GL_REPEAT);
    half (texRoad, W::roadN, world->roadMap(), GL_CLAMP_TO_EDGE);
    half (texLand, W::roadN, world->landMap(), GL_CLAMP_TO_EDGE);
    // The detail and the noise: RGBA8 with mips; the shadow map RGBA8 without.
    const auto rgba8 = [] (std::uint32_t& id, int size, const std::vector<std::uint8_t>& px, bool mips)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        if (mips)
            glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    rgba8 (texDetail, W::detailSize, world->detail(), true);
    rgba8 (texNoise, W::noiseSize, world->noise(), true);
    world->updateShadow();
    rgba8 (texShadow, W::shN, world->shadowPixels(), false);
    world->clearShadowUpdates();
    glBindTexture (GL_TEXTURE_2D, 0);
}

bool CaliforniaGl::loadMesh (const std::string& key, const std::string& base, const std::vector<V2>& parts)
{
    if (! art.bytes || meshes.count (key) != 0)     // none to read, or on the GPU already
        return false;
    const auto info = W::parseMeshInfo (art.text ? art.text (base + ".txt") : std::string());
    const auto bytes = art.bytes (base + ".mesh");
    const auto cb = W::parseCbMesh (bytes.data(), bytes.size(), parts);
    if (! cb)
        return false;
    MeshGpu m;
    glGenVertexArrays (1, &m.vao);
    glBindVertexArray (m.vao);
    glGenBuffers (1, &m.vbo);
    glBindBuffer (GL_ARRAY_BUFFER, m.vbo);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) cb->vertexBytes.size(), cb->vertexBytes.data(), GL_STATIC_DRAW);
    attrib (locPos, 3, GL_FLOAT, false, 40, 0);
    attrib (locNrm, 3, GL_FLOAT, false, 40, 12);
    attrib (locUV, 2, GL_FLOAT, false, 40, 24);
    attrib (locCol, 3, GL_UNSIGNED_BYTE, true, 40, 32);
    attrib (locKind, 1, GL_UNSIGNED_BYTE, false, 40, 35);
    attrib (locTex, 1, GL_UNSIGNED_BYTE, false, 40, 36);
    glGenBuffers (1, &m.ibo);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (cb->idx.size() * 4), cb->idx.data(), GL_STATIC_DRAW);
    glBindVertexArray (0);
    m.count = (int) cb->idx.size();
    m.parts = cb->parts;
    m.censor = info.censor;
    const auto dir = base.substr (0, base.find_last_of ('/') + 1);
    if (art.picture)
        for (size_t i = 0; i < std::min<size_t> (3, info.textures.size()); ++i)
            if (! info.textures[i].empty())
                m.tex[i] = pictureTexture (art.picture (dir + info.textures[i]), false, true);
    meshes[key] = std::move (m);
    return true;
}

void CaliforniaGl::makeArt (bool fresh)
{
    // Each part is read while it is not on the GPU yet: all of them as the GPU's half is made (`fresh`), then what has
    // come since (takeArt). What the world was told is told again only when something came.
    bool took = fresh;
    // THE CABIN: the dashboard straight, no mips (the stage's own size), the windshield's box from its alpha; the
    // pendant straight, with mips; its box and pivot.
    if (art.dashboard && texDash == 0)
    {
        const auto dash = art.dashboard();
        texDash = pictureTexture (dash, false, false);
        if (texDash != 0)
            glassBox = W::glassBounds (dash.rgba.data(), dash.width, dash.height, 2);
        else
            gpu::note ("california: the dashboard did not come; the face paints the panel");
    }
    if (art.pendant && texPend == 0)
        texPend = pictureTexture (art.pendant(), false, true);
    if (! pendBox)
        pendBox = W::parsePendantBox (art.pendantBox);
    // The CENSORED word, straight; a clear texel while there is none, so the post pass reads nothing from it.
    if (art.label && ! labelIn)
        if (const auto t = pictureTexture (art.label(), false, false); t != 0)
        {
            if (texLabel != 0)
                glDeleteTextures (1, &texLabel);
            texLabel = t;
            labelIn = true;
        }
    if (texLabel == 0)
        texLabel = pictureTexture (Picture { 1, 1, { 0, 0, 0, 0 } }, false, false);

    // THE FINDS' FILES, as the prototype's view takes them in: the signs' atlas and table, the sprites (premultiplied,
    // with mips), the flags' cloth, the chalk, and the meshes.
    if (fresh)
    {
        signTable.clear(); flagCells.clear(); chalkTable.clear(); came.clear();
        signAtlasW = signAtlasH = 0;
        flagAtlasW = flagAtlasH = 0;
    }
    const auto picture = [this] (const std::string& path) { return art.picture ? art.picture (path) : Picture {}; };
    const auto text = [this] (const std::string& path) { return art.text ? art.text (path) : std::string(); };
    if (signTable.empty())
        took |= ! (signTable = W::parseSigns (text ("signs/signs.txt"))).empty();
    if (texSigns == 0)
        if (const auto atlas = picture ("signs/atlas.png"); (texSigns = pictureTexture (atlas, false, true)) != 0)
        { signAtlasW = atlas.width; signAtlasH = atlas.height; took = true; }
    std::vector<std::string> spriteNames { "dish" };
    for (const auto& sp : world->spritePlaces())
        if (sp.mesh.empty())
            spriteNames.push_back (sp.name);
    for (const auto& name : spriteNames)
    {
        auto& s = sprites[name];
        if (s.in)
            continue;
        if (s.tex == 0)
        {
            const auto atlas = picture (name + "/atlas.png");
            s.tex = pictureTexture (atlas, true, true);
            s.size = { std::max (1, atlas.width), std::max (1, atlas.height) };
        }
        if (! s.tabled)
            if (const auto table = text (name + "/sprites.txt"); ! table.empty())
            {
                s.t = W::parseSpriteTable (table);
                s.tabled = true;
            }
        s.in = s.tex != 0 && s.tabled && s.t.scale > 0;
        if (s.in)
        {
            came.push_back (name);
            took = true;
        }
    }
    if (texFlags == 0)
        if (const auto atlas = picture ("flags/atlas.png"); (texFlags = pictureTexture (atlas, false, true)) != 0)
        { flagAtlasW = atlas.width; flagAtlasH = atlas.height; }
    if (texFlags != 0 && flagCells.empty())
        took |= ! (flagCells = W::parseFlagCells (text ("flags/flags.txt"), flagAtlasW, flagAtlasH)).empty();
    if (texChalk == 0)
        if (const auto atlas = picture ("chalk/atlas.png"); atlas.valid())
        {
            chalkTable = W::parseChalk (text ("chalk/chalk.txt"), atlas.width, atlas.height);
            if (! chalkTable.empty())
                texChalk = pictureTexture (atlas, false, true);
            if (texChalk == 0)
                chalkTable.clear();
            took |= texChalk != 0;
        }
    took |= loadMesh ("van", "van3d/van", {});
    took |= loadMesh ("saucer", "saucer3d/saucer", {});
    took |= loadMesh ("yellowsub3d/yellowsub", "yellowsub3d/yellowsub", {});
    // each file once (several places may share one, a row of things split into its parts)
    std::vector<std::string> order;
    std::map<std::string, std::vector<const W::SpritePlace*>> byMesh;
    for (const auto& sp : world->spritePlaces())
        if (! sp.mesh.empty())
        {
            if (byMesh.find (sp.mesh) == byMesh.end()) order.push_back (sp.mesh);
            byMesh[sp.mesh].push_back (&sp);
        }
    for (const auto& file : order)
    {
        std::vector<V2> parts;
        for (const auto* sp : byMesh[file])
            if (sp->hasPart && std::find (parts.begin(), parts.end(), sp->part) == parts.end())
                parts.push_back (sp->part);
        if (loadMesh (file, file, parts))
        {
            for (const auto* sp : byMesh[file])
                came.push_back (sp->name);
            took = true;
        }
    }
    tablesRead = true;
    if (took)
        applyTables();
}

void CaliforniaGl::takeArt (Art a)
{
    art = std::move (a);
    if (post.id != 0)               // made: what has come is read now; else create() reads it all
        makeArt (false);
}

void CaliforniaGl::applyTables()
{
    if (! tablesRead)
        return;
    if (! signTable.empty())
        world->buildSigns (signTable, signAtlasW, signAtlasH);
    if (! flagCells.empty())
        world->setFlagCells (flagCells);
    if (! chalkTable.empty())
        world->placeChalk (chalkTable, art.wink);
    for (const auto& name : came)
        world->spriteCame (name);
}

void CaliforniaGl::makeBuffers()
{
    // The ground lattice: 96 x 96 cells, every level's.
    {
        constexpr int NL = 96;
        static_assert (W::levels[0].n == NL && W::levels[1].n == NL && W::levels[2].n == NL);
        std::vector<float> g ((size_t) ((NL + 1) * (NL + 1) * 2));
        for (int j = 0; j <= NL; ++j) for (int i = 0; i <= NL; ++i) { g[(size_t) ((j * (NL + 1) + i) * 2)] = (float) i; g[(size_t) ((j * (NL + 1) + i) * 2 + 1)] = (float) j; }
        std::vector<std::uint16_t> idx ((size_t) (NL * NL * 6));
        for (int j = 0; j < NL; ++j)
            for (int i = 0; i < NL; ++i)
            {
                const size_t o = (size_t) ((j * NL + i) * 6);
                const auto v = (std::uint16_t) (j * (NL + 1) + i);
                idx[o] = v; idx[o + 1] = (std::uint16_t) (v + 1); idx[o + 2] = (std::uint16_t) (v + NL + 2);
                idx[o + 3] = v; idx[o + 4] = (std::uint16_t) (v + NL + 2); idx[o + 5] = (std::uint16_t) (v + NL + 1);
            }
        glGenVertexArrays (1, &grid.vao); glBindVertexArray (grid.vao);
        glGenBuffers (1, &grid.vbo); glBindBuffer (GL_ARRAY_BUFFER, grid.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (g.size() * 4), g.data(), GL_STATIC_DRAW);
        attrib (locGrid, 2, GL_FLOAT, false, 0, 0);
        glGenBuffers (1, &grid.ibo); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, grid.ibo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (idx.size() * 2), idx.data(), GL_STATIC_DRAW);
        grid.count = (int) idx.size();
    }
    // The props: the oaks at three details, the pole, the pump's tower and fan; each with its instance buffer (8 floats).
    const auto propVao = [] (Prop& P, const W::Mesh& m, int maxInst)
    {
        glGenVertexArrays (1, &P.vao); glBindVertexArray (P.vao);
        glGenBuffers (1, &P.vbo); glBindBuffer (GL_ARRAY_BUFFER, P.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (m.v.size() * 4), m.v.data(), GL_STATIC_DRAW);
        attrib (locPos, 3, GL_FLOAT, false, 28, 0); attrib (locNrm, 3, GL_FLOAT, false, 28, 12); attrib (locPart, 1, GL_FLOAT, false, 28, 24);
        glGenBuffers (1, &P.inst); glBindBuffer (GL_ARRAY_BUFFER, P.inst);
        glBufferData (GL_ARRAY_BUFFER, maxInst * 32, nullptr, GL_DYNAMIC_DRAW);
        attrib (locI0, 4, GL_FLOAT, false, 32, 0, true); attrib (locI1, 4, GL_FLOAT, false, 32, 16, true);
        glGenBuffers (1, &P.ibo); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, P.ibo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (m.idx.size() * 2), m.idx.data(), GL_STATIC_DRAW);
        P.count = (int) m.idx.size(); P.max = maxInst; P.data.assign ((size_t) maxInst * 8, 0.0f);
    };
    propVao (oakNear, W::oakMesh (2), 200); propVao (oakMid, W::oakMesh (1), 600); propVao (oakFar, W::oakMesh (0), 3000);
    propVao (poles, W::poleMesh(), 64);
    propVao (pumpTower, W::pumpTowerMesh(), 8); propVao (pumpFan, W::pumpFanMesh(), 8);
    // The flowers: three meshes, one VAO each.
    const auto fm = W::floraMeshes();
    for (size_t k = 0; k < 3; ++k)
    {
        auto& F = floraMesh[k];
        glGenVertexArrays (1, &F.vao); glBindVertexArray (F.vao);
        glGenBuffers (1, &F.vbo); glBindBuffer (GL_ARRAY_BUFFER, F.vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (fm[k].v.size() * 4), fm[k].v.data(), GL_STATIC_DRAW);
        attrib (locPos, 3, GL_FLOAT, false, 52, 0); attrib (locNrm, 3, GL_FLOAT, false, 52, 12); attrib (locCol, 3, GL_FLOAT, false, 52, 24);
        attrib (locUV, 2, GL_FLOAT, false, 52, 36); attrib (locFlag, 1, GL_FLOAT, false, 52, 44); attrib (locSway, 1, GL_FLOAT, false, 52, 48);
        glGenBuffers (1, &F.ibo); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, F.ibo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (fm[k].idx.size() * 2), fm[k].idx.data(), GL_STATIC_DRAW);
        F.count = (int) fm[k].idx.size();
    }
    // The butterflies: one mesh, an instance buffer of 8 floats (the world's and the attacks' bursts).
    {
        const auto bm = W::butterflyMesh();
        glGenVertexArrays (1, &vaoBfly); glBindVertexArray (vaoBfly);
        glGenBuffers (1, &vboBfly); glBindBuffer (GL_ARRAY_BUFFER, vboBfly);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (bm.v.size() * 4), bm.v.data(), GL_STATIC_DRAW);
        attrib (locPos, 3, GL_FLOAT, false, 24, 0); attrib (locUV, 2, GL_FLOAT, false, 24, 12); attrib (locPart, 1, GL_FLOAT, false, 24, 20);
        glGenBuffers (1, &instBfly); glBindBuffer (GL_ARRAY_BUFFER, instBfly);
        glBufferData (GL_ARRAY_BUFFER, bfMax * 32, nullptr, GL_DYNAMIC_DRAW);
        attrib (locI0, 4, GL_FLOAT, false, 32, 0, true); attrib (locI1, 4, GL_FLOAT, false, 32, 16, true);
        glGenBuffers (1, &iboBfly); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboBfly);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (bm.idx.size() * 2), bm.idx.data(), GL_STATIC_DRAW);
        bflyIndices = (int) bm.idx.size();
        bflyData.assign ((size_t) bfMax * 8, 0.0f);
    }
    // The wires: 24 segments a span, an instance per wire per span (7 floats: the ends, the sag).
    {
        std::vector<float> wv;
        std::vector<std::uint16_t> wi;
        for (int k = 0; k <= wireSegs; ++k)
        {
            const float t = (float) ((double) k / wireSegs);
            wv.insert (wv.end(), { t, 0.0f, t, 1.0f });
            if (k < wireSegs) { const auto o = (std::uint16_t) (k * 2); wi.insert (wi.end(), { o, (std::uint16_t) (o + 1), (std::uint16_t) (o + 2), (std::uint16_t) (o + 1), (std::uint16_t) (o + 3), (std::uint16_t) (o + 2) }); }
        }
        glGenVertexArrays (1, &vaoWire); glBindVertexArray (vaoWire);
        glGenBuffers (1, &vboWire); glBindBuffer (GL_ARRAY_BUFFER, vboWire);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (wv.size() * 4), wv.data(), GL_STATIC_DRAW);
        attrib (locTS, 2, GL_FLOAT, false, 0, 0);
        glGenBuffers (1, &instWire); glBindBuffer (GL_ARRAY_BUFFER, instWire);
        glBufferData (GL_ARRAY_BUFFER, wireMax * 28, nullptr, GL_DYNAMIC_DRAW);
        attrib (locA, 3, GL_FLOAT, false, 28, 0, true); attrib (locB, 3, GL_FLOAT, false, 28, 12, true); attrib (locSag, 1, GL_FLOAT, false, 28, 24, true);
        glGenBuffers (1, &iboWire); glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboWire);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (wi.size() * 2), wi.data(), GL_STATIC_DRAW);
        wireData.assign ((size_t) wireMax * 7, 0.0f);
    }
    // The glass butterflies: an instance buffer of 8 floats, the quad from gl_VertexID.
    glGenVertexArrays (1, &vaoGlass); glBindVertexArray (vaoGlass);
    glGenBuffers (1, &instGlass); glBindBuffer (GL_ARRAY_BUFFER, instGlass);
    glBufferData (GL_ARRAY_BUFFER, glassCount * 32, nullptr, GL_DYNAMIC_DRAW);
    attrib (locG0, 4, GL_FLOAT, false, 32, 0, true); attrib (locG1, 4, GL_FLOAT, false, 32, 16, true);
    // The signs, the fences, the stand-ins (boxes from gl_VertexID), the banners' and the flags' cloth: 16 floats each.
    const auto signBuf = [] (SignBuf& b, int max)
    {
        glGenVertexArrays (1, &b.vao); glBindVertexArray (b.vao);
        glGenBuffers (1, &b.inst); glBindBuffer (GL_ARRAY_BUFFER, b.inst);
        glBufferData (GL_ARRAY_BUFFER, max * 64, nullptr, GL_DYNAMIC_DRAW);
        attrib (locB0, 4, GL_FLOAT, false, 64, 0, true); attrib (locB1, 4, GL_FLOAT, false, 64, 16, true);
        attrib (locB2, 4, GL_FLOAT, false, 64, 32, true); attrib (locB3, 4, GL_FLOAT, false, 64, 48, true);
        b.max = max; b.data.assign ((size_t) max * 16, 0.0f);
    };
    signBuf (signBoxes, 512); signBuf (signBanners, 4); signBuf (signFlags, 160);
    glGenVertexArrays (1, &vaoEmpty);
    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
}

bool CaliforniaGl::ensureFrame (int w, int h, int cw, int ch)
{
    // The scene's colour (its alpha: 1 where the sky shows, for the sun's rays) and depth (read only while something
    // is censored) as textures the post pass reads.
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
        outsideGain = -1.0;         // a new frame: nothing of it drawn yet
    }
    return true;
}

void CaliforniaGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void CaliforniaGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (post.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, (int) Js::round ((double) w * scale)), rh = std::max (2, (int) Js::round ((double) h * scale));
    if (! ensureFrame (rw, rh, w, h))
        return;
    // The rung: what is drawn less of below the top, eased by the time since the last frame.
    const double now = gpu::milliseconds();
    quality.follow (scale, lastFrameMs < 0 ? 0.0 : (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

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

bool CaliforniaGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
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

void CaliforniaGl::uploadShadow()
{
    const auto& ups = world->shadowUpdates();
    if (ups.empty())
        return;
    glBindTexture (GL_TEXTURE_2D, texShadow);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei (GL_UNPACK_ROW_LENGTH, W::shN);
    for (const auto& rect : ups)
        for (const auto& [px, py, pw, ph] : W::shadowPieces (rect))   // a world rectangle in the toroidal texture
        {
            glPixelStorei (GL_UNPACK_SKIP_PIXELS, px); glPixelStorei (GL_UNPACK_SKIP_ROWS, py);
            glTexSubImage2D (GL_TEXTURE_2D, 0, px, py, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, world->shadowPixels().data());
        }
    glPixelStorei (GL_UNPACK_ROW_LENGTH, 0); glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0); glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
    glBindTexture (GL_TEXTURE_2D, 0);
    world->clearShadowUpdates();
}

//==============================================================================
void CaliforniaGl::drawScene (int rw, int rh, int cw, int ch)
{
    auto& wd = *world;
    const auto& s = wd.state();
    const V2 shCentre = wd.updateShadow();
    uploadShadow();
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const std::array<double, 2> camFrac { f32 (s.x - ccx), f32 (s.z - ccz) };
    const std::array<double, 3> camRel { camFrac[0], f32 (s.camY), camFrac[1] };
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double aspect = (double) rw / rh;
    // a constant width of view (the lens's), the height from the aspect; the centre where the window wants it
    const double th = tanH, tv = th / aspect, zn = W::zNear, zf = W::zFar, sh = shear;
    std::array<float, 16> Pm {};
    Pm[0] = (float) (1 / th); Pm[5] = (float) (1 / tv); Pm[10] = (float) (-(zf + zn) / (zf - zn)); Pm[11] = -1; Pm[14] = (float) (-2 * zf * zn / (zf - zn));
    Pm[9] = (float) -sh;
    const auto basis = W::cameraBasis (s.heading, s.pitch);
    std::array<float, 16> Vm {};
    {
        const double cx = camRel[0], cy = camRel[1], cz = camRel[2];
        const auto& r = basis.r; const auto& u = basis.u; const auto& f = basis.f;
        Vm[0] = (float) r[0]; Vm[4] = (float) r[1]; Vm[8] = (float) r[2];    Vm[12] = (float) -(r[0] * cx + r[1] * cy + r[2] * cz);
        Vm[1] = (float) u[0]; Vm[5] = (float) u[1]; Vm[9] = (float) u[2];    Vm[13] = (float) -(u[0] * cx + u[1] * cy + u[2] * cz);
        Vm[2] = (float) -f[0]; Vm[6] = (float) -f[1]; Vm[10] = (float) -f[2]; Vm[14] = (float) (f[0] * cx + f[1] * cy + f[2] * cz);
        Vm[15] = 1;
    }
    const auto VP = multiply (Pm, Vm);
    const V3 SUN = W::sunDir();
    const double sunL = Js::hypot (SUN[0], SUN[2]);
    const V2 SUN_XZ { SUN[0] / sunL, SUN[2] / sunL };
    const double SUN_TAN = std::tan (W::sunElev);
    const V2 WIND = W::wind();
    const std::array<const V3*, 3> axes { &basis.r, &basis.u, &basis.f };
    V3 sunC {};
    for (size_t k = 0; k < 3; ++k) sunC[k] = SUN[0] * (*axes[k])[0] + SUN[1] * (*axes[k])[1] + SUN[2] * (*axes[k])[2];
    const V2 sunNdc = sunC[2] > 0.05 ? V2 { sunC[0] / sunC[2] / th, sunC[1] / sunC[2] / tv + sh } : V2 { 0, 0 };
    const V2 off0 { fract (ccx / W::L0), fract (ccz / W::L0) }, off1 { fract (ccx / W::L1), fract (ccz / W::L1) }, off2 { fract (ccx / W::L2), fract (ccz / W::L2) };
    const double time = s.time;
    const double pxAngle = 2 * th / rw;                            // a pixel's angle at the view's centre (tan)
    const double windL = Js::hypot (WIND[0], WIND[1]), gustPh = fract ((ccx * WIND[0] / windL + ccz * WIND[1] / windL) * 0.21 / (2 * pi));
    V2 shWin { shCentre[0] - ccx, shCentre[1] - ccz };
    // the eight poles nearest the van (two behind, six ahead), for the ground's shader
    const int poleK = (int) std::floor (s.sr / W::poleDs()) - 2;
    std::vector<float> poleArr (32), poleSide (16);
    for (int i = 0; i < 8; ++i)
    {
        const auto q = wd.poleAt (poleK + i);
        put<4> (poleArr, (size_t) i * 4, { camFrac[0] + q.x - s.x, camFrac[1] + q.z - s.z, q.h / SUN_TAN, (q.h - 0.65) / SUN_TAN });
        put<2> (poleSide, (size_t) i * 2, q.side);
    }
    const auto finds = wd.findsNow();
    const double fvis = s.findFade;
    // the four chalk drawings nearest ahead, for the ground's shader
    std::vector<float> chalkA (16), chalkB (16), chalkC (16);
    if (texChalk != 0)
    {
        std::vector<std::pair<const W::Chalk*, double>> near;
        for (const auto& c : wd.chalks())
            if (const double d = Js::hypot (c.x - s.x, c.z - s.z); d < 220)
                near.emplace_back (&c, d);
        std::stable_sort (near.begin(), near.end(), [] (const auto& a, const auto& b) { return a.second < b.second; });
        for (size_t i = 0; i < std::min<size_t> (4, near.size()); ++i)
        {
            const auto& c = *near[i].first;
            put<4> (chalkA, i * 4, { camFrac[0] + c.x - s.x, camFrac[1] + c.z - s.z, c.tx, c.tz });
            put<4> (chalkB, i * 4, { c.hw, c.hl, 1, 0 });
            put<4> (chalkC, i * 4, c.cell);
        }
    }
    const W::Find* fPeace = nullptr; const W::Find* fVan = nullptr; const W::Find* fMono = nullptr;
    for (const auto& f : finds)
        (f.kind == 0 ? fPeace : f.kind == 1 ? fVan : fMono) = &f;
    // the censored boxes of this frame (filled where the meshes are drawn; the post pass draws them)
    struct Censor { int n = 0, nl = 0; std::array<float, 8> mos { -1, 0, 0, 0, -1, 0, 0, 0 }, blk {}, lab { -1, 0, 0, 0, -1, 0, 0, 0 }; std::array<float, 2> labA {}, labZ {}; std::array<float, 4> z {}; } censor;
    // the saucers' beam (the strongest; one at a time in practice): its foot on the ground, its radius there
    const W::Saucer* beamU = nullptr;
    for (const auto& u : wd.saucers()) if (u.beam > 0.01 && (beamU == nullptr || u.beam > beamU->beam)) beamU = &u;
    V3 beamFoot {};
    double beamR = 0;
    std::array<double, 4> beamPool { 0, 0, 1, 0 };
    if (beamU != nullptr)
    {
        beamFoot = { camFrac[0] + beamU->x - s.x, wd.floorHeight (beamU->x, beamU->z), camFrac[1] + beamU->z - s.z };
        beamR = 4.5 + 0.09 * (beamU->y - beamFoot[1]);
        beamPool = { beamFoot[0], beamFoot[2], beamR * 1.3, beamU->beam };
    }
    shWin[0] = nearest (shWin[0]); shWin[1] = nearest (shWin[1]);   // right across the world's wrap

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.u ("uHeight"), 0);
        glUniform2f (p.u ("uCamCell"), (float) ccx, (float) ccz);
        glUniform2f (p.u ("uCamFrac"), (float) camFrac[0], (float) camFrac[1]);
        glUniform2f (p.u ("uOff0"), (float) off0[0], (float) off0[1]); glUniform2f (p.u ("uOff1"), (float) off1[0], (float) off1[1]); glUniform2f (p.u ("uOff2"), (float) off2[0], (float) off2[1]);
        glUniform3f (p.u ("uCamRel"), (float) camRel[0], (float) camRel[1], (float) camRel[2]);
        glUniform1f (p.u ("uTime"), (float) time);
        glUniform4f (p.u ("uBeamPool"), (float) beamPool[0], (float) beamPool[1], (float) beamPool[2], (float) beamPool[3]);
        glUniform2f (p.u ("uLoopC"), (float) (W::loopCentre[0] - ccx), (float) (W::loopCentre[1] - ccz));
        glUniform1i (p.u ("uRoad"), 7); glUniform1i (p.u ("uLand"), 9);
        glUniform2f (p.u ("uRoadOrg"), (float) (W::roadOrigin[0] - ccx), (float) (W::roadOrigin[1] - ccz));
        glUniform4fv (p.u ("uPoles[0]"), 8, poleArr.data()); glUniform2fv (p.u ("uPoleSide[0]"), 8, poleSide.data());
        glUniformMatrix4fv (p.u ("uViewProj"), 1, GL_FALSE, VP.data());
    };
    const auto v3 = [] (int loc, const V3& v) { glUniform3f (loc, (float) v[0], (float) v[1], (float) v[2]); };
    ++frameStamp;
    const auto setLight = [&] (const Program& p)
    {
        v3 (p.u ("uSun"), SUN); glUniform2f (p.u ("uSunXZ"), (float) SUN_XZ[0], (float) SUN_XZ[1]); glUniform1f (p.u ("uSunTan"), (float) SUN_TAN);
        v3 (p.u ("uSunCol"), skyCol.sunCol);
        v3 (p.u ("uZenith"), skyCol.zenith); v3 (p.u ("uMidSky"), skyCol.mid); v3 (p.u ("uHorSun"), skyCol.horSun);
        v3 (p.u ("uHorAway"), skyCol.horAway); v3 (p.u ("uGlowCol"), skyCol.glow);
        v3 (p.u ("uAmbUp"), skyCol.ambUp); v3 (p.u ("uAmbDown"), skyCol.ambDown);
        glUniform1f (p.u ("uHazeK"), (float) W::hazeK);
        glUniform1i (p.u ("uShadow"), 3);
        glUniform2f (p.u ("uShadowOff"), (float) fract (ccx / (W::shN * W::shTexel)), (float) fract (ccz / (W::shN * W::shTexel)));
        glUniform3f (p.u ("uShadowWin"), (float) shWin[0], (float) shWin[1], (float) (W::shN * W::shTexel / 2));
        glUniform2f (p.u ("uWind"), (float) WIND[0], (float) WIND[1]);
        glUniform2f (p.u ("uHitT"), (float) std::min (s.hitT, 1e4), (float) s.hitS);
    };
    // The frame's uniforms, the same for every draw of a scene: set once a scene in each program (a program keeps
    // them), not again for each of its draws.
    const auto setFrame = [&] (const Program& p) { if (p.stamp == frameStamp) return; p.stamp = frameStamp; setCommon (p); setLight (p); };
    // At the foot the instance buffers take new storage for each draw (upload's renew): what was overwritten in place
    // cut the command buffer once a scene drawn again. Kept to the bottom rung: the rungs above send what they sent.
    const bool renew = quality.thin > 0;
    const bool cabin = drawsCabin();
    const auto box = cabin ? glassBox : std::optional<V4>();

    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (0, 0, rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE);
    glDepthMask (GL_TRUE);
    glDisable (GL_SCISSOR_TEST);
    glClearColor (0, 0, 0, 1);                              // alpha 1: the cabin's masked frame counts as air for the rays
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texNoise);
    glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, texShadow);
    glActiveTexture (GL_TEXTURE7); glBindTexture (GL_TEXTURE_2D, texRoad);
    glActiveTexture (GL_TEXTURE8); glBindTexture (GL_TEXTURE_2D, texChalk);
    glActiveTexture (GL_TEXTURE9); glBindTexture (GL_TEXTURE_2D, texLand);

    // Only the windshield's box is drawn (the cabin's dashboard covers the rest).
    if (box)
    {
        const int x0 = (int) std::floor ((*box)[0] * rw), x1 = (int) std::ceil ((*box)[2] * rw);
        const int y0 = (int) std::floor ((1 - (*box)[3]) * rh), y1 = (int) std::ceil ((1 - (*box)[1]) * rh);
        glEnable (GL_SCISSOR_TEST); glScissor (x0, y0, x1 - x0, y1 - y0);
    }

    // What stands on the ground first (the ground's fragments behind it are then never shaded).
    // The flowers: three instanced kinds in their fields.
    if (! off ("flora"))
    {
        const auto& p = flora;
        glUseProgram (p.id);
        setFrame (p);
        glUniform1f (p.u ("uThin"), (float) quality.thin); glUniform1f (p.u ("uGustPh"), (float) gustPh);
        glUniform1f (p.u ("uViewBottom"), (float) (box ? 1 - 2 * (*box)[3] : -1.0));   // the glass's lower edge (the cabin's own)
        // THE CELLS THAT CAN SHOW. The shader culls a flower whose root (somewhere in its cell's square) is past the
        // field's reach, behind the eye, off the view's sides or under the glass's lower edge — each test a disc or a
        // half-plane of the ground, as the shader writes it; a cell is left out here only when its whole square fails
        // one of them, so no flower that shows is lost. The window's rows go in bands of 8, each drawn over the run of
        // its cells that can show (the window's base and width moved to it: every cell keeps its own flower).
        const double vb = box ? 1 - 2 * (*box)[3] : -1.0, y0 = camRel[1] - 1.9, y1 = camRel[1] + 1.5;
        // the shader's tests as g (x, z) = a x + b z + c >= 0: in front (cp.w >= 0.2), within the left and right
        // edges (|cp.x| <= 1.06 cp.w + 0.8), over the glass's lower edge (ct.y >= ct.w (vb - 0.02))
        const auto plane = [&] (size_t row, double y) { return std::array<double, 3> { (double) VP[row], (double) VP[8 + row], (double) VP[4 + row] * y + (double) VP[12 + row] }; };
        const auto cpx = plane (0, y0), cpw = plane (3, y0), cty = plane (1, y1), ctw = plane (3, y1);
        std::array<std::array<double, 3>, 4> tests {};
        for (size_t k = 0; k < 3; ++k)
        {
            tests[0][k] = cpw[k] - (k == 2 ? 0.2 : 0.0);
            tests[1][k] = 1.06 * cpw[k] + cpx[k] + (k == 2 ? 0.8 : 0.0);
            tests[2][k] = 1.06 * cpw[k] - cpx[k] + (k == 2 ? 0.8 : 0.0);
            tests[3][k] = cty[k] - (vb - 0.02) * ctw[k];
        }
        const auto canShow = [&] (double x0, double z0, double cell, double reach)
        {
            const double x1 = x0 + cell, z1 = z0 + cell;
            const double ex = std::max ({ x0 - camFrac[0], 0.0, camFrac[0] - x1 }), ez = std::max ({ z0 - camFrac[1], 0.0, camFrac[1] - z1 });
            if (ex * ex + ez * ez > reach * reach) return false;
            for (const auto& g : tests)       // each test linear over the ground: its largest at one corner
                if (g[0] * (g[0] > 0 ? x1 : x0) + g[1] * (g[1] > 0 ? z1 : z0) + g[2] < -1.0e-9)
                    return false;
            return true;
        };
        for (const auto& F : W::flora)
        {
            const int bx = (int) std::floor ((s.x + fx * F.ahead) / F.cell) - F.w / 2, bz = (int) std::floor ((s.z + fz * F.ahead) / F.cell) - F.w / 2;
            // At the foot the field's outer band goes too: the half the ladder keeps shrinks away footFloraBeyond
            // past the half it thins (the cells beyond are then not drawn).
            auto far = F.far;
            if (quality.thin > 0)
            {
                far[0] = lerp (far[0], far[2] + footFloraBeyond, quality.thin);
                far[1] = lerp (far[1], far[3] + footFloraBeyond, quality.thin);
            }
            glBindVertexArray (floraMesh[(size_t) F.kind].vao);
            glUniform1i (p.u ("uFloraM"), (int) Js::round (W::worldPeriod / F.cell));
            glUniform1f (p.u ("uFloraCell"), (float) F.cell); glUniform1f (p.u ("uFloraKind"), (float) F.kind); glUniform1f (p.u ("uFloraDensity"), (float) F.density);
            glUniform2f (p.u ("uFloraSize"), (float) F.smin, (float) F.smax);
            glUniform4f (p.u ("uFloraFar"), (float) far[0], (float) far[1], (float) far[2], (float) far[3]);
            constexpr int band = 8;
            for (int j0 = 0; j0 < F.w; j0 += band)
            {
                const int rows = std::min (band, F.w - j0);
                int i0 = F.w, i1 = -1;
                for (int j = j0; j < j0 + rows; ++j)
                    for (int i = 0; i < F.w; ++i)
                        if ((i < i0 || i > i1) && canShow ((bx + i) * F.cell - ccx, (bz + j) * F.cell - ccz, F.cell, far[1]))
                        { i0 = std::min (i0, i); i1 = std::max (i1, i); }
                if (i1 < i0) continue;
                const int cols = i1 - i0 + 1;
                glUniform2i (p.u ("uFloraBase"), bx + i0, bz + j0); glUniform1i (p.u ("uFloraW"), cols);
                glDrawElementsInstanced (GL_TRIANGLES, floraMesh[(size_t) F.kind].count, GL_UNSIGNED_SHORT, nullptr, cols * rows);
            }
        }
    }
    // The oaks (near, mid and far), the poles, the windpumps.
    int nw = 0;
    if (! off ("props"))
    {
        const auto& p = prop;
        glUseProgram (p.id);
        setFrame (p);
        glUniform1i (p.u ("uNoise"), 2);
        std::array<int, 3> nOak {};
        const std::array<Prop*, 3> lods { &oakNear, &oakMid, &oakFar };
        const double fl = Js::hypot (fx, fz) > 0 ? Js::hypot (fx, fz) : 1;
        // At the foot the far oaks shrink into the ground, from footOakFrom, gone by footOakGone once thin is 1; while
        // it grows, coming in from beyond the oaks' reach.
        const bool oaksThin = quality.thin > 0;
        const double oakGone = lerp (W::farCut + footOakGone - footOakFrom, footOakGone, quality.thin), oakFrom = oakGone - (footOakGone - footOakFrom);
        for (const auto& o : wd.listOaks())
        {
            const double dx = nearest (o.x - s.x), dz = nearest (o.z - s.z), dist = Js::hypot (dx, dz);
            if (dist > W::farCut || (dx * fx + dz * fz) / fl < 0.52 * dist - o.R * 1.5) continue;   // in view, 58 degrees off the way at most
            const double k = oaksThin ? 1 - smoothstep (oakFrom, oakGone, dist) : 1;
            if (k <= 0) continue;
            const int lod = dist < W::oakNearDistance ? 0 : dist < W::oakMidDistance ? 1 : 2;
            auto& P0 = *lods[(size_t) lod];
            const int i = nOak[(size_t) lod]++;
            if (i >= P0.max) continue;
            put<8> (P0.data, (size_t) i * 8, { camFrac[0] + dx, o.y, camFrac[1] + dz, oaksThin ? o.R * k : o.R, o.yaw, o.s, o.tint, o.vis });
        }
        glUniform1f (p.u ("uKind"), 0);
        for (size_t lod = 0; lod < 3; ++lod)
        {
            auto& P0 = *lods[lod];
            const int n = std::min (nOak[lod], P0.max);
            if (n == 0) continue;
            upload (P0.inst, n * 32, P0.max * 32, P0.data.data(), renew);
            glBindVertexArray (P0.vao);
            glDrawElementsInstanced (GL_TRIANGLES, P0.count, GL_UNSIGNED_SHORT, nullptr, n);
        }
        int np = 0;
        std::optional<std::array<V3, 3>> prev;
        for (const auto& q : wd.listPoles())
        {
            const double dx = nearest (q.x - s.x), dz = nearest (q.z - s.z);
            const V3 at { camFrac[0] + dx, q.y, camFrac[1] + dz };
            if (np < poles.max)
                put<8> (poles.data, (size_t) np++ * 8, { at[0], at[1], at[2], q.h, q.yaw, q.lean, W::hs (q.k & 0xffff, 23), 1 });
            // the wires' ends: the pole's three points, leaned, scaled, turned
            std::array<V3, 3> ends {};
            const double c = Js::cos (q.yaw), sn = Js::sin (q.yaw);
            for (size_t k = 0; k < 3; ++k)
            {
                const auto& wa = W::wireAt[k];
                const double yy = wa[1] * q.h / 9, xx = wa[0] + q.lean * yy;
                ends[k] = { at[0] + c * xx - sn * wa[2], at[1] + yy, at[2] + sn * xx + c * wa[2] };
            }
            if (prev)
                for (int k = 0; k < 3 && nw < wireMax; ++k)
                {
                    const auto& a = (*prev)[(size_t) k]; const auto& b = ends[(size_t) k];
                    put<7> (wireData, (size_t) nw++ * 7, { a[0], a[1], a[2], b[0], b[1], b[2], 0.45 + 0.25 * W::hs ((q.k & 0xffff) * 3 + k, 24) });
                }
            prev = ends;
        }
        glUniform1f (p.u ("uKind"), 1);
        upload (poles.inst, np * 32, poles.max * 32, poles.data.data(), renew);
        glBindVertexArray (poles.vao);
        glDrawElementsInstanced (GL_TRIANGLES, poles.count, GL_UNSIGNED_SHORT, nullptr, np);
        // the windpumps: the towers, then the fans turning (the breeze is steady: one speed)
        const V2 upw { -WIND[0] / windL, -WIND[1] / windL };
        const double pyaw = Js::atan2 (upw[0], -upw[1]);
        int npm = 0;
        for (const auto& pm : wd.pumps())
        {
            const double dx = pm.x - s.x, dz = pm.z - s.z;
            if (Js::hypot (dx, dz) > W::farCut || npm >= 8) continue;
            const double ang = std::fmod (time * W::pumpOmega + pm.phase, 2 * pi);
            put<8> (pumpTower.data, (size_t) npm * 8, { camFrac[0] + dx, pm.y, camFrac[1] + dz, 1, pyaw, 0, 0.5, pm.vis });
            put<8> (pumpFan.data, (size_t) npm * 8, { camFrac[0] + dx, pm.y, camFrac[1] + dz, 1, pyaw, ang, 0.5, pm.vis });
            ++npm;
        }
        if (npm > 0)
            for (auto [P0, kind] : { std::pair<Prop*, int> { &pumpTower, 2 }, std::pair<Prop*, int> { &pumpFan, 3 } })
            {
                glUniform1f (p.u ("uKind"), (float) kind);
                upload (P0->inst, npm * 32, P0->max * 32, P0->data.data(), renew);
                glBindVertexArray (P0->vao);
                glDrawElementsInstanced (GL_TRIANGLES, P0->count, GL_UNSIGNED_SHORT, nullptr, npm);
            }
    }
    // The roadside signs, the fences and their flags, the stand-ins for the sprites to come.
    if (! off ("signs"))
    {
        const auto fill = [&] (SignBuf& buf, const std::vector<W::Box>& list, int partIs, int partNot, double reach, int kind)
        {
            int n = 0;
            for (const auto& b : list)
            {
                if ((partIs >= 0 && b.part != partIs) || (partNot >= 0 && b.part == partNot)) continue;
                const double dx = b.x - s.x, dz = b.z - s.z;
                if (n >= buf.max || std::abs (dx) > reach || std::abs (dz) > reach) continue;
                if (b.hx < 1.0 && dx * dx + dz * dz > 420 * 420) continue;                  // small things only near
                put<16> (buf.data, (size_t) n++ * 16, { camFrac[0] + dx, b.y, camFrac[1] + dz, b.yaw, b.hx, b.hy, b.hz, (double) b.part,
                                                        b.rect[0], b.rect[1], b.rect[2], b.rect[3], b.vis, b.flutter, (double) kind, 0 });
            }
            if (n > 0) { upload (buf.inst, n * 64, buf.max * 64, buf.data.data(), renew); }
            return n;
        };
        const auto& B = wd.signBoxes();
        const int nb = fill (signBoxes, B, -1, 2, 1500, 0);
        const int nn = fill (signBanners, B, 2, -1, 1500, 0);
        const int nf = fill (signFlags, wd.flags(), -1, -1, 420, 1);
        const auto& p = sign;
        glUseProgram (p.id);
        setFrame (p);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, texSigns);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, texFlags);
        glUniform1i (p.u ("uAtlas"), 5); glUniform1i (p.u ("uFlags"), 6); glUniform1i (p.u ("uNoise"), 2);
        glUniform1f (p.u ("uAtlasOn"), texSigns != 0 && signAtlasW > 0 ? 1.0f : 0.0f); glUniform1f (p.u ("uFlagsOn"), texFlags != 0 && ! flagCells.empty() ? 1.0f : 0.0f);
        if (nb > 0) { glUniform2f (p.u ("uClothGrid"), 16, 4); glBindVertexArray (signBoxes.vao); glDrawArraysInstanced (GL_TRIANGLES, 0, 36, nb); }
        if (nn > 0) { glUniform2f (p.u ("uClothGrid"), 16, 4); glBindVertexArray (signBanners.vao); glDrawArraysInstanced (GL_TRIANGLES, 0, 16 * 4 * 6, nn); }
        if (nf > 0) { glUniform2f (p.u ("uClothGrid"), 4, 3); glBindVertexArray (signFlags.vao); glDrawArraysInstanced (GL_TRIANGLES, 0, 4 * 3 * 6, nf); }
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, 0);
    }
    // The butterflies: the world's, and those an attack lifted off the flowers.
    if (! off ("bfly"))
    {
        const auto& p = bfly;
        glUseProgram (p.id);
        setFrame (p);
        glUniform1f (p.u ("uPxAngle"), (float) pxAngle);
        int n = 0;
        const auto& Bs = wd.butterflies();
        for (size_t i = 0; i < Bs.size(); ++i)
        {
            const auto& b = Bs[i];
            const double thinned = (i & 1) ? 1 - quality.thin : 1;
            put<8> (bflyData, (size_t) n++ * 8, { camRel[0] + b.rel[0], camRel[1] + b.rel[1], camRel[2] + b.rel[2], b.scale, b.yaw, b.flap, b.pitch,
                                                  b.vis * thinned + 2 * (double) (i % 8) });   // its colour: one of eight, evenly
        }
        for (int k = 0; k < petalSlots; ++k)
        {
            const auto& bs = s.bursts[(size_t) k];
            for (int j = 0; j < bfBurst; ++j)
            {
                const size_t o8 = (size_t) n++ * 8;
                if (bs.t > 6 || bs.level <= 0) { std::fill (bflyData.begin() + (long) o8, bflyData.begin() + (long) o8 + 8, 0.0f); continue; }
                const auto q = [&] (int m) { return W::hs (bs.seed * 16 + j + 1, 330 + m); };
                const double side = q (1) < 0.5 ? -1 : 1;
                const V2 rt { -bs.fz, bs.fx };
                const double ox = bs.x + bs.fx * (6 + 12 * q (2)) + rt[0] * side * (4.5 + 5 * q (3)), oz = bs.z + bs.fz * (6 + 12 * q (2)) + rt[1] * side * (4.5 + 5 * q (3));
                const double t = bs.t, rise = 2.2 * (1 - Js::exp (-t / 0.9)) + 0.25 * t;
                const double away = 1.4 * t + 0.4 * Js::sin (t * 1.7 + q (4) * 6);
                const double wx = ox + rt[0] * side * away + 0.6 * Js::sin (t * 2.3 + q (5) * 6), wz = oz + rt[1] * side * away + 0.6 * Js::cos (t * 1.9 + q (6) * 6);
                const double gy = wd.floorHeight (wx, wz);
                const double vis = smoothstep (0, 0.15, t) * (1 - smoothstep (4.5, 6, t)) * bs.level;
                const double flap = 0.1 + 0.95 * (0.5 + 0.5 * Js::sin (t * 2 * pi * 9 + q (7) * 6));
                put<8> (bflyData, o8, { camFrac[0] + nearest (wx - s.x), gy + 0.15 + rise, camFrac[1] + nearest (wz - s.z), 0.058,
                                        Js::atan2 (rt[0] * side, -rt[1] * side) + 0.4 * Js::sin (t * 1.3), flap, 0.4, std::min (vis, 0.999) + 2 * (double) ((bs.seed + j) % 8) });
            }
        }
        upload (instBfly, n * 32, bfMax * 32, bflyData.data(), renew);
        glBindVertexArray (vaoBfly);
        glDrawElementsInstanced (GL_TRIANGLES, bflyIndices, GL_UNSIGNED_SHORT, nullptr, n);
    }
    // The monolith, where it stands this lap.
    if (fMono != nullptr && fvis > 0.002 && ! off ("finds"))
    {
        const auto& p = mono;
        const auto& f = *fMono;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        setFrame (p);
        v3 (p.u ("uMonoC"), { camFrac[0] + nearest (f.x - s.x), f.y, camFrac[1] + nearest (f.z - s.z) });
        v3 (p.u ("uMonoX"), { -f.nz * monoWide / 2, 0, f.nx * monoWide / 2 }); v3 (p.u ("uMonoZ"), { f.nx * monoThick / 2, 0, f.nz * monoThick / 2 });
        glUniform1f (p.u ("uMonoH"), (float) (monoHigh + 0.3)); glUniform1f (p.u ("uMonoFade"), (float) smoothstep (0, 1, fvis));
        glDrawArrays (GL_TRIANGLES, 0, 30);
    }
    // The near roadside things in 3D: the Feynman van (a find), the barns, the drive-in, the giants, the walker, the
    // yellow submarine, the saucers.
    if (! off ("finds"))
    {
        const auto drawMesh = [&] (const MeshGpu& m, const V3& at, double yaw, double sun, double fade, const std::array<float, 9>& tilt,
                                   const std::array<float, 4>& lamp, const W::CbMesh::Part* part)
        {
            const auto& p = mesh;
            glUseProgram (p.id); glBindVertexArray (m.vao);
            setFrame (p);
            for (GLenum i = 0; i < 3; ++i) { glActiveTexture (GL_TEXTURE10 + i); glBindTexture (GL_TEXTURE_2D, m.tex[i]); }
            glUniform1i (p.u ("uTex1"), 10); glUniform1i (p.u ("uTex2"), 11); glUniform1i (p.u ("uTex3"), 12);
            v3 (p.u ("uMeshAt"), at); glUniform1f (p.u ("uMeshYaw"), (float) yaw);
            glUniformMatrix3fv (p.u ("uMeshTilt"), 1, GL_FALSE, tilt.data()); glUniform4fv (p.u ("uMeshLamp"), 1, lamp.data());
            glUniform1f (p.u ("uMeshSun"), (float) sun); glUniform1f (p.u ("uMeshFade"), (float) fade);
            if (part != nullptr) glDrawElements (GL_TRIANGLES, (GLsizei) part->count, GL_UNSIGNED_INT, offset ((int) part->first * 4));
            else glDrawElements (GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
        };
        const auto seen = [&] (double x, double z) { const double dx = x - s.x, dz = z - s.z, d = Js::hypot (dx, dz); return d < W::farCut && (d < 40 || dx * fx + dz * fz > 0.45 * d); };
        // a censored box (the file's coordinates, z up) on the screen: a flat square of big blocks hanging over it,
        // always facing the viewer, the word over it; none if a corner is behind the eye or a hill stands between.
        const auto censorBox = [&] (const W::Headstock& b, const V3& at, double yaw, double k)
        {
            if (censor.n >= 2) return;
            const double c = Js::cos (yaw), sn = Js::sin (yaw);
            {
                const double bx = at[0] + k * (c * b.c[0] - sn * -b.c[1]), by = at[1] + k * b.c[2], bz = at[2] + k * (sn * b.c[0] + c * -b.c[1]);
                for (int q = 1; q < 16; ++q)
                {
                    const double t = q / 16.0, gx = camRel[0] + (bx - camRel[0]) * t, gz = camRel[2] + (bz - camRel[2]) * t;
                    if (wd.floorHeight (gx - camFrac[0] + s.x, gz - camFrac[1] + s.z) > camRel[1] + (by - camRel[1]) * t) return;
                }
            }
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, z0 = 1e9, z1 = -1e9;
            for (int q = 0; q < 8; ++q)
            {
                const double fx_ = b.c[0] + ((q & 1) ? 0.5 : -0.5) * b.s[0], fy_ = b.c[1] + ((q & 2) ? 0.5 : -0.5) * b.s[1], fz_ = b.c[2] + ((q & 4) ? 0.5 : -0.5) * b.s[2];
                const double px = fx_, py = fz_, pz = -fy_;                                                  // z up -> y up
                const double wx = at[0] + k * (c * px - sn * pz), wy = at[1] + k * py, wz = at[2] + k * (sn * px + c * pz);
                const double cw_ = VP[3] * wx + VP[7] * wy + VP[11] * wz + VP[15];
                if (cw_ <= 0.1) return;
                z0 = std::min (z0, cw_); z1 = std::max (z1, cw_);
                const double nx = (VP[0] * wx + VP[4] * wy + VP[8] * wz + VP[12]) / cw_, ny = (VP[1] * wx + VP[5] * wy + VP[9] * wz + VP[13]) / cw_;
                x0 = std::min (x0, nx); x1 = std::max (x1, nx); y0 = std::min (y0, ny); y1 = std::max (y1, ny);
            }
            if (x1 < -1 || x0 > 1 || y1 < -1 || y0 > 1) return;
            const double boxH = (y1 - y0) * 0.5 * ch, boxW = (x1 - x0) * 0.5 * cw;                  // canvas px
            const double fade = smoothstep (12, 18, boxH * W::stage[1] / ch);
            if (fade <= 0) return;
            const double side0 = std::max (boxW, boxH);
            const double bPx = std::max (6.0 * ch / W::stage[1], side0 / 8), nB = std::max (2.0, Js::round (side0 / bPx)), side = nB * bPx;
            const double um = (x0 + x1) * 0.25 + 0.5, vmid = (y0 + y1) * 0.25 + 0.5;
            const double u0 = um - side / 2 / cw, u1 = um + side / 2 / cw, v0 = vmid - side / 2 / ch, v1 = vmid + side / 2 / ch;
            const auto n4 = (size_t) censor.n * 4;
            censor.mos[n4] = (float) u0; censor.mos[n4 + 1] = (float) v0; censor.mos[n4 + 2] = (float) u1; censor.mos[n4 + 3] = (float) v1;
            censor.blk[n4] = (float) (bPx / cw); censor.blk[n4 + 1] = (float) (bPx / ch); censor.blk[n4 + 2] = (float) fade; censor.blk[n4 + 3] = 0;
            censor.z[(size_t) censor.n * 2] = (float) z0; censor.z[(size_t) censor.n * 2 + 1] = (float) z1;
            ++censor.n;
            // the word labelling it: just above the square, touching, centred on it; as tall as the box on the screen
            if (censor.nl < 2)
            {
                const double lh = boxH / ch, lw = lh * (512.0 / 112.0) * ch / cw, lb = v1 - 0.16 * lh;
                const auto l4 = (size_t) censor.nl * 4;
                censor.lab[l4] = (float) (um - lw / 2); censor.lab[l4 + 1] = (float) lb; censor.lab[l4 + 2] = (float) (um + lw / 2); censor.lab[l4 + 3] = (float) (lb + lh);
                censor.labA[(size_t) censor.nl] = (float) fade; censor.labZ[(size_t) censor.nl] = (float) z0;
                ++censor.nl;
            }
        };
        if (const auto it = meshes.find ("van"); it != meshes.end() && fVan != nullptr && fvis > 0.002 && seen (fVan->x, fVan->z))   // its nose (-x in its file) along the way
            drawMesh (it->second, { camFrac[0] + fVan->x - s.x, fVan->y, camFrac[1] + fVan->z - s.z }, Js::atan2 (-fVan->nz, -fVan->nx), wd.sunAt (fVan->x, fVan->z), fvis,
                      tiltNone, lampNone, nullptr);
        for (const auto& sp : wd.spritePlaces())                                     // their fronts (-y in their files) toward the road
        {
            if (sp.mesh.empty()) continue;
            const auto it = meshes.find (sp.mesh);
            if (it == meshes.end() || ! seen (sp.x, sp.z)) continue;
            const auto& m = it->second;
            const double yaw = Js::atan2 (-sp.nx, sp.nz);
            const W::CbMesh::Part* part = nullptr;
            if (sp.hasPart)
                for (const auto& pt : m.parts) if (pt.range == sp.part) { part = &pt; break; }
            const double off = part != nullptr ? part->cx * sp.scale : 0;           // a part of a row: its middle on the place
            const V3 at { camFrac[0] + sp.x - s.x - Js::cos (yaw) * off, sp.y, camFrac[1] + sp.z - s.z - Js::sin (yaw) * off };
            drawMesh (m, at, yaw, sp.vis, 1, (sp.scale < 1 || sp.scale > 1) ? W::tiltOf ({ 0, 0 }, sp.scale) : tiltNone, sp.paint ? lampPaint : lampNone, part);
            for (const auto& b : m.censor)
                if (! sp.hasPart || (b.c[0] >= sp.part[0] && b.c[0] <= sp.part[1]))
                    censorBox (b, at, yaw, sp.scale);
        }
        // the yellow submarine, crossing
        const auto& ys = wd.sub();
        if (const auto it = meshes.find ("yellowsub3d/yellowsub"); ys.on && ys.fade > 0.003 && it != meshes.end())
            drawMesh (it->second, { camFrac[0] + ys.x - s.x, ys.y, camFrac[1] + ys.z - s.z }, ys.yaw, 1, ys.fade, W::tiltOf (ys.lean, W::subScale), lampPaint, nullptr);
        // the saucers: leaning, their lamps chasing and breathing, flashing with an attack
        if (const auto it = meshes.find ("saucer"); it != meshes.end())
        {
            const double flash = s.hitS * Js::exp (-std::min (s.hitT, 1e4) / 0.45);
            for (const auto& u : wd.saucers())
            {
                const double dx = u.x - s.x, dz = u.z - s.z, d = Js::hypot (dx, dz);
                if (d > W::farCut || (d > 60 && dx * fx + dz * fz < 0)) continue;
                const std::array<float, 4> lamp { 1, (float) (time + u.i * 7), (float) flash, 0 };
                drawMesh (it->second, { camFrac[0] + dx, u.y, camFrac[1] + dz }, u.yaw, 1, 1, W::tiltOf (u.lean, W::saucerScale), lamp, nullptr);
            }
        }
        for (GLenum i = 0; i < 3; ++i) { glActiveTexture (GL_TEXTURE10 + i); glBindTexture (GL_TEXTURE_2D, 0); }
    }
    // The ground: the near level, then the outer two with the inner one's square left out.
    if (! off ("ground"))
    {
        struct Level { double ox, oz, size, cenX, cenZ, half, cell, coarse; };
        std::array<Level, 3> outerAt {};
        for (size_t L = 0; L < 3; ++L)
        {
            const double cell = W::levels[L].cell, half = W::levels[L].n * cell / 2;
            const double coarse = L + 1 < 3 ? W::levels[L + 1].cell : cell * 4;   // the inner square snaps to the next level's cell
            const double cenX = s.x + fx * 0.35 * half, cenZ = s.z + fz * 0.35 * half;
            const double ox = std::floor ((cenX - half) / coarse) * coarse, oz = std::floor ((cenZ - half) / coarse) * coarse;
            outerAt[L] = { ox, oz, W::levels[L].n * cell, cenX, cenZ, half, cell, coarse };
        }
        const double SL = SUN_TAN;
        const auto shOf = [&] (const W::Find& q, double hgt, double w) { const double ax = camFrac[0] + q.x - s.x, az = camFrac[1] + q.z - s.z, Ls = hgt / SL; return std::array<double, 5> { ax, az, ax - SUN_XZ[0] * Ls, az - SUN_XZ[1] * Ls, w }; };
        const auto sv = fVan != nullptr ? shOf (*fVan, 2.3, 1.2) : std::array<double, 5> { 0, 0, 0, 1, 1 };
        const auto sm = fMono != nullptr ? shOf (*fMono, monoHigh, monoWide * 0.4) : std::array<double, 5> { 0, 0, 0, 1, 1 };
        glBindVertexArray (grid.vao);
        for (size_t L = 0; L < 3; ++L)
        {
            const auto& g = outerAt[L];
            const auto& p = L == 0 ? ground : groundH;
            glUseProgram (p.id);
            setFrame (p);
            glUniform1i (p.u ("uDetail"), 1);
            glUniform2f (p.u ("uDetOff0"), (float) fract (ccx / 3.3), (float) fract (ccz / 3.3)); glUniform2f (p.u ("uDetOff1"), (float) fract (ccx / 0.85), (float) fract (ccz / 0.85));
            glUniform2f (p.u ("uLvlOrigin"), (float) (g.ox - ccx), (float) (g.oz - ccz));
            glUniform1f (p.u ("uCellSize"), (float) g.cell);
            const bool last = L + 1 == 3;
            const double mEnd = last ? 1e9 : g.half - g.coarse - 0.5, mStart = last ? 1e9 : mEnd - 0.32 * g.half;
            const double thinEnd = L == 0 ? lerp (mEnd, 45, quality.thin) : mEnd, thinStart = L == 0 ? lerp (mStart, 30, quality.thin) : mStart;
            glUniform4f (p.u ("uMorph"), (float) (g.cenX - ccx), (float) (g.cenZ - ccz), (float) thinStart, (float) thinEnd);
            glUniform2f (p.u ("uCoarseOff"), (float) (fract (ccx / g.coarse) * g.coarse), (float) (fract (ccz / g.coarse) * g.coarse));
            glUniform1f (p.u ("uCoarse"), (float) g.coarse);
            glUniform1f (p.u ("uMarchK"), (float) (1 << L));
            if (L > 0)
            {
                const auto& inner = outerAt[L - 1];
                constexpr double e = 0.05;
                glUniform4f (p.u ("uHole"), (float) (inner.ox - ccx + e), (float) (inner.oz - ccz + e), (float) (inner.ox + inner.size - ccx - e), (float) (inner.oz + inner.size - ccz - e));
                glUniform1f (p.u ("uHoleOn"), 1);
            }
            else glUniform1f (p.u ("uHoleOn"), 0);
            glUniform1f (p.u ("uFarCut"), (float) W::farCut); glUniform1f (p.u ("uDebug"), 0);
            { const double fl = Js::hypot (fx, fz) > 0 ? Js::hypot (fx, fz) : 1; glUniform2f (p.u ("uFwdXZ"), (float) (fx / fl), (float) (fz / fl)); }
            glUniform1f (p.u ("uDashes"), (float) W::dashesN());
            glUniform4fv (p.u ("uChalkA[0]"), 4, chalkA.data()); glUniform4fv (p.u ("uChalkB[0]"), 4, chalkB.data()); glUniform4fv (p.u ("uChalkC[0]"), 4, chalkC.data());
            glUniform1i (p.u ("uChalk"), 8);
            if (fPeace != nullptr && fvis > 0.002)
            {
                const auto& f = *fPeace;
                v3 (p.u ("uFindC"), { camFrac[0] + nearest (f.x - s.x), f.y, camFrac[1] + nearest (f.z - s.z) });
                v3 (p.u ("uFindU"), f.u); v3 (p.u ("uFindV"), f.v);
                glUniform4f (p.u ("uFind"), (float) f.R, (float) fvis, (float) (f.k < 0 || f.k > 0 ? f.k : 1), 0);
            }
            else glUniform4f (p.u ("uFind"), 1, 0, 1, 0);
            glUniform4f (p.u ("uFindSh"), (float) sv[0], (float) sv[1], (float) sv[2], (float) sv[3]); glUniform4f (p.u ("uFindSh1"), (float) sm[0], (float) sm[1], (float) sm[2], (float) sm[3]);
            glUniform4f (p.u ("uFindShW"), (float) sv[4], (float) (fVan != nullptr ? 0.7 * fvis : 0), (float) sm[4], (float) (fMono != nullptr ? 0.7 * fvis : 0));
            // An outer level leaves the inner one's square out (uHole: discarded there); its cells wholly inside the
            // square, a ring of one cell short of its edge, are not drawn at all — the rows' runs round them, as
            // one multi-draw where GL has it — so the near ground is not rasterised twice more only to be discarded.
            constexpr int NL = 96;
            int si0 = 0, si1 = 0, sj0 = 0, sj1 = 0;
            if (L > 0 && ! off ("holes"))
            {
                const auto& inner = outerAt[L - 1];
                const auto cellOf = [&g] (double at, double from) { return (int) std::lround ((at - from) / g.cell); };
                si0 = std::max (0, cellOf (inner.ox, g.ox) + 1); si1 = std::min (NL, cellOf (inner.ox + inner.size, g.ox) - 1);
                sj0 = std::max (0, cellOf (inner.oz, g.oz) + 1); sj1 = std::min (NL, cellOf (inner.oz + inner.size, g.oz) - 1);
            }
            if (si0 >= si1 || sj0 >= sj1)
                glDrawElements (GL_TRIANGLES, grid.count, GL_UNSIGNED_SHORT, nullptr);
            else
            {
                std::vector<GLsizei> counts;
                std::vector<const void*> starts;
                const auto run = [&] (int c0, int c1) { if (c1 > c0) { counts.push_back ((GLsizei) ((c1 - c0) * 6)); starts.push_back (offset (c0 * 6 * 2)); } };
                int from = 0;
                for (int j = sj0; j < sj1; ++j) { run (from, j * NL + si0); from = j * NL + si1; }
                run (from, NL * NL);
               #if ! defined (__EMSCRIPTEN__)
                if (gpu::loaded (glMultiDrawElements))
                    glMultiDrawElements (GL_TRIANGLES, counts.data(), GL_UNSIGNED_SHORT, starts.data(), (GLsizei) counts.size());
                else
               #endif
                for (size_t k = 0; k < counts.size(); ++k)
                    glDrawElements (GL_TRIANGLES, counts[k], GL_UNSIGNED_SHORT, starts[k]);
            }
        }
    }

    // The sky, where nothing was drawn (depth == 1): before anything is blended, so nothing blended is lost.
    if (! off ("sky"))
    {
        const auto& p = sky;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        setFrame (p);
        glUniform1i (p.u ("uNoise"), 2);
        v3 (p.u ("uCamFwd"), basis.f); v3 (p.u ("uCamRight"), basis.r); v3 (p.u ("uCamUp"), basis.u);
        glUniform2f (p.u ("uTanHalf"), (float) th, (float) tv); glUniform1f (p.u ("uShear"), (float) sh);
        glUniform2f (p.u ("uCloudOff"), (float) fract (time * 0.0011), (float) fract (time * 0.0004));
        glUniform1f (p.u ("uCamAlt"), (float) s.camY); glUniform1f (p.u ("uFlat"), (float) quality.flat);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // What is blended, over the ground and the sky, writing no depth.
    glEnable (GL_BLEND); glBlendFuncSeparate (GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); glDepthMask (GL_FALSE);
    // a saucer's beam: a pale cone down to the poppies
    if (beamU != nullptr && ! off ("finds"))
    {
        const auto& p = beam;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        setFrame (p);
        v3 (p.u ("uBeamTop"), { camFrac[0] + beamU->x - s.x, beamU->y - 0.9 * W::saucerScale, camFrac[1] + beamU->z - s.z }); v3 (p.u ("uBeamFoot"), beamFoot);
        glUniform2f (p.u ("uBeamR"), (float) (1.3 * W::saucerScale), (float) beamR); glUniform1f (p.u ("uBeamA"), (float) beamU->beam);
        glDrawArrays (GL_TRIANGLES, 0, 28 * 6);
    }
    // The sprites: the radio dishes on the far ridges, the water tower.
    if (! off ("finds"))
    {
        struct Opt { double scale, pull, fade, light; bool ridge; V3 tint, rim; };
        const auto drawSprite = [&] (const Sprite& sp, const V3& foot, double distSeen, const V2& nose, const Opt& opt)
        {
            const auto& t = sp.t;
            const double dx = foot[0] - camRel[0], dz = foot[2] - camRel[2];
            const double tl0 = Js::hypot (dx, dz), tl = tl0 > 0 ? tl0 : 1, vx = dx / tl, vz = dz / tl;
            const V3 right { -vz, 0, vx };
            const double hDeg = Js::atan2 (nose[0] * right[0] + nose[1] * right[2], -(nose[0] * vx + nose[1] * vz)) * 180 / pi;
            const int n = t.count != 0 ? t.count : t.grid[0] * t.grid[1];
            const double fh = std::fmod (std::fmod (hDeg, 360) + 360, 360) / (360.0 / n);
            const int k0 = (int) std::floor (fh) % n, k1 = (k0 + 1) % n;
            const double mixK = fh - std::floor (fh);
            const int gc = t.grid[0], gr = t.grid[1];
            const auto rect = [&] (int k) { return std::array<float, 4> { (float) ((double) (k % gc) / gc), (float) (std::floor ((double) k / gc) / gr), (float) ((double) (k % gc + 1) / gc), (float) ((std::floor ((double) k / gc) + 1) / gr) }; };
            // the quad: the anchor on the foot; pulled toward the eye by `pull` (scaled so it looks the same)
            const double sc = opt.scale, hw = t.cell[0] / t.scale / 2 * sc, hh = t.cell[1] / t.scale / 2 * sc;
            const double ax = (t.anchor[0] - t.cell[0] / 2) / t.scale * sc, ay = (t.anchor[1] - t.cell[1] / 2) / t.scale * sc;
            V3 c { foot[0] - right[0] * ax, foot[1] + ay, foot[2] - right[2] * ax };
            const double D = Js::hypot (c[0] - camRel[0], c[1] - camRel[1], c[2] - camRel[2]), kk = std::max (0.2, (D - opt.pull) / D);
            c = { camRel[0] + (c[0] - camRel[0]) * kk, camRel[1] + (c[1] - camRel[1]) * kk, camRel[2] + (c[2] - camRel[2]) * kk };
            const auto& p = sprite;
            glUseProgram (p.id); glBindVertexArray (vaoEmpty);
            setFrame (p);
            glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, sp.tex);
            glUniform1i (p.u ("uAtlas"), 5);
            v3 (p.u ("uCenter"), c);
            v3 (p.u ("uAxisA"), { right[0] * hw * kk, right[1] * hw * kk, right[2] * hw * kk }); glUniform3f (p.u ("uAxisB"), 0, (float) (hh * kk), 0);
            const auto r0 = rect (k0), r1 = rect (k1);
            glUniform4fv (p.u ("uRect0"), 1, r0.data()); glUniform4fv (p.u ("uRect1"), 1, r1.data()); glUniform1f (p.u ("uMix"), (float) mixK);
            glUniform1f (p.u ("uFade"), (float) opt.fade); glUniform1f (p.u ("uHazeDist"), (float) distSeen); glUniform1f (p.u ("uLight"), (float) opt.light);
            glUniform1f (p.u ("uRidge"), opt.ridge ? 1.0f : 0.0f);
            v3 (p.u ("uTint"), opt.tint); v3 (p.u ("uRimCol"), opt.rim);
            // the rim: a step in the atlas toward the sun on the screen, about 1.5 screen pixels
            const double sunR = SUN[0] * right[0] + SUN[2] * right[2], sunU = SUN[1] * 0.5 + 0.12;
            const double rl0 = Js::hypot (sunR, sunU), rl = rl0 > 0 ? rl0 : 1;
            const double pxPerM = rw / (2 * th * std::max (D * kk, 1.0));
            const double atlasPx = 1.6 * t.scale / (pxPerM * kk / sc);
            glUniform2f (p.u ("uRimStep"), (float) (sunR / rl * atlasPx / sp.size[0]), (float) (-sunU / rl * atlasPx / sp.size[1]));
            glDrawArrays (GL_TRIANGLES, 0, 6);
            glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, 0);
        };
        if (const auto it = sprites.find ("dish"); it != sprites.end() && it->second.in)
            for (const auto& d : wd.dishes())
            {
                const V3 foot { camFrac[0] + d.x - s.x, d.y, camFrac[1] + d.z - s.z };
                const double dist = Js::hypot (foot[0] - camRel[0], foot[2] - camRel[2]);
                if (dist > W::farCut || (foot[0] * fx + foot[2] * fz) < 0) continue;
                drawSprite (it->second, foot, dist, { d.nx, d.nz }, { 1, 4, 1, 0.55, false, { 0.95, 0.88, 0.86 }, { 1.3, 0.85, 0.45 } });
            }
        for (const auto& sp : wd.spritePlaces())
        {
            if (! sp.mesh.empty()) continue;
            const auto it = sprites.find (sp.name);
            if (it == sprites.end() || ! it->second.in) continue;
            const V3 foot { camFrac[0] + sp.x - s.x, sp.y, camFrac[1] + sp.z - s.z };
            const double dist = Js::hypot (foot[0] - camRel[0], foot[2] - camRel[2]);
            if (dist > W::farCut || (foot[0] * fx + foot[2] * fz) < -60) continue;
            drawSprite (it->second, foot, dist, { sp.nx, sp.nz }, { 1, sp.pull, 1, 0.78, false, { 1.0, 0.88, 0.76 }, { 1.3, 0.9, 0.5 } });
        }
    }
    // The wires.
    if (nw > 0)
    {
        const auto& p = wire;
        glUseProgram (p.id);
        setFrame (p);
        glUniform1f (p.u ("uPxAngle"), (float) pxAngle);
        upload (instWire, nw * 28, wireMax * 28, wireData.data(), renew);
        glBindVertexArray (vaoWire);
        glDrawElementsInstanced (GL_TRIANGLES, wireSegs * 6, GL_UNSIGNED_SHORT, nullptr, nw);
    }
    // The particles: the pollen, the fluff, an attack's petals, the birds, a beam's petals.
    if (! off ("particles"))
    {
        const auto& p = particle;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        setFrame (p);
        v3 (p.u ("uCamR"), basis.r); v3 (p.u ("uCamU"), basis.u); v3 (p.u ("uCamF"), basis.f);
        std::vector<float> burst (16), burstF (16);
        bool anyBurst = false;
        for (size_t k = 0; k < (size_t) petalSlots; ++k)
        {
            const auto& b = s.bursts[k];
            put<4> (burst, k * 4, { camFrac[0] + nearest (b.x - s.x), 0, camFrac[1] + nearest (b.z - s.z), std::min (b.t, 1e4) });
            put<4> (burstF, k * 4, { b.fx, b.fz, b.level, (double) b.seed });
            if (b.t < 4.5 && b.level > 0) anyBurst = true;
        }
        glUniform4fv (p.u ("uBurst[0]"), 4, burst.data()); glUniform4fv (p.u ("uBurstF[0]"), 4, burstF.data());
        const auto& fl = s.flock;
        glUniform4f (p.u ("uFlock"), (float) (camFrac[0] + nearest (fl.x - s.x)), (float) fl.y, (float) (camFrac[1] + nearest (fl.z - s.z)), (float) (fl.on ? fl.vis : 0));
        glUniform4f (p.u ("uFlockDir"), (float) fl.fx, (float) fl.fz, (float) (fl.n * 1.7), 0);
        const auto draw = [&] (int kind, int count, double amount, const V3& bx, const V3& wrapOff, const V3& flow, double sizeK, double lift)
        {
            if (amount < 0.005) return;
            glUniform1f (p.u ("uKind"), (float) kind); glUniform1f (p.u ("uAmount"), (float) amount); glUniform1f (p.u ("uSizeK"), (float) sizeK);
            v3 (p.u ("uBox"), bx); v3 (p.u ("uWrapOff"), wrapOff); v3 (p.u ("uFlowAcc"), flow);
            glUniform1f (p.u ("uBoxLift"), (float) lift);
            glDrawArrays (GL_TRIANGLES, 0, count * 6);
        };
        const auto wrapOf = [&] (const V3& bx) { return V3 { fract (s.x / bx[0]), fract (s.camY / bx[1]), fract (s.z / bx[2]) }; };
        draw (0, W::pollenCount, 1.0 - 0.5 * quality.thin, W::pollenBox, wrapOf (W::pollenBox), s.pollenFlow, 0.022, 0.6);
        draw (1, W::fluffCount, 1.0 - 0.5 * quality.thin, W::fluffBox, wrapOf (W::fluffBox), s.fluffFlow, 0.075, 1.2);
        if (anyBurst) draw (2, petalSlots * W::petals, 1, { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, 0.045, 0);
        if (fl.on && fl.vis > 0.003) draw (3, W::birds, 1, { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, 1.0, 0);
        if (beamU != nullptr)
        {
            glUniform4f (p.u ("uBeamP"), (float) beamFoot[0], (float) beamFoot[1], (float) beamFoot[2], (float) beamU->beam);
            glUniform4f (p.u ("uBeamQ"), (float) (beamR * 0.8), (float) std::min (22.0, 0.5 * (beamU->y - beamFoot[1])), (float) beamU->beamT, 0);
            draw (4, W::beamPetals, 1, { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, 0.11, 0);
        }
    }
    // The butterflies on the glass (PARK), in the scene's frame (screen space), before the post.
    glDisable (GL_DEPTH_TEST);
    {
        bool any = false;
        std::vector<float> glassData ((size_t) glassCount * 8);
        constexpr std::array<int, 5> colours { 1, 3, 0, 5, 7 };                     // pink, cyan, monarch, lemon, magenta
        for (size_t i = 0; i < (size_t) glassCount; ++i)
        {
            const auto& g = s.glass[i];
            const double toSun = Js::hypot ((g.x - sunNdc[0]) * aspect, g.y - sunNdc[1]);
            const double light = 0.45 + 0.75 * Js::exp (-toSun * 1.6);
            const double halfSpan = W::glassSpots[i].r * g.k * rw / W::stage[0];
            put<8> (glassData, i * 8, { g.x, g.y, halfSpan, g.rot, clamp (g.open, 0.06, 1), g.a, light, (double) colours[i % 5] });
            if (g.a > 0.003) any = true;
        }
        if (any)
        {
            const auto& p = glass;
            glUseProgram (p.id); glBindVertexArray (vaoGlass);
            glUniform2f (p.u ("uCanvas"), (float) rw, (float) rh);
            upload (instGlass, glassCount * 32, glassCount * 32, glassData.data(), renew);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 6, glassCount);
        }
    }
    glDisable (GL_BLEND); glDepthMask (GL_TRUE);
    glDisable (GL_SCISSOR_TEST);
   #if defined (__EMSCRIPTEN__)
    if (censor.n == 0) { const GLenum depth = GL_DEPTH_ATTACHMENT; glInvalidateFramebuffer (GL_FRAMEBUFFER, 1, &depth); }   // kept only for the censorship
   #endif

    // The post pass: to the finished frame, the sun's rays added near it, the cabin's dashboard over it all.
    const double vanLight = wd.lightAtVan();
    const double gain = 1.0 - 0.14 * (1 - vanLight);
    // Outside the windshield's box and the pendant's swing the dashboard is opaque, and the frame gets there only the
    // dashboard times the gain: while the gain, the dashboard and the frame are as they were, those pixels are as the
    // last frame left them, and the pass that lays the dashboard is drawn only inside (a scissor).
    const auto inside = [&] (int w, int h, bool withPendant)
    {
        const auto& b = *box;
        double ux0 = b[0], uy0 = b[1], ux1 = b[2], uy1 = b[3];          // fractions of the stage, from its top
        if (withPendant && pendBox)
        {
            const auto& q = *pendBox;
            double reach = 0;
            for (int k = 0; k < 4; ++k)
                reach = std::max (reach, Js::hypot (q.box[(k & 1) ? 2 : 0] - q.pivot[0], q.box[(k & 2) ? 3 : 1] - q.pivot[1]));
            reach += 2;
            ux0 = std::min (ux0, (q.pivot[0] - reach) / W::stage[0]); ux1 = std::max (ux1, (q.pivot[0] + reach) / W::stage[0]);
            uy0 = std::min (uy0, (q.pivot[1] - reach) / W::stage[1]); uy1 = std::max (uy1, (q.pivot[1] + reach) / W::stage[1]);
        }
        const int x0 = std::max (0, (int) std::floor (ux0 * w) - 1), x1 = std::min (w, (int) std::ceil (ux1 * w) + 1);
        const int y0 = std::max (0, (int) std::floor ((1 - uy1) * h) - 1), y1 = std::min (h, (int) std::ceil ((1 - uy0) * h) + 1);
        glEnable (GL_SCISSOR_TEST); glScissor (x0, y0, x1 - x0, y1 - y0);
    };
    const bool same = cabin && box && outsideTex == texDash && ! (gain < outsideGain || gain > outsideGain) && ! off ("cache");
    glBindFramebuffer (GL_FRAMEBUFFER, fboOut);
    glViewport (0, 0, cw, ch);
    if (! same)
        glClear (GL_COLOR_BUFFER_BIT);      // all of it drawn: nothing of the last frame is read in
    {
        const auto& p = post;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        glActiveTexture (GL_TEXTURE4); glBindTexture (GL_TEXTURE_2D, fboColour);
        glUniform1i (p.u ("uColour"), 4);
        const double nx = sunNdc[0], ny = sunNdc[1];
        const double on = sunC[2] > 0.05 ? 1 - smoothstep (1.0, 1.6, std::max (std::abs (nx), std::abs (ny))) : 0;
        glUniform2f (p.u ("uSunUv"), (float) (nx * 0.5 + 0.5), (float) (ny * 0.5 + 0.5)); glUniform1f (p.u ("uSunOn"), (float) on);
        glUniform1f (p.u ("uAspect"), (float) aspect); glUniform1f (p.u ("uRayGain"), off ("rays") ? 0.0f : 0.42f); glUniform1f (p.u ("uFlat"), (float) quality.flat);
        v3 (p.u ("uRayCol"), skyCol.rays);
        glActiveTexture (GL_TEXTURE6); glBindTexture (GL_TEXTURE_2D, cabin ? texDash : 0);
        glUniform1i (p.u ("uDash"), 6); glUniform1f (p.u ("uDashOn"), cabin ? 1.0f : 0.0f); glUniform1f (p.u ("uPanelGain"), (float) gain);
        glUniform4fv (p.u ("uMos[0]"), 2, censor.mos.data()); glUniform4fv (p.u ("uMosBlk[0]"), 2, censor.blk.data()); glUniform4fv (p.u ("uLab[0]"), 2, censor.lab.data());
        glUniform2fv (p.u ("uLabA"), 1, censor.labA.data());
        glUniform2fv (p.u ("uMosZ[0]"), 2, censor.z.data()); glUniform2fv (p.u ("uLabZ"), 1, censor.labZ.data()); glUniform2f (p.u ("uZnf"), (float) W::zNear, (float) W::zFar);
        glActiveTexture (GL_TEXTURE14); glBindTexture (GL_TEXTURE_2D, censor.n > 0 ? fboDepth : 0); glUniform1i (p.u ("uDepth"), 14);
        glActiveTexture (GL_TEXTURE13); glBindTexture (GL_TEXTURE_2D, texLabel); glUniform1i (p.u ("uLabel"), 13);
        if (same) inside (cw, ch, true);
        if (! off ("post"))
            glDrawArrays (GL_TRIANGLES, 0, 3);
        glDisable (GL_SCISSOR_TEST);
        if (cabin && box && ! off ("post")) { outsideGain = gain; outsideTex = texDash; }
        else outsideGain = -1.0;
        for (const GLenum unit : { GL_TEXTURE4, GL_TEXTURE5, GL_TEXTURE6, GL_TEXTURE13, GL_TEXTURE14 })
        { glActiveTexture (unit); glBindTexture (GL_TEXTURE_2D, 0); }
    }
    // The pendant over it all: with the cabin, or alone over the glass for a plate laid over the view without it.
    if ((cabin || pendantAlone) && texPend != 0 && pendBox)
    {
        glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        const auto& p = pendant;
        const auto& pd = s.pend;
        glUseProgram (p.id); glBindVertexArray (vaoEmpty);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, texPend);
        glUniform1i (p.u ("uPend"), 5); glUniform1f (p.u ("uPanelGain"), (float) (0.6 + 0.4 * gain * vanLight));
        glUniform4f (p.u ("uPendBox"), (float) pendBox->box[0], (float) pendBox->box[1], (float) pendBox->box[2], (float) pendBox->box[3]);
        glUniform2f (p.u ("uPendPivot"), (float) pendBox->pivot[0], (float) pendBox->pivot[1]);
        glUniform2f (p.u ("uStage"), (float) W::stage[0], (float) W::stage[1]);
        glUniform1f (p.u ("uPendAngle"), (float) pd.th); glUniform1f (p.u ("uPendFore"), (float) Js::cos (clamp (pd.ph, -0.6, 0.6)));
        glUniform1f (p.u ("uGlint"), (float) (0.8 * std::pow (std::max (0.0, Js::cos (pd.th * 9 + pd.ph * 5 + 0.6)), 16) * vanLight));
        glDrawArrays (GL_TRIANGLES, 0, 6);
        glActiveTexture (GL_TEXTURE5); glBindTexture (GL_TEXTURE_2D, 0);
        glDisable (GL_BLEND);
    }
    glActiveTexture (GL_TEXTURE0);
    glEnable (GL_DEPTH_TEST);
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::california
