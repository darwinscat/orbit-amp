// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of metal-world.js, line for line: the hashes and the textures, the loop, makeWorld's places, the drive, the
// lightning, the ball lightning and the dragons; and what metal-gl.js works out on the CPU for a frame (the fires, the
// ribbons). Every expression keeps the prototype's order of operations, so that the doubles round where the JS's
// round; f32 where it stores into a Float32Array or calls Math.fround. The meshes and the finds' readers are in
// MetalFinds.cpp.

#include "MetalWorld.h"
#include "MetalJs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>

namespace orbitamp::metal
{

using namespace js;
using V2 = MetalWorld::V2;
using V3 = MetalWorld::V3;

namespace
{
    /** a == b, as the JS's === compares two numbers (std::equal_to: the same comparison, said where -Wfloat-equal
        does not take it for an accident). */
    inline bool same (double a, double b) noexcept { return std::equal_to<double>() (a, b); }
    /** The small 32-bit generator whose stream is the landscape (the rogue planet's). */
    struct Generator
    {
        uint32_t a;
        double operator()() noexcept
        {
            a = a + 0x6D2B79F5u;
            uint32_t t = (a ^ (a >> 15)) * (1u | a);
            t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
            return (double) (t ^ (t >> 14)) / 4294967296.0;
        }
    };
    /** The recursive-subdivision fractal on a wrapping n x n grid, in float (rogue-world.js's plasma). */
    std::vector<float> plasma (int n, uint32_t seed, double roughIn)
    {
        std::vector<float> h ((size_t) (n * n), 0.0f);
        Generator next { seed };
        const int m = n - 1;
        const double rough = f32 (roughIn);
        double amp = 1;
        const auto noise = [&] { return f32 (f32 (f32 (next()) * 2.0 - 1) * amp); };
        const auto H = [&] (int i) { return (double) h[(size_t) i]; };
        for (int step = n; step > 1; step >>= 1)
        {
            const int half = step >> 1;
            for (int y = 0; y < n; y += step)
                for (int x = 0; x < n; x += step)
                {
                    const int x1 = (x + step) & m, y1 = (y + step) & m;
                    const double sum = f32 (f32 (f32 (H (y * n + x) + H (y * n + x1)) + H (y1 * n + x)) + H (y1 * n + x1));
                    const double v = 0.25 * sum;
                    h[(size_t) ((y + half) * n + x + half)] = f32 (v + noise());
                }
            for (int y = 0; y < n; y += half)
                for (int x = ((y / half) & 1) ? 0 : half; x < n; x += step)
                {
                    const int xm = (x - half) & m, xp = (x + half) & m, ym = (y - half) & m, yp = (y + half) & m;
                    const double sum = f32 (f32 (f32 (H (y * n + xm) + H (y * n + xp)) + H (ym * n + x)) + H (yp * n + x));
                    const double v = 0.25 * sum;
                    h[(size_t) (y * n + x)] = f32 (v + noise());
                }
            amp = f32 (amp * rough);
        }
        return h;
    }
    std::vector<float> blurAsymmetric (std::vector<float> src, int n, int passes)
    {
        const int m = n - 1;
        const double sc = n / 256.0;
        const int o1x = (int) Js::round (3 * sc), o1y = (int) Js::round (-2 * sc), o2x = (int) Js::round (-2 * sc), o2y = (int) Js::round (4 * sc);
        for (int p = 0; p < passes; ++p)
        {
            std::vector<float> dst ((size_t) (n * n));
            for (int y = 0; y < n; ++y)
            {
                const int r1 = ((y + o1y) & m) * n, r2 = ((y + o2y) & m) * n;
                for (int x = 0; x < n; ++x)
                    dst[(size_t) (y * n + x)] = f32 (f32 (0.5 * (double) src[(size_t) (y * n + x)] + 0.25 * (double) src[(size_t) (r1 + ((x + o1x) & m))])
                                                    + 0.25 * (double) src[(size_t) (r2 + ((x + o2x) & m))]);
            }
            src = std::move (dst);
        }
        return src;
    }
    std::vector<float>& normalize01 (std::vector<float>& h) noexcept
    {
        double lo = h[0], hi = h[0];
        for (const float v : h) { if (v < lo) lo = v; if (v > hi) hi = v; }
        const double k = f32 (1 / f32 (hi - lo));
        for (auto& v : h) v = f32 (f32 ((double) v - lo) * k);
        return h;
    }
    /** Tileable value noise on the unit square, fx by fy cells. */
    double vnoise (double x, double y, double fx, double fy, double seed) noexcept
    {
        const double px = x * fx, py = y * fy, ix = std::floor (px), iy = std::floor (py);
        const double ux0 = px - ix, uy0 = py - iy, ux = ux0 * ux0 * (3 - 2 * ux0), uy = uy0 * uy0 * (3 - 2 * uy0);
        const auto h = [&] (double i, double j) { return MetalWorld::hcell (mod (mod (i, fx) + fx, fx), mod (mod (j, fy) + fy, fy), seed); };
        return lerp (lerp (h (ix, iy), h (ix + 1, iy), ux), lerp (h (ix, iy + 1), h (ix + 1, iy + 1), ux), uy);
    }
    double fbm (double x, double y, double f0, int octaves, double rough, double seed) noexcept
    {
        double a = 1, sum = 0, norm = 0, f = f0;
        for (int o = 0; o < octaves; ++o) { sum += a * vnoise (x, y, f, f, seed + o * 7919); norm += a; a *= rough; f *= 2; }
        return sum / norm;
    }
    inline uint8_t u8 (double v) noexcept { return (uint8_t) (toU32 (v) & 0xffu); }

    inline double yawOf (double nx, double nz) noexcept { return Js::atan2 (-nx, nz); }
    inline double hyp (double a, double b) noexcept { return Js::hypot (a, b); }
    inline double hyp3 (double a, double b, double c) noexcept { return Js::hypot (a, b, c); }
    inline double hyp3 (const V3& v) noexcept { return Js::hypot (v[0], v[1], v[2]); }
    V3 norm3 (const V3& v) noexcept { const double l = orOne (hyp3 (v)); return { v[0] / l, v[1] / l, v[2] / l }; }
}

//==============================================================================
// HASHES, NOISE, THE TEXTURES

uint32_t MetalWorld::ih (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
double MetalWorld::hcell (double cx, double cy, double s) noexcept
{
    return f32 ((double) ih ((toU32 (cx) * 0x9E3779B1u) ^ ih ((toU32 (cy) * 0x85EBCA77u) ^ toU32 (s))) / 4294967296.0);
}
double MetalWorld::hs (double i, double s) noexcept
{
    return f32 ((double) ih ((toU32 (i) * 0x9E3779B1u) ^ ih (toU32 (s) * 0x85EBCA77u)) / 4294967296.0);
}
double MetalWorld::toHalf (double v) noexcept
{
    const double a = std::abs (v);
    if (a < 6.103515625e-05) return Js::round (v / 5.960464477539063e-08) * 5.960464477539063e-08;
    // Math.pow (2, Math.floor (Math.log2 (a)) - 10): a power of two, exact; the floor of log2 of an f32 or a double
    // well away from a power of two is its binary exponent.
    int e = 0;
    std::frexp (a, &e);
    const double q = std::ldexp (1.0, (e - 1) - 10);
    return clamp (Js::round (v / q) * q, -65504, 65504);
}

std::vector<float> MetalWorld::makeBase()
{
    const int n = texSize, nn = n * n;
    const auto smooth = [] (uint32_t seed, double rough, int passes) { auto h = blurAsymmetric (plasma (texSize, seed, rough), texSize, passes); normalize01 (h); return h; };
    const auto R = smooth (1101, 0.52, 2);
    auto Gp = plasma (n, 2202, 0.58);
    for (int i = 0; i < nn; ++i) Gp[(size_t) i] = f32 (1 - std::abs (f32 (2 * (double) Gp[(size_t) i] - 1)));
    normalize01 (Gp);
    auto G = blurAsymmetric (Gp, n, 1);
    normalize01 (G);
    const auto B = smooth (3303, 0.66, 1);
    const auto A = smooth (4404, 0.45, 2);
    std::vector<float> H ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i)
    {
        H[i * 4] = f32 (toHalf (R[i])); H[i * 4 + 1] = f32 (toHalf (G[i])); H[i * 4 + 2] = f32 (toHalf (B[i])); H[i * 4 + 3] = f32 (toHalf (A[i]));
    }
    return H;
}
std::vector<uint8_t> MetalWorld::makeDetail()
{
    const int d = detailSize, m = d - 1;
    auto v = plasma (d, 5505, 0.62); normalize01 (v);
    auto g = plasma (d, 5606, 0.55); normalize01 (g);
    const auto V = [&] (int i) { return (double) v[(size_t) i]; };
    std::vector<uint8_t> px ((size_t) (d * d * 4));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double gx = (V (y * d + ((x + 1) & m)) - V (y * d + ((x - 1) & m))) * 4, gy = (V (((y + 1) & m) * d + x) - V (((y - 1) & m) * d + x)) * 4;
            const size_t i = (size_t) (y * d + x) * 4;
            px[i] = u8 (std::trunc (128 + clamp (gx * 127, -127, 127))); px[i + 1] = u8 (std::trunc (128 + clamp (gy * 127, -127, 127)));
            px[i + 2] = u8 (std::trunc (V (y * d + x) * 255)); px[i + 3] = u8 (std::trunc ((double) g[(size_t) (y * d + x)] * 255));
        }
    return px;
}
std::vector<uint8_t> MetalWorld::makeNoise()
{
    const int n = noiseSize, nn = n * n;
    std::array<std::vector<float>, 4> ch;
    for (auto& c : ch) c.assign ((size_t) nn, 0.0f);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n, v = (double) y / n;
            const size_t i = (size_t) (y * n + x);
            ch[0][i] = f32 (fbm (u, v, 4, 5, 0.5, 1101)); ch[1][i] = f32 (fbm (u, v, 8, 4, 0.5, 1202));
            ch[2][i] = f32 (fbm (u, v, 16, 2, 0.35, 1303)); ch[3][i] = f32 (vnoise (u, v, 64, 64, 1404));
        }
    for (auto& c : ch) normalize01 (c);
    std::vector<uint8_t> px ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i) for (size_t k = 0; k < 4; ++k) px[i * 4 + k] = u8 (std::trunc ((double) ch[k][i] * 255));
    return px;
}
std::vector<uint8_t> MetalWorld::makeCells()
{
    const int n = cellN;
    std::vector<uint8_t> px ((size_t) (n * n * 4));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
        {
            const size_t o = (size_t) (j * n + i) * 4;
            px[o] = u8 (std::trunc (hcell (i, j, 7) * 255)); px[o + 1] = u8 (std::trunc (hcell (i, j, 8) * 255));
            px[o + 2] = u8 (std::trunc (hcell (i, j, 9) * 255)); px[o + 3] = u8 (std::trunc (hcell (i, j, 10) * 255));
        }
    return px;
}

//==============================================================================
// THE LOOP

V2 MetalWorld::loopR (double th) noexcept
{
    double R = loopRadius, dR = 0;
    for (int k = 0; k < 4; ++k) { const double a = loopK[(size_t) k] * th + loopP[(size_t) k]; R += loopA[(size_t) k] * Js::cos (a); dR -= loopA[(size_t) k] * loopK[(size_t) k] * Js::sin (a); }
    return { R, dR };
}
V2 MetalWorld::loopPoint (double th) noexcept { const double R = loopR (th)[0]; return { loopC[0] + R * Js::cos (th), loopC[1] + R * Js::sin (th) }; }
const std::vector<double>& MetalWorld::loopTable()
{
    static const std::vector<double> t = []
    {
        std::vector<double> o ((size_t) loopN + 1, 0.0);
        double acc = 0;
        V2 prev = loopPoint (0);
        for (int i = 1; i <= loopN; ++i) { const V2 p = loopPoint ((double) i / loopN * 2 * pi); acc += hyp (p[0] - prev[0], p[1] - prev[1]); o[(size_t) i] = acc; prev = p; }
        return o;
    }();
    return t;
}
double MetalWorld::loopLength() { return loopTable()[(size_t) loopN]; }
double MetalWorld::modL (double v) { const double L = loopLength(); return mod (mod (v, L) + L, L); }
double MetalWorld::thetaAt (double s)
{
    const auto& S = loopTable();
    const double q = modL (s);
    int lo = 0, hi = loopN;
    while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (S[(size_t) mid] <= q) lo = mid; else hi = mid; }
    return (lo + (q - S[(size_t) lo]) / jsMax (S[(size_t) lo + 1] - S[(size_t) lo], 1e-9)) / loopN * 2 * pi;
}
V2 MetalWorld::tangentAt (double th) noexcept
{
    const V2 Rd = loopR (th);
    const double R = Rd[0], dR = Rd[1], c = Js::cos (th), sn = Js::sin (th), tx = dR * c - R * sn, tz = dR * sn + R * c, l = hyp (tx, tz);
    return { tx / l, tz / l };
}
V2 MetalWorld::rightAt (double th) noexcept { const V2 t = tangentAt (th); return { -t[1], t[0] }; }
V2 MetalWorld::roadPointAt (double s, double d)
{
    const double th = thetaAt (s);
    const V2 p = loopPoint (th), r = rightAt (th);
    return { p[0] + d * r[0], p[1] + d * r[1] };
}
double MetalWorld::headingAt (double sr) { const V2 t = tangentAt (thetaAt (sr)); return Js::atan2 (t[0], -t[1]); }
MetalWorld::Basis MetalWorld::cameraBasis (double heading, double pitch) noexcept
{
    const double cp = Js::cos (pitch), sp = Js::sin (pitch), f0x = Js::sin (heading), f0z = -Js::cos (heading);
    return { { Js::cos (heading), 0, Js::sin (heading) }, { -f0x * sp, cp, -f0z * sp }, { f0x * cp, sp, f0z * cp } };
}
int MetalWorld::studs() { return (int) Js::round (loopLength() / 10); }
V2 MetalWorld::frontAt (double px, double pz, double at, double turn)
{
    const double th = thetaAt (at);
    const V2 rp = roadPointAt (at, 0), t = tangentAt (th);
    double fx = rp[0] - px, fz = rp[1] - pz;
    const double fl = orOne (hyp (fx, fz)); fx /= fl; fz /= fl;
    const double tw = turn * pi / 180, side = (fx * t[1] - fz * t[0]) < 0 ? 1 : -1;
    const double c = Js::cos (tw * side), sn = Js::sin (tw * side);
    return { fx * c - fz * sn, fx * sn + fz * c };
}

//==============================================================================
// THE WORLD: THE HEIGHT MIRROR

double MetalWorld::tap (int ch, double u, double v) const noexcept
{
    const int n = texSize, m = n - 1;
    const double x = u * n - 0.5, y = v * n - 0.5, x0 = std::floor (x), y0 = std::floor (y);
    const double fx = x - x0, fy = y - y0;
    const int32_t ix = toI32 (x0), iy = toI32 (y0);
    const int i0 = ix & m, i1 = (ix + 1) & m, j0 = iy & m, j1 = (iy + 1) & m;
    const auto B = [this, ch] (int j, int i) { return (double) basePx[(size_t) ((j * texSize + i) * 4 + ch)]; };
    const double a = B (j0, i0), b = B (j0, i1), c = B (j1, i0), d = B (j1, i1);
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}
std::array<double, 4> MetalWorld::tap4 (double u, double v) const noexcept { return { tap (0, u, v), tap (1, u, v), tap (2, u, v), tap (3, u, v) }; }
double MetalWorld::terrainH (double px, double pz) const noexcept
{
    const auto a = tap4 (px / L0, pz / L0), b = tap4 (px / L1 + off1[0], pz / L1 + off1[1]), c = tap4 (px / L2 + off2[0], pz / L2 + off2[1]);
    const double mtnM = smoothstep (0.34, 0.16, a[3]);
    const double mtn = mtnM * mtnM * 160 * jsPow (b[1], 1.6);
    const double swell = (a[0] - 0.5) * 60, hills = (b[0] - 0.5) * 7 * (1 - 0.6 * mtnM);
    const double fine = (c[2] - 0.5) * 1.1 + (c[0] - 0.5) * 0.4;
    return swell + mtn + hills + fine + cragH (px, pz);
}
double MetalWorld::cragH (double px, double pz) const noexcept
{
    double h = 0;
    for (const auto& c : crags_)
    {
        const double ex = c.b[0] - c.a[0], ez = c.b[1] - c.a[1], ee = ex * ex + ez * ez;
        const double t = ee > 0 ? clamp (((px - c.a[0]) * ex + (pz - c.a[1]) * ez) / ee, 0, 1) : 0;
        const double d = hyp (px - c.a[0] - ex * t, pz - c.a[1] - ez * t);
        if (d < c.r) h += c.h * smoothstep (1.0, 0.4, d / c.r);
    }
    return h;
}
double MetalWorld::roadHeight (double th) const noexcept
{
    const double q = mod (mod (th / (2 * pi), 1) + 1, 1) * rhN;
    const int i = (int) mod (std::floor (q), rhN);
    const double f = q - std::floor (q);
    return rhTable[(size_t) i] * (1 - f) + rhTable[(size_t) ((i + 1) % rhN)] * f;
}
V2 MetalWorld::roadTap (double x, double z) const noexcept
{
    const int n = roadN;
    const double u = clamp ((x - roadOrg[0]) / roadSpan * n - 0.5, 0, n - 1), v = clamp ((z - roadOrg[1]) / roadSpan * n - 0.5, 0, n - 1);
    const double x0 = jsMin (std::floor (u), n - 2), y0 = jsMin (std::floor (v), n - 2), fx = u - x0, fy = v - y0;
    const int i0 = (int) x0, j0 = (int) y0;
    const auto at = [this] (int i, int j, int c) { return (double) roadPx[(size_t) ((j * roadN + i) * 4 + c)]; };
    const auto q = [&] (int c) { return (at (i0, j0, c) * (1 - fx) + at (i0 + 1, j0, c) * fx) * (1 - fy) + (at (i0, j0 + 1, c) * (1 - fx) + at (i0 + 1, j0 + 1, c) * fx) * fy; };
    return { q (0), q (1) };
}
double MetalWorld::floorHeight (double px, double pz) const noexcept
{
    if (! built)
        return 0;
    const V2 rd = roadTap (px, pz);
    const double ad = std::abs (rd[0]);
    double tH = terrainH (px, pz);
    // the vent of an unwrapped 48 m cell (ventAt)
    const double ci = std::floor (px / ventCell), cj = std::floor (pz / ventCell);
    const double cx = mod (mod (ci, ventM) + ventM, ventM), cz = mod (mod (cj, ventM) + ventM, ventM);
    if (! (hcell (cx, cz, 1) > ventProb))
    {
        const double vx = (ci + 0.2 + 0.6 * hcell (cx, cz, 2)) * ventCell, vz = (cj + 0.2 + 0.6 * hcell (cx, cz, 3)) * ventCell;
        const double d = hyp (px - vx, pz - vz);
        if (d < 8) { const double q = d / 2.6; tH += (0.6 * Js::exp (-(q * q)) - 0.1 * Js::exp (-(d * d))) * smoothstep (12, 16, ad); }
    }
    const double w = smoothstep (roadHalf + 2.5, 55, ad);
    return lerp (rd[1], tH, w) - 0.15 * smoothstep (roadHalf, roadHalf + 1.2, ad) * (1 - smoothstep (roadHalf + 2, roadHalf + 5, ad));
}
MetalWorld::Probe MetalWorld::probe (double px, double pz) const noexcept
{
    const auto a = tap4 (px / L0, pz / L0), b = tap4 (px / L1 + off1[0], pz / L1 + off1[1]);
    return { (0.12 + 0.88 * smoothstep (0.30, 0.50, a[3] * 0.5 + b[3] * 0.5)) * smoothstep (0.30, 0.40, a[3]), smoothstep (0.34, 0.16, a[3]) };
}
double MetalWorld::footY (double px, double pz, double r) const noexcept
{
    double y = floorHeight (px, pz);
    for (int k = 0; k < 6; ++k) { const double a = (double) k / 6 * 2 * pi; y = jsMin (y, floorHeight (px + r * Js::cos (a), pz + r * Js::sin (a))); }
    return y;
}

//==============================================================================
// THE WORLD: THE THINGS' PLACES

MetalWorld::Prop& MetalWorld::add (const std::string& mesh, double x, double z, double yaw, const Extra& e)
{
    const double y = e.y ? *e.y : footY (x, z, e.foot);
    Prop p;
    p.mesh = mesh; p.x = x; p.y = y; p.z = z; p.yaw = yaw; p.s = e.s; p.p = e.p ? *e.p : hs ((double) props_.size(), 41);
    p.name = e.name; p.idx = (int) props_.size();
    props_.push_back (p);
    return props_.back();
}

void MetalWorld::build()
{
    if (built)
        return;
    const double L = loopLength();
    basePx = makeBase();
    detailPx = makeDetail(); noisePx = makeNoise(); cellsPx = makeCells();
    // the crags the things stand on (cragsOf)
    {
        const auto at = [L] (double f, double d) { return roadPointAt (f * L, d); };
        { const V2 p = at (citadelSpec.at, citadelSpec.d); crags_.push_back ({ p, p, citadelSpec.crag[0], citadelSpec.crag[1], "citadel" }); }
        {
            const double s0 = ampsSpec.at * L;
            const V2 c = roadPointAt (s0, ampsSpec.d), nf = frontAt (c[0], c[1], s0, ampsSpec.turn);
            const double rx = -nf[1], rz = nf[0], hw = ampsSpec.n * ampsSpec.cab / 2 + 6;
            const double bx = c[0] - nf[0] * 5, bz = c[1] - nf[1] * 5;
            crags_.push_back ({ { bx - rx * hw, bz - rz * hw }, { bx + rx * hw, bz + rz * hw }, ampsSpec.cliff[0], ampsSpec.cliff[1], "amps" });
        }
        { const V2 p = at (hengeSpec.at, hengeSpec.d); crags_.push_back ({ p, p, hengeSpec.rise[0], hengeSpec.rise[1], "henge" }); }
        { const V2 p = at (catSpec.at, catSpec.d); crags_.push_back ({ p, p, catSpec.crag[0], catSpec.crag[1], "cat" }); }
        { const V2 p = roadPointAt (warriorSpec.at * L + warriorSpec.ahead, warriorSpec.d); crags_.push_back ({ p, p, warriorSpec.rise[0], warriorSpec.rise[1], "warrior" }); }
    }
    // the road's own height round the loop, low-passed over +-200 m
    {
        std::vector<double> raw ((size_t) rhN);
        rhTable.assign ((size_t) rhN, 0.0);
        for (int i = 0; i < rhN; ++i)
        {
            const double th = (double) i / rhN * 2 * pi;
            const V2 c = loopPoint (th);
            const auto a = tap4 (c[0] / L0, c[1] / L0);
            const double mtnM = smoothstep (0.34, 0.16, a[3]);
            raw[(size_t) i] = 0.4 * (a[0] - 0.5) * 60 + mtnM * mtnM * 30;
        }
        const int w = (int) Js::round (200 / L * rhN);
        for (int i = 0; i < rhN; ++i) { double acc = 0; for (int k = -w; k <= w; ++k) acc += raw[(size_t) ((((i + k) % rhN) + rhN) % rhN)]; rhTable[(size_t) i] = acc / (2 * w + 1); }
    }
    // THE ROAD MAP: per texel the signed distance from the centreline and the road's height
    roadPx.assign ((size_t) roadN * roadN * 4, 0.0f);
    for (int j = 0; j < roadN; ++j)
        for (int i = 0; i < roadN; ++i)
        {
            const double x = roadOrg[0] + (i + 0.5) * roadSpan / roadN, z = roadOrg[1] + (j + 0.5) * roadSpan / roadN;
            const double qx = x - loopC[0], qz = z - loopC[1], r = hyp (qx, qz), th = Js::atan2 (qz, qx);
            const V2 Rd = loopR (th);
            const double R = Rd[0], dR = Rd[1];
            const double d = (R - r) * R / std::sqrt (R * R + dR * dR);
            const size_t o = (size_t) (j * roadN + i) * 4;
            roadPx[o] = f32 (toHalf (d)); roadPx[o + 1] = f32 (toHalf (roadHeight (th)));
        }
    built = true;          // the height mirror reads from here on

    const auto tyaw = [] (const V2& t) { return yawOf (t[0], t[1]); };
    // the skull gate: the road runs through its jaws, its front toward the oncoming rover
    {
        const double at = gateAt * L;
        const V2 p = roadPointAt (at, 0), t = tangentAt (thetaAt (at));
        const double y = roadTap (p[0], p[1])[1];
        const Prop& pr = add ("gate", p[0], p[1], yawOf (-t[0], -t[1]), { y, 2, 1, {}, "skull gate" });
        gate_ = { at, p[0], p[1], y, pr.idx };
    }
    // the citadel: towers round the crag's top, walls between, spires on the towers, banners toward the road
    {
        const double at = citadelSpec.at * L;
        const V2 c = roadPointAt (at, citadelSpec.d), nf = frontAt (c[0], c[1], at, 0);
        const double cx = c[0], cz = c[1], nx = nf[0], nz = nf[1];
        citadel_ = { at, cx, cz, floorHeight (cx, cz), {} };
        const double rx = -nz, rz = nx;
        const double T[5][3] = { { -26, 10, 46 }, { -10, 16, 58 }, { 9, 13, 40 }, { 25, 6, 52 }, { 2, -16, 64 } };
        for (int i = 0; i < 5; ++i)
        {
            const double u = T[i][0], v = T[i][1], h = T[i][2];
            const double x = cx + rx * u + nx * v, z = cz + rz * u + nz * v, y = footY (x, z, 6) - 1;
            const Prop& pr = add ("tower" + std::to_string (i % 3), x, z, yawOf (nx, nz) + i * 0.7, { y, 2, h / 50, {}, "citadel tower" });
            const double tipY = y + h + 16 * h / 50;
            citadel_.towers.push_back ({ x, y, z, h, tipY, pr.idx });
            targets_.push_back ({ x, tipY, z, 1.4, false, -1 });
            spires_.push_back ({ x, tipY - 6 * h / 50, z, (int) props_.size() - 1 }); shakes_.push_back (1e4);
            perches_.push_back ({ x, y + h * 1.0 + 0.5, z, yawOf (nx, nz), "citadel", i });
            for (const int sd : { -1, 1 })
            {
                const double bw = 7.0, bh = 3 * bw, out = (5.4 + 0.6 * (i % 3)) * h / 50 * 1.25 + 0.4;
                const double bx = x + nx * out + rx * sd * (bw / 2 + 0.25), bz = z + nz * out + rz * sd * (bw / 2 + 0.25);
                const int cell = i * 2 + (sd > 0 ? 1 : 0);
                banners_.push_back ({ bx, y + h * 0.97, bz, Js::atan2 (nx, nz), bw, bh, hs (cell, 43), cell });
            }
        }
        for (int i = 0; i + 1 < 4; ++i)
        {
            const Tower A = citadel_.towers[(size_t) i], B = citadel_.towers[(size_t) i + 1];
            const double len = hyp (B.x - A.x, B.z - A.z), x = (A.x + B.x) / 2, z = (A.z + B.z) / 2;
            add (i == 1 ? "wallb" : "wall", x, z, Js::atan2 (B.x - A.x, B.z - A.z) + pi / 2, { footY (x, z, 5) - 1.5, 2, len / 30, {}, "citadel wall" });
        }
    }
    // the guardian on his rise, facing the road; the lightning likes him on a hit
    {
        const double at = warriorSpec.at * L + warriorSpec.ahead;
        const V2 p = roadPointAt (at, warriorSpec.d), nf = frontAt (p[0], p[1], at, 30);
        const double y = footY (p[0], p[1], 6) - 0.3;
        targets_.push_back ({ p[0], y + 25.5, p[1], 1.6, true, -1 });
        warrior_ = { at, p[0], y, p[1], yawOf (nf[0], nf[1]) };
    }
    {
        std::vector<double> f { gateAt, gateAt + 50.0 / 5147, gateAt + 110.0 / 5147, fossilSpec.at, stageSpec.at, warriorSpec.at + warriorSpec.ahead / 5147 };
        for (const auto& l : landmarkSpecs) if (l.at >= 0) f.push_back (l.at);
        for (const double a : { citadelSpec.at, ampsSpec.at, hengeSpec.at, anvilSpec.at, catSpec.at }) f.push_back (a);
        for (const auto& p : pyroSpecs) f.push_back (p.at);
        for (const auto& p : pyroSpecs) f.push_back (p.at + p.n * pyroStep / L);
        for (const auto& q : fountainSpecs) f.push_back (q.at);
        for (const double q : findSlots) f.push_back (q);
        for (const double q : f) taken_.push_back (q * L);
    }
    // the owner's new things: their places now, drawn (and struck by lightning) when their meshes come
    for (const auto& spec : landmarkSpecs)
    {
        Landmark lm;
        lm.role = spec.role; lm.label = spec.label; lm.d = spec.d; lm.turn = spec.turn; lm.along = spec.along; lm.height = spec.height; lm.paint = spec.paint;
        const double frac = spec.at < 0 ? crawlerSpot (spec.d) : spec.at;
        const double at = frac * L;
        const V2 p = roadPointAt (at, spec.d), t = tangentAt (thetaAt (at));
        const V2 nf = spec.along ? t : frontAt (p[0], p[1], at, spec.turn);
        lm.at = at; lm.x = p[0]; lm.z = p[1]; lm.y = footY (p[0], p[1], 4) - 0.2; lm.yaw = yawOf (nf[0], nf[1]);
        landmarks_.push_back (lm);
    }
    for (const auto& lm : landmarks_) if (lm.role == "crawler") { taken_.push_back (lm.at); break; }
    // the Tesla towers: in a line along the road between it and the crag
    for (int i = 0; i < teslaSpec.n; ++i)
    {
        const double at = teslaSpec.at * L + (i - (teslaSpec.n - 1) / 2.0) * teslaSpec.step, d = teslaSpec.d - 6 * Js::sin (i * 1.7);
        const V2 p = roadPointAt (at, d);
        const double y = footY (p[0], p[1], 3);
        const Prop& pr = add ("tesla", p[0], p[1], i * 0.9, { y, 2, 1, {}, "Tesla tower" });
        tesla_.push_back ({ p[0], y, p[1], y + 27.5, pr.idx });
        targets_.push_back ({ p[0], y + 27.5, p[1], 1.2, false, i });
    }
    // the backline: cabinets on the cliff's front edge, three high, facing the road
    {
        const double at = ampsSpec.at * L;
        const V2 c = roadPointAt (at, ampsSpec.d), nf = frontAt (c[0], c[1], at, ampsSpec.turn);
        const double cx = c[0], cz = c[1], nx = nf[0], nz = nf[1], rx = -nz, rz = nx, yaw = yawOf (nx, nz);
        double y0 = 1e9;
        for (int i = 0; i < ampsSpec.n; ++i)
        {
            const double u = (i - (ampsSpec.n - 1) / 2.0) * ampsSpec.cab, x = cx + rx * u, z = cz + rz * u, yc = footY (x, z, 2.3) - 0.3;
            y0 = jsMin (y0, yc);
            for (int r = 0; r < ampsSpec.rows; ++r)
                add (r == ampsSpec.rows - 1 ? "head" : "cab", x, z, yaw, { yc + r * ampsSpec.cab, 2, 1, hs (i + r * 50, 44), "amp" });
        }
        perches_.push_back ({ cx, y0 - 0.6 + ampsSpec.cab * 2 + 2.6, cz, yaw, "amps", -1 });
        targets_.push_back ({ cx, y0 + ampsSpec.cab * 2 + 2, cz, 0.5, false, -1 });
        const double fxp = cx + nx * 16, fzp = cz + nz * 16;
        amps_ = { at, cx, cz, y0, fxp, jsMax (floorHeight (fxp, fzp), y0 - 8) + 1.0, fzp };
    }
    // the steel Stonehenge: a circle of trilithons on its rise, a lintel or two fallen
    {
        const double at = hengeSpec.at * L;
        const V2 c = roadPointAt (at, hengeSpec.d);
        const double cx = c[0], cz = c[1], y = floorHeight (cx, cz);
        for (int i = 0; i < hengeSpec.n; ++i)
        {
            const double a = (double) i / hengeSpec.n * 2 * pi, x = cx + hengeSpec.r * Js::cos (a), z = cz + hengeSpec.r * Js::sin (a);
            const bool fallen = i == 3 || i == 7;
            add (fallen ? "trilithonF" : "trilithon", x, z, -a + pi / 2, { footY (x, z, 3) - 0.4, 2, 1, {}, "henge" });
            if (! fallen)
            {
                targets_.push_back ({ x, footY (x, z, 3) + 13.5, z, 0.6, false, -1 });
                if (i % 3 == 0) perches_.push_back ({ x, footY (x, z, 3) + 14.6, z, -a, "henge", -1 });
            }
        }
        add ("altar", cx, cz, 0.3, { {}, 2, 1, {}, "henge" });
        henge_ = { at, cx, cz, y };
    }
    // the anvil by the road, its horn along the way
    {
        const double at = anvilSpec.at * L;
        const V2 p = roadPointAt (at, anvilSpec.d), t = tangentAt (thetaAt (at));
        const double py = add ("anvil", p[0], p[1], tyaw (t) + 0.25, { {}, 7, 1, {}, "anvil" }).y;
        targets_.push_back ({ p[0], py + 8.6, p[1], 0.9, false, -1 });
        perches_.push_back ({ p[0], py + 8.7, p[1], yawOf (-t[0], -t[1]), "anvil", -1 });
        anvil_ = { at, p[0], p[1], py };
    }
    // Darwin's Cat on its crag, looking down at the road
    {
        const double at = catSpec.at * L;
        const V2 p = roadPointAt (at, catSpec.d), nf = frontAt (p[0], p[1], at, 25);
        const double py = add ("cat", p[0], p[1], yawOf (nf[0], nf[1]), { {}, 6, 1, {}, "cat" }).y;
        targets_.push_back ({ p[0], py + catSpec.h * 1.25, p[1], 1.2, false, -1 });
        perches_.push_back ({ p[0] + nf[0] * 2, py + catSpec.h * 0.88, p[1] + nf[1] * 2, yawOf (nf[0], nf[1]), "cat", -1 });
        cat_ = { at, p[0], p[1], py, nf[0], nf[1] };
    }
    // the festival stage across the plain, facing the road
    {
        const double at = stageSpec.at * L;
        const V2 p = roadPointAt (at, stageSpec.d), nf = frontAt (p[0], p[1], at, 0);
        const double py = add ("stage", p[0], p[1], yawOf (nf[0], nf[1]), { {}, 30, 1, {}, "stage" }).y;
        stage_ = { at, p[0], p[1], py, nf[0], nf[1], yawOf (nf[0], nf[1]) };
    }
    // the pyro columns: pairs on both sides along four stretches
    for (int k = 0; k < (int) pyroSpecs.size(); ++k)
        for (int i = 0; i < pyroSpecs[(size_t) k].n; ++i)
            for (const int sd : { -1, 1 })
            {
                const double at = pyroSpecs[(size_t) k].at * L + i * pyroStep;
                const V2 p = roadPointAt (at, sd * pyroSide);
                const double y = floorHeight (p[0], p[1]);
                add ("nozzle", p[0], p[1], 0, { y, 2, 1, {}, "pyro" });
                pyro_.push_back ({ p[0], y + 1.6, p[1], at, k, (int) pyro_.size() });
            }
    // the lava fountains: a glowing pool off the road that now and then throws bombs
    for (int i = 0; i < (int) fountainSpecs.size(); ++i)
    {
        const auto& f = fountainSpecs[(size_t) i];
        const V2 p = roadPointAt (f.at * L, f.d);
        const double y = footY (p[0], p[1], 5);
        add ("pool", p[0], p[1], i, { y, 2, 1, {}, "lava fountain" });
        Fountain q; q.x = p[0]; q.y = y; q.z = p[1]; q.at = f.at * L; q.next = 2 + 5 * hs (i, 45); q.t = 1e4; q.n = 0; q.i = i;
        fountains_.push_back (q);
    }
    for (int slot = 0; slot < 3; ++slot) for (int kind = 0; kind < 5; ++kind) findPlaces[(size_t) slot][(size_t) kind] = placeFind (slot, kind);
    // the ball lightning, the dragons
    for (int i = 0; i < numOrbs; ++i) { Orb o; o.i = i; o.n = i * 17; orbs_.push_back (o); }
    for (int i = 0; i < dragonSpec.n; ++i)
    {
        Dragon u;
        u.i = i; u.ph = hs (i, 801) * 2 * pi; u.beat = hs (i, 802); u.nextGest = 6 + 9 * hs (i, 804); u.nextHic = 6 + 8 * hs (i, 803);
        dragons_.push_back (u);
    }
    tunnel_ = { gate_.at + 3.0, gate_.at + 100.0, 6.5 };
    // the skull's top: where the lightning strikes and a dragon perches
    targets_.push_back ({ gate_.x, gate_.y + 40, gate_.z, 1.3, false, -1 });
    perches_.push_back ({ gate_.x, gate_.y + 39.5, gate_.z, props_[(size_t) gate_.prop].yaw, "gate", -1 });

    makeGroves();                                          // now that everything tall stands
    // the drive starts 280 m before the skull gate (the lap's start), the gate ahead
    s.sr = L - startBefore; place();
    resetDragons();
    for (auto& o : orbs_) respawnOrb (o, true);
}

double MetalWorld::crawlerSpot (double d) const
{
    const double L = loopLength();
    double best = -1e9, at = 0;
    for (double a = 0; a < L; a += 10)
    {
        if (! std::all_of (taken_.begin(), taken_.end(), [&] (double u) { return cyc (a, u, L) > 90; })) continue;
        const V2 p = roadPointAt (a, d);
        const Probe pr = probe (p[0], p[1]);
        if (pr.mtn > 0.03) continue;
        const double turn = std::abs (mod (headingAt (a + 90) - headingAt (a - 90) + 3 * pi, 2 * pi) - pi);
        double lo = 0, hi = 0;
        const double o4[4][2] = { { 4, 0 }, { -4, 0 }, { 0, 4 }, { 0, -4 } };
        for (int k = 0; k < 4; ++k)
        {
            const double h = floorHeight (p[0] + o4[k][0], p[1] + o4[k][1]);
            if (k == 0) lo = hi = h; else { hi = jsMax (hi, h); lo = jsMin (lo, h); }
        }
        const double spread = hi - lo;
        const double score = 2.0 * pr.hot - 9 * turn - 0.6 * spread;
        if (score > best) { best = score; at = a; }
    }
    return at / L;
}

int MetalWorld::blocksView (double at, double px, double py, double pz, const Tall& o, double margin) const
{
    int n = 0;
    for (double back = 60; back <= 330; back += 20)
    {
        const V2 e = roadPointAt (at - back, lane);
        const double ex = e[0], ez = e[1], ey = floorHeight (ex, ez) + eye;
        const double dx = px - ex, dz = pz - ez, L2s = dx * dx + dz * dz, t = ((o.x - ex) * dx + (o.z - ez) * dz) / L2s;
        if (t <= 0.02 || t >= 0.97) continue;
        const double d = hyp (o.x - ex - dx * t, o.z - ez - dz * t), yl = ey + (py - ey) * t;
        if (d < o.r + margin && yl > o.y0 - margin && yl < o.y1 + margin) ++n;
    }
    return n;
}
std::vector<MetalWorld::Tall> MetalWorld::tallThings() const
{
    std::vector<Tall> out;
    for (const auto& q : props_)
    {
        if (q.mesh == "nozzle" || q.mesh == "pool") continue;
        const bool low = q.mesh.find ("cab") != std::string::npos || q.mesh.find ("head") != std::string::npos;
        out.push_back ({ q.x, q.z, q.y, q.y + (low ? 5 : 50 * orOne (q.s)), q.mesh == "stage" ? 45 : 6 * orOne (q.s) });
    }
    for (const auto& lm : landmarks_) out.push_back ({ lm.x, lm.z, lm.y, lm.y + 17, 7 });
    out.push_back ({ warrior_.x, warrior_.z, warrior_.y, warrior_.y + 25, 7 });
    for (const auto& c : crags_)
    {
        const double x = (c.a[0] + c.b[0]) / 2, z = (c.a[1] + c.b[1]) / 2;
        out.push_back ({ x, z, -50, floorHeight (x, z) + 2, c.r * 0.6 });
    }
    return out;
}
void MetalWorld::makeGroves()
{
    const double L = loopLength();
    std::vector<Grove> out;
    const auto tall = tallThings();
    for (int g = 0; g < 4; ++g)
    {
        double best = -1e9, at = 0;
        int starSide = -1;
        for (double a = 0; a < L; a += 10)
        {
            const V2 p = roadPointAt (a, 0);
            if (probe (p[0], p[1]).mtn > 0.1) continue;
            double room = 1e300;
            bool first = true;
            for (const double u : taken_) { const double c = cyc (a, u, L); room = first ? c : jsMin (room, c); first = false; }
            if (room < 70 || std::any_of (findSlots.begin(), findSlots.end(), [&] (double f) { return cyc (a, f * L, L) < 120; })) continue;
            double apart = L / 2;
            if (! out.empty()) { apart = cyc (a, out[0].at, L); for (const auto& gr : out) apart = jsMin (apart, cyc (a, gr.at, L)); }
            const double base = apart + 0.3 * jsMin (room, 300);
            if (base < best) continue;
            for (const int sd : { -1, 1 })
            {
                const V2 sp = roadPointAt (a, sd * 13);
                const double sy = floorHeight (sp[0], sp[1]) + 13;
                int blocked = 0;
                for (const auto& o : tall) blocked += blocksView (a, sp[0], sy, sp[1], o, 5);
                const double score = base - 1000 * blocked;
                if (score > best) { best = score; at = a; starSide = sd; }
            }
        }
        Grove gr; gr.g = g; gr.at = at; gr.both = g % 2 == 0; gr.starSide = starSide;
        out.push_back (gr);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Grove& a, const Grove& b) { return a.at - b.at < 0; });
    for (size_t k = 0; k < out.size(); ++k) out[k].g = (int) k;
    groves_ = out;
}
bool MetalWorld::inApproach (double a) const
{
    const double L = loopLength();
    const auto rel = [&] (double b) { return mod (a - b + L * 1.5, L) - L / 2; };
    for (const auto& gr : groves_) { const double d = rel (gr.at); if (d > -330 && d < 70) return true; }
    const double spots[3][2] = { { gate_.at, 300 }, { citadel_.at, 300 }, { cat_.at, 480 } };
    for (const auto& sb : spots) { const double d = rel (sb[0]); if (d > -sb[1] && d < 20) return true; }
    return false;
}
const std::vector<MetalWorld::GuitarSlot>& MetalWorld::placeGuitars (int n)
{
    std::vector<GuitarItem> items;
    for (int i = 0; i < n; ++i) items.push_back ({ i % 4, false });
    return placeGuitars (items);
}
const std::vector<MetalWorld::GuitarSlot>& MetalWorld::placeGuitars (const std::vector<GuitarItem>& items)
{
    const int n = (int) items.size();
    std::vector<GuitarSlot> out;
    for (auto& gr : groves_) gr.members.clear();
    // groves are kept in g's order (makeGroves numbers them so): groveOf is groves itself
    for (int i = 0; i < n; ++i)
    {
        const int gi = items[(size_t) i].grove;
        if (gi < 0 || gi >= (int) groves_.size()) continue;
        Grove& gr = groves_[(size_t) gi];
        const int j = (int) gr.members.size();
        const bool star = items[(size_t) i].star;
        const auto h = [i] (int k) { return hs (i * 13 + 7, 600 + k); };
        int k0 = 0;
        for (const int m : gr.members) if (! out[(size_t) m].star) ++k0;
        const double along = star ? 0 : (k0 % 2 ? 1 : -1) * 30 * (std::floor (k0 / 2.0) + 1) + (h (0) - 0.5) * 8;
        const int starSide = gr.starSide;
        const bool before = ! star && along < 0;
        const int side = star ? starSide : gr.both ? (before ? -starSide : (j % 2 ? 1 : -1)) : starSide;
        double off = star ? 11 + 4 * h (1) : 10 + 20 * h (1);
        if (before && side == starSide) off = jsMax (off, 26 + 4 * h (1));
        const double d = side * off, at = modL (gr.at + along);
        const V2 p = roadPointAt (at, d);
        const double turn = -30 + 110 * h (2);
        const V2 nf = frontAt (p[0], p[1], at, turn);
        const double sink = ! star && h (3) < 0.45 ? 0.4 + 1.5 * h (4) : 0;
        const V2 lean = ! star && h (5) < 0.3 ? V2 { (h (6) - 0.5) * 0.28, (h (7) - 0.5) * 0.22 } : V2 { 0, 0 };
        const double k = star ? 1.1 : 0.85 + 0.35 * h (8);
        const double y = footY (p[0], p[1], 2.5) - 0.1 - sink;
        GuitarSlot sl;
        sl.i = i; sl.at = at; sl.x = p[0]; sl.y = y; sl.z = p[1]; sl.yaw = yawOf (nf[0], nf[1]); sl.k = k; sl.lean = lean; sl.grove = gr.g; sl.star = star;
        sl.side = side; sl.off = off; sl.along = along;
        gr.members.push_back ((int) out.size());
        out.push_back (sl);
    }
    // none of ours may stand in front of its grove's star on the approach: moved farther out (or across) until clear
    for (auto& gr : groves_)
    {
        int st = -1;
        for (const int m : gr.members) if (out[(size_t) m].star) { st = m; break; }
        if (st < 0) continue;
        const GuitarSlot S = out[(size_t) st];
        const double hy = S.y + 12.8 * S.k;
        for (const int m : gr.members)
        {
            GuitarSlot& q = out[(size_t) m];
            if (q.star) continue;
            for (int tries = 0; tries < 8 && blocksView (S.at, S.x, hy, S.z, { q.x, q.z, q.y, q.y + 14 * q.k, 2.6 * q.k }) != 0; ++tries)
            {
                q.off += 6; if (tries == 4) { q.side = -q.side; q.off = 10; }
                const V2 p = roadPointAt (q.at, q.side * q.off);
                q.x = p[0]; q.z = p[1];
                const double fy = footY (q.x, q.z, 2.5) - 0.1;
                q.y = fy - (q.y < floorHeight (q.x, q.z) - 0.5 ? 0.6 : 0);
                const V2 nf = frontAt (q.x, q.z, q.at, 30); q.yaw = yawOf (nf[0], nf[1]);
            }
        }
        for (const int m : gr.members) { const auto& q = out[(size_t) m]; targets_.push_back ({ q.x, q.y + 14 * q.k, q.z, 0.8, false, -1 }); }
    }
    guitarSlots = out;
    return guitarSlots;
}
MetalWorld::Find MetalWorld::placeFind (int slot, int kind) const
{
    const double L = loopLength();
    const double at = findSlots[(size_t) slot] * L + (kind - 2) * 7;
    const int side = (slot + kind) % 2 ? 1 : -1;
    const double dd[5] = { 12, 9, 11, 10, 22 };
    const double d = dd[kind] * side;
    const V2 p = roadPointAt (at, d), t = tangentAt (thetaAt (at));
    const V2 nf = kind == 0 ? t : frontAt (p[0], p[1], at, 30);
    const double yaw = kind == 0 ? Js::atan2 (t[0], t[1]) : kind == 4 ? yawOf (t[0], t[1]) + 0.5 : yawOf (nf[0], nf[1]);
    return { kind, p[0], p[1], footY (p[0], p[1], kind == 4 ? 6 : 3), yaw, at, d };
}
std::array<MetalWorld::Find, 3> MetalWorld::findsNow() const
{
    std::array<Find, 3> o {};
    for (int slot = 0; slot < 3; ++slot) o[(size_t) slot] = findPlaces[(size_t) slot][(size_t) ((slot + 3 * s.lap) % 5)];
    return o;
}
void MetalWorld::landmarkCame (const std::string& role, double top)
{
    for (auto& lm : landmarks_)
        if (lm.role == role)
        {
            if (lm.came) return;
            lm.came = true;
            targets_.push_back ({ lm.x, lm.y + top, lm.z, 1.0, false, -1 });
            return;
        }
}

void MetalWorld::buildLetters (const std::vector<LetterText>& table)
{
    const double L = loopLength();
    const std::vector<double>& used = taken_;
    std::vector<double> placedL;
    const auto pick = [&] (double want, double apart)
    {
        const struct { double gapL, gap; bool far; } tries[4] = { { 110, 45, true }, { 110, 45, false }, { 80, 30, false }, { 50, 15, false } };
        for (const auto& tr : tries)
        {
            double best = -1, bd = 1e9;
            for (double a = 0; a < L; a += 10)
            {
                if (inApproach (a)) continue;
                bool ok = true;
                for (const double u : used) if (! (cyc (a, u, L) > tr.gap)) { ok = false; break; }
                for (const double u : placedL) if (ok && ! (cyc (a, u, L) > tr.gapL)) { ok = false; break; }
                if (! ok) continue;
                if (tr.far && apart >= 0 && cyc (a, apart, L) < L / 4) continue;
                const double dd = cyc (a, want, L);
                if (dd < bd) { bd = dd; best = a; }
            }
            if (best >= 0) { placedL.push_back (best); return best; }
        }
        placedL.push_back (want);
        return want;
    };
    int logo = -1;
    for (size_t i = 0; i < table.size(); ++i) if (table[i].once) { logo = (int) i; break; }
    if (logo < 0)
    {
        std::vector<int> big;
        for (size_t i = 0; i < table.size(); ++i) if (table[i].size[0] >= 18) big.push_back ((int) i);
        std::stable_sort (big.begin(), big.end(), [&] (int a, int b) { return table[(size_t) b].size[0] - table[(size_t) a].size[0] < 0; });
        if (! big.empty()) logo = big[0];
    }
    const auto lower = [] (std::string t) { for (auto& c : t) c = (char) std::tolower ((unsigned char) c); return t; };
    int dragonT = -1;
    for (size_t i = 0; i < table.size(); ++i) if ((int) i != logo && lower (table[i].name).find ("dragon") != std::string::npos) { dragonT = (int) i; break; }
    std::vector<int> rest;
    for (size_t i = 0; i < table.size(); ++i) if ((int) i != logo && (int) i != dragonT) rest.push_back ((int) i);
    struct SpotAt { int t; double at; int side; bool big; };
    std::vector<SpotAt> spots;
    if (logo >= 0) spots.push_back ({ logo, pick (logoAt * L, -1), -1, true });
    if (dragonT >= 0)
    {
        const double a0 = pick (dragonSpec.swoopAt * L - 170, -1);
        spots.push_back ({ dragonT, a0, -1, false });
        spots.push_back ({ dragonT, pick (dragonSpec.swoopAt * L + L / 2, a0), 1, false });
    }
    for (size_t i = 0; i < rest.size(); ++i)
    {
        const double f = (i + 0.5) / jsMax ((double) rest.size(), 1) / 2, a0 = pick (f * L, -1);
        spots.push_back ({ rest[i], a0, -1, false });
        spots.push_back ({ rest[i], pick ((f + 0.5) * L, a0), 1, false });
    }
    letterNames.clear();
    letters_.clear();
    for (size_t k = 0; k < spots.size(); ++k)
    {
        const SpotAt& sp = spots[k];
        const LetterText& t = table[(size_t) sp.t];
        const double hw = t.size[0] / 2, hh = t.size[1] / 2;
        const double d = sp.side * (roadHalf + 2.5 + (sp.big ? 8 + hw * 0.3 : hw * 0.45));
        const double th = thetaAt (sp.at);
        const V2 tg = tangentAt (th), r = rightAt (th), p = roadPointAt (sp.at, d);
        const double tw = (sp.big ? 32 : 24) * pi / 180 * (d < 0 ? 1 : -1);
        const double nx = -tg[0] * Js::cos (tw) + r[0] * Js::sin (tw), nz = -tg[1] * Js::cos (tw) + r[1] * Js::sin (tw);
        ++letterNames[t.name];
        Letter q;
        q.name = t.name; q.at = sp.at; q.x = p[0]; q.z = p[1]; q.y = floorHeight (p[0], p[1]) + t.up + hh; q.yaw = Js::atan2 (nx, nz);
        q.nx = nx; q.nz = nz; q.hw = hw; q.hh = hh; q.cell = t.cell; q.colour = t.colour; q.seed = hs ((double) k, 49); q.big = sp.big;
        letters_.push_back (q);
    }
}

//==============================================================================
// THE DRIVE

double MetalWorld::speedFor (int g) const noexcept { return gearSpeed[(size_t) (std::clamp (g, 1, numGears) - 1)]; }
void MetalWorld::setGear (int g) noexcept { gearWanted = std::clamp (g == 0 ? 1 : g, 1, numGears); s.gear = gearWanted; }
void MetalWorld::place()
{
    const double th = thetaAt (s.sr);
    const V2 p = loopPoint (th), r = rightAt (th), t = tangentAt (th);
    s.x = p[0] + s.lat * r[0]; s.z = p[1] + s.lat * r[1];
    s.heading = Js::atan2 (t[0], -t[1]) + Js::atan2 (s.latV, jsMax (s.speed, 2.0)) * 0.8;
}
void MetalWorld::settle()
{
    s.speed = parked ? 0 : speedFor (gearWanted);
    s.storm = parked ? 0 : stormOf[(size_t) (gearWanted - 1)];
    s.calm = parked ? 1 : 0;
    if (parked) { s.lat = parkLat; s.park = 1; }
    place();
}
void MetalWorld::settleCamera() noexcept
{
    if (! built || ! same (s.camY, 0))
        return;
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double g0 = floorHeight (s.x, s.z), sp = s.speed / 17;
    const double bob = 0.012 * sp * (Js::sin (s.travel * 1.7) + 0.6 * Js::sin (s.travel * 4.3 + 1));
    s.camY = g0 + eye + bob;
    const double gb = floorHeight (s.x - fx * 5, s.z - fz * 5), gf = floorHeight (s.x + fx * 5, s.z + fz * 5);
    s.pitch = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 10);
}
void MetalWorld::startAt (double sr, int lap)
{
    s.sr = modL (sr == sr ? sr : 0);
    if (lap >= 0) s.lap = lap;
    s.camY = 0; place();
    s.findFade = smoothstep (findSwapFade[0], findSwapFade[1], jsMin (s.sr, loopLength() - s.sr));
    resetDragons();
    for (auto& o : orbs_) respawnOrb (o, true);
}
double MetalWorld::nextFindAt() const
{
    const double L = loopLength();
    double m = 0;
    for (size_t i = 0; i < findSlots.size(); ++i) { const double a = modL (findSlots[i] * L - 300 - s.sr); m = i == 0 ? a : jsMin (m, a); }
    return modL (s.sr + m);
}
void MetalWorld::hit (double level)
{
    const double lv = clamp (level == level ? level : 0, 0, 1);
    s.hitT = 0; s.hitS = lv; ++s.hits; s.stageHitT = 0;
    strikeSomewhere (0.8 + 0.4 * lv, true);
    for (auto& u : dragons_) if (u.mode != Mode::clang && u.mode != Mode::leave) u.flameT = 0;
}
void MetalWorld::quiet() noexcept
{
    s.hitT = 1e4; s.hitS = 0; s.stageHitT = 1e4;
    for (auto& u : dragons_) u.flameT = 1e4;
}
void MetalWorld::setPitchOffset (double rad) noexcept { s.pitchOffset = clamp (rad == rad ? rad : 0, -0.5, 0.5); }
double MetalWorld::cabinFlash() const noexcept { return clamp (s.flash / 0.75, 0, 1); }

void MetalWorld::step (double dt)
{
    const double L = loopLength();
    s.time += dt; s.dt = dt; s.hitT += dt; s.stageHitT += dt;
    if (parked != wasParked)
    {
        wasParked = parked;
        if (! parked) pull = Pull { 0, s.speed, s.lat, gearWanted, pullSeconds + pullPerMs * jsMax (0, speedFor (gearWanted) - gearSpeed[0]) };
        else pull.reset();
    }
    if (pull && pull->gear != gearWanted) pull.reset();
    if (pull)
    {
        pull->t += dt;
        const double u = clamp (pull->t / pull->T, 0, 1), e = u * u * u * (u * (u * 6 - 15) + 10);
        s.speed = lerp (pull->v0, speedFor (gearWanted), e);
        const double lat = lerp (pull->lat0, lane, e);
        s.latV = dt > 0 ? (lat - s.lat) / dt : 0; s.lat = lat;
        if (u >= 1) pull.reset();
    }
    else
    {
        const double target = parked ? 0 : speedFor (gearWanted);
        s.speed += (target - s.speed) * ease (dt, parked ? parkSeconds : speedSeconds);
        if (parked && s.speed < 0.02) s.speed = 0;
        const double latT = parked ? parkLat : lane, k = parked ? 1.5 : 2.0, mobile = smoothstep (0.0, 1.5, s.speed);
        const double latA = (k * k * (latT - s.lat) - 2 * k * s.latV) * mobile - s.latV * (1 - mobile) * 4;
        s.latV += latA * dt; s.lat += s.latV * dt;
    }
    s.park += ((parked ? 1 : 0) - s.park) * ease (dt, 1.2);
    s.storm += ((parked ? 0 : stormOf[(size_t) (gearWanted - 1)]) - s.storm) * ease (dt, stormSeconds);
    s.calm += ((parked ? 1 : 0) - s.calm) * ease (dt, 0.8);
    s.aurora += dt * (1 - 0.6 * s.calm) * (1 + 2.5 * s.storm);
    s.beat += dt * dragonSpec.tempo;
    if (s.beat > 1e4) s.beat -= 1e4;
    s.sr += s.speed * dt; s.travel += s.speed * dt;
    if (s.sr >= L) { s.sr -= L; ++s.lap; }
    place();
    s.findFade = smoothstep (findSwapFade[0], findSwapFade[1], jsMin (s.sr, L - s.sr));
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double g0 = floorHeight (s.x, s.z), sp = s.speed / 17;
    const double bob = 0.012 * sp * (Js::sin (s.travel * 1.7) + 0.6 * Js::sin (s.travel * 4.3 + 1));
    const double targetY = g0 + eye + bob;
    if (same (s.camY, 0)) s.camY = targetY;
    s.camY += (targetY - s.camY) * ease (dt, 0.12);
    const double gb = floorHeight (s.x - fx * 5, s.z - fz * 5), gf = floorHeight (s.x + fx * 5, s.z + fz * 5);
    const double pitchT = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 10);
    s.pitch += (pitchT - s.pitch) * ease (dt, 0.35);

    // the lightning: now and then, often in the storm
    for (auto& b : bolts_) b.t += dt;
    while (! bolts_.empty() && bolts_.front().t > 0.9) bolts_.erase (bolts_.begin());
    for (auto& sk : sparks_) sk.t += dt;
    nextStrike -= dt * (1 + 5 * s.storm);
    if (nextStrike <= 0)
    {
        strikeSomewhere (0.55 + 0.45 * s.storm, false);
        const V2 e = s.storm > 0.5 ? lightningStormEvery : lightningEvery;
        nextStrike = (e[0] + (e[1] - e[0]) * hs (boltN, 70)) * (s.storm > 0.5 ? 6 : 1);
    }
    double fl = 0;
    for (const auto& b : bolts_) { const double D = hyp (b.at[0] - s.x, b.at[2] - s.z); fl += b.k * boltI (b.t) * (0.35 + 0.65 * Js::exp (-D / 700)); }
    s.flash = jsMin (fl, 2.5);
    // the Tesla towers
    teslaArc.t += dt; teslaChain += dt;
    teslaNext -= dt * (1 + 2 * s.storm);
    if (teslaNext <= 0)
    {
        teslaArc = { (int) std::floor (hs (boltN + 900 + std::floor (s.time), 71) * (double) (tesla_.size() - 1)), 0, 0.3 + 0.35 * hs (std::floor (s.time * 7), 72) };
        teslaNext = 1.4 + 2.6 * hs (std::floor (s.time * 13), 73);
    }
    // the ball lightning
    for (auto& o : orbs_)
    {
        if (same (o.life, 1) && same (o.age, 0)) respawnOrb (o, true);
        o.age += dt;
        o.as += s.speed * 0.25 * dt;
        const V2 a = roadPointAt (o.as, o.lat + 6 * Js::sin (s.time * 0.21 + o.ph));
        const double tx = a[0] + 4 * Js::sin (s.time * 0.37 + o.ph * 2), tz = a[1] + 4 * Js::cos (s.time * 0.29 + o.ph);
        const double ty = floorHeight (a[0], a[1]) + o.up + 2.5 * Js::sin (s.time * 0.5 + o.ph);
        const double k = ease (dt, 3.0);
        o.x += (tx - o.x) * k; o.y += (ty - o.y) * k; o.z += (tz - o.z) * k;
        const bool behind = (o.x - s.x) * fx + (o.z - s.z) * fz < -20;
        if (o.age > o.life || behind)
        {
            int best = -1;
            double bd = 120;
            for (size_t i = 0; i < targets_.size(); ++i)
            {
                const auto& tg = targets_[i];
                const double d = hyp3 (tg.x - o.x, tg.y - o.y, tg.z - o.z);
                if (d < bd) { bd = d; best = (int) i; }
            }
            if (best >= 0 && ! behind)
            {
                const auto& tg = targets_[(size_t) best];
                const int n = boltN++;
                Bolt b; b.t = 0; b.k = 0.45; b.lines.push_back ({ jag ({ o.x, o.y, o.z }, { tg.x, tg.y, tg.z }, 5, 0.2, n), 0.6 }); b.at = { tg.x, tg.y, tg.z }; b.n = n;
                bolts_.push_back (b);
                sparks_.push_back ({ 0, { tg.x, tg.y, tg.z }, 0.5 }); if (sparks_.size() > 4) sparks_.erase (sparks_.begin());
            }
            respawnOrb (o, false);
        }
    }
    // the fountains erupt now and then
    for (auto& f : fountains_)
    {
        f.t += dt;
        if (s.time > f.next) { f.t = 0; ++f.n; f.next = s.time + 7 + 9 * hs (f.i * 100 + f.n, 46) * (1 - 0.5 * s.storm); }
    }
    for (auto& sh : shakes_) sh += dt;
    stepDragons (dt);
}

//==============================================================================
// THE LIGHTNING, THE TESLA ARCS, THE BALL LIGHTNING

std::vector<V3> MetalWorld::jag (const V3& a, const V3& b, int depth, double rough, double seed)
{
    std::vector<V3> pts { a, b };
    for (int d = 0; d < depth; ++d)
    {
        std::vector<V3> nx;
        nx.reserve (pts.size() * 2);
        for (size_t i = 0; i + 1 < pts.size(); ++i)
        {
            const V3 p = pts[i], q = pts[i + 1];
            const double L = hyp3 (q[0] - p[0], q[1] - p[1], q[2] - p[2]);
            const auto h = [&] (int k) { return (hs (seed * 977 + d * 131 + (double) i * 7 + k, 51) - 0.5) * 2 * rough * L; };
            nx.push_back (p);
            nx.push_back ({ (p[0] + q[0]) / 2 + h (0), (p[1] + q[1]) / 2 + h (1) * 0.5, (p[2] + q[2]) / 2 + h (2) });
        }
        nx.push_back (pts.back());
        pts = std::move (nx);
    }
    return pts;
}
double MetalWorld::boltI (double t) noexcept
{
    const double a = (t - 0.13) / 0.03, b = (t - 0.27) / 0.035;
    return Js::exp (-t / 0.05) + 0.75 * Js::exp (-(a * a)) + 0.5 * Js::exp (-(b * b)) + 0.25 * Js::exp (-t / 0.25);
}
void MetalWorld::strike (const V3& target, double k)
{
    const int n = boltN++;
    const double side = (hs (n, 52) - 0.5) * 2;
    const V3 top { target[0] + side * 120 + (hs (n, 53) - 0.5) * 60, target[1] + 380 + 220 * hs (n, 54), target[2] + (hs (n, 55) - 0.5) * 140 };
    Bolt bolt;
    bolt.lines.push_back ({ jag (top, target, 6, 0.18, n), 1 });
    const std::vector<V3> main = bolt.lines[0].pts;
    for (int b = 0; b < 3; ++b)
    {
        const int i0 = 6 + (int) std::floor (hs (n * 3 + b, 56) * 40);
        const V3 p = main[(size_t) i0];
        const V3 dir { (hs (n * 3 + b, 57) - 0.5) * 160, -60 - 80 * hs (n * 3 + b, 58), (hs (n * 3 + b, 59) - 0.5) * 160 };
        bolt.lines.push_back ({ jag (p, { p[0] + dir[0], p[1] + dir[1], p[2] + dir[2] }, 4, 0.22, n * 7 + b + 1), 0.45 });
    }
    bolt.t = 0; bolt.k = k; bolt.at = target; bolt.n = n;
    bolts_.push_back (bolt);
    if (bolts_.size() > 6) bolts_.erase (bolts_.begin());
    sparks_.push_back ({ 0, target, k });
    if (sparks_.size() > 4) sparks_.erase (sparks_.begin());
    // a steel thing struck rings a little (a spire shakes)
    for (size_t i = 0; i < spires_.size(); ++i)
        if (hyp (spires_[i].x - target[0], spires_[i].z - target[2]) < 8) shakes_[i] = jsMin (shakes_[i], 0.6);
}
std::vector<MetalWorld::Seen> MetalWorld::targetsInView (double reach) const
{
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    std::vector<Seen> out;
    for (size_t i = 0; i < targets_.size(); ++i)
    {
        const auto& tg = targets_[i];
        const double dx = tg.x - s.x, dz = tg.z - s.z, D = hyp (dx, dz);
        if (D < reach && D > 25 && dx * fx + dz * fz > 0.62 * D) out.push_back ({ (int) i, D });
    }
    std::stable_sort (out.begin(), out.end(), [this] (const Seen& a, const Seen& b)
    {
        return a.D / orOne (targets_[(size_t) a.tg].w) - b.D / orOne (targets_[(size_t) b.tg].w) < 0;
    });
    return out;
}
void MetalWorld::strikeSomewhere (double k, bool forceSteel)
{
    const auto seen = targetsInView (forceSteel ? 1200 : 900);
    if (! seen.empty() && (forceSteel || hs (boltN, 60) < 0.7))
    {
        // on a hit the guardian draws it first, else the nearest steel (one of the two nearest)
        int pick = -1;
        if (forceSteel) for (const auto& q : seen) if (targets_[(size_t) q.tg].hit && q.D < 700) { pick = q.tg; break; }
        if (pick < 0)
        {
            const double nn = (double) seen.size();
            pick = seen[(size_t) jsMin (nn - 1, std::floor (hs (boltN, 61) * jsMin (2, nn)))].tg;
        }
        const Target tg = targets_[(size_t) pick];
        strike ({ tg.x, tg.y, tg.z }, k);
        // near the Tesla towers a hit (or a strike into them) runs a discharge down their chain
        bool near = false;
        if (forceSteel) for (const auto& t : tesla_) if (hyp (t.x - s.x, t.z - s.z) < 450) { near = true; break; }
        if (tg.tesla >= 0 || near) teslaChain = 0;
        return;
    }
    // onto the plain or the ranges ahead, off to a side
    const double a = s.heading + (hs (boltN, 62) - 0.5) * 1.6, D = 260 + 600 * hs (boltN, 63);
    const double x = s.x + Js::sin (a) * D, z = s.z - Js::cos (a) * D;
    strike ({ x, floorHeight (x, z), z }, k * 0.8);
}
void MetalWorld::respawnOrb (Orb& o, bool first)
{
    o.n += 3;
    o.as = s.sr + (first ? 60 + 260 * hs (o.n, 64) : 200 + 200 * hs (o.n, 64));
    o.lat = (hs (o.n, 65) - 0.5) * 30; o.up = 7 + 9 * hs (o.n, 66); o.ph = hs (o.n, 67) * 6.28;
    o.life = 22 + 22 * hs (o.n, 68); o.age = first ? o.life * hs (o.n, 69) : 0;
    const V2 p = roadPointAt (o.as, o.lat);
    o.x = p[0]; o.z = p[1]; o.y = floorHeight (p[0], p[1]) + o.up;
}

//==============================================================================
// THE DRAGONS

double MetalWorld::dh (int k) { return hs (dc.n++, 820 + k); }
void MetalWorld::anchor (Dragon& u, Kind kind)
{
    const double L = loopLength();
    const int side = dh (1) < 0.5 ? -1 : 1;
    double A = 0, lat = 0, up = 0;
    if (kind == Kind::front) { A = 130 + 120 * dh (2); lat = side * (10 + 60 * dh (3)); up = 26 + 30 * dh (4); }
    else if (kind == Kind::circle) { A = 40 + 260 * dh (2); lat = side * (220 + 180 * dh (3)); up = 90 + 70 * dh (4); }
    else if (kind == Kind::park) { A = 70 + 50 * dh (2); lat = side * (15 + 35 * dh (3)); up = 20 + 14 * dh (4); }
    else { A = dragonSpec.swoopAt * L - s.sr + 120; lat = side * 40; up = 50; }
    const double at = kind == Kind::prep ? s.sr + modL (dragonSpec.swoopAt * L + 120 - s.sr) : s.sr + A;
    u.as = at; u.lat = lat; u.up = up; u.front = kind != Kind::circle; u.anchorT = 0; u.kind = kind;
    u.follow = kind == Kind::front ? 0.55 : kind == Kind::circle ? 0.9 : 0;
    u.loopR = kind == Kind::park ? 18 + 10 * dh (5) : kind == Kind::circle ? 60 + 50 * dh (5) : 25 + 25 * dh (5);
    const double sw = dh (6) < 0.5 ? -1 : 1;
    u.loopW = sw * 2 * pi / (14 + 10 * dh (7));
}
V3 MetalWorld::roamTarget (const Dragon& u, double t) const
{
    const V2 an = roadPointAt (u.as, u.lat);
    const double ax = an[0], az = an[1];
    const double a = u.ph + u.loopW * t;
    const double x = ax + u.loopR * Js::cos (a), z = az + u.loopR * 0.8 * Js::sin (a), g = floorHeight (x, z);
    double y;
    if (u.front) { const V2 r0 = roadPointAt (u.as, 0); y = jsMax (g + 22, floorHeight (r0[0], r0[1]) + u.up); }
    else y = jsMax (g + 40, floorHeight (ax, az) + u.up);
    return { x, y + 7 * Js::sin (a * 1.3 + u.i), z };
}
void MetalWorld::resetDragons()
{
    dc.n = Js::round (s.sr) * 7 + s.lap * 131;
    for (auto& u : dragons_)
    {
        anchor (u, parked ? Kind::park : u.i == 2 ? Kind::circle : Kind::front);
        const V3 p = roamTarget (u, s.time);
        u.x = p[0]; u.y = p[1]; u.z = p[2]; u.vx = u.vy = u.vz = 0; u.mode = Mode::roam; u.mt = 0; u.path.reset(); u.perch.reset(); u.perchOf = -1;
        u.clang.reset(); u.tumble = 0; u.rear = 0;
        const V2 f = tangentAt (thetaAt (s.sr)); u.vx = -f[0] * 8; u.vz = -f[1] * 8;
    }
    dc.sinceSeen = 0; dc.called = -1; dc.nextSwoop = s.time + 14 + 20 * dh (8);
}
namespace
{
    /** A path through points at times (a Hermite spline, the first tangent the current velocity). */
    V3 pathAt (const MetalWorld::Path& pa, double t)
    {
        const auto& pts = pa.pts;
        const auto& ts = pa.ts;
        const int n = (int) pts.size();
        t = clamp (t, 0, ts[(size_t) n - 1]);
        int i = 0; while (i < n - 2 && t > ts[(size_t) i + 1]) ++i;
        const auto m = [&] (int j) -> V3
        {
            if (! pa.tan.empty()) return pa.tan[(size_t) j];
            if (j == 0) return *pa.v0;
            V3 o {};
            if (j == n - 1) for (int c = 0; c < 3; ++c) o[(size_t) c] = (pts[(size_t) j][(size_t) c] - pts[(size_t) j - 1][(size_t) c]) / (ts[(size_t) j] - ts[(size_t) j - 1]);
            else for (int c = 0; c < 3; ++c) o[(size_t) c] = (pts[(size_t) j + 1][(size_t) c] - pts[(size_t) j - 1][(size_t) c]) / (ts[(size_t) j + 1] - ts[(size_t) j - 1]);
            return o;
        };
        const double h = ts[(size_t) i + 1] - ts[(size_t) i], q = (t - ts[(size_t) i]) / h, q2 = q * q, q3 = q2 * q;
        const double h00 = 2 * q3 - 3 * q2 + 1, h10 = q3 - 2 * q2 + q, h01 = -2 * q3 + 3 * q2, h11 = q3 - q2;
        const V3 m0 = m (i), m1 = m (i + 1);
        V3 o {};
        for (size_t c = 0; c < 3; ++c) o[c] = h00 * pts[(size_t) i][c] + h10 * h * m0[c] + h01 * pts[(size_t) i + 1][c] + h11 * h * m1[c];
        return o;
    }
    constexpr double takeoffSeconds = 1.6;                 // the somersault on take-off
    constexpr double perchLift = 2.0 * MetalWorld::dragonSpec.scale + 0.3;
}
V3 MetalWorld::P (double a, double lat, double up) const
{
    const V2 p = roadPointAt (a, lat), r0 = roadPointAt (a, 0);
    return { p[0], floorHeight (r0[0], r0[1]) + up, p[1] };
}
void MetalWorld::startSwoop (Dragon& u)
{
    const double v = jsMax (s.speed, 0.5), side = (hs (dc.n++, 830) < 0.5 ? -1 : 1);
    const V3 q = P (s.sr + 200, 0, 50);
    const double dist = hyp3 (q[0] - u.x, q[1] - u.y, q[2] - u.z);
    const double lead = jsMax (7.5, dist / 32 + 4.5);
    const double sC = s.sr + v * lead;
    Path pa;
    pa.pts = { { u.x, u.y, u.z }, P (sC + 170, side * 30, 44), P (sC + 60, side * 4, 11), P (sC + 26, 0, 8.5), P (sC + 8, -side * 45, 32), P (sC - 30, -side * 170, 110) };
    pa.ts = { 0, lead - 3.2, lead - 1.1, lead, lead + 2.2, lead + 6.5 };
    pa.v0 = V3 { u.vx, u.vy, u.vz }; pa.ringAt = lead - 0.15;
    u.path = pa;
    u.mode = Mode::swoop; u.mt = 0; u.perch.reset(); u.perchOf = -1;
}
MetalWorld::V2 MetalWorld::perchFacing (const Perch& q) const
{
    if (parked) { const double dx = s.x - q.x, dz = s.z - q.z, l = orOne (hyp (dx, dz)); return { dx / l, dz / l }; }
    return { -Js::sin (q.yaw), Js::cos (q.yaw) };
}
void MetalWorld::startPerch (Dragon& u, const Perch& spot, int index)
{
    u.perch = spot; u.perchOf = index; u.mode = Mode::toPerch; u.mt = 0; u.perchT = 0;
    const V2 f = perchFacing (spot);
    const double fx = f[0], fz = f[1], bx = -fx, bz = -fz, L = spot.y + perchLift;
    std::vector<V3> pts { { u.x, u.y, u.z } }, tan { { u.vx, u.vy, u.vz } };
    const double ahead = (u.x - spot.x) * fx + (u.z - spot.z) * fz;
    if (ahead > -50)
    {
        const int sd = ((u.x - spot.x) * -fz + (u.z - spot.z) * fx) < 0 ? -1 : 1;
        pts.push_back ({ spot.x + bx * 30 - fz * sd * 95, L + 34, spot.z + bz * 30 + fx * sd * 95 }); tan.push_back ({ bx * 20, -1, bz * 20 });
    }
    pts.push_back ({ spot.x + bx * 85, L + 26, spot.z + bz * 85 }); tan.push_back ({ fx * 20, -4, fz * 20 });
    pts.push_back ({ spot.x + bx * 26, L + 5, spot.z + bz * 26 }); tan.push_back ({ fx * 9, -2.4, fz * 9 });
    pts.push_back ({ spot.x, L, spot.z }); tan.push_back ({ 0, 0, 0 });
    std::vector<double> ts { 0 };
    for (size_t i = 1; i < pts.size(); ++i)
    {
        const double d = hyp3 (pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1], pts[i][2] - pts[i - 1][2]);
        ts.push_back (ts[i - 1] + (i == pts.size() - 1 ? 3.0 : jsMax (2.5, d / 21)));
    }
    {
        const V3 d { pts[1][0] - u.x, pts[1][1] - u.y, pts[1][2] - u.z };
        const double l = orOne (hyp3 (d));
        const V3 e { d[0] / l, d[1] / l, d[2] / l };
        const double back = jsMin (0, u.vx * e[0] + u.vy * e[1] + u.vz * e[2]);
        tan[0] = { u.vx - e[0] * back + e[0] * 10, u.vy - e[1] * back + e[1] * 10, u.vz - e[2] * back + e[2] * 10 };
    }
    Path pa; pa.pts = pts; pa.ts = ts; pa.tan = tan;
    u.path = pa;
}
void MetalWorld::perchGestures (Dragon& u, double dt)
{
    if (! u.gest && u.mt > u.nextGest)
    {
        const auto r = [&] (int k) { return hs (u.i * 7919 + std::floor (s.time * 3), 840 + k); };
        const Gesture kinds[7] = { Gesture::look, Gesture::look, Gesture::wings, Gesture::shuffle, Gesture::look, Gesture::wings, Gesture::stretch };
        const Gesture kind = kinds[(int) std::floor (r (0) * 7)];
        Gest G;
        G.kind = kind; G.t = 0;
        G.dur = kind == Gesture::stretch ? 4.0 : kind == Gesture::shuffle ? 2.4 : 2.6 + 1.6 * r (1);
        G.a = (r (2) < 0.5 ? -1 : 1) * (0.35 + 0.35 * r (3)); G.b = r (4);
        u.gest = G;
    }
    Pose g;
    if (u.gest)
    {
        Gest& G = *u.gest; G.t += dt;
        const double x = clamp (G.t / G.dur, 0, 1), sx = Js::sin (pi * x), env = sx * sx;
        if (G.kind == Gesture::look) { g.hy = G.a * Js::sin (2 * pi * x) * Js::sin (pi * x) * 1.3; g.hp = (G.b - 0.4) * 0.35 * env; }
        else if (G.kind == Gesture::wings) { g.wing = -(0.12 + 0.12 * G.b) * env; g.hp = 0.08 * env; }
        else if (G.kind == Gesture::shuffle) { g.yaw = G.a * 0.35 * env; g.side = sign (G.a) * 0.45 * env; g.pitch = 0.04 * env; }
        else { g.pitch = 0.22 * env; g.hp = 0.45 * env; g.wing = -0.34 * env; }
        if (x >= 1) { u.gest.reset(); u.nextGest = u.mt + 5 + 10 * hs (u.i * 31 + std::floor (s.time), 849); }
    }
    u.g = g;
}
void MetalWorld::startClang (Dragon& u, int spire)
{
    const Spire& sp = spires_[(size_t) spire];
    const double side = hs (dc.n++, 831) < 0.5 ? -1 : 1, th = thetaAt (citadel_.at);
    const V2 r = rightAt (th), t = tangentAt (th);
    const V3 hitP { sp.x - (r[0] * 0.3 + t[0] * side) * 2.5, sp.y, sp.z - (r[1] * 0.3 + t[1] * side) * 2.5 };
    const V3 from { sp.x - t[0] * side * 110 - r[0] * 20, sp.y + 30, sp.z - t[1] * side * 110 - r[1] * 20 };
    const double d1 = hyp3 (from[0] - u.x, from[1] - u.y, from[2] - u.z), T1 = jsMax (3, d1 / 30);
    Path pa;
    pa.pts = { { u.x, u.y, u.z }, from, { (from[0] + hitP[0]) / 2, sp.y + 8, (from[2] + hitP[2]) / 2 }, hitP };
    pa.ts = { 0, T1, T1 + 2.2, T1 + 3.6 };
    pa.v0 = V3 { u.vx, u.vy, u.vz };
    u.path = pa;
    u.mode = Mode::clang; u.mt = 0; u.clang = Clang { spire, T1 + 3.6, false };
}
void MetalWorld::stepDragons (double dt)
{
    if (dt <= 0) return;
    const double L = loopLength();
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading), v = s.speed;
    const double toStretch = modL (dragonSpec.swoopAt * L - s.sr), pass = s.lap * 100000.0;
    bool busy = false;
    for (const auto& u : dragons_) if (u.mode == Mode::swoop || u.mode == Mode::clang) busy = true;
    // the swooping stretch: one waits over it, then swoops at the rover
    if (! parked && toStretch < 520 && ! same (dc.swoopPass, pass))
    {
        Dragon& u = dragons_[0];
        if (u.mode == Mode::roam && u.kind != Kind::prep) anchor (u, Kind::prep);
        if (u.mode == Mode::roam && toStretch < 150 + v * 4) { startSwoop (u); dc.swoopPass = pass; }
    }
    else if (! parked && ! busy && s.time > dc.nextSwoop && toStretch > 700)
    {
        Dragon* u = nullptr;
        for (auto& q : dragons_) if (q.mode == Mode::roam && q.front) { u = &q; break; }
        if (! u) for (auto& q : dragons_) if (q.mode == Mode::roam) { u = &q; break; }
        if (u) startSwoop (*u);
        dc.nextSwoop = s.time + dragonSpec.swoopEvery[0] + (dragonSpec.swoopEvery[1] - dragonSpec.swoopEvery[0]) * dh (9);
    }
    // the citadel: two perch on its towers while the rover comes by; on odd laps one flies into a spire
    const double toCit = modL (citadel_.at - s.sr);
    if (! parked && toCit < 640 && toCit > 120 && ! same (dc.perchPass, pass))
    {
        dc.perchPass = pass;
        const int dragonOf[2] = { 1, 2 }, towerOf[2] = { 1, 3 };
        for (int k = 0; k < 2; ++k)
        {
            Dragon& u = dragons_[(size_t) dragonOf[k]];
            if (u.mode == Mode::clang) continue;
            int pi_ = -1;
            for (size_t q = 0; q < perches_.size(); ++q) if (perches_[q].name == "citadel" && perches_[q].tower == towerOf[k]) { pi_ = (int) q; break; }
            if (pi_ >= 0) startPerch (u, perches_[(size_t) pi_], pi_);
        }
    }
    if (! parked && s.lap % 2 == 1 && toCit < 460 && toCit > 200 && ! same (dc.clangPass, pass) && dragons_[0].mode == Mode::roam)
    { dc.clangPass = pass; startClang (dragons_[0], 2); }
    // PARK: they come nearer and perch on what stands ahead (or on the ground)
    if (parked && s.speed < 0.5)
        for (auto& u : dragons_)
        {
            if (u.mode != Mode::roam || u.kind != Kind::park || u.anchorT < 3 + 4 * u.i) continue;
            std::vector<int> free;
            for (size_t q = 0; q < perches_.size(); ++q)
            {
                const Perch& p = perches_[q];
                bool takenBy = false;
                for (const auto& o : dragons_) if (o.perchOf == (int) q) takenBy = true;
                if (! takenBy && hyp (p.x - s.x, p.z - s.z) < 380 && ((p.x - s.x) * fx + (p.z - s.z) * fz) > 40) free.push_back ((int) q);
            }
            std::stable_sort (free.begin(), free.end(), [this] (int a, int b)
            {
                return hyp (perches_[(size_t) a].x - s.x, perches_[(size_t) a].z - s.z) - hyp (perches_[(size_t) b].x - s.x, perches_[(size_t) b].z - s.z) < 0;
            });
            if (! free.empty()) startPerch (u, perches_[(size_t) free[0]], free[0]);
            else if (u.i < 2)
            {
                const V2 gp = roadPointAt (s.sr + 55 + 25 * u.i, (u.i ? 1 : -1) * (18 + 6 * u.i));
                startPerch (u, { gp[0], floorHeight (gp[0], gp[1]), gp[1], yawOf (s.x - gp[0], s.z - gp[1]), "ground", -1 }, -1);
            }
        }
    bool seen = false;
    for (auto& u : dragons_)
    {
        u.mt += dt; u.anchorT += dt; u.flameT += dt; u.hicT += dt; u.ringT += dt;
        const V3 pv { u.vx, u.vy, u.vz };
        if (u.mode == Mode::swoop || u.mode == Mode::toPerch || u.mode == Mode::clang || u.mode == Mode::leave)
        {
            Path& pa = *u.path;
            const double T = pa.ts.back();
            const V3 p1 = pathAt (pa, u.mt), p0 = pathAt (pa, u.mt - dt);
            u.x = p1[0]; u.y = p1[1]; u.z = p1[2]; u.vx = (p1[0] - p0[0]) / dt; u.vy = (p1[1] - p0[1]) / dt; u.vz = (p1[2] - p0[2]) / dt;
            if (u.mode == Mode::swoop && pa.ringAt && u.mt >= *pa.ringAt) { u.ringT = 0; u.ring = Ring { u.mouth, { s.x - u.x, s.camY - u.y, s.z - u.z } }; pa.ringAt.reset(); }
            if (u.mode == Mode::clang && ! u.clang->done && u.mt >= u.clang->hitAt)
            {
                u.clang->done = true; u.tumble = 1.6;
                const int k = u.clang->spire;
                shakes_[(size_t) k] = 0;
                const Spire& sp = spires_[(size_t) k];
                sparks_.push_back ({ 0, { sp.x, sp.y, sp.z }, 0.6 }); if (sparks_.size() > 4) sparks_.erase (sparks_.begin());
                // it drops, shakes it off and flies away
                const V3 out = P (citadel_.at + 200, -260, 140), dn { u.x, u.y - 14, u.z };
                Path np;
                np.pts = { { u.x, u.y, u.z }, dn, { (dn[0] + out[0]) / 2, dn[1] + 10, (dn[2] + out[2]) / 2 }, out };
                np.ts = { 0, 1.4, 4.5, 10 }; np.v0 = V3 { 0, -2, 0 };
                u.path = np;
                u.mode = Mode::leave; u.mt = 0; u.takeoff.reset();
            }
            if (u.mt >= T)
            {
                if (u.mode == Mode::toPerch) { u.mode = Mode::perch; u.mt = 0; u.perchT = dragonSpec.perchFor[0] + (dragonSpec.perchFor[1] - dragonSpec.perchFor[0]) * dh (10); }
                else { u.mode = Mode::roam; u.path.reset(); anchor (u, u.i == 0 ? Kind::front : Kind::circle); }
            }
        }
        else if (u.mode == Mode::perch)
        {
            const Perch q = *u.perch;
            perchGestures (u, dt);
            const V2 f0 = perchFacing (q);
            u.x = q.x - f0[1] * u.g.side; u.y = q.y + perchLift; u.z = q.z + f0[0] * u.g.side; u.vx = u.vy = u.vz = 0;
            const bool leaveNow = parked ? false : (q.name == "citadel" ? modL (s.sr - citadel_.at) < L / 2 && modL (s.sr - citadel_.at) > 160 : u.mt > u.perchT);
            if (leaveNow || (! parked && q.name == "ground"))
            {
                u.takeoff = std::array<V3, 2> { u.F, u.U };
                Path pa;
                pa.pts = { { u.x, u.y, u.z }, { u.x, u.y + 9, u.z }, P (s.sr + 300, (u.i % 2 ? 1 : -1) * 220, 120) };
                pa.ts = { 0, takeoffSeconds, 9 }; pa.v0 = V3 { 0, 4, 0 };
                u.path = pa;
                u.mode = Mode::leave; u.mt = 0; u.perch.reset(); u.perchOf = -1;
            }
        }
        else
        {
            u.as += v * u.follow * dt;
            const double along = u.as - s.sr;
            if (parked && u.kind != Kind::park) anchor (u, Kind::park);
            else if (! parked && u.kind == Kind::park) anchor (u, u.i == 0 ? Kind::front : Kind::circle);
            else if (u.kind != Kind::prep && ((u.front && along < 80) || u.anchorT > (u.front ? 40 : 28)))
            {
                const Kind next = parked ? Kind::park : dh (11) < (u.i == 0 ? 0.75 : 0.45) ? Kind::front : Kind::circle;
                anchor (u, next);
            }
            else if (u.kind == Kind::prep && (along < 0 || along > 640)) anchor (u, Kind::front);
            const V3 tt = roamTarget (u, s.time);
            double wx = (tt[0] - u.x) / 2.6, wy = (tt[1] - u.y) / 2.6, wz = (tt[2] - u.z) / 2.6;
            const double wl = hyp3 (wx, wy, wz), vm = dragonSpec.vmax;
            if (wl > vm) { wx *= vm / wl; wy *= vm / wl; wz *= vm / wl; }
            const double k = ease (dt, 1.1);
            u.vx += (wx - u.vx) * k; u.vy += (wy - u.vy) * k; u.vz += (wz - u.vz) * k;
            // a dragon is never still in the air: at least a glide's speed along its way
            const double hv = hyp (u.vx, u.vz);
            if (hv < 9) { const double a = Js::atan2 (u.vz, u.vx) + 0.0; u.vx = Js::cos (a) * 9; u.vz = Js::sin (a) * 9; }
            u.x += u.vx * dt; u.y += u.vy * dt; u.z += u.vz * dt;
            u.y = jsMax (u.y, floorHeight (u.x, u.z) + 12);
            // now and then a hiccup of fire
            if (u.hicT > u.nextHic) { u.hicT = 0; u.nextHic = dragonSpec.hiccupEvery[0] + (dragonSpec.hiccupEvery[1] - dragonSpec.hiccupEvery[0]) * hs (dc.n++, 832); }
        }
        orient (u, pv, dt);
        u.tumble = jsMax (0, u.tumble - dt);
        u.shake = u.tumble > 0 ? Js::sin (u.tumble * 18) * u.tumble * 0.25 : 0;
        // in the front view: ahead within 40 degrees, from just under the horizon to 25 degrees up, within 600 m
        const double dx = u.x - s.x, dz = u.z - s.z, dy = u.y - s.camY, D = hyp (dx, dz);
        if (D < 600 && (dx * fx + dz * fz) > D * 0.766 && dy > -0.05 * D && dy < 0.47 * D) seen = true;
    }
    dc.sinceSeen = seen ? 0 : dc.sinceSeen + dt;
    if (seen) dc.called = -1;
    if (dc.sinceSeen > dragonSpec.dark && dc.called < 0)
    {
        Dragon* u = nullptr;
        for (auto& q : dragons_) if (q.mode == Mode::roam && ! q.front) { u = &q; break; }
        if (! u) for (auto& q : dragons_) if (q.mode == Mode::roam) { u = &q; break; }
        if (u) { anchor (*u, parked ? Kind::park : Kind::front); dc.called = u->i; }
    }
    else if (dc.called >= 0 && dc.sinceSeen > dragonSpec.dark + 12) dc.called = -1;
}
std::array<V3, 2> MetalWorld::perchFrame (const Dragon& u) const
{
    const V2 a0 = perchFacing (*u.perch);
    const double gy = u.g.yaw, ax = a0[0] * Js::cos (gy) - a0[1] * Js::sin (gy), az = a0[0] * Js::sin (gy) + a0[1] * Js::cos (gy);
    const double br = 0.6 * Js::sin (s.time * 1.05 + u.i * 2.1) + 0.4 * Js::sin (s.time * 0.47 + u.i * 4.3);
    const double pitch = 0.15 + 0.022 * br + u.g.pitch, roll = 0.012 * Js::sin (s.time * 0.63 + u.i) + 0.01 * Js::sin (s.time * 0.29 + u.i * 1.7);
    const V3 f { ax * Js::cos (pitch), Js::sin (pitch), az * Js::cos (pitch) };
    const V3 up { -ax * Js::sin (pitch), Js::cos (pitch), -az * Js::sin (pitch) }, lf { az, 0, -ax };
    return { f, V3 { up[0] * Js::cos (roll) + lf[0] * Js::sin (roll), up[1] * Js::cos (roll), up[2] * Js::cos (roll) + lf[2] * Js::sin (roll) } };
}
void MetalWorld::orient (Dragon& u, const V3& pv, double dt)
{
    V3 F {}, U {};
    bool direct = false;
    double beatTo = -1;
    const double hv = hyp (u.vx, u.vz);
    if (hv > 3 || ! u.head) u.head = hv > 1e-3 ? V2 { u.vx / hv, u.vz / hv } : (u.head ? *u.head : V2 { 0, 1 });
    if (u.mode != Mode::perch) u.g = Pose {};
    if (u.mode == Mode::perch)
    {
        const auto fu = perchFrame (u); F = fu[0]; U = fu[1];
        beatTo = fract (0.35 + 0.006 * Js::sin (s.time * 1.05 + u.i * 2.1) + u.g.wing);
    }
    else if (u.mode == Mode::leave && u.takeoff && u.mt < takeoffSeconds)
    {
        const V3 f0 = (*u.takeoff)[0], u0 = (*u.takeoff)[1];
        const double e = u.mt / takeoffSeconds, a = 2 * pi * e * e * e * (e * (e * 6 - 15) + 10);
        for (size_t c = 0; c < 3; ++c) F[c] = f0[c] * Js::cos (a) + u0[c] * Js::sin (a);
        for (size_t c = 0; c < 3; ++c) U[c] = u0[c] * Js::cos (a) - f0[c] * Js::sin (a);
        direct = true; u.rate = 1.5;
    }
    else
    {
        const double hx = (*u.head)[0], hz = (*u.head)[1];
        const double climb = clamp (u.vy / jsMax (hv, 6), -0.8, 0.8);
        const double pitch = jsAtan (climb) * 0.8;
        F = { hx * Js::cos (pitch), Js::sin (pitch), hz * Js::cos (pitch) };
        const double ax = dt > 0 ? (u.vx - pv[0]) / dt : 0, az = dt > 0 ? (u.vz - pv[2]) / dt : 0;
        const double lat = clamp ((ax * -hz + az * hx) * 0.06, -0.75, 0.75) * smoothstep (2, 8, hv) + u.shake * 3;
        const double lx = -hz, lz = hx;
        const V3 up0 { -Js::sin (pitch) * hx, Js::cos (pitch), -Js::sin (pitch) * hz };
        U = { up0[0] * Js::cos (lat) + lx * Js::sin (lat), up0[1] * Js::cos (lat), up0[2] * Js::cos (lat) + lz * Js::sin (lat) };
        u.rate = clamp (1.0 + 0.6 * climb + (u.mode == Mode::swoop ? 0.2 : 0), 0.45, 1.7);
        if (climb < -0.3 && u.mode == Mode::swoop) u.rate = 0.4;
        if (u.mode == Mode::toPerch)
        {
            const auto& T = u.path->ts;
            const size_t n = T.size();
            const double k = smoothstep (T[n - 2] - 0.6, T[n - 1], u.mt);
            if (u.mt > T[n - 2] - 1.2) beatTo = 0.0;
            if (k > 0)
            {
                const auto pf = perchFrame (u);
                for (size_t c = 0; c < 3; ++c) F[c] = F[c] + (pf[0][c] - F[c]) * k;
                for (size_t c = 0; c < 3; ++c) U[c] = U[c] + (pf[1][c] - U[c]) * k;
            }
        }
    }
    const double bl = direct ? 1 : 1 - Js::exp (-dt / (u.mode == Mode::perch ? 0.7 : 0.25));
    for (size_t c = 0; c < 3; ++c) u.F[c] = u.F[c] + (F[c] - u.F[c]) * bl;
    for (size_t c = 0; c < 3; ++c) u.U[c] = u.U[c] + (U[c] - u.U[c]) * bl;
    const double fl = orOne (hyp3 (u.F)); for (auto& q : u.F) q = q / fl;
    const double d = u.U[0] * u.F[0] + u.U[1] * u.F[1] + u.U[2] * u.F[2];
    for (size_t c = 0; c < 3; ++c) u.U[c] = u.U[c] - u.F[c] * d;
    const double ul = orOne (hyp3 (u.U)); for (auto& q : u.U) q = q / ul;
    if (beatTo >= 0)
    {
        // toward a held pose, forward round the beat (the wings rise through it), never faster than the beat itself
        double dd = beatTo - u.beat; dd -= std::floor (dd);
        if (dd > 0.5 && u.mode == Mode::perch) dd -= 1;
        u.beat = fract (u.beat + clamp (dd, -dt * 0.25, dt / dragonSpec.beat * 1.2));
    }
    else u.beat = fract (u.beat + dt * u.rate / dragonSpec.beat);
    // its mouth: the tip of the head in the current pose of the wing-beat, the neck bent as the shader bends it
    const V3 L { u.U[1] * u.F[2] - u.U[2] * u.F[1], u.U[2] * u.F[0] - u.U[0] * u.F[2], u.U[0] * u.F[1] - u.U[1] * u.F[0] };
    const double k = dragonSpec.scale;
    V3 mo { 0, -3.28, -1.21 }, mi { 0, -2.72, -1.09 };
    if (dragonHead && ! dragonHead->mouth.empty())
    {
        const int nf = (int) dragonHead->mouth.size();
        const double f = u.beat * nf, mx = f - std::floor (f);
        const int k0 = (int) mod (std::floor (f), nf), k1 = (k0 + 1) % nf;
        const auto at = [&] (const std::vector<V3>& A) { V3 o {}; for (size_t c = 0; c < 3; ++c) o[c] = A[(size_t) k0][c] + (A[(size_t) k1][c] - A[(size_t) k0][c]) * mx; return o; };
        mo = at (dragonHead->mouth); mi = at (dragonHead->mid);
    }
    const auto bendNeck = [] (const V3& p, double hy, double hp)
    {
        const double w = 1 - smoothstep (-2.2, -0.6, p[1]);
        const double dx = p[0], dy = p[1] + 0.6, dz = p[2] + 0.5;
        const double a = hy * w, b = hp * w, ca = Js::cos (a), sa = Js::sin (a), cb = Js::cos (b), sb = Js::sin (b);
        const double x1 = dx * ca - dy * sa, y1 = dx * sa + dy * ca;
        const double y2 = y1 * cb + dz * sb, z2 = dz * cb - y1 * sb;
        return V3 { x1, y2 - 0.6, z2 - 0.5 };
    };
    mo = bendNeck (mo, u.g.hy, u.g.hp); mi = bendNeck (mi, u.g.hy, u.g.hp);
    const auto toW = [&] (const V3& p) { const double lx = p[0], ly = p[2], lz = -p[1]; V3 o {}; for (size_t c = 0; c < 3; ++c) o[c] = (L[c] * lx + u.U[c] * ly + u.F[c] * lz) * k; return o; };
    const V3 wm = toW (mo), wi = toW (mi);
    u.mouth = { u.x + wm[0], u.y + wm[1], u.z + wm[2] };
    const V3 hd { wm[0] - wi[0], wm[1] - wi[1], wm[2] - wi[2] };
    const double hl = orOne (hyp3 (hd));
    u.headDir = { hd[0] / hl, hd[1] / hl, hd[2] / hl };
    u.L = L;
}

//==============================================================================
// THE JUMP LIST

std::vector<MetalWorld::Place> MetalWorld::places() const
{
    std::vector<Place> out;
    const double L = loopLength();
    for (int lap = 0; lap < 5; ++lap)
        for (int slot = 0; slot < 3; ++slot)
        {
            const int k = (slot + 3 * lap) % 5;
            out.push_back ({ "finds", std::string (findKinds[(size_t) k]) + " \xc2\xb7 lap " + std::to_string (lap), findSlots[(size_t) slot] * L, 260, lap, k });
        }
    const auto big = [&] (const char* name, double at, double seen) { out.push_back ({ "big", name, at, seen, -1, -1 }); };
    big ("skull gate", 0, 380);
    big ("citadel and Tesla towers", citadel_.at, 560);
    big ("the steel guardian", warrior_.at, 420);
    big ("amp wall", amps_.at, 420);
    big ("steel Stonehenge", henge_.at, 480);
    big ("anvil", anvil_.at, 300);
    big ("Darwin's Cat", cat_.at, 420);
    big ("festival stage", stage_.at, 700);
    big ("fossil", fossilSpec.at * L, 260);
    for (size_t i = 0; i < pyroSpecs.size(); ++i) out.push_back ({ "road", "pyro columns " + std::to_string (i + 1), pyroSpecs[i].at * L, 160, -1, -1 });
    for (size_t i = 0; i < fountainSpecs.size(); ++i) out.push_back ({ "road", "lava fountain " + std::to_string (i + 1), fountainSpecs[i].at * L, 220, -1, -1 });
    std::map<std::string, int> seenN;
    for (const auto& q : letters_)
    {
        const int n = ++seenN[q.name];
        const auto it = letterNames.find (q.name);
        const bool many = it != letterNames.end() && it->second > 1;
        out.push_back ({ "signs", many ? q.name + " " + std::to_string (n) : q.name, q.at, q.big ? 600.0 : 260.0, -1, -1 });
    }
    for (const auto& lm : landmarks_) if (lm.came) out.push_back ({ "big", lm.label, lm.at, 320, -1, -1 });
    for (const auto& gr : groves_) out.push_back ({ "big", "guitar grove " + std::to_string (gr.g + 1) + " (" + std::to_string (gr.members.size()) + ")", gr.at - 50, 380, -1, -1 });
    out.push_back ({ "sky", "dragon swoop", dragonSpec.swoopAt * L, 140, -1, -1 });
    out.push_back ({ "sky", "dragons perch on the citadel", citadel_.at, 520, -1, -1 });
    out.push_back ({ "sky", "a dragon flies into a spire \xc2\xb7 lap 1", citadel_.at, 520, 1, -1 });
    const auto lapOf = [] (const Place& q) { return q.group == "finds" ? q.lap : -1; };
    std::stable_sort (out.begin(), out.end(), [&] (const Place& a, const Place& b)
    {
        const int dl = lapOf (a) - lapOf (b);
        return (dl != 0 ? (double) dl : a.at - b.at) < 0;
    });
    return out;
}
MetalWorld::Jump MetalWorld::jumpTo (const Place& pl)
{
    const double L = loopLength();
    const double at = mod (mod (pl.at - pl.seen - 3 * speedOf(), L) + L, L);
    const int lap = pl.lap >= 0 ? pl.lap : s.lap;
    startAt (at, lap);
    return { Js::round (at), lap };
}

//==============================================================================
// WHAT THE RENDERER FEEDS THE GPU EACH FRAME (metal-gl.js's drawScene)

MetalWorld::Fires MetalWorld::fires() const
{
    Fires out;
    out.em.assign ((size_t) maxEmit * 12, 0.0f);
    const double L = loopLength();
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double hitAge = jsMin (s.hitT, 1e4);
    struct Light { V3 p; double k, D; };
    std::vector<Light> lights;
    const auto emit = [&] (const V3& p, double kind, const V3& dir, double size, double age, double seed, double str, double dur)
    {
        if (out.n >= maxEmit) return;
        const double v[12] = { p[0] - ccx, p[1], p[2] - ccz, kind, dir[0], dir[1], dir[2], size, age, seed, str, dur };
        for (int c = 0; c < 12; ++c) out.em[(size_t) (out.n * 12 + c)] = f32 (v[c]);
        ++out.n;
    };
    for (const auto& pz : pyro_)
    {
        const double dx = pz.x - s.x, dz = pz.z - s.z, D = hyp (dx, dz);
        if (D > 420 || (D > 40 && dx * fx + dz * fz < 0.2 * D)) continue;
        const double delay = std::abs (modL (pz.at - s.sr + L / 2) - L / 2) / 160;
        const double age = s.hitS > 0 && hitAge < 4 ? hitAge - delay : 1e4;
        emit ({ pz.x, pz.y, pz.z }, 0, { 0, 1, 0 }, 1, age, mod (pz.i * 0.137, 1), s.hitS, 0);
        if (age > 0 && age < 1.6) lights.push_back ({ { pz.x, pz.y + 7, pz.z }, 3.2 * Js::exp (-age / 0.7) * s.hitS, D });
    }
    for (const auto& u : dragons_)
    {
        const double D = hyp (u.x - s.x, u.z - s.z);
        if (D > farCut) continue;
        // its fire goes out of the mouth along its heading, never at the rover: what of the heading points at the
        // rover is taken out and turned up instead
        const auto away = [&] (const V3& d0)
        {
            const V3 tv = norm3 ({ s.x - u.mouth[0], s.camY - u.mouth[1], s.z - u.mouth[2] });
            const double k = d0[0] * tv[0] + d0[1] * tv[1] + d0[2] * tv[2];
            return k > 0 ? norm3 ({ d0[0] - tv[0] * k, d0[1] - tv[1] * k + 0.6 * k + 0.1, d0[2] - tv[2] * k }) : norm3 ({ d0[0], d0[1] + 0.08, d0[2] });
        };
        if (u.flameT < 1.9)
        {
            const V3 d = away (u.headDir);
            emit (u.mouth, 1, d, dragonSpec.scale, u.flameT, mod (u.i * 0.31, 1), 1, 1.5);
            lights.push_back ({ { u.mouth[0] + d[0] * 12, u.mouth[1] + d[1] * 12, u.mouth[2] + d[2] * 12 },
                                4.0 * smoothstep (0, 0.2, u.flameT) * (1 - smoothstep (1.3, 1.8, u.flameT)), D });
        }
        if (u.hicT < 0.9) emit (u.mouth, 4, away (u.headDir), dragonSpec.scale, u.hicT, mod (u.i * 0.53 + 0.2, 1), 1, 0.3);
        if (u.ringT < 3.6 && u.ring) emit (u.ring->at, 2, u.ring->dir, 1.0, u.ringT, 0.5, 1, 0);
    }
    for (const auto& f : fountains_)
    {
        const double D = hyp (f.x - s.x, f.z - s.z);
        if (D > 800 || f.t > 5) continue;
        emit ({ f.x, f.y + 0.4, f.z }, 3, { 0, 1, 0 }, 1, f.t, mod (f.n * 0.173, 1), 1, 0);
        if (f.t < 3) lights.push_back ({ { f.x, f.y + 6, f.z }, 2.0 * (1 - f.t / 3), D });
    }
    {
        const Stage& st = stage_;
        const double D = hyp (st.x - s.x, st.z - s.z);
        if (D < 2200 && s.hitS > 0 && hitAge < 3)
            for (int k = 0; k < 4; ++k)
            {
                const double lx = (k - 1.5) * 16, c = Js::cos (st.yaw), sn = Js::sin (st.yaw);
                emit ({ st.x + c * lx + (-sn) * 12, st.y + 6, st.z + sn * lx + c * 12 }, 0, { 0, 1, 0 }, 2.2, hitAge - k * 0.05, 0.7 + k * 0.05, s.hitS, 0);
            }
    }
    for (const auto& o : orbs_) emit ({ o.x, o.y, o.z }, 5, { 0, 1, 0 }, 1, 0, mod (o.i * 0.29, 1), 1, 0);
    for (const auto& Lt : letters_)
    {
        const double dx = Lt.x - s.x, dz = Lt.z - s.z, D = hyp (dx, dz);
        if (D > (Lt.big ? 700 : 320) || (D > 40 && dx * fx + dz * fz < 0.3 * D)) continue;
        const double rx = Js::cos (Lt.yaw) * Lt.hw, rz = -Js::sin (Lt.yaw) * Lt.hw;
        emit ({ Lt.x, Lt.y, Lt.z }, 7, { rx, 0, rz }, 1, 0, mod (Lt.seed * 7.3, 1), 1 + 0.8 * s.hitS * Js::exp (-hitAge / 1.2), Lt.hh);
        lights.push_back ({ { Lt.x, Lt.y - Lt.hh, Lt.z }, Lt.big ? 1.6 : 0.9, D });
    }
    std::stable_sort (lights.begin(), lights.end(), [] (const Light& a, const Light& b)
    {
        const double qa = a.D / 60, qb = b.D / 60;
        return b.k / (1 + qa * qa) - a.k / (1 + qb * qb) < 0;
    });
    for (size_t i = 0; i < lights.size() && i < 2; ++i)
    {
        out.lights[i * 4] = f32 (lights[i].p[0] - ccx); out.lights[i * 4 + 1] = f32 (lights[i].p[1]);
        out.lights[i * 4 + 2] = f32 (lights[i].p[2] - ccz); out.lights[i * 4 + 3] = f32 (lights[i].k);
    }
    return out;
}

MetalWorld::Ribbons MetalWorld::ribbons (bool beams) const
{
    Ribbons out;
    out.rb.assign ((size_t) maxRib * 16, 0.0f);
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const double hitAge = jsMin (s.hitT, 1e4), time = mod (s.time, 100000);
    const auto seg = [&] (const V3& a, const V3& b, double wa, double wb, const V3& col, double k, double kind, double ta, double tb)
    {
        if (out.n >= maxRib) return;
        const double v[16] = { a[0] - ccx, a[1], a[2] - ccz, wa, b[0] - ccx, b[1], b[2] - ccz, wb, col[0], col[1], col[2], k, kind, ta, tb, 0 };
        for (int c = 0; c < 16; ++c) out.rb[(size_t) (out.n * 16 + c)] = f32 (v[c]);
        ++out.n;
    };
    const auto line = [&] (const std::vector<V3>& pts, double w, const V3& col, double k) { for (size_t i = 0; i + 1 < pts.size(); ++i) seg (pts[i], pts[i + 1], w, w, col, k, 0, 0, 0); };
    for (const auto& b : bolts_)
    {
        const double I = boltI (b.t) * b.k;
        if (I < 0.02) continue;
        for (const auto& Ln : b.lines) line (Ln.pts, 2.6 * Ln.w, { 0.62, 0.58, 1.0 }, I * (0.5 + 0.7 * Ln.w));
    }
    {
        const auto& T = tesla_;
        const auto arc = [&] (int i, double k, double sd)
        {
            const TeslaTower& A = T[(size_t) i];
            const TeslaTower& B = T[(size_t) i + 1];
            const double dx = B.x - A.x, dz = B.z - A.z, l = hyp (dx, dz);
            const V3 a { A.x + dx / l * 3.4, A.top + 0.5, A.z + dz / l * 3.4 }, b { B.x - dx / l * 3.4, B.top + 0.5, B.z - dz / l * 3.4 };
            line (jag (a, b, 4, 0.16, sd), 0.5, { 0.55, 0.7, 1.0 }, k * 1.3);
        };
        const double crackle = std::floor (time / 0.045);
        if (teslaArc.t < teslaArc.dur) arc (teslaArc.i, 1.0, crackle * 7 + teslaArc.i);
        for (size_t i = 0; i + 1 < T.size(); ++i)
        {
            const double t0 = teslaChain - (double) i * 0.07;
            if (t0 > 0 && t0 < 0.5) { arc ((int) i, 1.8 * (1 - t0 / 0.5), crackle * 11 + (double) i * 3); arc ((int) i, 1.0 * (1 - t0 / 0.5), crackle * 13 + (double) i * 5 + 1); }
        }
    }
    {
        const Stage& st = stage_;
        const double D = hyp (st.x - s.x, st.z - s.z);
        if (D < 2600 && beams)
        {
            const double c = Js::cos (st.yaw), sn = Js::sin (st.yaw), fl = s.hitS * Js::exp (-hitAge / 0.5);
            for (int k = 0; k < 6; ++k)
            {
                const double lx = (k - 2.5) * 11;
                const V3 base { st.x + c * lx - sn * 13, st.y + 41, st.z + sn * lx + c * 13 };
                const double az = st.yaw + 0.55 * Js::sin (time * (0.21 + 0.04 * k) + k * 1.7), el = 1.05 + 0.3 * Js::sin (time * (0.17 + 0.03 * k) + k * 2.3);
                const V3 d { -Js::sin (az) * Js::cos (el) * 0 + Js::sin (az) * Js::cos (el), Js::sin (el), Js::cos (az) * Js::cos (el) };
                const auto at = [&] (double q) { return V3 { base[0] + d[0] * 1100 * q, base[1] + d[1] * 1100 * q, base[2] + d[2] * 1100 * q }; };
                for (int j = 0; j < 10; ++j)
                {
                    const double q0 = j / 10.0, q1 = (j + 1) / 10.0;
                    seg (at (q0), at (q1), 1.6 + 73 * q0, 1.6 + 73 * q1, { 0.60, 0.66, 0.85 }, 0.42 * (1 + 2.0 * fl), 1, q0, q1);
                }
            }
        }
    }
    return out;
}

} // namespace orbitamp::metal
