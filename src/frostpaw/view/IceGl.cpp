// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IceGl.h"

#include "../../shaders/Shaders.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::frostpaw
{

using namespace orbitamp::gpu;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double cell0 = 1.0;             // metres, the finest level
    constexpr double fwdShift = 0.4;          // the levels pushed ahead of the rover, of their half-extent
    constexpr double vfovHalfTan = 0.23086819;  // tan 13 deg: vfov 26 deg, hfov follows from the aspect
    constexpr float shear = 0.2f;             // the horizon at NDC y = +0.2: 40 % from the top
    constexpr double jupiterRadiusDegrees = 9.0;   // angular radius: 18 degrees across
    constexpr double ioRadiusDegrees = 0.32;
    constexpr double skyClock = 2 * pi * 2000;     // seconds: Jupiter's turn (0.0045 rad/s) and Io's lap (0.011) both whole in it
    constexpr double snowClock = 36.0 * 2000;      // metres: the flakes' 36 m depth cycle, whole in it

    // THE SHADERS are files in resources/shaders/ice (src/shaders/Shaders.h), written without the
    // #version, which glsl() puts in front of them — the app's or the web's (src/shaders/Gpu.h). ice/height.glsl is the height function, shared by the
    // terrain's two shaders and the steel's vertex shader: HTEX in it is textureLod (.., 0) in a vertex
    // shader and texture() in a fragment shader, and glsl() defines it for the stage it builds.
    // ice/grad.glsl is the analytic world-space gradient of terrainH from the gradient textures, the
    // per-pixel normal: uGrad1 = (dR/du, dR/dv, dB/du, dB/dv) and uGrad2 = (dG/du, dG/dv, dA/du, dA/dv)
    // per uv unit, as 0.5 + 0.5 * g / gmax in RGBA8; uGS1 = (gmaxR, gmaxB), uGS2 = (gmaxG, gmaxA). The
    // steel (ice/steel.vert): instanced bar meshes, one draw per variant; aCell is the instance's
    // unwrapped 70 m cell. The sky (ice/sky.frag): stars, Jupiter, Io, the sun, a thin mist band at the
    // horizon. The snow (ice/snow.vert): streak quads in camera space, radiating from the vanishing point.
    enum class Htex { none, vertex, fragment };
    std::string glsl (const char* path, Htex htex = Htex::none)
    {
        std::string s (gpu::header());
        if (htex == Htex::vertex)   s += "#define HTEX(uv) textureLod(uHeight, uv, 0.0)\n";
        if (htex == Htex::fragment) s += "#define HTEX(uv) texture(uHeight, uv)\n";
        return s + shaders::source (path);
    }

    //==========================================================================
    /** THE STEEL'S GEOMETRY: bars are boxes with flat normals. A vertex is position (3), normal (3),
        its direction across the bar (3, for the widening with distance) and a LOD flag (1): ten floats.
        The lumps are the same every time: no generator, only arithmetic. */
    struct V3 { double x, y, z; };
    inline V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3 operator* (V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
    inline V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline V3 norm (V3 a) { const double l = std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z); return l > 0 ? a * (1 / l) : a; }

    struct MeshBuilder
    {
        std::vector<float> v;

        void tri (V3 A, V3 B, V3 C, V3 n, V3 ea, V3 eb, V3 ec, float lod)
        {
            for (const auto& [P, E] : { std::pair { A, ea }, std::pair { B, eb }, std::pair { C, ec } })
                for (double d : { P.x, P.y, P.z, n.x, n.y, n.z, E.x, E.y, E.z, (double) lod })
                    v.push_back ((float) d);
        }

        /** A square-section bar of thickness t along a -> b. */
        void bar (V3 a, V3 b, double t, float lod = 0)
        {
            const V3 u = norm (b - a);
            const V3 ref = std::abs (u.y) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
            const V3 vv = norm (cross (u, ref)), w = cross (u, vv);
            const double h = t / 2, r2 = 0.70710678118654752;
            V3 C[8], E[8];
            int k = 0;
            for (int end = 0; end < 2; ++end)
                for (double sv : { -1.0, 1.0 })
                    for (double sw : { -1.0, 1.0 })
                    {
                        C[k] = (end ? b : a) + vv * (sv * h) + w * (sw * h);
                        E[k] = (vv * sv + w * sw) * r2;
                        ++k;
                    }
            const auto quad = [&] (int i0, int i1, int i2, int i3, V3 n)
            {
                tri (C[i0], C[i1], C[i2], n, E[i0], E[i1], E[i2], lod);
                tri (C[i0], C[i2], C[i3], n, E[i0], E[i2], E[i3], lod);
            };
            quad (2, 3, 7, 6, vv); quad (0, 4, 5, 1, vv * -1.0);     // +v / -v faces
            quad (1, 5, 7, 3, w);  quad (0, 2, 6, 4, w * -1.0);      // +w / -w
            quad (4, 6, 7, 5, u);  quad (0, 1, 3, 2, u * -1.0);      // the ends
        }

        /** A paraboloid shell of radius R and depth, both faces, segs around. */
        void dish (V3 c, V3 axis, double R, double depth, int segs)
        {
            const V3 u = axis;
            const V3 ref = std::abs (u.y) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
            const V3 vv = norm (cross (u, ref)), w = cross (u, vv);
            const auto pt = [&] (double r, double ang) { const double d = depth * (r / R) * (r / R); return c + vv * (r * std::cos (ang)) + w * (r * std::sin (ang)) + u * d; };
            const double rings[] { 0, R * 0.5, R };
            const V3 z { 0, 0, 0 };
            for (int s = 0; s < segs; ++s)
            {
                const double a0 = s / (double) segs * pi * 2, a1 = (s + 1) / (double) segs * pi * 2;
                for (int r = 0; r < 2; ++r)
                {
                    const V3 P0 = pt (rings[r], a0), P1 = pt (rings[r + 1], a0), P2 = pt (rings[r + 1], a1), P3 = pt (rings[r], a1);
                    const V3 n = norm (cross (P1 - P0, P2 - P0));
                    tri (P0, P1, P2, n, z, z, z, 0); if (r > 0) tri (P0, P2, P3, n, z, z, z, 0);
                    const V3 nn = n * -1.0;
                    tri (P0, P2, P1, nn, z, z, z, 0); if (r > 0) tri (P0, P3, P2, nn, z, z, z, 0);
                }
            }
        }

        /** A vertical prism (the tank), segs sides, with its lid. */
        void prism (V3 c, double R, double h, int segs, float lod)
        {
            const V3 z { 0, 0, 0 };
            for (int s = 0; s < segs; ++s)
            {
                const double a0 = s / (double) segs * pi * 2, a1 = (s + 1) / (double) segs * pi * 2;
                const V3 P0 { c.x + R * std::cos (a0), c.y, c.z + R * std::sin (a0) }, P1 { c.x + R * std::cos (a1), c.y, c.z + R * std::sin (a1) };
                const V3 Q0 { P0.x, c.y + h, P0.z }, Q1 { P1.x, c.y + h, P1.z };
                const double am = (a0 + a1) / 2; const V3 n { std::cos (am), 0, std::sin (am) };
                tri (P0, P1, Q1, n, z, z, z, lod); tri (P0, Q1, Q0, n, z, z, z, lod);
                const V3 T { c.x, c.y + h, c.z }; tri (Q0, Q1, T, { 0, 1, 0 }, z, z, z, lod);
            }
        }
    };

    /** The four variants: a 24 m lattice mast, an 11 m gantry, a dish on a tripod, a frame module with a tank. */
    std::array<std::vector<float>, 4> buildSteelMeshes()
    {
        std::array<std::vector<float>, 4> out;
        const std::pair<double, double> corners[] { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        const std::pair<std::pair<double, double>, std::pair<double, double>> faces[] { { { -1, -1 }, { 1, -1 } }, { { 1, -1 }, { 1, 1 } }, { { 1, 1 }, { -1, 1 } }, { { -1, 1 }, { -1, -1 } } };
        {   // 0: the lattice mast — 4 tapering legs, rings and X-braces on every face, a whip and a small dish
            MeshBuilder m; const double H = 24, b0 = 1.6, b1 = 0.55; const int nl = 5;
            const auto at = [&] (double h, double sx, double sz) { const double b = b0 + (b1 - b0) * (h / H); return V3 { sx * b, h, sz * b }; };
            for (const auto& [sx, sz] : corners) m.bar (at (0, sx, sz), at (H, sx, sz), 0.26);
            for (int l = 0; l <= nl; ++l)
            {
                const double h = H * l / nl;
                for (const auto& [p, q] : faces) m.bar (at (h, p.first, p.second), at (h, q.first, q.second), 0.14, (l == 0 || l == nl) ? 0.0f : 1.0f);
                if (l < nl)
                {
                    const double h2 = H * (l + 1) / nl;
                    for (const auto& [p, q] : faces) { m.bar (at (h, p.first, p.second), at (h2, q.first, q.second), 0.10, 1); m.bar (at (h2, p.first, p.second), at (h, q.first, q.second), 0.10, 1); }
                }
            }
            m.bar ({ 0, H, 0 }, { 0, H + 4.5, 0 }, 0.16); m.bar ({ -0.9, H + 2.2, 0 }, { 0.9, H + 2.2, 0 }, 0.10, 1);
            m.dish ({ 0.0, H * 0.72, b0 * 0.9 }, { 0, 0.25, 1 }, 1.3, 0.35, 10);   // the axis as given, a shade over unit: the dish is that much deeper
            out[0] = std::move (m.v);
        }
        {   // 1: the gantry — two A-frames and a trussed beam between them, a hook
            MeshBuilder m; const double W = 11, H = 9;
            for (double x : { -W / 2, W / 2 }) { m.bar ({ x, 0, -2.2 }, { x, H, 0 }, 0.30); m.bar ({ x, 0, 2.2 }, { x, H, 0 }, 0.30); m.bar ({ x, H * 0.45, -1.2 }, { x, H * 0.45, 1.2 }, 0.14, 1); }
            m.bar ({ -W / 2, H, -0.5 }, { W / 2, H, -0.5 }, 0.28); m.bar ({ -W / 2, H, 0.5 }, { W / 2, H, 0.5 }, 0.28);
            m.bar ({ -W / 2, H - 1.3, 0 }, { W / 2, H - 1.3, 0 }, 0.22);
            for (int i = 0; i < 6; ++i)
            {
                const double x0 = -W / 2 + W * i / 6, x1 = -W / 2 + W * (i + 1) / 6;
                m.bar ({ x0, H, (i & 1) ? 0.5 : -0.5 }, { x1, H - 1.3, 0 }, 0.10, 1); m.bar ({ x0, H - 1.3, 0 }, { x1, H, (i & 1) ? -0.5 : 0.5 }, 0.10, 1);
            }
            m.bar ({ 1.5, H - 1.3, 0 }, { 1.5, H - 4.2, 0 }, 0.12, 1); m.bar ({ 0.6, H - 4.2, 0 }, { 2.4, H - 4.2, 0 }, 0.18, 1);
            out[1] = std::move (m.v);
        }
        {   // 2: a dish on a tripod, a feed strut
            MeshBuilder m; const double H = 6;
            for (int k = 0; k < 3; ++k)
            {
                const double a = k / 3.0 * pi * 2, a2 = (k + 1) / 3.0 * pi * 2;
                m.bar ({ 2.2 * std::cos (a), 0, 2.2 * std::sin (a) }, { 0, H, 0 }, 0.22);
                m.bar ({ 1.1 * std::cos (a), H / 2, 1.1 * std::sin (a) }, { 1.1 * std::cos (a2), H / 2, 1.1 * std::sin (a2) }, 0.10, 1);
            }
            m.bar ({ 0, H, 0 }, { 0, H + 1.4, 0 }, 0.22);
            const V3 ax { 0, 0.55, 0.835 }; m.dish ({ 0, H + 1.4, 0 }, ax, 3.2, 0.9, 12);
            m.bar ({ 0, H + 1.4, 0 }, { ax.x * 2.4, H + 1.4 + ax.y * 2.4, ax.z * 2.4 }, 0.10, 1);
            out[2] = std::move (m.v);
        }
        {   // 3: the frame module — an open cube frame on short legs, a tank inside, a boom
            MeshBuilder m; const double S = 4.2, H0 = 1.2, Ht = 4.0;
            const auto c = [&] (double sx, double h, double sz) { return V3 { sx * S / 2, h, sz * S / 2 }; };
            for (const auto& [sx, sz] : corners) m.bar (c (sx, 0, sz), c (sx, H0 + Ht, sz), 0.22);
            for (double h : { H0, H0 + Ht }) for (const auto& [p, q] : faces) m.bar (c (p.first, h, p.second), c (q.first, h, q.second), 0.18);
            m.bar (c (-1, H0, -1), c (1, H0 + Ht, -1), 0.10, 1); m.bar (c (1, H0, 1), c (-1, H0 + Ht, 1), 0.10, 1);
            m.prism ({ 0, H0 + 0.1, 0 }, 1.3, Ht - 0.4, 8, 0);
            m.bar ({ S / 2, H0 + Ht, 0 }, { S / 2 + 2.0, H0 + Ht + 1.5, 0 }, 0.12, 1);
            out[3] = std::move (m.v);
        }
        return out;
    }

    inline double fract (double x) noexcept { return x - std::floor (x); }

    /** To the nearest, a half to the even one, as JUCE's roundToInt does it: the frame's size at a render scale. */
    inline int roundToInt (float x) noexcept { return (int) std::lrint ((double) x); }

    using Mat4 = std::array<float, 16>;   // column-major
    Mat4 multiply (const Mat4& a, const Mat4& b)
    {
        Mat4 o {};
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { float s = 0; for (int k = 0; k < 4; ++k) s += a[(size_t) (k * 4 + r)] * b[(size_t) (c * 4 + k)]; o[(size_t) (c * 4 + r)] = s; }
        return o;
    }
    struct Basis { float r[3], u[3], f[3]; };
    Basis cameraBasis (double heading, double pitch)   // right, up, forward (world); pitch positive = nose up
    {
        const double cp = std::cos (pitch), sp = std::sin (pitch);
        const double f0x = std::sin (heading), f0z = -std::cos (heading);
        Basis b;
        b.r[0] = (float) std::cos (heading); b.r[1] = 0; b.r[2] = (float) std::sin (heading);
        b.f[0] = (float) (f0x * cp); b.f[1] = (float) sp; b.f[2] = (float) (f0z * cp);
        b.u[0] = (float) (-f0x * sp); b.u[1] = (float) cp; b.u[2] = (float) (-f0z * sp);
        return b;
    }

    /** The sky's fixed frame: the sun, Jupiter, and Jupiter's right, up and axis as seen from here. */
    struct SkyFrame
    {
        float sun[3], jup[3], jupR[3], jupU[3], jupAxis[3];
        float jupSin, jupCos, ioSin;
    };
    SkyFrame skyFrame()
    {
        const auto dir = [] (double azDeg, double elDeg) { const double az = azDeg * pi / 180, el = elDeg * pi / 180; return V3 { std::sin (az) * std::cos (el), std::sin (el), -std::cos (az) * std::cos (el) }; };
        const V3 sun = dir (-118, 16);     // low, to the left of heading 0 (azimuth from -z, positive = right)
        const V3 jup = dir (27, 4.0);      // huge, low, right of heading 0 — a fixed world direction
        const V3 R = norm (cross ({ 0, 1, 0 }, jup)), U = norm (cross (jup, R));
        const V3 axis = norm (U * 0.96 + R * 0.12 - jup * 0.25);   // tilted a little toward us
        SkyFrame f;
        const auto put = [] (float* o, V3 a) { o[0] = (float) a.x; o[1] = (float) a.y; o[2] = (float) a.z; };
        put (f.sun, sun); put (f.jup, jup); put (f.jupR, R); put (f.jupU, U); put (f.jupAxis, axis);
        f.jupSin = (float) std::sin (jupiterRadiusDegrees * pi / 180); f.jupCos = (float) std::cos (jupiterRadiusDegrees * pi / 180);
        f.ioSin = (float) std::sin (ioRadiusDegrees * pi / 180);
        return f;
    }
    /** Io circles Jupiter slowly, out to ~2.3 Jupiter radii from the disc's edge. */
    void ioDirection (const SkyFrame& f, double time, float* out)
    {
        const double a = time * 0.011 + 2.2, R = f.jupSin * 3.3;
        const V3 J { f.jup[0], f.jup[1], f.jup[2] }, JR { f.jupR[0], f.jupR[1], f.jupR[2] }, JU { f.jupU[0], f.jupU[1], f.jupU[2] };
        const V3 io = norm (J + JR * (std::cos (a) * R) + JU * (std::sin (a) * R * 0.2));
        out[0] = (float) io.x; out[1] = (float) io.y; out[2] = (float) io.z;
    }
}

//==============================================================================
int IceGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool IceGl::compile (Program& p, const std::string& vs, const std::string& fs, const char* what)
{
    std::string log;
    p.id = gpu::program (vs, fs, log);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("frostpaw: " + trouble);
        return false;
    }
    return true;
}

bool IceGl::create()
{
    trouble.clear();
    if (! gpu::current())
        return false;
    world.build();

    if (! compile (terrain, glsl ("ice/terrain.vert", Htex::vertex), glsl ("ice/terrain.frag", Htex::fragment), "terrain")
        || ! compile (steel, glsl ("ice/steel.vert", Htex::vertex), glsl ("ice/steel.frag"), "steel")
        || ! compile (sky, glsl ("common/sky.vert"), glsl ("ice/sky.frag"), "sky")
        || ! compile (snow, glsl ("ice/snow.vert"), glsl ("ice/snow.frag"), "snow"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeGeometry();

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

void IceGl::destroy()
{
    for (auto& l : level) { if (l.indexBuffer != 0) glDeleteBuffers (1, &l.indexBuffer); l = {}; }
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (vboGrid, glDeleteBuffers); del (vboSteel, glDeleteBuffers);
    for (auto& b : instanceBuffer) del (b, glDeleteBuffers);
    instanceCount.fill (0); instanceBx = instanceBz = 0x7fffffff;
    del (vaoTerrain, glDeleteVertexArrays); del (vaoSteel, glDeleteVertexArrays); del (vaoEmpty, glDeleteVertexArrays); del (vboEmpty, glDeleteBuffers);
    del (texHeight, glDeleteTextures); del (texGrad1, glDeleteTextures); del (texGrad2, glDeleteTextures); del (texDetail, glDeleteTextures);
    del (fboColour, glDeleteTextures); del (fboDepth, glDeleteRenderbuffers); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &terrain, &steel, &sky, &snow }) { if (p->id != 0) glDeleteProgram (p->id); p->id = 0; }
}

void IceGl::makeTextures()
{
    const int n = IceWorld::texSize;
    const GLenum heightFormat = gpu::filteredFloatFormat();
    glGenTextures (1, &texHeight);
    glBindTexture (GL_TEXTURE_2D, texHeight);
    {
        int size = n, lvl = 0;
        for (const auto& mip : world.heightMips())
        {
            glTexImage2D (GL_TEXTURE_2D, lvl++, (GLint) heightFormat, size, size, 0, GL_RGBA, GL_FLOAT, mip.data());
            size >>= 1;
        }
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lvl - 1);
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const auto rgba8 = [] (std::uint32_t& id, int size, const std::vector<std::uint8_t>& px)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    rgba8 (texGrad1, n, world.gradient1());
    rgba8 (texGrad2, n, world.gradient2());
    rgba8 (texDetail, IceWorld::detailSize, world.detail());
    glBindTexture (GL_TEXTURE_2D, 0);
}

void IceGl::makeGeometry()
{
    // The grid: (N+1)^2 vertices of (i, j); one full index buffer for level 0, a ring buffer per other level.
    glGenVertexArrays (1, &vaoTerrain);
    glBindVertexArray (vaoTerrain);
    {
        std::vector<float> g ((size_t) (N + 1) * (N + 1) * 2);
        for (int j = 0; j <= N; ++j) for (int i = 0; i <= N; ++i) { g[(size_t) ((j * (N + 1) + i) * 2)] = (float) i; g[(size_t) ((j * (N + 1) + i) * 2 + 1)] = (float) j; }
        glGenBuffers (1, &vboGrid);
        glBindBuffer (GL_ARRAY_BUFFER, vboGrid);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (g.size() * sizeof (float)), g.data(), GL_STATIC_DRAW);
        const auto loc = (GLuint) glGetAttribLocation (terrain.id, "aGrid");
        glEnableVertexAttribArray (loc);
        glVertexAttribPointer (loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    fullIndices.resize ((size_t) N * N * 6);   // per cell (j-major): two triangles, the diagonal (i,j)-(i+1,j+1)
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
        {
            const size_t o = (size_t) (j * N + i) * 6; const std::uint16_t v = (std::uint16_t) (j * (N + 1) + i);
            fullIndices[o] = v; fullIndices[o + 1] = (std::uint16_t) (v + 1); fullIndices[o + 2] = (std::uint16_t) (v + N + 2);
            fullIndices[o + 3] = v; fullIndices[o + 4] = (std::uint16_t) (v + N + 2); fullIndices[o + 5] = (std::uint16_t) (v + N + 1);
        }
    ringIndices.resize (fullIndices.size());
    for (int k = 0; k < levels; ++k)
    {
        auto& L = level[(size_t) k];
        glGenBuffers (1, &L.indexBuffer);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (fullIndices.size() * sizeof (std::uint16_t)), fullIndices.data(), k == 0 ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
        L.count = (int) fullIndices.size();
        L.cell = cell0 * std::pow (2.0, k);
        L.holeX = L.holeY = -1;
    }
    glBindVertexArray (0);

    // The steel: the four meshes in one buffer, the instance cells in a small dynamic buffer per variant.
    glGenVertexArrays (1, &vaoSteel);
    glBindVertexArray (vaoSteel);
    {
        auto meshes = buildSteelMeshes();
        std::vector<float> all;
        for (int v = 0; v < IceWorld::numVariants; ++v)
        {
            steelRange[(size_t) v] = { (int) (all.size() / 10), (int) (meshes[(size_t) v].size() / 10) };
            all.insert (all.end(), meshes[(size_t) v].begin(), meshes[(size_t) v].end());
        }
        glGenBuffers (1, &vboSteel);
        glBindBuffer (GL_ARRAY_BUFFER, vboSteel);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (all.size() * sizeof (float)), all.data(), GL_STATIC_DRAW);
        const auto lp = (GLuint) glGetAttribLocation (steel.id, "aPos"), ln = (GLuint) glGetAttribLocation (steel.id, "aNrm");
        const auto le = (GLuint) glGetAttribLocation (steel.id, "aExp"), ll = (GLuint) glGetAttribLocation (steel.id, "aLod");
        glEnableVertexAttribArray (lp); glVertexAttribPointer (lp, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
        glEnableVertexAttribArray (ln); glVertexAttribPointer (ln, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 12);
        glEnableVertexAttribArray (le); glVertexAttribPointer (le, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 24);
        glEnableVertexAttribArray (ll); glVertexAttribPointer (ll, 1, GL_FLOAT, GL_FALSE, 40, (const void*) 36);
        locCell = glGetAttribLocation (steel.id, "aCell");
        instancing = gpu::loaded (glVertexAttribDivisor) && locCell >= 0;
        for (auto& b : instanceBuffer)
        {
            glGenBuffers (1, &b);
            glBindBuffer (GL_ARRAY_BUFFER, b);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (IceWorld::structWindow * IceWorld::structWindow * 2 * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
        }
        instanceCount.fill (0); instanceBx = instanceBz = 0x7fffffff;
    }
    glBindVertexArray (0);
    vaoEmpty = gpu::attributeless (vboEmpty);   // the sky and the snow draw with no attributes (gpu::attributeless)
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void IceGl::rebuildRing (Level& L, int hx, int hy)   // leaves out the N/2 x N/2 cells at [hx, hx + N/2) x [hy, hy + N/2)
{
    size_t n = 0; const int H = N / 2;
    const auto put = [&] (size_t from, size_t to) { std::copy (fullIndices.begin() + (long) from, fullIndices.begin() + (long) to, ringIndices.begin() + (long) n); n += to - from; };
    for (int j = 0; j < N; ++j)
    {
        if (j < hy || j >= hy + H) put ((size_t) j * N * 6, (size_t) (j + 1) * N * 6);
        else { put ((size_t) j * N * 6, (size_t) (j * N + hx) * 6); put ((size_t) (j * N + hx + H) * 6, (size_t) (j + 1) * N * 6); }
    }
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
    glBufferSubData (GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr) (n * sizeof (std::uint16_t)), ringIndices.data());
    L.count = (int) n; L.holeX = hx; L.holeY = hy;
}

void IceGl::ensureFrame (int w, int h)
{
    if (fbo != 0 && w == fbW && h == fbH)
        return;
    if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); glGenRenderbuffers (1, &fboDepth); }
    glBindTexture (GL_TEXTURE_2D, fboColour);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindRenderbuffer (GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer (GL_RENDERBUFFER, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
    fbW = w; fbH = h;
}

void IceGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void IceGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (terrain.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, roundToInt ((float) w * scale)), rh = std::max (2, roundToInt ((float) h * scale));
    ensureFrame (rw, rh);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);

    // The timer: queries, read back a few frames late so nothing waits — or, without them, one frame
    // in sixty finished and clocked. On the web, neither (gpu::timerQueries).
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
        drawScene (rw, rh);

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

bool IceGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (fbo == 0)
        return false;
    rgba.resize ((size_t) fbW * (size_t) fbH * 4);
    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    w = fbW; h = fbH;
    return true;
}

void IceGl::drawScene (int rw, int rh)
{
    const auto& s = world.state();
    // The snow: the world eases it, graded by the gear, TURBO's blizzard at 1. The air thickens and
    // whitens with it, the sun dims, the mist band grows.
    const float snowAmount = s.snow;
    const auto mix3 = [] (std::array<float, 3> a, std::array<float, 3> b, float t) { return std::array<float, 3> { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; };
    const auto horizon = mix3 ({ 0.74f, 0.80f, 0.90f }, { 0.84f, 0.87f, 0.92f }, snowAmount);     // what distance fades to: the cold haze
    const auto mist    = mix3 ({ 0.52f, 0.61f, 0.76f }, { 0.70f, 0.76f, 0.86f }, snowAmount);     // the thin band on the horizon
    const float hazeK = (1.0f / 3200.0f) * (1 + 6.0f * snowAmount);
    const float sunInt = 2.0f - 0.8f * snowAmount;
    static const SkyFrame skyF = skyFrame();
    const double skyTime = std::fmod (s.time, skyClock);
    float io[3]; ioDirection (skyF, skyTime, io);

    // The frame of reference: the integer cell at the rover; every shader position is relative to it.
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const float camFrac[2] { (float) (s.x - ccx), (float) (s.z - ccz) };
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);

    // The projection: off-centre, the horizon at NDC y = +shear; near 0.5 m, far 40 km.
    const double aspect = (double) rw / rh;
    const double tv = vfovHalfTan, th = tv * aspect, zn = 0.5, zf = 40000.0;
    Mat4 P {};
    P[0] = (float) (1 / th); P[5] = (float) (1 / tv); P[10] = (float) (-(zf + zn) / (zf - zn)); P[11] = -1; P[14] = (float) (-2 * zf * zn / (zf - zn));
    P[9] = -shear;
    const Basis basis = cameraBasis (s.heading, s.pitch);
    Mat4 V {};
    {
        const float cx = camFrac[0], cy = (float) s.camY, cz = camFrac[1];
        const auto* r = basis.r; const auto* u = basis.u; const auto* f = basis.f;
        V[0] = r[0]; V[4] = r[1]; V[8] = r[2];    V[12] = -(r[0] * cx + r[1] * cy + r[2] * cz);
        V[1] = u[0]; V[5] = u[1]; V[9] = u[2];    V[13] = -(u[0] * cx + u[1] * cy + u[2] * cz);
        V[2] = -f[0]; V[6] = -f[1]; V[10] = -f[2]; V[14] = (f[0] * cx + f[1] * cy + f[2] * cz);
        V[15] = 1;
    }
    const Mat4 VP = multiply (P, V);
    const float tex0[2] { (float) fract (ccx / 7000), (float) fract (ccz / 7000) };
    const float tex1[2] { (float) (fract (ccx / 1250) + 0.37), (float) (fract (ccz / 1250) + 0.61) };
    const float tex2[2] { (float) (fract (ccx / 190) + 0.71), (float) (fract (ccz / 190) + 0.13) };
    const float tex3[2] { (float) (fract (ccx / 50) + 0.23), (float) (fract (ccz / 50) + 0.87) };
    const float det0[2] { (float) fract (ccx / 3.1), (float) fract (ccz / 3.1) };
    const float det1[2] { (float) fract (ccx / 0.45), (float) fract (ccz / 0.45) };
    const float det2[2] { (float) fract (ccx / 0.13), (float) fract (ccz / 0.13) };
    // The lineae's coordinates of the cell, wrapped to their period and wavelength, and the line's index.
    struct LinOff { float s, t; int base; };
    const auto linOff = [&] (double dx, double dz, double px, double pz, double period, double wavelength)
    {
        const double sv = ccx * dx + ccz * dz, tvv = ccx * px + ccz * pz;
        return LinOff { (float) (sv - std::floor (sv / period) * period), (float) (tvv - std::floor (tvv / wavelength) * wavelength), (int) std::floor (sv / period) };
    };
    const LinOff linA = linOff (0.904752, 0.425939, -0.425939, 0.904752, 1100.0, 1600.0);
    const LinOff linB = linOff (-0.370181, 0.928960, -0.928960, -0.370181, 1700.0, 2300.0);
    const float chaosOff[2] { (float) fract (ccx / IceWorld::chaosCell), (float) fract (ccz / IceWorld::chaosCell) };
    const int chaosBase[2] { (int) std::floor (ccx / IceWorld::chaosCell), (int) std::floor (ccz / IceWorld::chaosCell) };
    const float structOff[2] { (float) fract (ccx / IceWorld::structCell), (float) fract (ccz / IceWorld::structCell) };
    const int structBase[2] { (int) std::floor (ccx / IceWorld::structCell), (int) std::floor (ccz / IceWorld::structCell) };

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uHeight"), 0);
        glUniform2f (p.uniform ("uCamCell"), (float) ccx, (float) ccz);
        glUniform2f (p.uniform ("uCamFrac"), camFrac[0], camFrac[1]);
        glUniform2f (p.uniform ("uTexOff0"), tex0[0], tex0[1]);
        glUniform2f (p.uniform ("uTexOff1"), tex1[0], tex1[1]);
        glUniform2f (p.uniform ("uTexOff2"), tex2[0], tex2[1]);
        glUniform2f (p.uniform ("uTexOff3"), tex3[0], tex3[1]);
        glUniform4f (p.uniform ("uLinOff"), linA.s, linA.t, linB.s, linB.t);
        glUniform2i (p.uniform ("uLinBase"), linA.base, linB.base);
        glUniform2f (p.uniform ("uChaosOff"), chaosOff[0], chaosOff[1]); glUniform2i (p.uniform ("uChaosBase"), chaosBase[0], chaosBase[1]);
        glUniform2f (p.uniform ("uStructOff"), structOff[0], structOff[1]); glUniform2i (p.uniform ("uStructBase"), structBase[0], structBase[1]);
    };
    const auto setLight = [&] (const Program& p)
    {
        glUniform3fv (p.uniform ("uSun"), 1, skyF.sun);
        glUniform3fv (p.uniform ("uJup"), 1, skyF.jup);
        glUniform3fv (p.uniform ("uHorizon"), 1, horizon.data());
        glUniform3fv (p.uniform ("uMist"), 1, mist.data());
        glUniform1f (p.uniform ("uHazeK"), hazeK);
        glUniform1f (p.uniform ("uSnow"), snowAmount);
        glUniform1f (p.uniform ("uSunInt"), sunInt);
    };

    glViewport (0, 0, rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE);
    glDepthMask (GL_TRUE);
    glClear (GL_DEPTH_BUFFER_BIT);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texGrad1);
    glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, texGrad2);

    // The terrain: eight clipmap levels, finest first.
    glUseProgram (terrain.id); glBindVertexArray (vaoTerrain);
    setCommon (terrain); setLight (terrain);
    glUniform1i (terrain.uniform ("uDetail"), 1); glUniform1i (terrain.uniform ("uGrad1"), 2); glUniform1i (terrain.uniform ("uGrad2"), 3);
    glUniform2f (terrain.uniform ("uGS1"), world.gradientScale1()[0], world.gradientScale1()[1]);
    glUniform2f (terrain.uniform ("uGS2"), world.gradientScale2()[0], world.gradientScale2()[1]);
    glUniformMatrix4fv (terrain.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    glUniform3f (terrain.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
    glUniform2f (terrain.uniform ("uDetOff0"), det0[0], det0[1]); glUniform2f (terrain.uniform ("uDetOff1"), det1[0], det1[1]); glUniform2f (terrain.uniform ("uDetOff2"), det2[0], det2[1]);
    const int uLvlOrigin = terrain.uniform ("uLvlOrigin"), uCellSize = terrain.uniform ("uCellSize"), uMorphOn = terrain.uniform ("uMorphOn");
    for (int k = 0; k < levels; ++k)
    {
        auto& L = level[(size_t) k];
        const double cell = L.cell, snap = 2 * cell, half = N / 2 * cell;
        const double cx = s.x + fx * fwdShift * half, cz = s.z + fz * fwdShift * half;
        L.ox = std::floor (cx / snap) * snap - half; L.oz = std::floor (cz / snap) * snap - half;   // the absolute world origin of vertex (0, 0)
        if (k > 0)
        {
            const auto& F = level[(size_t) k - 1];
            const int hx = std::clamp ((int) std::lround ((F.ox - L.ox) / cell), 0, N / 2), hy = std::clamp ((int) std::lround ((F.oz - L.oz) / cell), 0, N / 2);
            if (hx != L.holeX || hy != L.holeY) rebuildRing (L, hx, hy);
        }
        glUniform2f (uLvlOrigin, (float) (L.ox - ccx), (float) (L.oz - ccz));
        glUniform1f (uCellSize, (float) cell);
        glUniform1f (uMorphOn, k < levels - 1 ? 1.0f : 0.0f);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
        glDrawElements (GL_TRIANGLES, L.count, GL_UNSIGNED_SHORT, nullptr);
    }

    // The steel: four instanced draws from the world's lists, remade when the window moves.
    if (instancing)
    {
        glUseProgram (steel.id); glBindVertexArray (vaoSteel);
        setCommon (steel); setLight (steel);
        glUniformMatrix4fv (steel.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
        glUniform3f (steel.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
        glUniform1i (steel.uniform ("uDetail"), 1); glUniform1f (steel.uniform ("uJupCos"), skyF.jupCos);
        glUniform1f (steel.uniform ("uPxExpand"), (float) (2 * tv / rh));
        const double shift = IceWorld::structWindow * IceWorld::structCell * 0.3;
        const int bx = (int) std::floor ((s.x + fx * shift) / IceWorld::structCell) - IceWorld::structWindow / 2;
        const int bz = (int) std::floor ((s.z + fz * shift) / IceWorld::structCell) - IceWorld::structWindow / 2;
        if (bx != instanceBx || bz != instanceBz)
        {
            world.structuresInWindow (bx, bz, instanceCells);
            for (int v = 0; v < IceWorld::numVariants; ++v)
            {
                const auto& cells = instanceCells[(size_t) v];
                instanceCount[(size_t) v] = (int) (cells.size() / 2);
                glBindBuffer (GL_ARRAY_BUFFER, instanceBuffer[(size_t) v]);
                if (! cells.empty()) glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (cells.size() * sizeof (float)), cells.data());
            }
            instanceBx = bx; instanceBz = bz;
        }
        const int uVariant = steel.uniform ("uVariant");
        for (int v = 0; v < IceWorld::numVariants; ++v)
        {
            if (instanceCount[(size_t) v] == 0) continue;
            glUniform1i (uVariant, v);
            glBindBuffer (GL_ARRAY_BUFFER, instanceBuffer[(size_t) v]);
            glEnableVertexAttribArray ((GLuint) locCell);
            glVertexAttribPointer ((GLuint) locCell, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glVertexAttribDivisor ((GLuint) locCell, 1);
            glDrawArraysInstanced (GL_TRIANGLES, steelRange[(size_t) v].first, steelRange[(size_t) v].count, instanceCount[(size_t) v]);
        }
        glBindBuffer (GL_ARRAY_BUFFER, 0);
    }

    // The sky, where nothing was drawn (depth == 1).
    glUseProgram (sky.id); glBindVertexArray (vaoEmpty);
    setLight (sky);
    glUniform1i (sky.uniform ("uDetail"), 1);
    glUniform3fv (sky.uniform ("uCamFwd"), 1, basis.f); glUniform3fv (sky.uniform ("uCamRight"), 1, basis.r); glUniform3fv (sky.uniform ("uCamUp"), 1, basis.u);
    glUniform2f (sky.uniform ("uTanHalf"), (float) th, (float) tv); glUniform1f (sky.uniform ("uShear"), shear);
    glUniform1f (sky.uniform ("uTime"), (float) skyTime); glUniform1f (sky.uniform ("uPxAng"), (float) (2 * tv / rh));
    glUniform3fv (sky.uniform ("uJupR"), 1, skyF.jupR); glUniform3fv (sky.uniform ("uJupU"), 1, skyF.jupU); glUniform3fv (sky.uniform ("uJupAxis"), 1, skyF.jupAxis);
    glUniform3fv (sky.uniform ("uIo"), 1, io);
    glUniform1f (sky.uniform ("uJupSin"), skyF.jupSin); glUniform1f (sky.uniform ("uJupCos"), skyF.jupCos); glUniform1f (sky.uniform ("uIoSin"), skyF.ioSin);
    glDrawArrays (GL_TRIANGLES, 0, 3);

    // The snow: the flakes blended over everything, as many as the snow asks for.
    if (snowAmount > 0.005f)
    {
        glUseProgram (snow.id);
        glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable (GL_DEPTH_TEST);
        glUniformMatrix4fv (snow.uniform ("uProj"), 1, GL_FALSE, P.data()); glUniform2f (snow.uniform ("uResolution"), (float) rw, (float) rh);
        glUniform1f (snow.uniform ("uTravel"), (float) std::fmod (s.travel, snowClock)); glUniform1f (snow.uniform ("uTime"), (float) skyTime);
        glUniform1f (snow.uniform ("uSpeed"), s.speed); glUniform1f (snow.uniform ("uSnow"), snowAmount); glUniform1f (snow.uniform ("uCount"), (float) IceWorld::snowCount);
        const int count = std::min (IceWorld::snowCount, (int) std::ceil (IceWorld::snowCount * std::min (1.0f, snowAmount + 0.12f)));
        glDrawArrays (GL_TRIANGLES, 0, count * 6);
        glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST);
    }
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::frostpaw
