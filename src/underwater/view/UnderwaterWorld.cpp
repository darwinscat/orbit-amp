// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of sea-gl.js's world, line for line: the makers of the textures and the meshes, makeWorld's drive, the
// jellyfish and the passing bathyscaphe. Every expression keeps the prototype's order of operations, so that the
// doubles round where the JS's round; f32 where it stores into a Float32Array or calls Math.fround.

#include "UnderwaterWorld.h"

#include "../../redgroove/view/WalkerWorld.h"     // Js: V8's sin, cos, exp, atan2, hypot and round

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <istream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <string>

namespace orbitamp::underwater
{

namespace
{
    using Js = redgroove::WalkerWorld::Js;
    using V3 = UnderwaterWorld::V3;
    constexpr double pi = UnderwaterWorld::pi;
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    inline double jsMin (double a, double b) noexcept { return (a != a || b != b) ? nan : (a < b ? a : b); }
    inline double jsMax (double a, double b) noexcept { return (a != a || b != b) ? nan : (a > b ? a : b); }
    inline double clamp (double v, double lo, double hi) noexcept { return jsMin (hi, jsMax (lo, v)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline double ease (double dt, double tau) noexcept { return 1 - Js::exp (-dt / tau); }
    inline float f32 (double x) noexcept { return (float) x; }
    /** JS's `%` on doubles: the C library's fmod, which is exact. */
    inline double mod (double a, double b) noexcept { return std::fmod (a, b); }
    /** `x || 1` for a length: 1 where it is 0 or NaN. */
    inline double orOne (double x) noexcept { return std::abs (x) > 0 ? x : 1; }
    inline double sign (double x) noexcept { return x > 0 ? 1 : (x < 0 ? -1 : x); }

    /** Tileable value noise on the unit square: f lattice cells, smooth interpolation. */
    double vnoise (double x, double y, int f, int seed) noexcept
    {
        const double px = x * f, py = y * f, ix = std::floor (px), iy = std::floor (py);
        const double fx = px - ix, fy = py - iy, ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
        const auto h = [f, seed] (double i, double j)
        {
            const int a = (int) i, b = (int) j;
            return UnderwaterWorld::hcell (((a % f) + f) % f, ((b % f) + f) % f, seed);
        };
        return lerp (lerp (h (ix, iy), h (ix + 1, iy), ux), lerp (h (ix, iy + 1), h (ix + 1, iy + 1), ux), uy);
    }
    double fbm (double x, double y, int f0, int octaves, double rough, int seed) noexcept
    {
        double a = 1, sum = 0, norm = 0;
        int f = f0;
        for (int o = 0; o < octaves; ++o) { sum += a * vnoise (x, y, f, seed + o * 7919); norm += a; a *= rough; f *= 2; }
        return sum / norm;
    }
    void normalize01 (std::vector<float>& h) noexcept
    {
        double lo = h[0], hi = h[0];
        for (const float v : h) { if (v < lo) lo = v; if (v > hi) hi = v; }
        const double k = 1 / (hi - lo);
        for (auto& v : h) v = f32 ((v - lo) * k);
    }

    /** A tileable Voronoi filament pattern: bright where two cells meet. */
    void voronoiEdges (int n, int cells, int seed, std::vector<float>& out)
    {
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const double px = (double) x / n * cells, py = (double) y / n * cells, ix = std::floor (px), iy = std::floor (py);
                double f1 = 9, f2 = 9;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i)
                    {
                        const int cx = (((int) ix + i) % cells + cells) % cells, cy = (((int) iy + j) % cells + cells) % cells;
                        const double ox = ix + i + UnderwaterWorld::hcell (cx, cy, seed), oy = iy + j + UnderwaterWorld::hcell (cx, cy, seed + 1);
                        const double d = Js::hypot (px - ox, py - oy);
                        if (d < f1) { f2 = f1; f1 = d; } else if (d < f2) f2 = d;
                    }
                const double e = 1 - smoothstep (0, 0.11, f2 - f1);
                out[(size_t) (y * n + x)] = f32 (std::pow (e, 1.3));
            }
    }

    inline V3 cross (const V3& a, const V3& b) noexcept
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }

    /** A mesh being built: the prototype's `build ()` with its vert, quad, tube, ribbon and cone. */
    struct Builder
    {
        std::vector<double> v;
        std::vector<int> idx;

        int vert (const V3& p, const V3& n, const V3& c, std::array<double, 2> uv, double flag)
        {
            for (const double x : { p[0], p[1], p[2], n[0], n[1], n[2], c[0], c[1], c[2], uv[0], uv[1], flag })
                v.push_back (x);
            return (int) (v.size() / 12) - 1;
        }
        void quad (int a, int b, int c, int d) { for (const int i : { a, b, c, a, c, d }) idx.push_back (i); }

        /** A tube along `pts`, `sides` around, radii and colours per point. */
        template <typename FlagAt>
        void tube (const std::vector<V3>& pts, int sides, const std::vector<double>& radii, const std::vector<V3>& cols, FlagAt flagAt)
        {
            std::vector<std::vector<int>> rings;
            const int np = (int) pts.size();
            for (int k = 0; k < np; ++k)
            {
                const auto& p = pts[(size_t) k];
                const auto& q = pts[(size_t) std::min (k + 1, np - 1)];
                const auto& o = pts[(size_t) std::max (k - 1, 0)];
                const V3 d = UnderwaterWorld::normalised ({ q[0] - o[0], q[1] - o[1], q[2] - o[2] });
                const V3 h = std::abs (d[1]) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
                const V3 s1 = UnderwaterWorld::normalised (cross (d, h));
                const V3 s2 = cross (d, s1);
                std::vector<int> ring;
                for (int i = 0; i < sides; ++i)
                {
                    const double a = (double) i / sides * 2 * pi, ca = Js::cos (a), sa = Js::sin (a);
                    const V3 n { s1[0] * ca + s2[0] * sa, s1[1] * ca + s2[1] * sa, s1[2] * ca + s2[2] * sa };
                    const double r = radii[(size_t) k];
                    ring.push_back (vert ({ p[0] + n[0] * r, p[1] + n[1] * r, p[2] + n[2] * r }, n, cols[(size_t) k],
                                          { (double) i / sides, (double) k / (np - 1) }, flagAt (k)));
                }
                rings.push_back (std::move (ring));
            }
            for (size_t k = 0; k + 1 < rings.size(); ++k)
                for (int i = 0; i < sides; ++i)
                    quad (rings[k][(size_t) i], rings[k][(size_t) ((i + 1) % sides)], rings[k + 1][(size_t) ((i + 1) % sides)], rings[k + 1][(size_t) i]);
        }

        /** A ribbon of `segs` segments from `a` to `b`, width `w0` to `w1`, its side vector `side`. */
        void ribbon (const V3& a, const V3& b, int segs, double w0, double w1, const V3& side, const V3& col0, const V3& col1,
                     double flag, double flagTip)
        {
            std::vector<std::array<int, 2>> rows;
            for (int k = 0; k <= segs; ++k)
            {
                const double t = (double) k / segs;
                const V3 p { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
                const double w = w0 + (w1 - w0) * t;
                const V3 c { col0[0] + (col1[0] - col0[0]) * t, col0[1] + (col1[1] - col0[1]) * t, col0[2] + (col1[2] - col0[2]) * t };
                const V3 d = UnderwaterWorld::normalised ({ b[0] - a[0], b[1] - a[1], b[2] - a[2] });
                const V3 n = UnderwaterWorld::normalised (cross (d, side));
                const double f = k >= segs - 1 ? flagTip : flag;
                const int l = vert ({ p[0] - side[0] * w / 2, p[1] - side[1] * w / 2, p[2] - side[2] * w / 2 }, n, c, { 0, t }, f);
                const int r = vert ({ p[0] + side[0] * w / 2, p[1] + side[1] * w / 2, p[2] + side[2] * w / 2 }, n, c, { 1, t }, f);
                rows.push_back ({ l, r });
            }
            for (int k = 0; k < segs; ++k)
                quad (rows[(size_t) k][0], rows[(size_t) k][1], rows[(size_t) k + 1][1], rows[(size_t) k + 1][0]);
        }

        /** A squat cone: `sides` around, radius r, height h. */
        void cone (double r, double h, int sides, const V3& col, double flag)
        {
            const int top = vert ({ 0, h, 0 }, { 0, 1, 0 }, col, { 0.5, 1 }, flag);
            std::vector<int> ring;
            for (int i = 0; i < sides; ++i)
            {
                const double a = (double) i / sides * 2 * pi;
                ring.push_back (vert ({ r * Js::cos (a), 0, r * Js::sin (a) }, UnderwaterWorld::normalised ({ Js::cos (a), 0.6, Js::sin (a) }),
                                      col, { (double) i / sides, 0 }, flag));
            }
            for (int i = 0; i < sides; ++i)
                for (const int k : { ring[(size_t) i], top, ring[(size_t) ((i + 1) % sides)] })
                    idx.push_back (k);
        }

        UnderwaterWorld::Mesh done() const
        {
            UnderwaterWorld::Mesh m;
            for (const double x : v) m.v.push_back (f32 (x));
            for (const int i : idx) m.idx.push_back ((uint16_t) i);
            return m;
        }
    };

    // The jellyfish's five palettes: the bell, the lantern's glow.
    constexpr std::array<std::array<V3, 2>, 5> palette { {
        { { { 0.95, 0.25, 0.65 }, { 0.30, 1.00, 0.90 } } },
        { { { 0.25, 0.85, 0.95 }, { 1.00, 0.60, 0.20 } } },
        { { { 0.55, 0.95, 0.35 }, { 1.00, 0.30, 0.80 } } },
        { { { 1.00, 0.65, 0.25 }, { 0.45, 0.55, 1.00 } } },
        { { { 0.75, 0.45, 1.00 }, { 0.60, 1.00, 0.50 } } },
    } };

    inline double wrapBox (double d, double L) noexcept { return mod (mod (d, L) + L + L / 2, L) - L / 2; }
    inline double wrapWorld (double v) noexcept
    {
        constexpr double P = UnderwaterWorld::worldPeriod;
        return mod (mod (v, P) + P, P);
    }
}

//==============================================================================
uint32_t UnderwaterWorld::ih (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

double UnderwaterWorld::hcell (int32_t cx, int32_t cy, int32_t s) noexcept
{
    const uint32_t inner = ih (((uint32_t) cy * 0x85EBCA77u) ^ (uint32_t) s);
    return (double) f32 ((double) ih (((uint32_t) cx * 0x9E3779B1u) ^ inner) / 4294967296.0);
}

double UnderwaterWorld::hs (int32_t i, int32_t s) noexcept
{
    const uint32_t inner = ih ((uint32_t) s * 0x85EBCA77u);
    return (double) f32 ((double) ih (((uint32_t) i * 0x9E3779B1u) ^ inner) / 4294967296.0);
}

UnderwaterWorld::Basis UnderwaterWorld::cameraBasis (double heading, double pitch) noexcept
{
    const double cp = Js::cos (pitch), sp = Js::sin (pitch), f0x = Js::sin (heading), f0z = -Js::cos (heading);
    return { { Js::cos (heading), 0, Js::sin (heading) }, { -f0x * sp, cp, -f0z * sp }, { f0x * cp, sp, f0z * cp } };
}

UnderwaterWorld::V3 UnderwaterWorld::normalised (const V3& v) noexcept
{
    const double l = orOne (Js::hypot (v[0], v[1], v[2]));
    return { v[0] / l, v[1] / l, v[2] / l };
}

//==============================================================================
std::vector<float> UnderwaterWorld::makeHeight()
{
    const int n = texSize, nn = n * n;
    std::vector<float> R ((size_t) nn), G ((size_t) nn), B ((size_t) nn), A ((size_t) nn);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n, v = (double) y / n;
            const auto i = (size_t) (y * n + x);
            R[i] = f32 (fbm (u, v, 2, 4, 0.5, 101));
            G[i] = f32 (fbm (u, v, 3, 3, 0.55, 202));
            B[i] = f32 (fbm (u, v, 3, 3, 0.5, 303));
            const double r = fbm (u, v, 2, 2, 0.5, 404);
            A[i] = f32 (1 - std::abs (2 * r - 1));
        }
    normalize01 (R); normalize01 (G); normalize01 (B); normalize01 (A);
    std::vector<float> H ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i) { H[i * 4] = R[i]; H[i * 4 + 1] = G[i]; H[i * 4 + 2] = B[i]; H[i * 4 + 3] = A[i]; }
    return H;
}

std::vector<uint8_t> UnderwaterWorld::makeDetail()
{
    const int d = detailSize, m = d - 1;
    std::vector<float> v ((size_t) (d * d));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double u = (double) x / d, w = (double) y / d;
            const double warp = fbm (u, w, 3, 3, 0.5, 505);
            const double ripple = 0.5 + 0.5 * Js::sin (2 * pi * (u * 7 + w * 1.0 + 0.9 * warp));
            const double grit = fbm (u, w, 16, 3, 0.5, 606);
            v[(size_t) (y * d + x)] = f32 (0.65 * ripple * ripple + 0.35 * grit);
        }
    normalize01 (v);
    std::vector<uint8_t> px ((size_t) (d * d * 4));
    const auto at = [&v] (int y, int x) { return (double) v[(size_t) (y * d + x)]; };
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double gx = (at (y, (x + 1) & m) - at (y, (x - 1) & m)) * 3;
            const double gy = (at ((y + 1) & m, x) - at ((y - 1) & m, x)) * 3;
            const auto i = (size_t) ((y * d + x) * 4);
            px[i]     = (uint8_t) (int) std::trunc (128 + clamp (gx * 127, -127, 127));
            px[i + 1] = (uint8_t) (int) std::trunc (128 + clamp (gy * 127, -127, 127));
            px[i + 2] = (uint8_t) (int) std::trunc (at (y, x) * 255);
            px[i + 3] = 255;
        }
    return px;
}

UnderwaterWorld::Caustic UnderwaterWorld::makeCaustic()
{
    const int n = causticSize, nn = n * n;
    std::vector<float> R ((size_t) nn), G ((size_t) nn), B ((size_t) nn), A ((size_t) nn);
    voronoiEdges (n, 9, 707, R);
    voronoiEdges (n, 7, 808, G);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n, v = (double) y / n;
            const auto i = (size_t) (y * n + x);
            B[i] = f32 (fbm (u, v, 3, 3, 0.5, 909));
            A[i] = f32 (fbm (u, v, 3, 4, 0.55, 1010));
        }
    normalize01 (B); normalize01 (A);
    for (auto& a : A) a = f32 (smoothstep (0.3, 0.8, a));
    normalize01 (A);
    double mr = 0, mg = 0;
    for (size_t i = 0; i < (size_t) nn; ++i) { mr += R[i]; mg += G[i]; }
    mr /= nn; mg /= nn;
    Caustic c;
    c.px.resize ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i)
    {
        c.px[i * 4]     = (uint8_t) (int) std::trunc ((double) R[i] * 255); c.px[i * 4 + 1] = (uint8_t) (int) std::trunc ((double) G[i] * 255);
        c.px[i * 4 + 2] = (uint8_t) (int) std::trunc ((double) B[i] * 255); c.px[i * 4 + 3] = (uint8_t) (int) std::trunc ((double) A[i] * 255);
    }
    c.gain = 1 / (mr * mg);
    return c;
}

//==============================================================================
std::vector<float> UnderwaterWorld::icosphere (int seed)
{
    const double t = (1 + std::sqrt (5.0)) / 2;
    std::vector<V3> V;
    for (const V3& p : std::initializer_list<V3> { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                                                  { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } })
        V.push_back (normalised (p));
    const int F[20][3] { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                         { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    std::map<int, int> cache;
    const auto mid = [&] (int a, int b)
    {
        const int key = a < b ? a * 4096 + b : b * 4096 + a;
        if (const auto it = cache.find (key); it != cache.end())
            return it->second;
        V.push_back (normalised ({ (V[(size_t) a][0] + V[(size_t) b][0]) / 2, (V[(size_t) a][1] + V[(size_t) b][1]) / 2, (V[(size_t) a][2] + V[(size_t) b][2]) / 2 }));
        cache[key] = (int) V.size() - 1;
        return (int) V.size() - 1;
    };
    std::vector<std::array<int, 3>> F2;
    for (const auto& f : F)
    {
        const int a = mid (f[0], f[1]), b = mid (f[1], f[2]), c = mid (f[2], f[0]);
        F2.push_back ({ f[0], a, c }); F2.push_back ({ f[1], b, a }); F2.push_back ({ f[2], c, b }); F2.push_back ({ a, b, c });
    }
    std::vector<V3> lumpy;
    for (size_t i = 0; i < V.size(); ++i)
    {
        const double k = 0.78 + 0.44 * hs ((int) i + 1, seed);
        lumpy.push_back ({ V[i][0] * k, V[i][1] * k, V[i][2] * k });
    }
    std::vector<float> out;
    for (const auto& f : F2)
    {
        const auto& a = lumpy[(size_t) f[0]]; const auto& b = lumpy[(size_t) f[1]]; const auto& c = lumpy[(size_t) f[2]];
        const V3 n = normalised ({ (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]), (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                                   (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) });
        for (const V3* v : { &a, &b, &c })
            for (const double x : { (*v)[0], (*v)[1], (*v)[2], n[0], n[1], n[2] })
                out.push_back (f32 (x));
    }
    return out;
}

UnderwaterWorld::Mesh UnderwaterWorld::tubeMesh (int sides, int rings)
{
    Mesh m;
    for (int r = 0; r <= rings; ++r)
        for (int k = 0; k <= sides; ++k)
        {
            const double a = (double) k / sides * 2 * pi;
            m.v.push_back (f32 (Js::cos (a))); m.v.push_back (f32 ((double) r / rings)); m.v.push_back (f32 (Js::sin (a)));
        }
    const int W = sides + 1;
    for (int r = 0; r < rings; ++r)
        for (int k = 0; k < sides; ++k)
        {
            const int o = r * W + k;
            for (const int i : { o, o + 1, o + W, o + 1, o + W + 1, o + W })
                m.idx.push_back ((uint16_t) i);
        }
    return m;
}

std::array<UnderwaterWorld::Mesh, 5> UnderwaterWorld::floraMeshes()
{
    std::array<Mesh, 5> out;
    {   // 0: the fan — a flat sheet, 8 x 8, slightly cupped; the outline and the lace are cut in the fragment shader
        Builder m;
        constexpr int G = 8;
        std::vector<std::vector<int>> rows;
        for (int j = 0; j <= G; ++j)
        {
            std::vector<int> row;
            for (int i = 0; i <= G; ++i)
            {
                const double x = (double) i / G - 0.5, y = (double) j / G;
                row.push_back (m.vert ({ x, y, 0.06 * Js::sin (x * pi) * y }, { 0, 0, 1 }, { 0.55, 0.22, 0.68 }, { (double) i / G, y }, 1));
            }
            rows.push_back (std::move (row));
        }
        for (size_t j = 0; j < G; ++j)
            for (size_t i = 0; i < G; ++i)
                m.quad (rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]);
        out[0] = m.done();
    }
    {   // 1: the bush — a trunk, seven branches, two twigs each, glowing tips
        Builder m;
        const V3 body { 0.85, 0.45, 0.18 }, tip { 1.0, 0.72, 0.32 };
        m.tube ({ { 0, 0, 0 }, { 0, 0.3, 0 } }, 4, { 0.07, 0.05 }, { body, body }, [] (int) { return 0.0; });
        for (int i = 0; i < 7; ++i)
        {
            const double az = (double) i / 7 * 2 * pi + 0.4 * hs (i + 1, 301), el = 0.55 + 0.35 * hs (i + 1, 302);
            const V3 d { Js::cos (az) * Js::cos (el), Js::sin (el), Js::sin (az) * Js::cos (el) };
            const double L = 0.55 + 0.2 * hs (i + 1, 303);
            const std::vector<V3> pts { { 0, 0.28, 0 }, { d[0] * L * 0.5, 0.28 + d[1] * L * 0.5, d[2] * L * 0.5 }, { d[0] * L, 0.28 + d[1] * L * 1.1, d[2] * L } };
            m.tube (pts, 3, { 0.045, 0.03, 0.012 }, { body, body, tip }, [] (int k) { return k == 2 ? 11.0 : 1.0; });
            for (int t = 0; t < 2; ++t)
            {
                const double az2 = az + (t ? 0.9 : -0.9), el2 = el + 0.3;
                const V3 d2 { Js::cos (az2) * Js::cos (el2), Js::sin (el2), Js::sin (az2) * Js::cos (el2) };
                const V3 base = pts[1];
                const double L2 = 0.28;
                m.tube ({ base, { base[0] + d2[0] * L2, base[1] + d2[1] * L2, base[2] + d2[2] * L2 } }, 3, { 0.025, 0.01 }, { body, tip },
                        [] (int k) { return k == 1 ? 11.0 : 1.0; });
            }
        }
        out[1] = m.done();
    }
    {   // 2: the anemone — a squat base and fourteen tentacles that wave and curl shut
        Builder m;
        m.cone (0.32, 0.14, 8, { 0.18, 0.46, 0.46 }, 0);
        for (int i = 0; i < 14; ++i)
        {
            const double a = (double) i / 14 * 2 * pi + 0.2 * hs (i + 1, 311), r0 = 0.08 + 0.16 * hs (i + 1, 312);
            const double el = 0.75 + 0.5 * hs (i + 1, 313), L = 0.6 + 0.3 * hs (i + 1, 314);
            const V3 from { r0 * Js::cos (a), 0.12, r0 * Js::sin (a) };
            const V3 to { from[0] + Js::cos (a) * Js::cos (el) * L, from[1] + Js::sin (el) * L, from[2] + Js::sin (a) * Js::cos (el) * L };
            m.ribbon (from, to, 5, 0.07, 0.025, { -Js::sin (a), 0, Js::cos (a) }, { 0.5, 0.28, 0.78 }, { 0.95, 0.5, 0.85 }, 2, 12);
        }
        out[2] = m.done();
    }
    {   // 3: the kelp — one tall ribbon, amber-edged in the fragment shader
        Builder m;
        m.ribbon ({ 0, 0, 0 }, { 0, 1, 0 }, 8, 0.085, 0.045, { 1, 0, 0 }, { 0.42, 0.66, 0.72 }, { 0.55, 0.78, 0.82 }, 1, 1);
        out[3] = m.done();
    }
    {   // 4: the polyp — a tiny glowing cone, cyan or pink by its cell
        Builder m;
        m.cone (0.09, 0.12, 6, { 0.35, 1.0, 0.9 }, 10);
        out[4] = m.done();
    }
    return out;
}

UnderwaterWorld::Mesh UnderwaterWorld::bellMesh (int segsU, int segsV)
{
    Mesh m;
    for (int j = 0; j <= segsV; ++j)
        for (int i = 0; i <= segsU; ++i) { m.v.push_back (f32 ((double) i / segsU)); m.v.push_back (f32 ((double) j / segsV)); }
    const int W = segsU + 1;
    for (int j = 0; j < segsV; ++j)
        for (int i = 0; i < segsU; ++i)
        {
            const int o = j * W + i;
            for (const int k : { o, o + 1, o + W, o + 1, o + W + 1, o + W })
                m.idx.push_back ((uint16_t) k);
        }
    return m;
}

UnderwaterWorld::Mesh UnderwaterWorld::tendrilMesh (int count, int segs)
{
    Mesh m;
    for (int j = 0; j < count; ++j)
    {
        const int base = (int) (m.v.size() / 3);
        for (int k = 0; k <= segs; ++k)
            for (const double side : { 0.0, 1.0 })
            {
                m.v.push_back (f32 ((double) k / segs)); m.v.push_back ((float) side); m.v.push_back ((float) j);
            }
        for (int k = 0; k < segs; ++k)
        {
            const int o = base + k * 2;
            for (const int i : { o, o + 1, o + 2, o + 1, o + 3, o + 2 })
                m.idx.push_back ((uint16_t) i);
        }
    }
    return m;
}

//==============================================================================
void UnderwaterWorld::startAt (double x, double z, double heading) noexcept
{
    s.x = x; s.z = z; s.heading = heading; s.lookHeading = heading;
}

void UnderwaterWorld::build()
{
    if (built)
        return;
    heightPx = makeHeight();
    detailPx = makeDetail();
    caustic_ = makeCaustic();
    makeJellies();
    built = true;
}

void UnderwaterWorld::makeJellies()
{
    jelly.clear();
    for (int i = 0; i < jellyCount; ++i)
    {
        const auto h = [i] (int k) { return hs (i + 1, k); };
        Jelly j;
        j.giant = h (5) > 0.93;
        j.scale = (0.32 + 1.3 * h (4) * h (4)) * (j.giant ? 1.8 : 1);
        const auto& pal = palette[(size_t) jsMin (4, std::floor (h (6) * 5))];
        const V3 ax { (h (8) - 0.5) * 0.8, 1, (h (9) - 0.5) * 0.8 };
        const double al = Js::hypot (ax[0], ax[1], ax[2]);
        j.ax = h (1) * jellyBox; j.az = h (2) * jellyBox;
        j.alt = 0.5 + 7 * std::pow (h (3), 1.5) * (j.giant ? 1.4 : 1);
        j.col = pal[0]; j.glow = pal[1];
        j.omega = (0.9 + 0.5 * h (7)) / std::sqrt (j.scale);
        j.phase = h (10) * 2 * pi;
        j.axis = { ax[0] / al, ax[1] / al, ax[2] / al };
        j.tend = 0.7 + 0.7 * h (11);
        j.curiousW = h (12) < 0.6 && ! j.giant ? 1 : 0;
        j.flinchK = 3 + 4 * h (13); j.near = 2.4 + 3 * h (14); j.side = (h (15) - 0.5) * 7; j.lift = (h (16) - 0.35) * 2.2;
        j.vx = j.axis[0] * 0.14; j.vz = j.axis[2] * 0.14;
        j.id = i;
        jelly.push_back (j);
    }
}

double UnderwaterWorld::tap (int ch, double u, double v) const noexcept
{
    // Bilinear, wrapping, as GL_LINEAR + REPEAT.
    constexpr int n = texSize, m = n - 1;
    const double x = u * n - 0.5, y = v * n - 0.5, x0 = std::floor (x), y0 = std::floor (y);
    const double fx = x - x0, fy = y - y0;
    const auto i32 = [] (double d) { return (int32_t) (int64_t) d; };   // ToInt32 of an integer well within range
    const int i0 = i32 (x0) & m, i1 = (i32 (x0) + 1) & m, j0 = i32 (y0) & m, j1 = (i32 (y0) + 1) & m;
    const auto at = [this, ch] (int j, int i) { return (double) heightPx[(size_t) ((j * n + i) * 4 + ch)]; };
    const double a = at (j0, i0), b = at (j0, i1), c = at (j1, i0), d = at (j1, i1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

double UnderwaterWorld::floorHeight (double px, double pz) const noexcept
{
    if (! built)
        return 0;
    const double ar = tap (0, px / 420, pz / 420), ag = tap (1, px / 420, pz / 420);
    const double bb = tap (2, px / 96, pz / 96);
    const double cr = tap (0, px / 27, pz / 27), ca = tap (3, px / 27, pz / 27);
    const double swell = (ar - 0.5) * 10, dunes = (bb - 0.5) * 2.6;
    const double mounds = smoothstep (0.55, 0.95, ca) * 1.8 * smoothstep (0.45, 0.75, ag);
    return swell + dunes + mounds + (cr - 0.5) * 0.25;
}

double UnderwaterWorld::speedFor (int g, bool t) const noexcept
{
    return gearSpeed[(size_t) (std::clamp (g, 1, numGears) - 1)] * (t ? turboFactor : 1);
}

void UnderwaterWorld::setGear (int g) noexcept       { gearWanted = std::clamp (g, 1, numGears); }
void UnderwaterWorld::setSteer (double v) noexcept   { steer = v != v ? 0 : clamp (v, -1, 1); }
void UnderwaterWorld::settle() noexcept              { s.speed = parked ? 0 : speedFor (gearWanted, turbo); }
void UnderwaterWorld::hit (double level) noexcept    { s.hitT = 0; s.hitS = level != level ? 0 : clamp (level, 0, 1); }
void UnderwaterWorld::setPitchOffset (double rad) noexcept { s.pitchOffset = rad != rad ? 0 : clamp (rad, -0.5, 0.5); }

//==============================================================================
void UnderwaterWorld::step (double dt)
{
    s.time += dt; s.dt = dt; s.hitT += dt;
    const bool driving = ! parked;
    const double target = driving ? speedFor (gearWanted, turbo) : 0;
    s.speed += (target - s.speed) * ease (dt, parked ? parkSeconds : speedSeconds);
    const bool thrustOn = driving && turbo;
    const double on = thrustOn ? 1 : 0;
    s.silt   += (on - s.silt)   * ease (dt, thrustOn ? siltUp : siltDown);
    s.flinch += (on - s.flinch) * ease (dt, thrustOn ? flinchUp : flinchDown);
    s.thrust += (on - s.thrust) * ease (dt, thrustOn ? thrustUp : thrustDown);
    s.curious += ((parked ? 1 : 0) - s.curious) * ease (dt, parked ? curiousUp : curiousDown);

    // Yaw: the hand wins; a gentle meander after 2 s without it. Parked, the hand turns the view in place.
    double yawTarget;
    if (parked) { yawTarget = steer * parkedYawDegrees * pi / 180; lastSteerTime = s.time; }
    else if (std::abs (steer) > 0.001) { yawTarget = steer * 28 * pi / 180; lastSteerTime = s.time; }
    else yawTarget = smoothstep (2, 5, s.time - lastSteerTime) * 3.5 * pi / 180 * Js::sin (s.time * 0.09);
    yawRate += (yawTarget - yawRate) * ease (dt, 0.35);
    s.heading += yawRate * dt;

    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    s.lookHeading += (s.heading - s.lookHeading) * ease (dt, 3.5);    // where the curious jellyfish think we look
    s.x += fx * s.speed * dt; s.z += fz * s.speed * dt; s.travel += s.speed * dt;
    s.x = wrapWorld (s.x);
    s.z = wrapWorld (s.z);

    // The camera: low-passed floor height plus a little heave; the pitch follows the slope slowly.
    const double g0 = floorHeight (s.x, s.z), g1 = floorHeight (s.x + fx * 4, s.z + fz * 4);
    const double targetY = jsMax (g0, g1) + camHeight;
    if (std::fpclassify (s.camY) == FP_ZERO) s.camY = targetY;
    s.camY += (targetY - s.camY) * ease (dt, 0.6);
    s.camY = jsMax (s.camY, g0 + 1.1);
    const double gb = floorHeight (s.x - fx * 4, s.z - fz * 4), gf = floorHeight (s.x + fx * 10, s.z + fz * 10);
    const double pitchT = basePitch + s.pitchOffset + clamp (Js::atan2 (gf - gb, 14) * 0.7, -0.15, 0.15);
    s.pitch += (pitchT - s.pitch) * ease (dt, 1.0);

    // Another bathyscaphe, now and then, that never comes to meet the rover (Oleh, 16.11): it appears off the
    // screen's edge on the side the rover is about to turn toward (58-64 degrees off the way), 26-32 m off, and
    // always goes AWAY - from the rover and on ahead of its way - a little faster than the rover (0.5 m/s), dashing
    // off (3 m/s more) inside 22 m. It turns at most 25 degrees a second (the sprite's headings blend), is seen
    // between 14 and 70 m (fading by 9 and 85 m, and over its first 6 s), and is gone once past 85 m; the next
    // comes a minute later.
    auto& sb = s.sub;
    if (! sb.on && s.time >= subNext)
    {
        const int pn = subCount++;
        // the hand's side if it steers, else where the meander (sin 0.09 t) goes in the next few seconds
        const double side = std::abs (steer) > 0.05 ? sign (steer) : (Js::sin ((s.time + 6) * 0.09) < 0 ? -1 : 1);
        const double dist = lerp (26, 32, hs (pn, 92)), v = lerp (2.0, 2.8, hs (pn, 93));
        const double bear = lerp (58, 64, hs (pn, 94)) * pi / 180 * side;           // at the 54 degree half-view's edge
        const double rx = Js::cos (s.heading), rz = Js::sin (s.heading);
        const double ox = fx * Js::cos (bear) + rx * Js::sin (bear), oz = fz * Js::cos (bear) + rz * Js::sin (bear);
        sb.on = true; sb.n = pn; sb.v = v; sb.v0 = v; sb.h = lerp (2.5, 6.0, hs (pn, 95)); sb.t = 0; sb.vis = 0; sb.d = 1e9;
        sb.x = s.x + ox * dist; sb.z = s.z + oz * dist; sb.fx = ox + fx * 1.4; sb.fz = oz + fz * 1.4;
        const double l0 = Js::hypot (sb.fx, sb.fz);
        sb.fx /= l0; sb.fz /= l0;
        sb.y = floorHeight (sb.x, sb.z) + sb.h;
    }
    if (sb.on)
    {
        double ddx = sb.x - s.x, ddz = sb.z - s.z;
        ddx -= Js::round (ddx / worldPeriod) * worldPeriod; ddz -= Js::round (ddz / worldPeriod) * worldPeriod;
        const double dNow = jsMax (Js::hypot (ddx, ddz), 0.01);
        // away from the rover, leaning on ahead along the rover's way (less so the nearer it is)
        const double near = smoothstep (22, 12, dNow);
        double wx = ddx / dNow + fx * 1.4 * (1 - near), wz = ddz / dNow + fz * 1.4 * (1 - near);
        const double wl = orOne (Js::hypot (wx, wz));
        wx /= wl; wz /= wl;
        const double cur = Js::atan2 (sb.fz, sb.fx), want = Js::atan2 (wz, wx);
        const double dA = mod (want - cur + 3 * pi, 2 * pi) - pi, turn = 25 * pi / 180 * dt;
        const double na = cur + clamp (dA, -turn, turn);
        sb.fx = Js::cos (na); sb.fz = Js::sin (na);
        sb.v += (jsMax (sb.v0, s.speed + 0.5) + 3.0 * near - sb.v) * ease (dt, 1.5);
        sb.t += dt; sb.x += sb.fx * sb.v * dt; sb.z += sb.fz * sb.v * dt;
        sb.x = wrapWorld (sb.x);
        sb.z = wrapWorld (sb.z);
        sb.y += (floorHeight (sb.x, sb.z) + sb.h - sb.y) * ease (dt, 3.0);
        sb.vis = smoothstep (85, 70, dNow) * smoothstep (9, 14, dNow) * smoothstep (0, 6, sb.t);
        if (sb.t > 2 && dNow > 85 && dNow > sb.d) { sb.on = false; subNext = s.time + subAfter; }   // lost, going away
        sb.d = dNow;
    }

    // The particle flows, in box units: the snow in the world; the silt and the bubbles in the rover's frame.
    s.snowFlow[0] += 0.12 * dt / snowBox[0]; s.snowFlow[1] -= 0.07 * dt / snowBox[1]; s.snowFlow[2] += 0.04 * dt / snowBox[2];
    // The silt: streams back with the way (z), rises while the thrusters run and sinks when they stop (y, in rises).
    const double rise = lerp (-0.35, 0.8, s.thrust);
    s.siltFlow[2] -= (s.speed * 0.9 + 0.3) * dt / siltBox[2]; s.siltFlow[1] += rise * dt / siltBox[1];
    s.bubbleFlow[1] += dt / 2.2;                                   // a plume bubble's life: 2.2 s from the thruster to past the glass
    // The particles' paces are multiples of 1/64, so a wrap at 4096 moves nothing (fract each frame did, every unit).
    for (auto* fl : { &s.snowFlow, &s.siltFlow, &s.bubbleFlow })
        for (auto& k : *fl) { if (k > 4096) k -= 4096; else if (k < -4096) k += 4096; }
}

void UnderwaterWorld::updateJellies (double dt)
{
    const double t = s.time;
    const Basis lookB = cameraBasis (s.lookHeading, s.pitch);
    for (auto& j : jelly)
    {
        j.phase += j.omega * (1 + 2.5 * s.flinch) * (1 - 0.3 * s.curious) * dt;
        const double c = (0.5 + 0.5 * Js::sin (j.phase)); const double contraction = c * c * c;
        const double wx = j.ax + j.vx * t + 1.5 * Js::sin (t * 0.13 + j.phase * 0.01 + j.ax);
        const double wz = j.az + j.vz * t + 1.5 * Js::cos (t * 0.11 + j.az);
        double dx = wrapBox (wx - s.x, jellyBox), dz = wrapBox (wz - s.z, jellyBox);
        const double floorY = floorHeight (s.x + dx, s.z + dz);
        const double y = floorY + j.alt + 0.6 * Js::sin (t * 0.21 + j.az) + 0.25 * contraction * j.scale;
        double dy = y - s.camY;
        const double dist0 = Js::hypot (dx, dy, dz);
        // The flinch: away from the rover, the near ones most.
        const double fl = s.flinch * j.flinchK * smoothstep (45, 6, dist0);
        if (fl > 0)
        {
            const double L = jsMax (1e-3, Js::hypot (dx, dz));
            dx += dx / L * fl; dz += dz / L * fl; dy += 0.35 * fl;
        }
        // The curiosity: parked, the near ones glide in to look through the glass - where the window looked a few
        // seconds ago (lookHeading lags the heading by 3.5 s), so turning in place they drift after the view
        // instead of turning with it as if stuck to the glass (Oleh, 16.11).
        V3 axis = j.axis;
        const double w = s.curious * j.curiousW * smoothstep (50, 12, dist0);
        if (w > 0)
        {
            const double tx = lookB.f[0] * j.near + lookB.r[0] * j.side + lookB.u[0] * j.lift;
            const double ty = lookB.f[1] * j.near + lookB.r[1] * j.side + lookB.u[1] * j.lift;
            const double tz = lookB.f[2] * j.near + lookB.r[2] * j.side + lookB.u[2] * j.lift;
            dx = lerp (dx, tx, w); dy = lerp (dy, ty, w); dz = lerp (dz, tz, w);
            const double L = jsMax (1e-3, Js::hypot (dx, dy, dz));
            const V3 look { -dx / L, -dy / L + 0.3, -dz / L };
            const double LL = Js::hypot (look[0], look[1], look[2]);
            axis = { lerp (j.axis[0], look[0] / LL, 0.6 * w), lerp (j.axis[1], look[1] / LL, 0.6 * w), lerp (j.axis[2], look[2] / LL, 0.6 * w) };
            const double AL = Js::hypot (axis[0], axis[1], axis[2]);
            axis = { axis[0] / AL, axis[1] / AL, axis[2] / AL };
        }
        const double minY = floorHeight (s.x + dx, s.z + dz) + 0.9 * j.scale + 0.3 - s.camY;
        if (dy < minY) dy = minY;
        const double minD = j.giant ? 7.0 : 1.0 + 1.3 * j.scale, dNow = Js::hypot (dx, dy, dz);
        if (dNow < minD) { const double k = minD / jsMax (dNow, 1e-3); dx *= k; dy *= k; dz *= k; }
        j.rel = { dx, dy, dz };
        j.dist = Js::hypot (dx, dy, dz);
        j.outAxis = axis;
    }
    // Far to near, as the prototype's sort (V8's is stable) leaves them.
    std::stable_sort (jelly.begin(), jelly.end(), [] (const Jelly& a, const Jelly& b) { return b.dist - a.dist < 0; });
}

//==============================================================================
UnderwaterWorld::Sprites UnderwaterWorld::parseSprites (std::string_view text)
{
    // sprites.txt: the scale, the cell size, and per cell its lamps (lens x y px, beam dx dy, toward the viewer).
    Sprites out;
    const auto number = [] (const std::string& w)
    {
        std::istringstream in (w);
        in.imbue (std::locale::classic());
        double v = 0;
        in >> v;
        return in.fail() ? nan : v;
    };
    while (! text.empty())
    {
        const auto end = text.find ('\n');
        const std::string line (text.substr (0, end));
        text.remove_prefix (end == std::string_view::npos ? text.size() : end + 1);

        std::istringstream words (line);
        words.imbue (std::locale::classic());
        std::vector<std::string> w;
        for (std::string x; words >> x;)
            w.push_back (x);
        if (line.rfind ('#', 0) == 0 && line.find ("px per metre") != std::string::npos)
        {
            // "cells W x H px, S px per metre" and "atlas C x R"
            int cw = 0, ch = 0, gc = 0, gr = 0;
            double scale = 0;
            if (const auto at = line.find ("cells "); at != std::string::npos
                && std::sscanf (line.c_str() + at, "cells %d x %d px, %lf px per metre", &cw, &ch, &scale) == 3)
            {
                out.cell = { (double) cw, (double) ch };
                out.scale = scale;
            }
            if (const auto at = line.find ("atlas "); at != std::string::npos
                && std::sscanf (line.c_str() + at, "atlas %d x %d", &gc, &gr) == 2)
                out.grid = { gc, gr };
        }
        else if (! w.empty() && w[0] == "cell")
            out.cells.emplace_back();
        else if (! w.empty() && w[0] == "lamp" && ! out.cells.empty() && w.size() >= 7)
            out.cells.back().push_back ({ number (w[2]), number (w[3]), number (w[4]), number (w[5]), number (w[6]) });
    }
    return out;
}

UnderwaterWorld::Glass UnderwaterWorld::Glass::of (const uint8_t* rgba, int w, int h, int step)
{
    Glass g;
    if (rgba == nullptr || w <= 0 || h <= 0 || step <= 0)
        return g;
    g.width = (w + step - 1) / step;
    g.height = (h + step - 1) / step;
    g.alpha.resize ((size_t) g.width * (size_t) g.height);
    for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x)
            g.alpha[(size_t) (y * g.width + x)] = rgba[((size_t) (y * step) * (size_t) w + (size_t) (x * step)) * 4 + 3];
    return g;
}

bool UnderwaterWorld::Glass::open (float x01, float y01) const noexcept
{
    if (alpha.empty())
        return true;
    const int x = std::clamp ((int) (x01 * (float) width), 0, width - 1), y = std::clamp ((int) (y01 * (float) height), 0, height - 1);
    return alpha[(size_t) (y * width + x)] < 128;    // more than half see-through: the window, not the metal
}

UnderwaterWorld::PanelMask UnderwaterWorld::PanelMask::of (const uint8_t* rgba, int w, int h, int block, int coverMargin, int openMargin)
{
    PanelMask m;
    if (rgba == nullptr || w <= 0 || h <= 0 || block <= 0)
        return m;
    m.block = block; m.pictureWidth = w; m.pictureHeight = h;
    m.width = (w + block - 1) / block;
    m.height = (h + block - 1) / block;
    // Each block's least and greatest alpha...
    const size_t n = (size_t) m.width * (size_t) m.height;
    std::vector<uint8_t> lo (n, 255), hi (n, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const uint8_t a = rgba[((size_t) y * (size_t) w + (size_t) x) * 4 + 3];
            const size_t b = (size_t) ((y / block) * m.width + x / block);
            lo[b] = std::min (lo[b], a); hi[b] = std::max (hi[b], a);
        }
    // ...then over the blocks the margin reaches (beyond the picture's edge there is nothing: a sampler clamps there).
    const int rc = (coverMargin + block - 1) / block, ro = (openMargin + block - 1) / block;
    m.coverMargin = rc * block; m.openMargin = ro * block;
    m.cells.assign (n, neither);
    for (int by = 0; by < m.height; ++by)
        for (int bx = 0; bx < m.width; ++bx)
        {
            bool opaque = true, bare = true;
            for (int y = std::max (0, by - std::max (rc, ro)); y <= std::min (m.height - 1, by + std::max (rc, ro)); ++y)
                for (int x = std::max (0, bx - std::max (rc, ro)); x <= std::min (m.width - 1, bx + std::max (rc, ro)); ++x)
                {
                    const size_t b = (size_t) (y * m.width + x);
                    if (std::abs (x - bx) <= rc && std::abs (y - by) <= rc && lo[b] < 255) opaque = false;
                    if (std::abs (x - bx) <= ro && std::abs (y - by) <= ro && hi[b] > 1)   bare = false;
                }
            m.cells[(size_t) (by * m.width + bx)] = opaque ? covered : bare ? open : neither;
        }
    return m;
}

} // namespace orbitamp::underwater
