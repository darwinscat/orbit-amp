// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of walker-gl.js's drawing, call for call: the same numbers handed to the same shaders, the matrices
// made as the prototype makes them (in doubles from the f32 values it keeps them in), the camera's trigonometry
// V8's (WalkerWorld::Js), so a frame here is the prototype's frame at the same state.

#include "WalkerGl.h"

#include "../../shaders/Shaders.h"

#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

namespace orbitamp::redgroove
{

using namespace orbitamp::gpu;

namespace
{
    using W = WalkerWorld;
    using Js = WalkerWorld::Js;

    constexpr int lmTex = 512;              // the light map's texels across ...
    constexpr double lmSize = 240.0;        // ... and the metres it covers, centred on the eye
    constexpr double lmScale = 1.0;         // its scale: RGBA16F renders here, so no packing into RGBA8
    constexpr int groundRings = 68, groundSpokes = 96;
    constexpr double groundNear = 0.5, groundFar = 420.0;
    constexpr double hTan = 1.2;            // tan of the half horizontal field: ~100 degrees across at the wide aspects
    constexpr double zNear = 0.3, zFar = 3000.0;
    constexpr int sporeCount = 4200;        // the drifting spores

    // The attributes' locations, shared by every program that reads them (walker-gl.js's LOC).
    constexpr GLuint locPos = 0, locNrm = 1, locUv = 2, locPart = 3, locI0 = 4, locI1 = 5, locOff = 0;
    const std::vector<gpu::Attribute> attributes { { "aNrm", locNrm }, { "aUv", locUv }, { "aPart", locPart },
                                                   { "aI0", locI0 }, { "aI1", locI1 }, { "aOff", locOff } };

    /** To the nearest, a half to the even one, as JUCE's roundToInt does it: the frame's size at a render scale. */
    inline int roundToInt (float x) noexcept { return (int) std::lrint ((double) x); }

    using V3 = std::array<double, 3>;
    inline V3 mix3 (const V3& a, const V3& b, double t) noexcept { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
    inline std::array<float, 3> f3 (const V3& v) noexcept { return { (float) v[0], (float) v[1], (float) v[2] }; }

    /** The camera's forward, right and up for a heading, a pitch and a roll. */
    struct Basis { V3 f, r, u; };
    Basis cameraBasis (double heading, double pitch, double roll) noexcept
    {
        const double ch = Js::cos (heading), sh = Js::sin (heading), cp = Js::cos (pitch), sp = Js::sin (pitch);
        const V3 f { sh * cp, sp, -ch * cp }, r0 { ch, 0, sh };
        const V3 u0 { r0[1] * f[2] - r0[2] * f[1], r0[2] * f[0] - r0[0] * f[2], r0[0] * f[1] - r0[1] * f[0] };
        const double cr = Js::cos (roll), sr = Js::sin (roll);
        const V3 r { r0[0] * cr + u0[0] * sr, r0[1] * cr + u0[1] * sr, r0[2] * cr + u0[2] * sr };
        const V3 u { u0[0] * cr - r0[0] * sr, u0[1] * cr - r0[1] * sr, u0[2] * cr - r0[2] * sr };
        return { f, r, u };
    }

    /** A model matrix taking the unit-length y axis from a to b (column-major), and its normal matrix. */
    void boneMatrix (const V3& a, const V3& b, std::array<float, 16>& M, std::array<float, 9>& N) noexcept
    {
        double yx = b[0] - a[0], yy = b[1] - a[1], yz = b[2] - a[2];
        double L = Js::hypot (yx, yy, yz);
        if (! (L > 0)) L = 1e-4;            // the prototype's `|| 1e-4`
        yx /= L; yy /= L; yz /= L;
        const V3 ref = std::abs (yz) > 0.9 ? V3 { 1, 0, 0 } : V3 { 0, 0, 1 };
        double xx = yy * ref[2] - yz * ref[1], xy = yz * ref[0] - yx * ref[2], xz = yx * ref[1] - yy * ref[0];
        double xl = Js::hypot (xx, xy, xz);
        if (! (xl > 0)) xl = 1;
        xx /= xl; xy /= xl; xz /= xl;
        const double zx = xy * yz - xz * yy, zy = xz * yx - xx * yz, zz = xx * yy - xy * yx;
        const double m[16] { xx, xy, xz, 0, yx * L, yy * L, yz * L, 0, zx, zy, zz, 0, a[0], a[1], a[2], 1 };
        const double n[9] { xx, xy, xz, yx / L, yy / L, yz / L, zx, zy, zz };
        for (int i = 0; i < 16; ++i) M[(size_t) i] = (float) m[i];
        for (int i = 0; i < 9; ++i) N[(size_t) i] = (float) n[i];
    }

    /** An instance's 8 floats onto `out`; a withering one's (WalkerWorld::Quality) with its size and its glow
        times `keep`. */
    void putInstance (std::vector<float>& out, const float* in, bool withers, float keep)
    {
        out.insert (out.end(), in, in + 8);
        if (withers)
        {
            out[out.size() - 6] *= keep;        // the size
            out[out.size() - 2] *= keep;        // the glow
        }
    }

    /** The floats into an instance buffer, in new storage so that nothing waits for the GPU (an empty list as
        one instance of zeros); the instances they hold. */
    int upload (std::uint32_t buffer, const std::vector<float>& data)
    {
        static const float none[8] {};
        glBindBuffer (GL_ARRAY_BUFFER, buffer);
        if (data.empty())
            glBufferData (GL_ARRAY_BUFFER, sizeof (none), none, GL_DYNAMIC_DRAW);
        else
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (data.size() * sizeof (float)), data.data(), GL_DYNAMIC_DRAW);
        return (int) (data.size() / 8);
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
}

//==============================================================================
std::array<double, 2> WalkerGl::fieldFor (double aspect) noexcept
{
    const double tv = std::min (0.80, std::max (0.24, hTan / aspect));
    const double th = std::min (1.32, tv * aspect);
    return { th, tv };
}

int WalkerGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool WalkerGl::compile (Program& p, const char* vertexPath, const char* fragmentPath, const char* what)
{
    std::string log;
    p.id = gpu::program (gpu::header() + shaders::source (vertexPath), gpu::header() + shaders::source (fragmentPath), log, attributes);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("walker: " + trouble);
        return false;
    }
    return true;
}

bool WalkerGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("walker: " + trouble);
        return false;
    }
    world.build();

    if (! compile (ground, "walker/ground.vert", "walker/ground.frag", "ground")
        || ! compile (mush, "walker/mush.vert", "walker/mush.frag", "mushroom")
        || ! compile (splat, "walker/splat.vert", "walker/splat.frag", "light map")
        || ! compile (halo, "walker/halo.vert", "walker/halo.frag", "halo")
        || ! compile (sky, "walker/sky.vert", "walker/sky.frag", "sky")
        || ! compile (leg, "walker/leg.vert", "walker/leg.frag", "leg")
        || ! compile (spore, "walker/spore.vert", "walker/spore.frag", "spore")
        || ! compile (burst, "walker/burst.vert", "walker/burst.frag", "burst"))
    {
        destroy();
        return false;
    }
    if (! makeTextures())
    {
        destroy();
        return false;
    }
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

void WalkerGl::destroy()
{
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    for (auto& L : lists)
    {
        del (L.inst, glDeleteBuffers); del (L.vaoSplat, glDeleteVertexArrays);
        del (L.seen, glDeleteBuffers); del (L.vaoMesh, glDeleteVertexArrays); del (L.vaoHalo, glDeleteVertexArrays);
        L.count = L.splatCount = L.seenCount = 0;
        L.splatThin = 0;
        L.data.clear(); L.ground.clear();
    }
    for (auto& b : windowBase) b.set = false;
    del (vboGround, glDeleteBuffers); del (iboGround, glDeleteBuffers); del (vaoGround, glDeleteVertexArrays);
    del (vboLeg, glDeleteBuffers); del (iboLeg, glDeleteBuffers); del (vaoLeg, glDeleteVertexArrays);
    del (vboMesh, glDeleteBuffers); del (iboMesh, glDeleteBuffers); del (vaoEmpty, glDeleteVertexArrays); del (vboEmpty, glDeleteBuffers);
    del (texHeight, glDeleteTextures); del (texDetail, glDeleteTextures); del (texLight, glDeleteTextures); del (fboLight, glDeleteFramebuffers);
    del (fboColour, glDeleteRenderbuffers); del (fboDepth, glDeleteRenderbuffers); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    quality = {};
    lastFrameMs = -1.0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &ground, &mush, &splat, &halo, &sky, &leg, &spore, &burst }) { if (p->id != 0) glDeleteProgram (p->id); p->id = 0; }
}

bool WalkerGl::makeTextures()
{
    // The heightfield, its mips as the world made them, in half floats as the prototype uploads it.
    const auto& mips = world.heightMips();
    glGenTextures (1, &texHeight);
    glBindTexture (GL_TEXTURE_2D, texHeight);
    for (int lvl = 0, size = W::heightTex; lvl < (int) mips.size(); ++lvl, size >>= 1)
        glTexImage2D (GL_TEXTURE_2D, lvl, GL_RGBA16F, size, size, 0, GL_RGBA, GL_FLOAT, mips[(size_t) lvl].data());
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint) mips.size() - 1);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // The moss's detail, its mips made by GL.
    glGenTextures (1, &texDetail);
    glBindTexture (GL_TEXTURE_2D, texDetail);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, W::detailTex, W::detailTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, world.detail().data());
    glGenerateMipmap (GL_TEXTURE_2D);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // The light map, rendered to.
    glGenTextures (1, &texLight);
    glBindTexture (GL_TEXTURE_2D, texLight);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA16F, lmTex, lmTex, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);
    glGenFramebuffers (1, &fboLight);
    glBindFramebuffer (GL_FRAMEBUFFER, fboLight);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texLight, 0);
    const bool ok = glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    if (! ok)
    {
        trouble = "the light map (RGBA16F) is not renderable here";
        gpu::note ("walker: " + trouble);
    }
    return ok;
}

void WalkerGl::makeBuffers()
{
    // The ground: the polar lattice around the eye.
    const auto grid = W::polarGrid (groundRings, groundSpokes, groundNear, groundFar);
    groundIndices = (int) grid.idx.size();
    glGenVertexArrays (1, &vaoGround);
    glBindVertexArray (vaoGround);
    glGenBuffers (1, &vboGround);
    glBindBuffer (GL_ARRAY_BUFFER, vboGround);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (grid.verts.size() * sizeof (float)), grid.verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray (locOff);
    glVertexAttribPointer (locOff, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers (1, &iboGround);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboGround);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (grid.idx.size() * sizeof (std::uint16_t)), grid.idx.data(), GL_STATIC_DRAW);
    glBindVertexArray (0);

    // The mushrooms: nine meshes in one buffer pair, one vertex array per (class, shape) over its own instance
    // buffer, and one more per list for the quads (splats, halos) that read only the instances.
    std::vector<float> allVerts;
    std::vector<std::uint16_t> allIdx;
    int vbase = 0;
    for (int k = 0; k < W::numClasses; ++k)
        for (int v = 0; v < W::variants; ++v)
        {
            const auto m = W::mushroomMesh (v, W::classes[(size_t) k].segs);
            auto& L = lists[(size_t) (k * W::variants + v)];
            L.first = (int) allIdx.size(); L.indices = (int) m.idx.size();
            L.shape = W::extentOf (m);
            allVerts.insert (allVerts.end(), m.verts.begin(), m.verts.end());
            for (auto i : m.idx) allIdx.push_back ((std::uint16_t) (i + vbase));
            vbase += (int) (m.verts.size() / 9);
        }
    glGenBuffers (1, &vboMesh);
    glBindBuffer (GL_ARRAY_BUFFER, vboMesh);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (allVerts.size() * sizeof (float)), allVerts.data(), GL_STATIC_DRAW);
    glGenBuffers (1, &iboMesh);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboMesh);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (allIdx.size() * sizeof (std::uint16_t)), allIdx.data(), GL_STATIC_DRAW);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);

    const auto offset = [] (int bytes) { return reinterpret_cast<const void*> ((std::uintptr_t) bytes); };
    for (auto& L : lists)
    {
        const float none[8] {};
        for (auto* b : { &L.inst, &L.seen })
        {
            glGenBuffers (1, b);
            glBindBuffer (GL_ARRAY_BUFFER, *b);
            glBufferData (GL_ARRAY_BUFFER, sizeof (none), none, GL_DYNAMIC_DRAW);
        }
        const auto instAttrs = [&] (std::uint32_t buffer)
        {
            glBindBuffer (GL_ARRAY_BUFFER, buffer);
            glEnableVertexAttribArray (locI0); glVertexAttribPointer (locI0, 4, GL_FLOAT, GL_FALSE, 32, offset (0)); glVertexAttribDivisor (locI0, 1);
            glEnableVertexAttribArray (locI1); glVertexAttribPointer (locI1, 4, GL_FLOAT, GL_FALSE, 32, offset (16)); glVertexAttribDivisor (locI1, 1);
        };
        glGenVertexArrays (1, &L.vaoMesh);
        glBindVertexArray (L.vaoMesh);
        glBindBuffer (GL_ARRAY_BUFFER, vboMesh);
        glEnableVertexAttribArray (locPos);  glVertexAttribPointer (locPos, 3, GL_FLOAT, GL_FALSE, 36, offset (0));
        glEnableVertexAttribArray (locNrm);  glVertexAttribPointer (locNrm, 3, GL_FLOAT, GL_FALSE, 36, offset (12));
        glEnableVertexAttribArray (locUv);   glVertexAttribPointer (locUv, 2, GL_FLOAT, GL_FALSE, 36, offset (24));
        glEnableVertexAttribArray (locPart); glVertexAttribPointer (locPart, 1, GL_FLOAT, GL_FALSE, 36, offset (32));
        instAttrs (L.seen);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboMesh);
        glGenVertexArrays (1, &L.vaoHalo);
        glBindVertexArray (L.vaoHalo);
        instAttrs (L.seen);
        glGenVertexArrays (1, &L.vaoSplat);
        glBindVertexArray (L.vaoSplat);
        instAttrs (L.inst);
        glBindVertexArray (0);
    }
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    vaoEmpty = gpu::attributeless (vboEmpty);

    // The legs: four lathe pieces in one buffer pair (the bones unit-long along y, the radii in metres), the
    // renderer's profiles, the joint's a sphere's.
    std::vector<std::array<double, 2>> sphere;
    for (int i = 1; i < 12; ++i) { const double a = -W::pi / 2 + W::pi * i / 12; sphere.push_back ({ 0.56 * Js::cos (a), 0.56 * Js::sin (a) }); }
    const std::pair<Piece*, W::Mesh> pieces[] {
        { &femur, W::lathe ({ { 0.30, 0.0 }, { 0.44, 0.06 }, { 0.42, 0.45 }, { 0.36, 0.92 }, { 0.28, 1.0 } }, 28) },
        { &tibia, W::lathe ({ { 0.30, 0.0 }, { 0.31, 0.05 }, { 0.25, 0.45 }, { 0.19, 0.93 }, { 0.15, 1.0 } }, 28) },
        { &joint, W::lathe (sphere, 28) },
        { &foot,  W::lathe ({ { 0.60, 0.0 }, { 0.95, 0.05 }, { 0.95, 0.20 }, { 0.70, 0.34 }, { 0.38, 0.46 }, { 0.30, 0.58 } }, 28) },
    };
    std::vector<float> legVerts;
    std::vector<std::uint16_t> legIdx;
    int lbase = 0;
    for (const auto& [piece, m] : pieces)
    {
        *piece = { (int) legIdx.size(), (int) m.idx.size() };
        legVerts.insert (legVerts.end(), m.verts.begin(), m.verts.end());
        for (auto i : m.idx) legIdx.push_back ((std::uint16_t) (i + lbase));
        lbase += (int) (m.verts.size() / 8);
    }
    glGenVertexArrays (1, &vaoLeg);
    glBindVertexArray (vaoLeg);
    glGenBuffers (1, &vboLeg);
    glBindBuffer (GL_ARRAY_BUFFER, vboLeg);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (legVerts.size() * sizeof (float)), legVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray (locPos); glVertexAttribPointer (locPos, 3, GL_FLOAT, GL_FALSE, 32, offset (0));
    glEnableVertexAttribArray (locNrm); glVertexAttribPointer (locNrm, 3, GL_FLOAT, GL_FALSE, 32, offset (12));
    glEnableVertexAttribArray (locUv);  glVertexAttribPointer (locUv, 2, GL_FLOAT, GL_FALSE, 32, offset (24));
    glGenBuffers (1, &iboLeg);
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, iboLeg);
    glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (legIdx.size() * sizeof (std::uint16_t)), legIdx.data(), GL_STATIC_DRAW);
    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

bool WalkerGl::ensureFrame (int w, int h)
{
    if (fbo != 0 && w == fbW && h == fbH)
        return true;
    if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenRenderbuffers (1, &fboColour); glGenRenderbuffers (1, &fboDepth); }
    glBindRenderbuffer (GL_RENDERBUFFER, fboColour); glRenderbufferStorage (GL_RENDERBUFFER, GL_RGBA8, w, h);
    glBindRenderbuffer (GL_RENDERBUFFER, fboDepth);  glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer (GL_RENDERBUFFER, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fboColour);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
    if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        fbW = fbH = 0;
        return false;
    }
    fbW = w; fbH = h;
    return true;
}

void WalkerGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void WalkerGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (sky.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, roundToInt ((float) w * scale)), rh = std::max (2, roundToInt ((float) h * scale));
    if (! ensureFrame (rw, rh))
        return;
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    // The rung: what is drawn less of below the top, eased by the time since the last frame. Then the frame's
    // camera, the instance windows where the walker is, and of them what the view can see — uploaded once, before
    // the timer starts, whatever the repeats.
    const double now = gpu::milliseconds();
    quality.follow (scale, lastFrameMs < 0 ? 0.0 : (now - lastFrameMs) * 0.001);
    lastFrameMs = now;
    const Camera cam = cameraFor (rw, rh);
    updateWindows();
    cullLists (cam.VP);
    thinSplats();

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
        drawScene (rw, rh, cam);

    if (queriesWork)
        gpu::endTimer();
    else if (probe)
    {
        glFinish();
        noteCost ((float) (gpu::milliseconds() - probeStart));
    }

    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer (0, 0, rw, rh, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer (GL_FRAMEBUFFER, target);
}

bool WalkerGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (fbo == 0 || fbW == 0)
        return false;
    rgba.resize ((size_t) fbW * (size_t) fbH * 4);
    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    w = fbW; h = fbH;
    return true;
}

//==============================================================================
void WalkerGl::updateWindows()
{
    // The instance lists follow the walker: a class's window is remade when its base cell moves.
    const auto& s = world.state();
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    std::array<std::vector<float>, W::variants> per;
    for (int k = 0; k < W::numClasses; ++k)
    {
        const auto& C = W::classes[(size_t) k];
        const double shift = C.w * C.cell * 0.3;
        const int bx = (int) std::floor ((s.x + fx * shift) / C.cell) - C.w / 2, bz = (int) std::floor ((s.z + fz * shift) / C.cell) - C.w / 2;
        auto& B = windowBase[(size_t) k];
        if (B.set && bx == B.bx && bz == B.bz)
            continue;
        B = { true, bx, bz };
        world.instancesOf (k, bx, bz, per);
        for (int v = 0; v < W::variants; ++v)
        {
            auto& L = lists[(size_t) (k * W::variants + v)];
            const auto& data = per[(size_t) v];
            L.count = L.splatCount = (int) (data.size() / 8);
            L.splatThin = 0;
            L.data = data;
            L.ground.resize ((size_t) L.count * 2);
            for (int i = 0; i < L.count; ++i)
            {
                const auto span = world.groundSpan (data[(size_t) i * 8], data[(size_t) i * 8 + 1]);
                L.ground[(size_t) i * 2] = (float) span[0]; L.ground[(size_t) i * 2 + 1] = (float) span[1];
            }
            upload (L.inst, data);
        }
    }
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void WalkerGl::cullLists (const std::array<float, 16>& VP)
{
    // The view's six planes from VP's rows, in the scene's frame (xz from the eye, y absolute): a mushroom whose
    // ball (WalkerWorld::ballOf) lies wholly outside one of them puts no pixel in the frame, mesh or halo. Those
    // that are left keep their order, so what is drawn is drawn as before.
    const auto& s = world.state();
    std::array<std::array<double, 4>, 6> planes {};
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < 4; ++c)
        {
            planes[(size_t) (2 * k)][(size_t) c] = (double) VP[(size_t) (c * 4 + 3)] + VP[(size_t) (c * 4 + k)];
            planes[(size_t) (2 * k + 1)][(size_t) c] = (double) VP[(size_t) (c * 4 + 3)] - VP[(size_t) (c * 4 + k)];
        }
    std::array<double, 6> norm {};
    for (size_t i = 0; i < planes.size(); ++i)
        norm[i] = std::sqrt (planes[i][0] * planes[i][0] + planes[i][1] * planes[i][1] + planes[i][2] * planes[i][2]);
    const double eyeX = (float) s.eyeX, eyeZ = (float) s.eyeZ;
    const double breath = (float) s.breath, storm = (float) s.storm;
    // Below the top rung half the knee-high caps wither (WalkerWorld::Quality): their size and their glow
    // eased toward nothing, then left out.
    const float keep = (float) (1 - quality.thin);
    for (size_t l = 0; l < lists.size(); ++l)
    {
        auto& L = lists[l];
        const bool thinning = l < (size_t) W::variants && quality.thin > 0;
        seenData.clear();
        for (int i = 0; i < L.count; ++i)
        {
            const float* in = L.data.data() + (size_t) i * 8;
            const bool withers = thinning && W::Quality::thinned (in[5]);
            if (withers && keep <= 0.0f)
                continue;
            const auto ball = W::ballOf (L.shape, in, L.ground[(size_t) i * 2], L.ground[(size_t) i * 2 + 1], breath, storm);
            const double x = ball.x - eyeX, z = ball.z - eyeZ;
            bool seen = true;
            for (size_t p = 0; p < planes.size() && seen; ++p)
                seen = planes[p][0] * x + planes[p][1] * ball.y + planes[p][2] * z + planes[p][3] >= -ball.r * norm[p];
            if (seen)
                putInstance (seenData, in, withers, keep);
        }
        L.seenCount = upload (L.seen, seenData);
    }
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void WalkerGl::thinSplats()
{
    // The knee-high caps' splats as cullLists leaves them: withering, then gone; the whole window again once
    // the view climbs back to the top. Remade only when the thinning moves or the window does.
    for (int v = 0; v < W::variants; ++v)
    {
        auto& L = lists[(size_t) v];
        if (std::abs (L.splatThin - quality.thin) < 1.0e-9)
            continue;
        L.splatThin = quality.thin;
        const float keep = (float) (1 - quality.thin);
        seenData.clear();
        for (int i = 0; i < L.count; ++i)
        {
            const float* in = L.data.data() + (size_t) i * 8;
            const bool withers = quality.thin > 0 && W::Quality::thinned (in[5]);
            if (! withers || keep > 0.0f)
                putInstance (seenData, in, withers, keep);
        }
        L.splatCount = upload (L.inst, seenData);
    }
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

WalkerGl::Camera WalkerGl::cameraFor (int rw, int rh) const
{
    // The field from the frame's aspect, the horizon sheared to where it is asked, the gait's heave, pitch, nod
    // and roll.
    const auto& s = world.state();
    Camera c;
    const double aspect = (double) rw / rh;
    const auto field = fieldFor (aspect);
    c.th = field[0]; c.tv = field[1]; c.shear = 1 - 2 * horizon;
    auto& P = c.P;
    P[0] = (float) (1 / c.th); P[5] = (float) (1 / c.tv); P[10] = (float) (-(zFar + zNear) / (zFar - zNear)); P[11] = -1;
    P[14] = (float) (-2 * zFar * zNear / (zFar - zNear));
    P[9] = (float) -c.shear;
    const Basis basis = cameraBasis (s.heading, s.pitch + s.nod, s.roll);
    c.f = basis.f; c.r = basis.r; c.u = basis.u;
    auto& V = c.V;
    {
        const double cy = s.camY;
        const auto& r = basis.r; const auto& u = basis.u; const auto& f = basis.f;
        V[0] = (float) r[0]; V[4] = (float) r[1]; V[8] = (float) r[2];     V[12] = (float) -(r[1] * cy);
        V[1] = (float) u[0]; V[5] = (float) u[1]; V[9] = (float) u[2];     V[13] = (float) -(u[1] * cy);
        V[2] = (float) -f[0]; V[6] = (float) -f[1]; V[10] = (float) -f[2]; V[14] = (float) (f[1] * cy);
        V[15] = 1;
    }
    c.VP = multiply (P, V);
    return c;
}

void WalkerGl::drawScene (int rw, int rh, const Camera& cam)
{
    const auto& s = world.state();
    const double storm = s.storm;
    const V3 skyZen = mix3 ({ 0.040, 0.018, 0.095 }, { 0.05, 0.03, 0.09 }, storm);
    const V3 skyHor = mix3 ({ 0.17, 0.065, 0.15 }, { 0.15, 0.08, 0.15 }, storm);
    const V3 skyHorSun = mix3 ({ 0.50, 0.14, 0.08 }, { 0.36, 0.12, 0.09 }, storm);
    const V3 sunGlow = mix3 ({ 0.90, 0.27, 0.09 }, { 0.55, 0.20, 0.09 }, storm);
    const V3 fogCol = mix3 ({ 0.080, 0.038, 0.092 }, { 0.10, 0.07, 0.13 }, storm);
    const V3 sunCol = mix3 ({ 0.55, 0.18, 0.07 }, { 0.30, 0.11, 0.06 }, storm);
    const double fogK = (1.0 / 125) * (1 + 1.4 * storm);
    const double sunInt = 1.15 - 0.5 * storm;
    const double tNow = std::fmod (s.time, 100000.0), tBase = s.time - tNow;   // the shaders' clock, kept small
    const V3 sunDir { Js::sin (W::sunAzimuth) * Js::cos (W::sunElevation), Js::sin (W::sunElevation), -Js::cos (W::sunAzimuth) * Js::cos (W::sunElevation) };
    std::array<float, W::maxPrints * 4> prints {};
    for (int i = 0; i < W::maxPrints; ++i)
    {
        const auto at = (size_t) (i * 4);
        prints[at] = s.prints[at]; prints[at + 1] = s.prints[at + 1];
        prints[at + 2] = (float) ((double) s.prints[at + 2] - tBase); prints[at + 3] = s.prints[at + 3];
    }

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uHeight"), 0);
        glUniform2f (p.uniform ("uCamXZ"), (float) s.eyeX, (float) s.eyeZ);
        glUniform1f (p.uniform ("uCamY"), (float) s.camY);
        glUniform1f (p.uniform ("uTime"), (float) tNow);
        glUniform1f (p.uniform ("uBreath"), (float) s.breath);
        glUniform1f (p.uniform ("uStorm"), (float) storm);
        glUniform1f (p.uniform ("uHit"), (float) s.hit);
        glUniform1f (p.uniform ("uHitGlow"), (float) s.hitGlow);
    };
    const auto set3 = [] (const Program& p, const char* name, const V3& v) { const auto f = f3 (v); glUniform3fv (p.uniform (name), 1, f.data()); };
    const auto setLight = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uLight"), 2);
        glUniform1f (p.uniform ("uLmSize"), (float) lmSize); glUniform1f (p.uniform ("uLmScale"), (float) lmScale);
        set3 (p, "uSunDir", sunDir); set3 (p, "uSunCol", sunCol);
        set3 (p, "uSkyZen", skyZen); set3 (p, "uSkyHor", skyHor);
        set3 (p, "uFogCol", fogCol); set3 (p, "uSunGlow", sunGlow);
        glUniform1f (p.uniform ("uFogK"), (float) fogK); glUniform1f (p.uniform ("uFogGlow"), (float) (0.07 + 0.06 * storm));
        set3 (p, "uSkyHorSun", skyHorSun);
    };

    const auto& P = cam.P; const auto& V = cam.V; const auto& VP = cam.VP;
    const double th = cam.th, tv = cam.tv, shear = cam.shear;
    const Basis basis { cam.f, cam.r, cam.u };

    // 1. The light map.
    glBindFramebuffer (GL_FRAMEBUFFER, fboLight);
    glViewport (0, 0, lmTex, lmTex);
    glDisable (GL_DEPTH_TEST); glDisable (GL_CULL_FACE);
    glClearColor (0, 0, 0, 1); glClear (GL_COLOR_BUFFER_BIT);
    glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE);
    glUseProgram (splat.id); setCommon (splat);
    glUniform1f (splat.uniform ("uLmSize"), (float) lmSize); glUniform1f (splat.uniform ("uLmScale"), (float) lmScale);
    for (const auto& L : lists)
        if (L.splatCount > 0)
        {
            glBindVertexArray (L.vaoSplat);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 6, L.splatCount);
        }
    glDisable (GL_BLEND);

    // 2. The scene.
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glViewport (0, 0, rw, rh);
    glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDepthMask (GL_TRUE);
    // The colour cleared too though every pixel is drawn (the sky takes what nothing nearer did), so that a GPU
    // that keeps the frame in tiles need not read the last one back first.
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texLight);

    // The ground: at the top rung first, as it always was. Below it, after what stands on it, so that none of it
    // is shaded under a cap or a leg on a GPU that shades in the order it is given (Apple's sort that out
    // themselves, so here it is worth nothing): it takes a pixel only where it is nearer, so the picture is the same
    // but for the ground's texture detail where a quad of pixels is part cap — a level or three in about one pixel
    // of a thousand, which at the top rung would not be the frame it always was.
    const auto drawGround = [&] (GLenum depthFunc)
    {
        glUseProgram (ground.id); setCommon (ground); setLight (ground);
        glUniform1i (ground.uniform ("uDetail"), 1);
        glUniform4fv (ground.uniform ("uPrint[0]"), W::maxPrints, prints.data());
        glUniformMatrix4fv (ground.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
        glBindVertexArray (vaoGround);
        glDepthFunc (depthFunc);
        glDrawElements (GL_TRIANGLES, groundIndices, GL_UNSIGNED_SHORT, nullptr);
        glDepthFunc (GL_LEQUAL);
    };
    const bool groundLast = quality.level >= 1;
    if (! groundLast)
        drawGround (GL_LEQUAL);

    glUseProgram (mush.id); setCommon (mush); setLight (mush);
    glUniform4fv (mush.uniform ("uPrint[0]"), W::maxPrints, prints.data());
    glUniformMatrix4fv (mush.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    for (const auto& L : lists)
        if (L.seenCount > 0)
        {
            glBindVertexArray (L.vaoMesh);
            glDrawElementsInstanced (GL_TRIANGLES, L.indices, GL_UNSIGNED_SHORT, reinterpret_cast<const void*> ((std::uintptr_t) L.first * 2), L.seenCount);
        }

    {
        // The legs: hip to knee (the femur), the knee joint, knee to ankle (the tibia), the foot on the ground.
        glUseProgram (leg.id); setCommon (leg); setLight (leg);
        glUniformMatrix4fv (leg.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
        glBindVertexArray (vaoLeg);
        std::array<float, 16> M {};
        std::array<float, 9> N {};
        const auto piece = [&] (const Piece& r, double ring)
        {
            glUniformMatrix4fv (leg.uniform ("uModel"), 1, GL_FALSE, M.data());
            glUniformMatrix3fv (leg.uniform ("uNormal"), 1, GL_FALSE, N.data());
            glUniform1f (leg.uniform ("uRing"), (float) ring);
            glDrawElements (GL_TRIANGLES, r.count, GL_UNSIGNED_SHORT, reinterpret_cast<const void*> ((std::uintptr_t) r.first * 2));
        };
        const auto translate = [&] (const V3& t) { M = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float) t[0], (float) t[1], (float) t[2], 1 }; };
        for (const auto& L : s.legs)
        {
            const V3 footAt { L.pos[0] - s.eyeX, L.pos[1], L.pos[2] - s.eyeZ }, ankle { footAt[0], footAt[1] + 0.5, footAt[2] };
            boneMatrix (L.hip, L.knee, M, N); piece (femur, 0.86);
            boneMatrix (L.knee, ankle, M, N); piece (tibia, 0.12);
            translate (L.knee); N = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; piece (joint, -1);
            translate (footAt); piece (foot, 0.72);
        }
    }

    if (groundLast)
        drawGround (GL_LESS);

    glUseProgram (sky.id); setCommon (sky); setLight (sky);
    set3 (sky, "uCamFwd", basis.f); set3 (sky, "uCamRight", basis.r); set3 (sky, "uCamUp", basis.u);
    glUniform2f (sky.uniform ("uTanHalf"), (float) th, (float) tv); glUniform1f (sky.uniform ("uShear"), (float) shear);
    glUniform1f (sky.uniform ("uSunInt"), (float) sunInt);
    glBindVertexArray (vaoEmpty);
    glDrawArrays (GL_TRIANGLES, 0, 3);

    // 3. The glow: halos over the caps, added over everything.
    glEnable (GL_BLEND); glBlendFunc (GL_ONE, GL_ONE); glDepthMask (GL_FALSE);
    glUseProgram (halo.id); setCommon (halo);
    glUniformMatrix4fv (halo.uniform ("uView"), 1, GL_FALSE, V.data()); glUniformMatrix4fv (halo.uniform ("uProj"), 1, GL_FALSE, P.data());
    glUniform1f (halo.uniform ("uFogK"), (float) fogK);
    for (const auto& L : lists)
        if (L.seenCount > 0)
        {
            glBindVertexArray (L.vaoHalo);
            glDrawArraysInstanced (GL_TRIANGLES, 0, 6, L.seenCount);
        }
    {
        // The bursts: a footfall's puff of spores, an attack's sparks from the caps.
        std::array<float, W::maxBursts * 4> b0 {}, b1 {};
        for (int i = 0; i < W::maxBursts; ++i)
        {
            const auto at = (size_t) (i * 4), from = (size_t) (i * 8);
            b0[at] = s.bursts[from]; b0[at + 1] = s.bursts[from + 1]; b0[at + 2] = s.bursts[from + 2];
            b0[at + 3] = (float) ((double) s.bursts[from + 3] - tBase);
            b1[at] = s.bursts[from + 4]; b1[at + 1] = s.bursts[from + 5]; b1[at + 2] = s.bursts[from + 6]; b1[at + 3] = s.bursts[from + 7];
        }
        glUseProgram (burst.id); setCommon (burst);
        glUniformMatrix4fv (burst.uniform ("uView"), 1, GL_FALSE, V.data()); glUniformMatrix4fv (burst.uniform ("uProj"), 1, GL_FALSE, P.data());
        glUniform4fv (burst.uniform ("uB0[0]"), W::maxBursts, b0.data()); glUniform4fv (burst.uniform ("uB1[0]"), W::maxBursts, b1.data());
        glUniform1i (burst.uniform ("uPer"), W::burstParticles);
        glBindVertexArray (vaoEmpty);
        // Only the bursts alive: burst.vert's test, a little wider, on the same single floats — one not begun,
        // spent or of no strength puts every vertex of its particles behind the far plane. A run of them in
        // one draw, its vertices counted from the run's first by uFirst rather than by the draw's own first (which
        // not every WebGL has added to gl_VertexID), so each vertex is the one it was.
        const float now = (float) tNow;
        const auto alive = [&] (int b)
        {
            const auto at = (size_t) (b * 4);
            const float age = now - b0[at + 3], life = b1[at] > 0.5f ? 1.6f : 3.2f;
            return b1[at + 1] > 0.0f && age >= -1.0e-3f && age <= life + 1.0e-3f;
        };
        constexpr int perBurst = W::burstParticles * 6;
        for (int b = 0; b < W::maxBursts;)
        {
            if (! alive (b)) { ++b; continue; }
            int e = b + 1;
            while (e < W::maxBursts && alive (e)) ++e;
            glUniform1i (burst.uniform ("uFirst"), b * perBurst);
            glDrawArrays (GL_TRIANGLES, 0, (e - b) * perBurst);
            b = e;
        }
    }
    glDisable (GL_DEPTH_TEST);
    if (s.spores > 0.005)
    {
        // The drifting spores, in a box ahead of the glass.
        glUseProgram (spore.id); setCommon (spore);
        glUniformMatrix4fv (spore.uniform ("uProj"), 1, GL_FALSE, P.data());
        glUniform2f (spore.uniform ("uResolution"), (float) rw, (float) rh); glUniform2f (spore.uniform ("uTanHalf"), (float) th, (float) tv);
        glUniform1f (spore.uniform ("uTravel"), (float) std::fmod (s.travel, 100000.0)); glUniform1f (spore.uniform ("uSpeed"), (float) s.speed);
        // Below the top rung a share of the cloud: the last of them fade as they fade in.
        const float spores = quality.spare > 0 ? (float) (s.spores * quality.sporeShare()) : (float) s.spores;
        glUniform1f (spore.uniform ("uSpores"), spores); glUniform1f (spore.uniform ("uCount"), (float) sporeCount);
        glBindVertexArray (vaoEmpty);
        // Only the spores the cloud has: spore.vert fades the i-th in as uSpores * uCount passes i, so one
        // past that adds nothing — however near, however long its streak.
        const int drawn = std::min (sporeCount, (int) std::ceil ((double) spores * sporeCount) + 1);
        glDrawArrays (GL_TRIANGLES, 0, drawn * 6);
    }
    glDisable (GL_BLEND); glDepthMask (GL_TRUE); glDisable (GL_DEPTH_TEST);
    glBindVertexArray (0);
    glUseProgram (0);
    glActiveTexture (GL_TEXTURE0);
}

} // namespace orbitamp::redgroove
