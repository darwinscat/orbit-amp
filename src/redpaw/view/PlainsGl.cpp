// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PlainsGl.h"

#include "../../shaders/Shaders.h"

#include <cmath>
#include <map>

namespace orbitamp::redpaw
{

using namespace juce::gl;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double cell0 = 1.0;             // metres, the finest level
    constexpr double fwdShift = 0.4;          // the levels pushed ahead of the rover, of their half-extent
    constexpr double vfovHalfTan = 0.23086819;  // tan 13 deg: vfov 26 deg, hfov follows from the aspect
    constexpr float shear = 0.2f;             // the horizon at NDC y = +0.2: 40 % from the top
    constexpr int dustCount = 1800;
    constexpr double worldPeriod = PlainsWorld::worldPeriod;

    struct RockSet { double cell; int w; float prob, smin, smax; };
    constexpr RockSet rocks    { 3.5,  32, 0.30f,  0.22f, 1.0f };    // near rocks, a 112 m window
    constexpr RockSet boulders { 25.0, 32, 0.045f, 1.2f,  4.2f };    // boulders, an 800 m window

    // THE SHADERS are files in resources/shaders/plains (src/shaders/Shaders.h), written without the
    // #version, which glsl() puts in front of them. plains/height.glsl is the height function, shared by
    // the terrain's two shaders and the rock and relic vertex shaders: HTEX in it is textureLod (.., 0) in
    // a vertex shader and texture() in a fragment shader, and glsl() defines it for the stage it builds.
    // plains/grad.glsl is the analytic world-space gradient of terrainH from the gradient textures, the
    // per-pixel normal: uGrad1 = (dR/du, dR/dv, dB/du, dB/dv) and uGrad2 = (dG/du, dG/dv, dA/du, dA/dv)
    // per uv unit, as 0.5 + 0.5 * g / gmax in RGBA8; uGS1 = (gmaxR, gmaxB), uGS2 = (gmaxG, gmaxA). The
    // relics (plains/relic.vert) are instanced, one draw per variant; aCell is the instance's unwrapped
    // 532 m cell. A vertex of kind 1 is a solar panel, of kind 2 a track behind the wheels: it follows
    // the ground, and only some have one.
    enum class Htex { none, vertex, fragment };
    std::string glsl (const char* path, Htex htex = Htex::none)
    {
        std::string s ("#version 150\n");
        if (htex == Htex::vertex)   s += "#define HTEX(uv) textureLod(uHeight, uv, 0.0)\n";
        if (htex == Htex::fragment) s += "#define HTEX(uv) texture(uHeight, uv)\n";
        return s + shaders::source (path);
    }

    // Two lumpy icospheres, 80 flat faces each: 240 vertices of position + normal, the two one after
    // the other. Watertight; the lumps are the generator's stream, the same every time.
    std::vector<float> icosphere (uint32_t seed)
    {
        struct V3 { double x, y, z; };
        const double t = (1 + std::sqrt (5.0)) / 2;
        std::vector<V3> V { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                            { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
        for (auto& v : V) { const double l = std::sqrt (v.x * v.x + v.y * v.y + v.z * v.z); v = { v.x / l, v.y / l, v.z / l }; }
        std::vector<std::array<int, 3>> F { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                            { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        std::map<std::pair<int, int>, int> cache;
        const auto mid = [&] (int a, int b)
        {
            const auto key = a < b ? std::make_pair (a, b) : std::make_pair (b, a);
            if (const auto it = cache.find (key); it != cache.end()) return it->second;
            V3 m { (V[(size_t) a].x + V[(size_t) b].x) / 2, (V[(size_t) a].y + V[(size_t) b].y) / 2, (V[(size_t) a].z + V[(size_t) b].z) / 2 };
            const double l = std::sqrt (m.x * m.x + m.y * m.y + m.z * m.z);
            V.push_back ({ m.x / l, m.y / l, m.z / l });
            cache[key] = (int) V.size() - 1;
            return (int) V.size() - 1;
        };
        std::vector<std::array<int, 3>> F2;
        for (const auto& f : F)
        {
            const int ab = mid (f[0], f[1]), bc = mid (f[1], f[2]), ca = mid (f[2], f[0]);
            F2.push_back ({ f[0], ab, ca }); F2.push_back ({ f[1], bc, ab }); F2.push_back ({ f[2], ca, bc }); F2.push_back ({ ab, bc, ca });
        }
        struct Rng { uint32_t a; double next() { a += 0x6D2B79F5u; uint32_t t = (a ^ (a >> 15)) * (1u | a); t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t; return (double) (t ^ (t >> 14)) / 4294967296.0; } };
        Rng rnd { seed };
        for (auto& v : V) { const double r = 0.8 + 0.4 * rnd.next(); v = { v.x * r, v.y * r, v.z * r }; }
        std::vector<float> out;
        out.reserve (F2.size() * 3 * 6);
        for (const auto& f : F2)
        {
            const V3 A = V[(size_t) f[0]], B = V[(size_t) f[1]], C = V[(size_t) f[2]];
            const V3 e1 { B.x - A.x, B.y - A.y, B.z - A.z }, e2 { C.x - A.x, C.y - A.y, C.z - A.z };
            V3 n { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
            const double l = std::sqrt (n.x * n.x + n.y * n.y + n.z * n.z);
            n = { n.x / l, n.y / l, n.z / l };
            for (const V3& P : { A, B, C })
                for (double d : { P.x, P.y, P.z, n.x, n.y, n.z })
                    out.push_back ((float) d);
        }
        return out;
    }

    //==========================================================================
    /** THE RELICS' GEOMETRY: boxes, bars, eight-sided wheels and plates with flat normals, each
        vertex position (3), normal (3), colour (3) and a kind (1): ten floats. Kind 2 is a track. */
    struct P3 { double x, y, z; };
    inline P3 operator+ (P3 a, P3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline P3 operator- (P3 a, P3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline P3 operator* (P3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
    inline P3 cross3 (P3 a, P3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline P3 norm3 (P3 a) { const double l = std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z); return l > 0 ? a * (1 / l) : a; }

    const P3 white { 0.82, 0.80, 0.76 }, grey { 0.62, 0.60, 0.58 }, dark { 0.30, 0.30, 0.32 }, tyre { 0.20, 0.19, 0.18 };
    const P3 solar { 0.10, 0.12, 0.26 }, foil { 0.75, 0.62, 0.30 }, airbag { 0.70, 0.66, 0.60 }, rut { 0.50, 0.30, 0.19 };

    struct RelicBuilder
    {
        std::vector<float> v;

        void tri (P3 A, P3 B, P3 C, P3 n, P3 col, float kind = 0)
        {
            for (const P3& P : { A, B, C })
                for (double d : { P.x, P.y, P.z, n.x, n.y, n.z, col.x, col.y, col.z, (double) kind })
                    v.push_back ((float) d);
        }
        void quad (P3 A, P3 B, P3 C, P3 D, P3 n, P3 col, float kind = 0) { tri (A, B, C, n, col, kind); tri (A, C, D, n, col, kind); }

        /** An axis-aligned box about its centre. Kind 1 is a solar panel: the dust lies thinner on it. */
        void box (P3 c, P3 s, P3 col, float kind = 0)
        {
            const double hx = s.x / 2, hy = s.y / 2, hz = s.z / 2;
            const P3 p[8] { { c.x - hx, c.y - hy, c.z - hz }, { c.x + hx, c.y - hy, c.z - hz }, { c.x + hx, c.y + hy, c.z - hz }, { c.x - hx, c.y + hy, c.z - hz },
                            { c.x - hx, c.y - hy, c.z + hz }, { c.x + hx, c.y - hy, c.z + hz }, { c.x + hx, c.y + hy, c.z + hz }, { c.x - hx, c.y + hy, c.z + hz } };
            quad (p[1], p[0], p[3], p[2], { 0, 0, -1 }, col, kind); quad (p[4], p[5], p[6], p[7], { 0, 0, 1 }, col, kind);
            quad (p[0], p[4], p[7], p[3], { -1, 0, 0 }, col, kind); quad (p[5], p[1], p[2], p[6], { 1, 0, 0 }, col, kind);
            quad (p[3], p[7], p[6], p[2], { 0, 1, 0 }, col, kind);  quad (p[0], p[1], p[5], p[4], { 0, -1, 0 }, col, kind);
        }

        /** A square-section bar of thickness t along a -> b. */
        void bar (P3 a, P3 b, double t, P3 col)
        {
            const P3 u = norm3 (b - a);
            const P3 ref = std::abs (u.y) < 0.9 ? P3 { 0, 1, 0 } : P3 { 1, 0, 0 };
            const P3 vv = norm3 (cross3 (u, ref)), w = cross3 (u, vv);
            const double h = t / 2;
            P3 C[8]; int k = 0;
            for (int end = 0; end < 2; ++end)
                for (double sv : { -1.0, 1.0 })
                    for (double sw : { -1.0, 1.0 })
                        C[k++] = (end ? b : a) + vv * (sv * h) + w * (sw * h);
            quad (C[2], C[3], C[7], C[6], vv, col); quad (C[0], C[4], C[5], C[1], vv * -1.0, col);
            quad (C[1], C[5], C[7], C[3], w, col);  quad (C[0], C[2], C[6], C[4], w * -1.0, col);
            quad (C[4], C[6], C[7], C[5], u, col);  quad (C[0], C[1], C[3], C[2], u * -1.0, col);
        }

        /** An eight-sided wheel on the x axis, with its two faces. */
        void wheel (P3 c, double r, double w, P3 col)
        {
            for (int s = 0; s < 8; ++s)
            {
                const double a0 = s / 8.0 * pi * 2, a1 = (s + 1) / 8.0 * pi * 2, am = (a0 + a1) / 2;
                const P3 L0 { c.x - w / 2, c.y + r * std::cos (a0), c.z + r * std::sin (a0) }, L1 { c.x - w / 2, c.y + r * std::cos (a1), c.z + r * std::sin (a1) };
                const P3 R0 { c.x + w / 2, L0.y, L0.z }, R1 { c.x + w / 2, L1.y, L1.z };
                quad (L0, L1, R1, R0, { 0, std::cos (am), std::sin (am) }, col);
                tri ({ c.x - w / 2, c.y, c.z }, L1, L0, { -1, 0, 0 }, col);
                tri ({ c.x + w / 2, c.y, c.z }, R0, R1, { 1, 0, 0 }, col);
            }
        }

        /** A flat plate, one face, lit from above: its normal is the side that points up. */
        void plate (P3 A, P3 B, P3 C, P3 col, float kind = 0)
        {
            P3 n = norm3 (cross3 (B - A, C - A));
            if (n.y < 0) n = n * -1.0;
            tri (A, B, C, n, col, kind);
        }

        /** A low eight-sided mound: a slack airbag. */
        void mound (P3 c, double r, double h, P3 col)
        {
            for (int s = 0; s < 8; ++s)
            {
                const double a0 = s / 8.0 * pi * 2, a1 = (s + 1) / 8.0 * pi * 2;
                const P3 P0 { c.x + r * std::cos (a0), c.y, c.z + r * std::sin (a0) }, P1 { c.x + r * std::cos (a1), c.y, c.z + r * std::sin (a1) };
                const P3 T { c.x, c.y + h, c.z };
                tri (P0, T, P1, norm3 (cross3 (T - P0, P1 - P0)), col);
            }
        }

        /** A track: a ribbon on the ground behind the wheel at x, from 2 m back to `length`, kind 2. */
        void track (double x, double w, double length)
        {
            const int segs = (int) std::ceil (length / 2.0);
            for (int s = 0; s < segs; ++s)
            {
                const double z0 = 2.0 + (length - 2.0) * s / segs, z1 = 2.0 + (length - 2.0) * (s + 1) / segs;
                const double b0 = 0.6 * (z0 / length) * (z0 / length) * (x < 0 ? -1 : 1), b1 = 0.6 * (z1 / length) * (z1 / length) * (x < 0 ? -1 : 1);   // a gentle bend
                quad ({ x + b0 - w / 2, 0, z0 }, { x + b0 + w / 2, 0, z0 }, { x + b1 + w / 2, 0, z1 }, { x + b1 - w / 2, 0, z1 }, { 0, 1, 0 }, rut, 2);
            }
        }
    };

    /** The four relics, in a frame where -z is ahead and the ground is y = 0; the wheels sit a
        little below it, sunk in. The sizes are the real machines'. */
    std::array<std::vector<float>, PlainsWorld::numRelics> buildRelicMeshes()
    {
        std::array<std::vector<float>, PlainsWorld::numRelics> out;
        {   // 0: the car-sized rover — six half-metre wheels on rocker-bogies, a mast with a camera head, the power unit at the back, an arm
            RelicBuilder m;
            m.box ({ 0, 1.05, 0 }, { 1.4, 0.55, 1.9 }, white);
            m.box ({ 0, 1.34, 0 }, { 1.5, 0.04, 2.0 }, grey);
            m.bar ({ 0.45, 1.35, -0.8 }, { 0.45, 2.35, -0.8 }, 0.12, dark);
            m.box ({ 0.45, 2.42, -0.8 }, { 0.5, 0.3, 0.26 }, dark);
            m.bar ({ 0, 1.3, 0.9 }, { 0, 1.8, 1.75 }, 0.42, dark);
            m.bar ({ -0.45, 1.36, 0.5 }, { -0.45, 1.62, 0.5 }, 0.08, dark);
            m.wheel ({ -0.45, 1.72, 0.5 }, 0.24, 0.03, white);
            m.bar ({ 0, 0.95, -0.95 }, { 0.35, 0.55, -1.85 }, 0.1, dark);
            m.box ({ 0.4, 0.5, -1.95 }, { 0.3, 0.3, 0.3 }, dark);
            for (double sx : { -1.0, 1.0 })
            {
                for (double z : { -1.1, 0.0, 1.1 }) m.wheel ({ sx * 1.15, 0.15, z }, 0.25, 0.4, tyre);
                m.bar ({ sx * 0.72, 0.95, -0.25 }, { sx * 1.15, 0.15, -1.1 }, 0.09, grey);
                m.bar ({ sx * 0.72, 0.95, -0.25 }, { sx * 1.0, 0.7, 0.55 }, 0.09, grey);
                m.bar ({ sx * 1.0, 0.7, 0.55 }, { sx * 1.15, 0.15, 0.0 }, 0.09, grey);
                m.bar ({ sx * 1.0, 0.7, 0.55 }, { sx * 1.15, 0.15, 1.1 }, 0.09, grey);
                m.track (sx * 1.15, 0.4, 45);
            }
            out[0] = std::move (m.v);
        }
        {   // 1: the knee-high rover — a flat solar deck on a gold box, six small wheels, a whip antenna
            RelicBuilder m;
            m.box ({ 0, 0.21, 0 }, { 0.42, 0.17, 0.62 }, foil);
            m.box ({ 0, 0.31, 0 }, { 0.62, 0.02, 0.68 }, solar, 1);
            m.box ({ 0.12, 0.4, -0.3 }, { 0.12, 0.08, 0.08 }, dark);
            m.bar ({ -0.22, 0.32, 0.25 }, { -0.22, 0.75, 0.25 }, 0.02, dark);
            for (double sx : { -1.0, 1.0 })
            {
                for (double z : { -0.26, 0.0, 0.26 }) m.wheel ({ sx * 0.31, 0.035, z }, 0.065, 0.08, tyre);
                m.bar ({ sx * 0.21, 0.2, -0.1 }, { sx * 0.31, 0.035, -0.26 }, 0.03, dark);
                m.bar ({ sx * 0.21, 0.2, -0.1 }, { sx * 0.27, 0.15, 0.13 }, 0.03, dark);
                m.bar ({ sx * 0.27, 0.15, 0.13 }, { sx * 0.31, 0.035, 0.0 }, 0.03, dark);
                m.bar ({ sx * 0.27, 0.15, 0.13 }, { sx * 0.31, 0.035, 0.26 }, 0.03, dark);
                m.track (sx * 0.31, 0.08, 20);
            }
            out[1] = std::move (m.v);
        }
        {   // 2: the lander — a base open on three petals, slack airbags between them, a mast
            RelicBuilder m;
            m.box ({ 0, 0.4, 0 }, { 1.1, 0.7, 1.1 }, white);
            for (int k = 0; k < 3; ++k)
            {
                const double a = (90 + 120 * k) * pi / 180, ca = std::cos (a), sa = std::sin (a);
                const P3 dir { ca, 0, sa }, side { -sa, 0, ca };
                const P3 base = dir * 0.6;
                m.plate (base + side * 0.75 + P3 { 0, 0.15, 0 }, base - side * 0.75 + P3 { 0, 0.15, 0 }, dir * 2.6 + P3 { 0, 0.6, 0 }, solar, 1);   // open, not flat: a petal that never came all the way down
                const double ab = a + pi / 3;
                m.mound ({ std::cos (ab) * 1.3, 0, std::sin (ab) * 1.3 }, 0.95, 0.35, airbag);
            }
            m.bar ({ 0.25, 0.75, 0 }, { 0.25, 1.7, 0 }, 0.06, white);
            m.box ({ 0.25, 1.75, 0 }, { 0.3, 0.12, 0.14 }, dark);
            m.bar ({ -0.3, 0.75, -0.2 }, { -0.3, 1.1, -0.2 }, 0.03, dark);
            out[2] = std::move (m.v);
        }
        {   // 3: the winged rover — a wide solar deck swept back in two wings, a mast with a camera bar, six wheels
            RelicBuilder m;
            m.box ({ 0, 0.6, 0 }, { 1.0, 0.42, 1.3 }, white);
            m.box ({ 0, 0.83, 0 }, { 1.6, 0.03, 1.5 }, solar, 1);
            m.box ({ -1.05, 0.81, 0.45 }, { 0.7, 0.03, 0.9 }, solar, 1);
            m.box ({ 1.05, 0.81, 0.45 }, { 0.7, 0.03, 0.9 }, solar, 1);
            m.bar ({ 0.3, 0.85, -0.5 }, { 0.3, 1.6, -0.5 }, 0.08, dark);
            m.box ({ 0.3, 1.66, -0.5 }, { 0.38, 0.14, 0.16 }, dark);
            m.bar ({ -0.35, 0.85, 0.3 }, { -0.35, 1.05, 0.3 }, 0.05, dark);
            m.wheel ({ -0.35, 1.15, 0.3 }, 0.14, 0.03, white);
            for (double sx : { -1.0, 1.0 })
            {
                for (double z : { -0.55, 0.05, 0.6 }) m.wheel ({ sx * 0.72, 0.08, z }, 0.13, 0.16, tyre);
                m.bar ({ sx * 0.5, 0.5, -0.2 }, { sx * 0.72, 0.08, -0.55 }, 0.05, grey);
                m.bar ({ sx * 0.5, 0.5, -0.2 }, { sx * 0.62, 0.38, 0.32 }, 0.05, grey);
                m.bar ({ sx * 0.62, 0.38, 0.32 }, { sx * 0.72, 0.08, 0.05 }, 0.05, grey);
                m.bar ({ sx * 0.62, 0.38, 0.32 }, { sx * 0.72, 0.08, 0.6 }, 0.05, grey);
                m.track (sx * 0.72, 0.16, 35);
            }
            out[3] = std::move (m.v);
        }
        return out;
    }

    inline double fract (double x) noexcept { return x - std::floor (x); }

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
}

//==============================================================================
int PlainsGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool PlainsGl::compile (Program& p, const std::string& vs, const std::string& fs, const char* what)
{
    auto s = std::make_unique<juce::OpenGLShaderProgram> (*juce::OpenGLContext::getCurrentContext());
    if (! s->addVertexShader (vs) || ! s->addFragmentShader (fs) || ! s->link())
    {
        DBG ("redpaw: the " << what << " shader did not build — " << s->getLastError());
        juce::ignoreUnused (what);
        return false;
    }
    p.id = s->getProgramID();
    p.program = std::move (s);
    return true;
}

bool PlainsGl::create()
{
    if (juce::OpenGLContext::getCurrentContext() == nullptr)
        return false;
    world.build();

    if (! compile (terrain, glsl ("plains/terrain.vert", Htex::vertex), glsl ("plains/terrain.frag", Htex::fragment), "terrain")
        || ! compile (rock, glsl ("plains/rock.vert", Htex::vertex), glsl ("plains/rock.frag"), "rock")
        || ! compile (relic, glsl ("plains/relic.vert", Htex::vertex), glsl ("plains/relic.frag"), "relic")
        || ! compile (sky, glsl ("common/sky.vert"), glsl ("plains/sky.frag"), "sky")
        || ! compile (dust, glsl ("plains/dust.vert"), glsl ("plains/dust.frag"), "dust"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeGeometry();

    // The frame timer: a timer query where the driver has one. Asked once; an error means no.
    while (glGetError() != GL_NO_ERROR) {}
    if (glGenQueries != nullptr && glGetQueryObjectui64v != nullptr)
    {
        glGenQueries ((GLsizei) query.size(), query.data());
        glBeginQuery (GL_TIME_ELAPSED, query[0]);
        glEndQuery (GL_TIME_ELAPSED);
        queriesWork = glGetError() == GL_NO_ERROR;
        if (queriesWork)
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v (query[0], GL_QUERY_RESULT, &ns);   // drains the probe
        }
        else
        {
            glDeleteQueries ((GLsizei) query.size(), query.data());
            query.fill (0);
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    return true;
}

void PlainsGl::destroy()
{
    for (auto& l : level) { if (l.indexBuffer != 0) glDeleteBuffers (1, &l.indexBuffer); l = {}; }
    const auto del = [] (juce::uint32& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (vboGrid, glDeleteBuffers); del (vboRock, glDeleteBuffers); del (vboRelic, glDeleteBuffers);
    for (auto& b : relicInstances) del (b, glDeleteBuffers);
    relicCount.fill (0); relicBx = relicBz = 0x7fffffff;
    del (vaoTerrain, glDeleteVertexArrays); del (vaoRock, glDeleteVertexArrays); del (vaoRelic, glDeleteVertexArrays); del (vaoEmpty, glDeleteVertexArrays);
    del (texHeight, glDeleteTextures); del (texGrad1, glDeleteTextures); del (texGrad2, glDeleteTextures); del (texDetail, glDeleteTextures);
    del (fboColour, glDeleteTextures); del (fboDepth, glDeleteRenderbuffers); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &terrain, &rock, &relic, &sky, &dust }) { p->program.reset(); p->id = 0; }
}

void PlainsGl::makeTextures()
{
    const int n = PlainsWorld::texSize;
    glGenTextures (1, &texHeight);
    glBindTexture (GL_TEXTURE_2D, texHeight);
    {
        int size = n, lvl = 0;
        for (const auto& mip : world.heightMips())
        {
            glTexImage2D (GL_TEXTURE_2D, lvl++, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT, mip.data());
            size >>= 1;
        }
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lvl - 1);
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const auto rgba8 = [] (juce::uint32& id, int size, const std::vector<juce::uint8>& px)
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
    rgba8 (texDetail, PlainsWorld::detailSize, world.detail());
    glBindTexture (GL_TEXTURE_2D, 0);
}

void PlainsGl::makeGeometry()
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
            const size_t o = (size_t) (j * N + i) * 6; const juce::uint16 v = (juce::uint16) (j * (N + 1) + i);
            fullIndices[o] = v; fullIndices[o + 1] = (juce::uint16) (v + 1); fullIndices[o + 2] = (juce::uint16) (v + N + 2);
            fullIndices[o + 3] = v; fullIndices[o + 4] = (juce::uint16) (v + N + 2); fullIndices[o + 5] = (juce::uint16) (v + N + 1);
        }
    ringIndices.resize (fullIndices.size());
    for (int k = 0; k < levels; ++k)
    {
        auto& L = level[(size_t) k];
        glGenBuffers (1, &L.indexBuffer);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (fullIndices.size() * sizeof (juce::uint16)), fullIndices.data(), k == 0 ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
        L.count = (int) fullIndices.size();
        L.cell = cell0 * std::pow (2.0, k);
        L.holeX = L.holeY = -1;
    }
    glBindVertexArray (0);

    // The rocks: two variants, one buffer.
    glGenVertexArrays (1, &vaoRock);
    glBindVertexArray (vaoRock);
    {
        auto a = icosphere (77); const auto b = icosphere (78);
        a.insert (a.end(), b.begin(), b.end());
        glGenBuffers (1, &vboRock);
        glBindBuffer (GL_ARRAY_BUFFER, vboRock);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (a.size() * sizeof (float)), a.data(), GL_STATIC_DRAW);
        const auto lp = (GLuint) glGetAttribLocation (rock.id, "aPos"), ln = (GLuint) glGetAttribLocation (rock.id, "aNrm");
        glEnableVertexAttribArray (lp); glVertexAttribPointer (lp, 3, GL_FLOAT, GL_FALSE, 24, nullptr);
        glEnableVertexAttribArray (ln); glVertexAttribPointer (ln, 3, GL_FLOAT, GL_FALSE, 24, (const void*) 12);
    }
    glBindVertexArray (0);

    // The relics: the four meshes in one buffer, the instance cells in a small dynamic buffer per variant.
    glGenVertexArrays (1, &vaoRelic);
    glBindVertexArray (vaoRelic);
    {
        auto meshes = buildRelicMeshes();
        std::vector<float> all;
        for (int v = 0; v < PlainsWorld::numRelics; ++v)
        {
            relicRange[(size_t) v] = { (int) (all.size() / 10), (int) (meshes[(size_t) v].size() / 10) };
            all.insert (all.end(), meshes[(size_t) v].begin(), meshes[(size_t) v].end());
        }
        glGenBuffers (1, &vboRelic);
        glBindBuffer (GL_ARRAY_BUFFER, vboRelic);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (all.size() * sizeof (float)), all.data(), GL_STATIC_DRAW);
        const auto lp = (GLuint) glGetAttribLocation (relic.id, "aPos"), ln = (GLuint) glGetAttribLocation (relic.id, "aNrm");
        const auto lc = (GLuint) glGetAttribLocation (relic.id, "aCol"), lk = (GLuint) glGetAttribLocation (relic.id, "aKind");
        glEnableVertexAttribArray (lp); glVertexAttribPointer (lp, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
        glEnableVertexAttribArray (ln); glVertexAttribPointer (ln, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 12);
        glEnableVertexAttribArray (lc); glVertexAttribPointer (lc, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 24);
        glEnableVertexAttribArray (lk); glVertexAttribPointer (lk, 1, GL_FLOAT, GL_FALSE, 40, (const void*) 36);
        locRelicCell = glGetAttribLocation (relic.id, "aCell");
        instancing = glVertexAttribDivisor != nullptr && locRelicCell >= 0;
        for (auto& b : relicInstances)
        {
            glGenBuffers (1, &b);
            glBindBuffer (GL_ARRAY_BUFFER, b);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (PlainsWorld::relicWindow * PlainsWorld::relicWindow * 2 * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
        }
        relicCount.fill (0); relicBx = relicBz = 0x7fffffff;
    }
    glBindVertexArray (0);
    glGenVertexArrays (1, &vaoEmpty);   // the sky and the dust draw with no attributes; core needs a VAO bound all the same
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void PlainsGl::rebuildRing (Level& L, int hx, int hy)   // leaves out the N/2 x N/2 cells at [hx, hx + N/2) x [hy, hy + N/2)
{
    size_t n = 0; const int H = N / 2;
    const auto put = [&] (size_t from, size_t to) { std::copy (fullIndices.begin() + (long) from, fullIndices.begin() + (long) to, ringIndices.begin() + (long) n); n += to - from; };
    for (int j = 0; j < N; ++j)
    {
        if (j < hy || j >= hy + H) put ((size_t) j * N * 6, (size_t) (j + 1) * N * 6);
        else { put ((size_t) j * N * 6, (size_t) (j * N + hx) * 6); put ((size_t) (j * N + hx + H) * 6, (size_t) (j + 1) * N * 6); }
    }
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
    glBufferSubData (GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr) (n * sizeof (juce::uint16)), ringIndices.data());
    L.count = (int) n; L.holeX = hx; L.holeY = hy;
}

void PlainsGl::ensureFrame (int w, int h)
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

void PlainsGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void PlainsGl::render (int w, int h, float scale, juce::uint32 target, int x, int y)
{
    if (terrain.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, juce::roundToInt ((float) w * scale)), rh = std::max (2, juce::roundToInt ((float) h * scale));
    ensureFrame (rw, rh);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);

    // The timer: queries, read back a few frames late so nothing waits — or, without them, one frame
    // in sixty finished and clocked.
    const bool probe = ! queriesWork && (frameCounter % 60) == 0;
    ++frameCounter;
    if (queriesWork)
    {
        while (queriesPending > 0)
        {
            const auto oldest = query[(size_t) ((queryNext - queriesPending + (int) query.size()) % (int) query.size())];
            GLuint available = 0;
            glGetQueryObjectuiv (oldest, GL_QUERY_RESULT_AVAILABLE, &available);
            if (! available) break;
            GLuint64 ns = 0;
            glGetQueryObjectui64v (oldest, GL_QUERY_RESULT, &ns);
            noteCost ((float) ns * 1.0e-6f);
            --queriesPending;
        }
        if (queriesPending < (int) query.size())
        {
            glBeginQuery (GL_TIME_ELAPSED, query[(size_t) queryNext]);
            queryNext = (queryNext + 1) % (int) query.size();
            ++queriesPending;
        }
    }
    else if (probe)
    {
        glFinish();
        probeStart = juce::Time::getMillisecondCounterHiRes();
    }

    for (int i = 0; i < repeats; ++i)
        drawScene (rw, rh);

    if (queriesWork)
        glEndQuery (GL_TIME_ELAPSED);
    else if (probe)
    {
        glFinish();
        noteCost ((float) (juce::Time::getMillisecondCounterHiRes() - probeStart));
    }

    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer (0, 0, rw, rh, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer (GL_FRAMEBUFFER, target);
}

bool PlainsGl::readFrame (std::vector<juce::uint8>& rgba, int& w, int& h)
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

void PlainsGl::drawScene (int rw, int rh)
{
    const auto& s = world.state();
    // The dust: the world eases it, TURBO's and graded by the gear; 1 is the full storm.
    const float storm = s.storm;
    const auto mix3 = [] (std::array<float, 3> a, std::array<float, 3> b, float t) { return std::array<float, 3> { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; };
    const auto horizon = mix3 ({ 0.87f, 0.64f, 0.44f }, { 0.76f, 0.58f, 0.40f }, storm);
    const auto zenith  = mix3 ({ 0.50f, 0.33f, 0.27f }, { 0.62f, 0.47f, 0.34f }, storm);
    const auto sunGlow = mix3 ({ 0.55f, 0.40f, 0.22f }, { 0.36f, 0.28f, 0.17f }, storm);
    const float hazeK = (1.0f / 6500.0f) * (1 + 4.0f * storm);
    const float sunInt = 1.4f - 1.0f * storm;
    const double sunAz = 32 * pi / 180, sunEl = 11 * pi / 180;
    const float sun[3] { (float) (std::sin (sunAz) * std::cos (sunEl)), (float) std::sin (sunEl), (float) (-std::cos (sunAz) * std::cos (sunEl)) };

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
    const float det0[2] { (float) fract (ccx / 1.75), (float) fract (ccz / 1.75) };
    const float det1[2] { (float) fract (ccx / 0.35), (float) fract (ccz / 0.35) };

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uHeight"), 0);
        glUniform2f (p.uniform ("uCamCell"), (float) ccx, (float) ccz);
        glUniform2f (p.uniform ("uCamFrac"), camFrac[0], camFrac[1]);
        glUniform2f (p.uniform ("uTexOff0"), tex0[0], tex0[1]);
        glUniform2f (p.uniform ("uTexOff1"), tex1[0], tex1[1]);
        glUniform2f (p.uniform ("uTexOff2"), tex2[0], tex2[1]);
    };
    const auto setLight = [&] (const Program& p)
    {
        glUniform3fv (p.uniform ("uSun"), 1, sun);
        glUniform3fv (p.uniform ("uHorizon"), 1, horizon.data());
        glUniform3fv (p.uniform ("uZenith"), 1, zenith.data());
        glUniform3fv (p.uniform ("uSunGlow"), 1, sunGlow.data());
        glUniform1f (p.uniform ("uHazeK"), hazeK);
        glUniform1f (p.uniform ("uStorm"), storm);
    };

    glViewport (0, 0, rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE);
    glDepthMask (GL_TRUE);
    glClear (GL_DEPTH_BUFFER_BIT);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texGrad1);
    glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, texGrad2);

    // The terrain: nine clipmap levels, finest first.
    glUseProgram (terrain.id); glBindVertexArray (vaoTerrain);
    setCommon (terrain); setLight (terrain);
    glUniform1i (terrain.uniform ("uDetail"), 1); glUniform1i (terrain.uniform ("uGrad1"), 2); glUniform1i (terrain.uniform ("uGrad2"), 3);
    glUniform2f (terrain.uniform ("uGS1"), world.gradientScale1()[0], world.gradientScale1()[1]);
    glUniform2f (terrain.uniform ("uGS2"), world.gradientScale2()[0], world.gradientScale2()[1]);
    glUniformMatrix4fv (terrain.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    glUniform3f (terrain.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
    glUniform2f (terrain.uniform ("uDetOff0"), det0[0], det0[1]); glUniform2f (terrain.uniform ("uDetOff1"), det1[0], det1[1]);
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

    // The rocks: two instanced sets, placed by the world-cell hash.
    glUseProgram (rock.id); glBindVertexArray (vaoRock);
    setCommon (rock); setLight (rock);
    glUniformMatrix4fv (rock.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    glUniform3f (rock.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
    for (const auto& R : { rocks, boulders })
    {
        const double shift = R.w * R.cell * 0.3;
        const int bx = (int) std::floor ((s.x + fx * shift) / R.cell) - R.w / 2, bz = (int) std::floor ((s.z + fz * shift) / R.cell) - R.w / 2;
        glUniform2i (rock.uniform ("uRockBase"), bx, bz); glUniform1i (rock.uniform ("uRockW"), R.w); glUniform1i (rock.uniform ("uRockM"), (int) std::lround (worldPeriod / R.cell));
        glUniform1f (rock.uniform ("uRockCell"), (float) R.cell); glUniform1f (rock.uniform ("uRockProb"), R.prob); glUniform2f (rock.uniform ("uRockSize"), R.smin, R.smax);
        glDrawArraysInstanced (GL_TRIANGLES, 0, 480, R.w * R.w);
    }

    // The relics: four instanced draws from the world's lists, remade when the window moves. Rare,
    // so most frames draw a few or none.
    if (instancing)
    {
        const double shift = PlainsWorld::relicWindow * PlainsWorld::relicCell * 0.3;
        const int bx = (int) std::floor ((s.x + fx * shift) / PlainsWorld::relicCell) - PlainsWorld::relicWindow / 2;
        const int bz = (int) std::floor ((s.z + fz * shift) / PlainsWorld::relicCell) - PlainsWorld::relicWindow / 2;
        if (bx != relicBx || bz != relicBz)
        {
            world.relicsInWindow (bx, bz, relicCells);
            for (int v = 0; v < PlainsWorld::numRelics; ++v)
            {
                const auto& cells = relicCells[(size_t) v];
                relicCount[(size_t) v] = (int) (cells.size() / 2);
                glBindBuffer (GL_ARRAY_BUFFER, relicInstances[(size_t) v]);
                if (! cells.empty()) glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (cells.size() * sizeof (float)), cells.data());
            }
            relicBx = bx; relicBz = bz;
        }
        bool any = false;
        for (int n : relicCount) any = any || n > 0;
        if (any)
        {
            glUseProgram (relic.id); glBindVertexArray (vaoRelic);
            setCommon (relic); setLight (relic);
            glUniformMatrix4fv (relic.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
            glUniform3f (relic.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
            for (int v = 0; v < PlainsWorld::numRelics; ++v)
            {
                if (relicCount[(size_t) v] == 0) continue;
                glBindBuffer (GL_ARRAY_BUFFER, relicInstances[(size_t) v]);
                glEnableVertexAttribArray ((GLuint) locRelicCell);
                glVertexAttribPointer ((GLuint) locRelicCell, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
                glVertexAttribDivisor ((GLuint) locRelicCell, 1);
                glDrawArraysInstanced (GL_TRIANGLES, relicRange[(size_t) v].first, relicRange[(size_t) v].count, relicCount[(size_t) v]);
            }
            glBindBuffer (GL_ARRAY_BUFFER, 0);
        }
    }

    // The sky, where nothing was drawn (depth == 1).
    glUseProgram (sky.id); glBindVertexArray (vaoEmpty);
    setLight (sky);
    glUniform3fv (sky.uniform ("uCamFwd"), 1, basis.f); glUniform3fv (sky.uniform ("uCamRight"), 1, basis.r); glUniform3fv (sky.uniform ("uCamUp"), 1, basis.u);
    glUniform2f (sky.uniform ("uTanHalf"), (float) th, (float) tv); glUniform1f (sky.uniform ("uShear"), shear);
    glUniform1f (sky.uniform ("uSunInt"), sunInt); glUniform1f (sky.uniform ("uTime"), (float) std::fmod (s.time, 100000.0));
    glDrawArrays (GL_TRIANGLES, 0, 3);

    // The storm's streaks, head-on, blended over everything.
    if (storm > 0.01f)
    {
        glUseProgram (dust.id);
        glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable (GL_DEPTH_TEST);
        glUniformMatrix4fv (dust.uniform ("uProj"), 1, GL_FALSE, P.data()); glUniform2f (dust.uniform ("uResolution"), (float) rw, (float) rh);
        glUniform1f (dust.uniform ("uTravel"), (float) std::fmod (s.travel, 100000.0)); glUniform1f (dust.uniform ("uTime"), (float) std::fmod (s.time, 100000.0));
        glUniform1f (dust.uniform ("uSpeed"), s.speed); glUniform1f (dust.uniform ("uStorm"), storm);
        glDrawArrays (GL_TRIANGLES, 0, dustCount * 6);
        glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST);
    }
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::redpaw
