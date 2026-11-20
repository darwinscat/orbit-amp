// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of io-world.js, line for line: the hashes and the textures, the loop, the Sun, makeWorld's places (the
// paterae, the plumes, the fountains, the flows and the feature map, the hardware, the rocks), the drive, the knobs,
// the activity and its history, the dosimeter, the jump list. Every expression keeps the prototype's order of
// operations, so that the doubles round where the JS's round; f32 where it stores into a Float32Array or calls
// Math.fround. What io-gl.js works out for a frame, the meshes and the finds' readers are in IoFrame.cpp.

#include "IoWorld.h"
#include "IoJs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace orbitamp::io
{

using namespace js;
using V2 = IoWorld::V2;
using V3 = IoWorld::V3;

namespace
{
    constexpr double PI = IoWorld::pi, DEG = IoWorld::deg;

    /** The small 32-bit generator whose stream is the landscape (the family's). */
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
    /** The recursive-subdivision fractal on a wrapping n x n grid, in float. */
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
    double vnoise (double x, double y, double fx, double fy, double seed) noexcept
    {
        const double px = x * fx, py = y * fy, ix = std::floor (px), iy = std::floor (py);
        const double ux0 = px - ix, uy0 = py - iy, ux = ux0 * ux0 * (3 - 2 * ux0), uy = uy0 * uy0 * (3 - 2 * uy0);
        const auto h = [&] (double i, double j) { return IoWorld::hcell (mod (mod (i, fx) + fx, fx), mod (mod (j, fy) + fy, fy), seed); };
        return lerp (lerp (h (ix, iy), h (ix + 1, iy), ux), lerp (h (ix, iy + 1), h (ix + 1, iy + 1), ux), uy);
    }
    double fbm (double x, double y, double f0, int octaves, double rough, double seed) noexcept
    {
        double a = 1, sum = 0, norm = 0, f = f0;
        for (int o = 0; o < octaves; ++o) { sum += a * vnoise (x, y, f, f, seed + o * 7919); norm += a; a *= rough; f *= 2; }
        return sum / norm;
    }
    inline uint8_t u8 (double v) noexcept { return (uint8_t) (toU32 (v) & 0xffu); }
    inline double hyp (double a, double b) noexcept { return Js::hypot (a, b); }
    V3 norm3 (const V3& v) noexcept { const double l = orOne (Js::hypot (v[0], v[1], v[2])); return { v[0] / l, v[1] / l, v[2] / l }; }
    V3 cross3 (const V3& a, const V3& b) noexcept { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    V3 mix3 (const V3& a, const V3& b, double t) noexcept { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
    double smin (double a, double b, double k) noexcept { const double h = jsMax (k - std::abs (a - b), 0) / k; return jsMin (a, b) - h * h * k / 4; }
    double smax (double a, double b, double k) noexcept { return -smin (-a, -b, k); }

    // THE TABLES (io-world.js CALDERAS, PLUMES, FOUNTAINS, STATIONS, SITES)
    struct CalderaSpec { const char* name; double at, d, R, W, D, rim, lake, th; bool big, near; };
    const CalderaSpec calderaSpecs[] = {
        { "the great patera",          0.300, 345,  240, 16, 7,   2.0, 0.94, 0.00, true,  false },
        { "the lava lake by the road", 0.090, -62,  46,  7,  2.6, 1.2, 0.90, 0.00, false, true },
        { "the twin lakes",            0.560, 70,   38,  8,  3.5, 1.4, 0.86, 0.05, false, false },
        { "the twin lakes (west)",     0.575, 152,  34,  8,  4.0, 1.4, 0.80, 0.15, false, false },
        { "a crusted patera",          0.760, -120, 80,  12, 9,   2.0, 0.55, 0.35, false, false },
        { "the deep patera",           0.880, 210,  120, 18, 22,  3.0, 0.70, 0.20, false, false },
        { "a dark patera",             0.180, -300, 95,  14, 12,  2.0, 0.0,  1.10, false, false },
        { "a small patera",            0.430, -95,  30,  6,  3.0, 1.0, 0.70, 0.25, false, false },
    };
    struct PlumeSpec { const char* name; bool far; double a, b, H, th; bool pele; double base, view; };    // far: degrees, metres; else at, d; view < 0: none
    const PlumeSpec plumeSpecs[] = {
        { "the great plume (Pele-type)",   true,  32,    2300, 470, 0.12, true,  0,    4525 },
        { "the plume beside Jupiter",      true,  99,    2555, 380, 0.08, false, 0,    -1 },
        { "a plume over the ridge",        true,  200,   2150, 380, 0.05, true,  0.06, 1950 },
        { "a plume to the south",          true,  262,   2400, 300, 0.42, false, 0,    -1 },
        { "a plume by the crusted patera", true,  -25,   1850, 260, 0.55, false, 0,    -1 },
        { "a near plume",                  false, 0.660, -330, 240, 0.48, false, 0,    2905 },
        { "a plume inside the loop",       true,  150,   330,  290, 0.62, false, 0,    -1 },
        { "a plume out west",              true,  -80,   2600, 420, 0.20, true,  0.05, -1 },
        { "a plume on the plain",          false, 0.960, -520, 270, 0.70, false, 0,    -1 },
    };
    struct FountainSpec { int cald; double toward, H, th; };
    const FountainSpec fountainSpecs[] = { { 1, 0.35, 46, 0.55 }, { 1, -0.25, 30, 0.75 }, { 2, 0.30, 38, 0.62 }, { 0, 0.82, 60, 0.58 }, { 0, 0.55, 44, 0.80 }, { 7, 0.20, 26, 0.85 } };
    struct StationSpec { const char* name; int variant; bool lake; double at, side, d; V2 foot; bool hasRover; V2 rover; double seen; };   // seen 0: 75
    const StationSpec stationSpecs[] = {
        { "the buried base",             6, false, 0.150, -1, 38, { 60, 34 }, true,  { 22.5, 22 }, 90 },
        { "the lava-tube skylight base", 7, false, 0.470, 1,  38, { 50, 44 }, false, { 0, 0 },     90 },
        { "the lava-lake observatory",   8, true,  0,     0,  0,  { 0, 0 },   false, { 0, 0 },     0 },
        { "the SO2 plant",               9, false, 0.625, 1,  40, { 66, 42 }, true,  { 10, 24 },   100 },
    };
    struct SiteSpec { double at, side; int third; bool rover; };
    const SiteSpec siteSpecs[] = { { 0.055, 1, 0, true }, { 0.205, -1, 3, false }, { 0.370, 1, 1, false },
                                   { 0.535, -1, 2, true }, { 0.690, 1, 3, false }, { 0.845, -1, 0, false } };
    constexpr double structProbIn = 0.34, structProbOut = 0.008, siteProb = 0.0;

    std::string numberText (double v)
    {
        char b[32];
        std::snprintf (b, sizeof b, "%.17g", v);
        return b;
    }
}

const std::array<const char*, IoWorld::numVariants> IoWorld::variants { "mast", "dish", "module", "lander", "station", "rover", "base", "tubebase", "observatory", "plant" };

//==============================================================================
// HASHES, NOISE, THE TEXTURES

uint32_t IoWorld::ih (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
double IoWorld::hcell (double cx, double cy, double s) noexcept
{
    return f32 ((double) ih ((toU32 (cx) * 0x9E3779B1u) ^ ih ((toU32 (cy) * 0x85EBCA77u) ^ toU32 (s))) / 4294967296.0);
}
double IoWorld::hs (double i, double s) noexcept
{
    return f32 ((double) ih ((toU32 (i) * 0x9E3779B1u) ^ ih (toU32 (s) * 0x85EBCA77u)) / 4294967296.0);
}
double IoWorld::toHalf (double v) noexcept
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
std::vector<float> IoWorld::makeBase()
{
    const int n = texSize, nn = n * n;
    const auto smooth = [] (uint32_t seed, double rough, int passes) { auto h = blurAsymmetric (plasma (texSize, seed, rough), texSize, passes); normalize01 (h); return h; };
    const auto R = smooth (7101, 0.50, 2);
    const auto G = smooth (7202, 0.54, 2);
    const auto B = smooth (7303, 0.66, 1);
    const auto A = smooth (7404, 0.52, 1);
    std::vector<float> H ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i)
    {
        H[i * 4] = f32 (toHalf (R[i])); H[i * 4 + 1] = f32 (toHalf (G[i])); H[i * 4 + 2] = f32 (toHalf (B[i])); H[i * 4 + 3] = f32 (toHalf (A[i]));
    }
    return H;
}
std::vector<uint8_t> IoWorld::makeDetail()
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
std::vector<uint8_t> IoWorld::makeNoise()
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
std::vector<uint8_t> IoWorld::makeCells()
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

V2 IoWorld::loopR (double th) noexcept
{
    double R = loopRadius, dR = 0;
    for (size_t k = 0; k < 4; ++k) { const double a = loopK[k] * th + loopP[k]; R += loopA[k] * Js::cos (a); dR -= loopA[k] * loopK[k] * Js::sin (a); }
    return { R, dR };
}
V2 IoWorld::loopPoint (double th) noexcept { const double R = loopR (th)[0]; return { loopC[0] + R * Js::cos (th), loopC[1] + R * Js::sin (th) }; }
const std::vector<double>& IoWorld::loopTable()
{
    static const std::vector<double> t = []
    {
        std::vector<double> o ((size_t) loopN + 1, 0.0);
        double acc = 0;
        V2 prev = loopPoint (0);
        for (int i = 1; i <= loopN; ++i) { const V2 p = loopPoint ((double) i / loopN * 2 * PI); acc += hyp (p[0] - prev[0], p[1] - prev[1]); o[(size_t) i] = acc; prev = p; }
        return o;
    }();
    return t;
}
double IoWorld::loopLength() { return loopTable()[(size_t) loopN]; }
double IoWorld::modL (double v) { const double L = loopLength(); return mod (mod (v, L) + L, L); }
double IoWorld::thetaAt (double s)
{
    const auto& S = loopTable();
    const double q = modL (s);
    int lo = 0, hi = loopN;
    while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (S[(size_t) mid] <= q) lo = mid; else hi = mid; }
    return (lo + (q - S[(size_t) lo]) / jsMax (S[(size_t) lo + 1] - S[(size_t) lo], 1e-9)) / loopN * 2 * PI;
}
V2 IoWorld::tangentAt (double th) noexcept
{
    const V2 Rd = loopR (th);
    const double R = Rd[0], dR = Rd[1], c = Js::cos (th), sn = Js::sin (th), tx = dR * c - R * sn, tz = dR * sn + R * c, l = hyp (tx, tz);
    return { tx / l, tz / l };
}
V2 IoWorld::rightAt (double th) noexcept { const V2 t = tangentAt (th); return { -t[1], t[0] }; }
V2 IoWorld::roadPointAt (double s, double d)
{
    const double th = thetaAt (s);
    const V2 p = loopPoint (th), r = rightAt (th);
    return { p[0] + d * r[0], p[1] + d * r[1] };
}
double IoWorld::headingAt (double sr) { const V2 t = tangentAt (thetaAt (sr)); return Js::atan2 (t[0], -t[1]); }
IoWorld::Near IoWorld::nearestOnLoop (double x, double z)
{
    const double L = loopLength();
    double best = 0, bd = 1e18;
    for (double s = 0; s < L; s += 5) { const V2 p = roadPointAt (s, 0); const double d = sq (p[0] - x) + sq (p[1] - z); if (d < bd) { bd = d; best = s; } }
    for (double s = best - 5; s <= best + 5; s += 0.25) { const V2 p = roadPointAt (s, 0); const double d = sq (p[0] - x) + sq (p[1] - z); if (d < bd) { bd = d; best = s; } }
    return { modL (best), std::sqrt (bd) };
}
IoWorld::Basis IoWorld::cameraBasis (double heading, double pitch) noexcept
{
    const double cp = Js::cos (pitch), sp = Js::sin (pitch), f0x = Js::sin (heading), f0z = -Js::cos (heading);
    return { { Js::cos (heading), 0, Js::sin (heading) }, { -f0x * sp, cp, -f0z * sp }, { f0x * cp, sp, f0z * cp } };
}

//==============================================================================
// THE SKY

double IoWorld::jupRho() noexcept { static const double r = jsAsin (71492.0 / 421700.0); return r; }
V3 IoWorld::dirOf (double az, double el) noexcept { return { Js::sin (az) * Js::cos (el), Js::sin (el), -Js::cos (az) * Js::cos (el) }; }
double IoWorld::jupEl() noexcept { return 14.0 * DEG - 3.5 * DEG; }
double IoWorld::jupAz() { static const double a = headingAt (startAt0 + 160) + 6 * DEG; return a; }
const IoWorld::JupFrame& IoWorld::jupFrame()
{
    static const JupFrame f = []
    {
        const V3 J = dirOf (jupAz(), jupEl()), R = norm3 (cross3 (J, { 0, 1, 0 })), U = norm3 (cross3 (R, J));
        const V3 axis = norm3 ({ U[0] * 0.996 + R[0] * 0.07 - J[0] * 0.05, U[1] * 0.996 + R[1] * 0.07 - J[1] * 0.05, U[2] * 0.996 + R[2] * 0.07 - J[2] * 0.05 });
        return JupFrame { J, R, U, axis };
    }();
    return f;
}
double IoWorld::sunHeightAt (double drive, const SunMap& M) noexcept
{
    const double y = M.horizon - clamp (drive, 0, 1);
    const bool up = y >= 0;
    const double H = up ? M.top : M.bottom, span = up ? M.horizon : 1 - M.horizon, k = M.dusk * span / H, u = std::abs (y) / span;
    return sign (y) * H * (k * u + (3 - 2 * k) * u * u + (k - 2) * u * u * u);
}
double IoWorld::sunSepAt (double blend) noexcept
{
    const auto& M = sunMap;
    const double b = clamp (blend, 0, 1), rho = jupRho();
    const double r0 = M.deep / M.contact, r1 = 2 * (M.winSep - rho) / (M.win - M.contact) - r0, r2 = 2 * (PI - M.winSep) / (1 - M.win) - r1;
    if (b <= M.contact) return rho - M.deep + r0 * b;
    if (b <= M.win) { const double x = b - M.contact; return rho + r0 * x + (r1 - r0) / (M.win - M.contact) * x * x / 2; }
    const double x = b - M.win; return M.winSep + r1 * x + (r2 - r1) / (1 - M.win) * x * x / 2;
}
IoWorld::Sun IoWorld::sunLook (double sep, double late)
{
    const double rho = jupRho();
    Sun L;
    L.sep = sep;
    L.vis = smoothstep (rho - sunRho * 1.2, rho + sunRho * 1.2, sep);
    const double w = clamp (1 - (sep - rho) / sunMap.graze, 0, 1);
    V3 hue = mix3 ({ 1.0, 0.975, 0.94 }, { 1.0, 0.94, 0.85 }, smoothstep (0, 0.2, w));
    hue = w < 0.35 ? mix3 (hue, { 1.0, 0.84, 0.56 }, smoothstep (0, 0.35, w))
        : w < 0.65 ? mix3 ({ 1.0, 0.84, 0.56 }, { 1.0, 0.60, 0.27 }, smoothstep (0.35, 0.65, w))
                   : mix3 ({ 1.0, 0.60, 0.27 }, { 1.0, 0.33, 0.12 }, smoothstep (0.65, 1.0, w));
    L.hue = hue;
    L.light = 1 - 0.95 * jsPow (w, 1.8);
    L.ring = 0.05 * (1 - smoothstep (0.6, 0.9, late)) * (1 - L.vis);
    L.flat = w * w; L.graze = w;
    L.depth = clamp ((rho - sep) / sunMap.deep, 0, 1);
    L.u = clamp ((late - 0.6) / 0.4, 0, 1);
    L.eclAir = smoothstep (0.6, 0.92, late); L.eclExpo = smoothstep (0.6, 0.95, late);
    L.eclDark = jsMax (1 - L.vis, 0.6 * smoothstep (0.4, 1.0, w));
    return L;
}
IoWorld::Sun IoWorld::sunSolve (double drive, double blend)
{
    const auto& M = sunMap;
    const double j = jupEl(), eA = sunHeightAt (drive);
    const double sep = clamp (smin (sunSepAt (blend), PI - std::abs (eA + j), M.softTop), 0, PI);
    const double e = clamp (smax (smin (eA, j + sep, M.softPull), j - sep, M.softPull), j - sep, j + sep);
    const double cz = clamp ((Js::cos (sep) - Js::sin (e) * Js::sin (j)) / jsMax (Js::cos (e) * Js::cos (j), 1e-9), -1, 1);
    const double az = jupAz() - Js::acos (cz);
    const double late = 0.6 + 0.4 * jsPow (clamp ((jupRho() - sep) / M.deep, 0, 1), 1 / 0.85);
    Sun S = sunLook (sep, late);
    S.dir = dirOf (az, e); S.el = e; S.az = az; S.eA = eA;
    S.gate = smoothstep (M.gate[0], M.gate[1], eA);
    S.night = 1 - smoothstep (M.night[0], M.night[1], eA);
    const double adapt = 1 - smoothstep (M.adapt[0], M.adapt[1], eA);
    S.dark = jsMax (S.eclDark, S.night);
    S.airglow = jsMax (S.eclAir, adapt);
    S.expo = 1 + 0.6 * jsMax (S.eclExpo, adapt);
    S.cabin = clamp (S.light * S.vis * S.gate * smoothstep (M.cabin[0], M.cabin[1], e), 0, 1);
    return S;
}
namespace
{
    constexpr double pathStart = 25 * DEG, pathIngress = 0.40, pathPa = 166 * DEG, pathDeep = 4.6 * DEG, pathTwilight = 0.90;
    double lateOf (double d) { return d <= pathTwilight ? 0.6 + (d - pathIngress) * (0.3 / (pathTwilight - pathIngress)) : d; }
}
double IoWorld::pathSepAt (double x)
{
    const double d = clamp (x, 0, 1), rho = jupRho() / DEG;
    if (d <= pathIngress) return rho + pathStart / DEG * (1 - d / pathIngress);
    return rho - pathDeep / DEG * jsPow ((lateOf (d) - 0.6) / 0.4, 0.85);
}
IoWorld::Sun IoWorld::pathSun (double x)
{
    const double d = clamp (x, 0, 1), sep = pathSepAt (d) * DEG, c = Js::cos (pathPa), s = Js::sin (pathPa);
    const auto& F = jupFrame();
    V3 v {};
    for (size_t k = 0; k < 3; ++k) v[k] = F.J[k] * Js::cos (sep) + (F.R[k] * c + F.U[k] * s) * Js::sin (sep);
    const V3 dir = norm3 (v);
    Sun S = sunLook (sep, lateOf (d));
    S.dir = dir; S.el = jsAsin (dir[1]); S.az = Js::atan2 (dir[0], -dir[2]); S.eA = jsAsin (dir[1]);
    S.gate = 1; S.night = 0; S.dark = S.eclDark; S.airglow = S.eclAir; S.expo = 1 + 0.6 * S.eclExpo; S.cabin = clamp (S.light * S.vis, 0, 1);
    return S;
}
const std::array<IoWorld::Moon, 2>& IoWorld::moons()
{
    static const std::array<Moon, 2> m {
        Moon { "Europa",   dirOf (jupAz() + 25.0 * DEG, jupEl() - 1.1 * DEG), 0.225 * DEG, { 0.86, 0.80, 0.70 } },
        Moon { "Ganymede", dirOf (jupAz() - 33.0 * DEG, jupEl() - 2.3 * DEG), 0.27 * DEG,  { 0.58, 0.54, 0.50 } } };
    return m;
}

//==============================================================================
// THE WORLD: THE HEIGHT MIRROR

double IoWorld::tap (int ch, double u, double v) const noexcept
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
double IoWorld::plainH (double px, double pz) const noexcept
{
    return (tap (0, px / l0, pz / l0) - 0.5) * 26 + (tap (0, px / l1 + off1[0], pz / l1 + off1[1]) - 0.5) * 5;
}
double IoWorld::roadHeight (double th) const noexcept
{
    const double q = mod (mod (th / (2 * PI), 1) + 1, 1) * rhN;
    const int i = (int) mod (std::floor (q), rhN);
    const double f = q - std::floor (q);
    return rhTable[(size_t) i] * (1 - f) + rhTable[(size_t) ((i + 1) % rhN)] * f;
}
std::array<double, 3> IoWorld::roadDist (double x, double z) const noexcept
{
    const double qx = x - loopC[0], qz = z - loopC[1], r = hyp (qx, qz), th = Js::atan2 (qz, qx);
    const V2 Rd = loopR (th);
    const double R = Rd[0], dR = Rd[1];
    return { (R - r) * R / std::sqrt (R * R + dR * dR), th, R };
}
std::array<double, 3> IoWorld::calderaField (double px, double pz) const noexcept
{
    const double ti = std::floor ((px - roadOrg[0]) / roadSpan * roadN), tj = std::floor ((pz - roadOrg[1]) / roadSpan * roadN);
    if (ti < 0 || tj < 0 || ti >= roadN || tj >= roadN) return { 0, 0, 0 };
    const int ci = (int) Js::round (roadPx[(size_t) ((tj * roadN + ti) * 4 + 2)]) - 1;
    if (ci < 0) return { 0, 0, 0 };
    const auto& c = calderas_[(size_t) ci];
    const double d = hyp (px - c.x, pz - c.z);
    const double push = 0.16 * (1 - smoothstep (c.R + c.W, c.R + c.W + 160, d));
    if (d >= c.out) return { 0, 0, push };
    const double floorY = c.baseY - c.D;
    if (d < c.R + c.W) return { floorY + (c.D + c.rim) * smoothstep (c.R, c.R + c.W, d), 1, push };
    const double r2 = (d - c.R - c.W) / (c.out - c.R - c.W);
    return { c.baseY + c.rim * (1 - r2) * (1 - r2), 1 - smoothstep (0, 1, r2), push };
}
V2 IoWorld::roadTap (double x, double z) const noexcept
{
    const int n = roadN;
    const double u = clamp ((x - roadOrg[0]) / roadSpan * n - 0.5, 0, n - 1), v = clamp ((z - roadOrg[1]) / roadSpan * n - 0.5, 0, n - 1);
    const double x0 = jsMin (std::floor (u), n - 2), y0 = jsMin (std::floor (v), n - 2), fx = u - x0, fy = v - y0;
    const int i0 = (int) x0, j0 = (int) y0;
    const auto at = [this] (int i, int j, int c) { return (double) roadPx[(size_t) ((j * roadN + i) * 4 + c)]; };
    const auto q = [&] (int c) { return (at (i0, j0, c) * (1 - fx) + at (i0 + 1, j0, c) * fx) * (1 - fy) + (at (i0, j0 + 1, c) * (1 - fx) + at (i0 + 1, j0 + 1, c) * fx) * fy; };
    return { q (0), q (1) };
}
std::array<double, 4> IoWorld::featTap (double x, double z) const noexcept
{
    const int FN = featN;
    const double fcell = featSpan / FN;
    const double u = clamp ((x - featOrg[0]) / fcell - 0.5, 0, FN - 1), v = clamp ((z - featOrg[1]) / fcell - 0.5, 0, FN - 1);
    const double x0 = jsMin (std::floor (u), FN - 2), y0 = jsMin (std::floor (v), FN - 2), fx = u - x0, fy = v - y0;
    const int i0 = (int) x0, j0 = (int) y0;
    const auto q = [&] (int c)
    {
        const auto at = [&] (int i, int j) { return featPx[(size_t) (((j * FN) + i) * 4 + c)] / 255.0; };
        return (at (i0, j0) * (1 - fx) + at (i0 + 1, j0) * fx) * (1 - fy) + (at (i0, j0 + 1) * (1 - fx) + at (i0 + 1, j0 + 1) * fx) * fy;
    };
    return { (q (0) - 0.5) * 48, q (1), q (2), q (3) };
}
double IoWorld::terrainH (double px, double pz, double ad) const noexcept
{
    const std::array<double, 4> a { tap (0, px / l0, pz / l0), tap (1, px / l0, pz / l0), tap (2, px / l0, pz / l0), tap (3, px / l0, pz / l0) };
    const double bu = px / l1 + off1[0], bv = pz / l1 + off1[1], cu = px / l2 + off2[0], cv = pz / l2 + off2[1];
    const std::array<double, 4> b { tap (0, bu, bv), tap (1, bu, bv), tap (2, bu, bv), tap (3, bu, bv) };
    const std::array<double, 4> c { tap (0, cu, cv), tap (1, cu, cv), tap (2, cu, cv), tap (3, cu, cv) };
    const auto cf = calderaField (px, pz);
    const double plain = (a[0] - 0.5) * 26 + (b[0] - 0.5) * 5;
    const double m = a[1] * 0.86 + b[1] * 0.14 - 0.11 * (1 - smoothstep (30, 160, ad)) - cf[2];
    const double mesa = (28 * smoothstep (0.684, 0.716, m) + 16 * smoothstep (0.768, 0.792, m)) * (0.9 + 0.2 * b[2]);
    const double fine = (c[2] - 0.5) * 0.9 + (c[0] - 0.5) * 0.35;
    const double fl = featTap (px, pz)[0], lobe = 1.5 * smoothstep (0, -5, fl);
    return lerp (plain + mesa + fine + lobe, cf[0], cf[1]);
}
double IoWorld::floorHeight (double px, double pz) const noexcept
{
    const V2 rd = roadTap (px, pz);
    const double ad = std::abs (rd[0]);
    const double tH = terrainH (px, pz, ad);
    const double w = smoothstep (roadHalf + 2.5, 45, ad);
    return lerp (rd[1], tH, w) - 0.10 * smoothstep (roadHalf, roadHalf + 1.2, ad) * (1 - smoothstep (roadHalf + 2, roadHalf + 5, ad));
}
double IoWorld::slopeAt (double x, double z, double r) const noexcept
{
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 8; ++k) { const double a = k / 8.0 * 2 * PI, h = floorHeight (x + r * Js::cos (a), z + r * Js::sin (a)); lo = jsMin (lo, h); hi = jsMax (hi, h); }
    return hi - lo;
}

//==============================================================================
// THE WORLD MADE (makeWorld)

void IoWorld::build()
{
    if (built) return;
    basePx = makeBase(); detailPx = makeDetail(); noisePx = makeNoise(); cellsPx = makeCells();
    const double L = loopLength();

    // THE ROAD'S HEIGHT: the plain's swell low-passed along the loop over +-200 m
    {
        std::vector<double> raw ((size_t) rhN);
        rhTable.assign ((size_t) rhN, 0.0);
        for (int i = 0; i < rhN; ++i) { const V2 c = loopPoint ((double) i / rhN * 2 * PI); raw[(size_t) i] = 0.6 * (tap (0, c[0] / l0, c[1] / l0) - 0.5) * 26; }
        const int w = (int) Js::round (200 / L * rhN);
        for (int i = 0; i < rhN; ++i) { double acc = 0; for (int k = -w; k <= w; ++k) acc += raw[(size_t) (((i + k) % rhN + rhN) % rhN)]; rhTable[(size_t) i] = acc / (2 * w + 1); }
    }
    // THE CALDERAS
    calderas_.clear();
    int ci = 0;
    for (const auto& c : calderaSpecs)
    {
        const V2 p = roadPointAt (c.at * L, c.d);
        Caldera q;
        q.name = c.name; q.at = c.at; q.d = c.d; q.R = c.R; q.W = c.W; q.D = c.D; q.rim = c.rim; q.lake = c.lake; q.th = c.th; q.big = c.big; q.near = c.near;
        q.i = ci++; q.x = p[0]; q.z = p[1]; q.baseY = f32 (plainH (p[0], p[1])); q.out = c.R + c.W + jsMax (30, c.R * 0.45); q.lakeR = c.R * c.lake;
        calderas_.push_back (q);
    }
    // the skylight's pit, sought near its fraction where the plain is flat and far from the paterae and the mesas
    {
        const auto& st = stationSpecs[1];
        bool found = false;
        double pa = 0, px = 0, pz = 0;
        for (int k = 0; k < 80 && ! found; ++k)
        {
            const double a = modL (st.at * L + (k % 2 ? 1 : -1) * std::ceil (k / 2.0) * 8);
            const V2 p = roadPointAt (a, st.d * st.side);
            bool far = true;
            for (const auto& c : calderas_) if (! (hyp (p[0] - c.x, p[1] - c.z) > c.R + c.W + 420)) { far = false; break; }
            const double mesa = tap (1, p[0] / l0, p[1] / l0) * 0.86 + tap (1, p[0] / l1 + off1[0], p[1] / l1 + off1[1]) * 0.14;
            double lo = 1e9, hi = -1e9;
            for (int q = 0; q < 8; ++q) { const double h = plainH (p[0] + 30 * Js::cos (q * PI / 4), p[1] + 30 * Js::sin (q * PI / 4)); lo = jsMin (lo, h); hi = jsMax (hi, h); }
            if (far && mesa < 0.62 && hi - lo < 2.5) { found = true; pa = a; px = p[0]; pz = p[1]; }
        }
        if (! found) { const V2 p = roadPointAt (st.at * L, st.d * st.side); pa = st.at * L; px = p[0]; pz = p[1]; }
        pitAt = pa;
        const double pitW = 2.0;
        Caldera q;
        q.name = "the skylight"; q.at = pa / L; q.d = 0; q.R = pitR - pitW; q.W = pitW; q.D = pitDepth; q.rim = 0.4; q.lake = 0; q.th = 9; q.pit = true;
        q.i = (int) calderas_.size(); q.x = px; q.z = pz; q.baseY = f32 (plainH (px, pz)); q.out = pitR + 5; q.lakeR = 0;
        calderas_.push_back (q);
    }
    // THE ROAD MAP
    roadPx.assign ((size_t) roadN * roadN * 4, 0.0f);
    for (int j = 0; j < roadN; ++j)
        for (int i = 0; i < roadN; ++i)
        {
            const double x = roadOrg[0] + (i + 0.5) * roadSpan / roadN, z = roadOrg[1] + (j + 0.5) * roadSpan / roadN;
            const auto rd = roadDist (x, z);
            const size_t o = (size_t) (j * roadN + i) * 4;
            int best = 0; double bd = 1e9;
            for (const auto& c : calderas_) { const double e = hyp (x - c.x, z - c.z) - (c.R + c.W + 175); if (e < 9 && e < bd) { bd = e; best = c.i + 1; } }
            roadPx[o] = f32 (toHalf (rd[0])); roadPx[o + 1] = f32 (toHalf (roadHeight (rd[1]))); roadPx[o + 2] = (float) best; roadPx[o + 3] = 0;
        }
    built = true;     // the height mirror's parts from here on read the world's own maps

    // THE PLUMES' AND THE FOUNTAINS' PLACES
    const auto farPoint = [] (double dg, double r) { return V2 { loopC[0] + r * Js::cos (dg * DEG), loopC[1] + r * Js::sin (dg * DEG) }; };
    plumes_.clear();
    int pi_ = 0;
    for (const auto& p : plumeSpecs)
    {
        Plume q;
        q.name = p.name; q.far = p.far; q.H = p.H; q.th = p.th; q.pele = p.pele; q.base = p.base;
        if (p.view >= 0) q.view = p.view;
        if (p.far) q.farAt = { p.a, p.b }; else { q.at = p.a; q.d = p.b; }
        const V2 xz = p.far ? farPoint (p.a, p.b) : roadPointAt (p.a * L, p.b);
        q.i = pi_; q.x = xz[0]; q.z = xz[1]; q.y = f32 (plainH (xz[0], xz[1])); q.row = pi_; q.ring = p.pele ? 2.0 * p.H : 1.5 * p.H;
        q.near = nearestOnLoop (xz[0], xz[1]);
        plumes_.push_back (q);
        ++pi_;
    }
    fountains_.clear();
    int fi = 0;
    for (const auto& f : fountainSpecs)
    {
        const auto& c = calderas_[(size_t) f.cald];
        const Near n = nearestOnLoop (c.x, c.z);
        const V2 r = roadPointAt (n.at, 0);
        const double dx = r[0] - c.x, dz = r[1] - c.z, dl = orOne (hyp (dx, dz));
        const double k = f.toward * c.lakeR * 0.85, side = (fi % 2 ? 1 : -1) * 0.2 * c.lakeR;
        Fountain q;
        q.cald = f.cald; q.toward = f.toward; q.H = f.H; q.th = f.th; q.i = fi;
        q.x = c.x + dx / dl * k - dz / dl * side; q.z = c.z + dz / dl * k + dx / dl * side; q.y = c.baseY - c.D + 0.3; q.row = (int) plumes_.size() + fi;
        fountains_.push_back (q);
        ++fi;
    }

    // THE FEATURE MAP: the flows' signed distance, freshness, the red deposits, the white frost
    {
        const int FN = featN;
        const double fcell = featSpan / FN;
        featPx.assign ((size_t) FN * FN * 4, 0);
        std::vector<float> sdf ((size_t) FN * FN, 24.0f), fresh ((size_t) FN * FN, 0.0f), red ((size_t) FN * FN, 0.0f), frost ((size_t) FN * FN, 0.0f);
        flows_.clear();
        struct Start { double x, z, a, w, len, fresh, seed; bool fissure; int cald; };
        std::vector<Start> starts;
        for (const auto& c : calderas_)
        {
            if (c.lake <= 0) continue;
            const int n = c.big ? 4 : c.R > 60 ? 2 : 1;
            const Near nr = nearestOnLoop (c.x, c.z);
            const V2 r = roadPointAt (nr.at, 0);
            const double away = Js::atan2 (c.z - r[1], c.x - r[0]);
            for (int k = 0; k < n; ++k)
            {
                const double a = away + (k - (n - 1) / 2.0) * 0.9 + (hs (c.i * 10 + k, 501) - 0.5) * 0.5;
                starts.push_back ({ c.x + Js::cos (a) * (c.R + c.W * 0.5), c.z + Js::sin (a) * (c.R + c.W * 0.5), a, c.big ? 34.0 : 22.0,
                                    c.big ? 900 : 420 + 300 * hs (c.i, 502), c.th < 0.3 ? 0.85 + 0.15 * hs (c.i, 503) : 0.35, (double) (c.i * 10 + k), false, c.i });
            }
        }
        const std::array<std::array<double, 3>, 5> fissures { { { 0.045, -22, 380 }, { 0.215, 24, 300 }, { 0.635, -26, 460 }, { 0.835, 22, 340 }, { 0.395, 30, 260 } } };
        for (const auto& [at, d, len] : fissures)
        {
            const V2 p = roadPointAt (at * L, d), r = rightAt (thetaAt (at * L));
            const double a = Js::atan2 (r[1] * sign (d), r[0] * sign (d));
            starts.push_back ({ p[0], p[1], a, 16, len, 1.0, 900 + Js::round (at * 1000), true, -1 });
        }
        for (int k = 0; k < 7; ++k)
        {
            const V2 p = farPoint (k * 51 + 20, 1300 + 500 * hs (k, 504));
            starts.push_back ({ p[0], p[1], hs (k, 505) * 2 * PI, 30, 700, 0.12, (double) (1500 + k), false, -1 });
        }
        for (const auto& st : starts)
        {
            double x = st.x, z = st.z, a = st.a;
            std::vector<V3> pts { { x, z, st.w * 0.55 } };
            for (double sl = 0, k = 0; sl < st.len && k < 200; sl += 22, ++k)
            {
                a += (hcell (st.seed, k, 506) - 0.5) * 0.55;
                const double nx = x + Js::cos (a) * 22, nz = z + Js::sin (a) * 22, rdn = roadDist (nx, nz)[0];
                if (std::abs (rdn) < roadHalf + 26)
                {
                    const double g = 1.5, ex = (roadDist (nx + g, nz)[0] - roadDist (nx - g, nz)[0]) / (2 * g), ez = (roadDist (nx, nz + g)[0] - roadDist (nx, nz - g)[0]) / (2 * g);
                    const double sg = orOne (sign (rdn)); a = Js::atan2 (ez * sg, ex * sg); continue;
                }
                bool inOther = false;
                for (const auto& c : calderas_) if (c.i != st.cald && hyp (nx - c.x, nz - c.z) < c.R + c.W + 8) { inOther = true; break; }
                if (inOther) break;
                if (tap (1, nx / l0, nz / l0) * 0.86 + tap (1, nx / l1 + off1[0], nz / l1 + off1[1]) * 0.14 > 0.672) { a += 0.9 * (hcell (st.seed, k, 508) < 0.5 ? 1 : -1); continue; }
                x = nx; z = nz;
                const double u = sl / st.len, w = st.w * (0.55 + 0.5 * Js::sin (u * PI) * (0.8 + 0.4 * hcell (st.seed, k, 507))) * (1 - 0.45 * u);
                pts.push_back ({ x, z, w });
            }
            if (pts.size() > 2) flows_.push_back ({ pts, st.fresh, st.fissure });
        }
        const auto toTex = [&] (double x, double z) { return V2 { (x - featOrg[0]) / fcell - 0.5, (z - featOrg[1]) / fcell - 0.5 }; };
        const auto rangeOf = [&] (double lo, double hi, int& a, int& b) { a = (int) jsMax (0, std::floor (lo)); b = (int) jsMin (FN - 1, std::ceil (hi)); };
        for (const auto& f : flows_)
            for (size_t k = 0; k + 1 < f.pts.size(); ++k)
            {
                const double ax = f.pts[k][0], az = f.pts[k][1], aw = f.pts[k][2], bx = f.pts[k + 1][0], bz = f.pts[k + 1][1], bw = f.pts[k + 1][2];
                const double ex = bx - ax, ez = bz - az, ee = ex * ex + ez * ez;
                const double m = jsMax (aw, bw) / 2 + 26;
                const V2 t0 = toTex (jsMin (ax, bx) - m, jsMin (az, bz) - m), t1 = toTex (jsMax (ax, bx) + m, jsMax (az, bz) + m);
                int i0, i1, j0, j1;
                rangeOf (t0[0], t1[0], i0, i1); rangeOf (t0[1], t1[1], j0, j1);
                for (int j = j0; j <= j1; ++j)
                    for (int i = i0; i <= i1; ++i)
                    {
                        const double px = featOrg[0] + (i + 0.5) * fcell, pz = featOrg[1] + (j + 0.5) * fcell;
                        const double t = ee > 0 ? clamp (((px - ax) * ex + (pz - az) * ez) / ee, 0, 1) : 0;
                        const double d = hyp (px - ax - ex * t, pz - az - ez * t) - lerp (aw, bw, t) / 2;
                        const size_t o = (size_t) (j * FN + i);
                        if (d < sdf[o]) { if (d < 6) fresh[o] = f32 (f.fresh); sdf[o] = f32 (d); }
                    }
            }
        const auto ring = [&] (double x, double z, double R, double wR, double inner, std::vector<float>& arr, double k)
        {
            const double m = R + 3 * wR;
            const V2 t0 = toTex (x - m, z - m), t1 = toTex (x + m, z + m);
            int i0, i1, j0, j1;
            rangeOf (t0[0], t1[0], i0, i1); rangeOf (t0[1], t1[1], j0, j1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                {
                    const double px = featOrg[0] + (i + 0.5) * fcell, pz = featOrg[1] + (j + 0.5) * fcell, d = hyp (px - x, pz - z);
                    const double v = k * (Js::exp (-sq ((d - R) / wR)) + inner * (1 - smoothstep (0, R, d)));
                    const size_t o = (size_t) (j * FN + i); arr[o] = f32 (jsMax (arr[o], v));
                }
        };
        for (const auto& p : plumes_)
            if (p.pele) ring (p.x, p.z, p.ring, p.ring * 0.16, 0.22, red, 0.85);
            else ring (p.x, p.z, p.ring, p.ring * 0.10, 0.35, frost, 0.75);
        for (const auto& c : calderas_)
            if (c.pit) continue;
            else if (c.lake > 0) ring (c.x, c.z, c.R + c.W + 20, 26 + c.R * 0.2, 0.0, red, c.big ? 0.75 : 0.55);
            else ring (c.x, c.z, c.R + c.W + 24, 30, 0.0, frost, 0.7);
        for (size_t o = 0; o < (size_t) FN * FN; ++o)
        {
            featPx[o * 4] = u8 (Js::round (clamp (0.5 + sdf[o] / 48.0, 0, 1) * 255));
            featPx[o * 4 + 1] = u8 (Js::round (clamp (fresh[o], 0, 1) * 255));
            featPx[o * 4 + 2] = u8 (Js::round (clamp (red[o], 0, 1) * 255));
            featPx[o * 4 + 3] = u8 (Js::round (clamp (frost[o], 0, 1) * 255));
        }
        for (int i = 0; i < FN; ++i)
            for (const int o : { i, (FN - 1) * FN + i, i * FN, i * FN + FN - 1 })
            { featPx[(size_t) o * 4] = 255; featPx[(size_t) o * 4 + 1] = 0; featPx[(size_t) o * 4 + 2] = 0; featPx[(size_t) o * 4 + 3] = 0; }
    }

    // THE SITES BY THE TRACK
    sites_.clear();
    {
        const auto foot = [this] (double x, double z, double r) { double y = floorHeight (x, z); for (int q = 0; q < 8; ++q) { const double a = q / 8.0 * 2 * PI; y = jsMin (y, floorHeight (x + r * Js::cos (a), z + r * Js::sin (a))); } return y; };
        const auto ok = [this] (const V2& p, double r)
        {
            if (! (slopeAt (p[0], p[1], r) < 1.1)) return false;
            for (const auto& c : calderas_) if (! (hyp (p[0] - c.x, p[1] - c.z) > c.R + c.W + 18)) return false;
            for (const auto& q : plumes_) if (! (hyp (p[0] - q.x, p[1] - q.z) > 60)) return false;
            return featTap (p[0], p[1])[0] > 3;
        };
        for (int si = 0; si < (int) std::size (siteSpecs); ++si)
        {
            const auto& st = siteSpecs[si];
            for (int k = 0; k < 60; ++k)
            {
                const double a = modL (st.at * L + (k % 2 ? 1 : -1) * std::ceil (k / 2.0) * 6);
                const double sd = st.side;
                const auto P = [&] (double along, double dd) { return roadPointAt (a + along, dd * sd); };
                const V2 rv = st.rover ? P (0, 21) : P (0, 15), stn = P (13, 26), third = P (-16, 36);
                if (! ok (rv, st.rover ? 9 : 3.5) || ! ok (stn, 3) || ! ok (third, 3.5)) continue;
                const double seed = hs (si, 621);
                const bool flip = hs (si, 622) < 0.5;
                std::vector<Structure> items;
                if (! st.rover)
                    items.push_back ({ st.third == 3 ? 2 : 3, rv[0], rv[1], foot (rv[0], rv[1], 2.5) - 0.12, hs (si, 626) * 2 * PI, 1, { 0.02, 0.01 }, false, false, 0, 0, 0 });
                else
                {
                    const V2 b = roadPointAt (a - 25, 0);
                    const double lx = rv[0] - b[0], lz = rv[1] - b[1], ll = orOne (hyp (lx, lz));
                    items.push_back ({ 5, rv[0], rv[1], foot (rv[0], rv[1], 5) - 0.12, Js::atan2 (lz / ll, lx / ll) + (flip ? PI : 0), 1, { 0.03, 0.02 * (seed - 0.5) }, false, false, 0, 0, 0 });
                }
                items.push_back ({ 4, stn[0], stn[1], foot (stn[0], stn[1], 2.5) - 0.1, hs (si, 623) * 2 * PI, 1, { 0, 0 }, false, false, 0, 0, 0 });
                items.push_back ({ st.third, third[0], third[1], foot (third[0], third[1], 2.5) - 0.15, hs (si, 624) * 2 * PI, 0.95, { 0.02, -0.02 }, false, false, 0, 0, 0 });
                for (size_t n = 0; n < items.size(); ++n) { auto& q = items[n]; q.site = true; q.ci = -1000 - si; q.cj = (double) n; q.seed = hs (si * 7 + (int) n, 625); }
                sites_.push_back ({ a, items, "site " + std::to_string (si + 1) });
                break;
            }
        }
    }
    // THE CREWED STATIONS
    stations_.clear();
    {
        const auto clearOf = [this] (double x, double z, double r)
        {
            for (const auto& g : sites_) for (const auto& q : g.items) if (! (hyp (x - q.x, z - q.z) > r + 40)) return false;
            for (const auto& q : stations_) if (! (hyp (x - q.items[0].x, z - q.items[0].z) > r + q.r + 40)) return false;
            for (const auto& c : calderas_) if (! (c.pit || hyp (x - c.x, z - c.z) > c.R + c.W + r + 15)) return false;
            for (const auto& p : plumes_) if (! (hyp (x - p.x, z - p.z) > r + 60)) return false;
            return true;
        };
        const auto flatOver = [this] (double x, double z, double hx, double hz, double yaw)
        {
            double lo = 1e9, hi = -1e9;
            const double c = Js::cos (yaw), sn = Js::sin (yaw);
            for (const double u : { -1.0, 0.0, 1.0 }) for (const double v : { -1.0, 0.0, 1.0 })
            { const double lx = u * hx, lz = v * hz, h = floorHeight (x + c * lx - sn * lz, z + sn * lx + c * lz); lo = jsMin (lo, h); hi = jsMax (hi, h); }
            return V2 { lo, hi - lo };
        };
        for (const auto& st : stationSpecs)
        {
            const double seen = st.seen > 0 ? st.seen : 75;
            if (st.lake)
            {
                int best = -1; Near bn; double bk = 0;
                for (const auto& q : calderas_)
                {
                    if (! (q.lake > 0)) continue;
                    const Near n = nearestOnLoop (q.x, q.z);
                    const double key = n.d - q.lakeR;
                    if (best < 0 || key < bk) { best = q.i; bn = n; bk = key; }
                }
                auto& c = calderas_[(size_t) best];
                const V2 r = roadPointAt (bn.at, 0);
                const double tr = Js::atan2 (r[1] - c.z, r[0] - c.x);
                const V2 t = tangentAt (thetaAt (bn.at));
                const double ang = tr + (Js::cos (tr) * t[0] + Js::sin (tr) * t[1] > 0 ? -1 : 1) * 1.15;
                c.D = jsMax (c.D, 8);
                const double rr = c.R + c.W + 6.5, x = c.x + Js::cos (ang) * rr, z = c.z + Js::sin (ang) * rr;
                const double ox = Js::cos (ang), oz = Js::sin (ang);
                const double yaw = Js::atan2 (-ox, oz);
                stations_.push_back ({ st.name, nearestOnLoop (x, z).at, seen, 30, { { 8, x, z, floorHeight (x, z) - 0.15, yaw, 1, { 0, 0 }, false, true, 0, 0, 0 } } });
                continue;
            }
            const Caldera* pit = nullptr;
            for (const auto& q : calderas_) if (q.pit) { pit = &q; break; }
            for (int k = 0; k < 320; ++k)
            {
                const int kk = k >> 1;
                const double a = modL (st.at * L + (kk % 2 ? 1 : -1) * std::ceil (kk / 2.0) * 8);
                const double th = thetaAt (a);
                const V2 r = rightAt (th);
                const double sd = st.variant == 7 || ! (k & 1) ? st.side : -st.side;
                double x, z;
                if (st.variant == 7) { x = pit->x; z = pit->z; }
                else { const V2 p = roadPointAt (a, st.d * sd); x = p[0]; z = p[1]; }
                const V2 toRoad { -r[0] * sd, -r[1] * sd };
                const double yaw = Js::atan2 (-toRoad[0], toRoad[1]);
                const double R = hyp (st.foot[0], st.foot[1]) / 2;
                if (st.variant != 7 && ! clearOf (x, z, R)) continue;
                const V2 fo = flatOver (x, z, st.foot[0] / 2, st.foot[1] / 2, yaw);
                if (st.variant != 7 && (fo[1] > 4.0 || featTap (x, z)[0] < 22)) continue;
                std::vector<Structure> items { { st.variant, x, z, st.variant == 7 ? pit->baseY + 0.4 : fo[0] + 0.2, yaw, 1, { 0, 0 }, false, true, 0, 0, 0 } };
                if (st.hasRover)
                {
                    const double c = Js::cos (yaw), sn = Js::sin (yaw), lx = st.rover[0], lz = st.rover[1];
                    const double qx = x + c * lx - sn * lz, qz = z + sn * lx + c * lz;
                    items.push_back ({ 5, qx, qz, floorHeight (qx, qz) - 0.12, yaw + PI / 2 + 0.22, 1, { 0.01, -0.02 }, false, true, 0, 0, 0 });
                }
                stations_.push_back ({ st.name, st.variant == 7 ? pitAt : a, seen, R, items });
                break;
            }
        }
        for (size_t gi = 0; gi < stations_.size(); ++gi)
            for (size_t n = 0; n < stations_[gi].items.size(); ++n)
            { auto& q = stations_[gi].items[n]; q.ci = -2000 - (double) gi; q.cj = (double) n; q.seed = hs ((double) (gi * 5 + n), 631); }
    }

    // THE DRIVE'S STATE, AS MADE
    hist.assign ((size_t) histN * histRows, 0);
    s = State();
    sky(); place();
}

//==============================================================================
// THE HARDWARE AND THE ROCKS

std::optional<IoWorld::Structure> IoWorld::structureAt (double ci, double cj) const
{
    const auto key = std::make_pair (ci, cj);
    if (const auto it = structCache.find (key); it != structCache.end()) return it->second;
    const auto made = [&]() -> std::optional<Structure>
    {
        const double M = worldPeriod / structCell, wi = mod (mod (ci, M) + M, M), wj = mod (mod (cj, M) + M, M);
        const bool site = hcell (std::floor (wi / 4), std::floor (wj / 4), 601) < siteProb;
        if (hcell (wi, wj, 602) > (site ? structProbIn : structProbOut)) return {};
        const double x = (ci + 0.2 + 0.6 * hcell (wi, wj, 603)) * structCell, z = (cj + 0.2 + 0.6 * hcell (wi, wj, 604)) * structCell;
        const double rd = std::abs (roadTap (x, z)[0]);
        if (rd < roadHalf + 13) return {};
        for (const auto& c : calderas_) if (hyp (x - c.x, z - c.z) < c.R + c.W + 12) return {};
        for (const auto& p : plumes_) if (hyp (x - p.x, z - p.z) < 40) return {};
        double hot = 1e9;
        for (const auto& c : calderas_) hot = jsMin (hot, hyp (x - c.x, z - c.z) - c.R - c.W);
        const double r = hcell (wi, wj, 605);
        const int variant = hot < 140 && r < 0.6 ? 4 : r < 0.27 ? 0 : r < 0.50 ? 1 : r < 0.72 ? 2 : r < 0.90 ? 3 : 4;
        if (slopeAt (x, z, variant == 0 ? 3 : 4) > 2.2) return {};
        const auto fl = featTap (x, z);
        const bool onFlow = fl[0] < -2 && fl[1] > 0.3;
        const double yaw = hcell (wi, wj, 606) * 2 * PI, k = 0.9 + 0.25 * hcell (wi, wj, 607);
        const double sink = onFlow ? 1.0 + 1.6 * hcell (wi, wj, 608) : 0.05 + 0.3 * hcell (wi, wj, 608);
        const V2 lean = onFlow ? V2 { (hcell (wi, wj, 609) - 0.5) * 0.22, (hcell (wi, wj, 610) - 0.5) * 0.22 } : V2 { (hcell (wi, wj, 609) - 0.5) * 0.04, (hcell (wi, wj, 610) - 0.5) * 0.04 };
        double y = floorHeight (x, z);
        for (int q = 0; q < 6; ++q) { const double a = q / 6.0 * 2 * PI; y = jsMin (y, floorHeight (x + 2.5 * Js::cos (a), z + 2.5 * Js::sin (a))); }
        return Structure { variant, x, z, y - sink, yaw, k, lean, onFlow, site, ci, cj, hcell (wi, wj, 611) };
    }();
    if (structCache.size() > 40000) structCache.clear();
    structCache.emplace (key, made);
    return made;
}
std::vector<IoWorld::Structure> IoWorld::structuresNear (double x, double z, int cellsN) const
{
    std::vector<Structure> out;
    const double c0 = std::floor (x / structCell) - cellsN / 2, c1 = std::floor (z / structCell) - cellsN / 2, half = cellsN / 2 * structCell;
    for (int j = 0; j < cellsN; ++j) for (int i = 0; i < cellsN; ++i) if (const auto st = structureAt (c0 + i, c1 + j)) out.push_back (*st);
    for (const auto& g : sites_) for (const auto& q : g.items) if (std::abs (q.x - x) < half && std::abs (q.z - z) < half) out.push_back (q);
    for (const auto& g : stations_) for (const auto& q : g.items) if (std::abs (q.x - x) < half + 60 && std::abs (q.z - z) < half + 60) out.push_back (q);
    return out;
}
std::optional<IoWorld::Rock> IoWorld::rockAt (double ci, double cj) const
{
    const auto key = std::make_pair (ci, cj);
    if (const auto it = rockCache.find (key); it != rockCache.end()) return it->second;
    if (rockCache.size() > 30000) rockCache.clear();
    std::optional<Rock> r;
    if (hcell (ci, cj, 41) <= 0.30)
    {
        const double x = (ci + 0.15 + 0.7 * hcell (ci, cj, 42)) * rockCell, z = (cj + 0.15 + 0.7 * hcell (ci, cj, 43)) * rockCell;
        const bool big = hcell (ci, cj, 44) > 0.97;
        const double size = big ? 1.2 + 1.6 * hcell (ci, cj, 45) : 0.18 + 0.55 * sq (hcell (ci, cj, 45));
        const double rd = std::abs (roadTap (x, z)[0]);
        const auto cf = calderaField (x, z);
        if (rd >= roadHalf + 0.5 + size && cf[1] <= 0.5)
        {
            const double sx = (1.0 + 0.4 * hcell (ci, cj, 47)) * size, sy = (0.75 + 0.45 * hcell (ci, cj, 48)) * size, sz = (1.0 + 0.35 * hcell (ci, cj, 49)) * size;
            const auto f = featTap (x, z);
            r = Rock { x, z, floorHeight (x, z) - 0.12 * sy, hcell (ci, cj, 46) * 2 * PI, sx, sy, sz, hcell (ci, cj, 50) + (f[1] > 0.3 && f[0] < 0 ? 2 : 0) };
        }
    }
    rockCache.emplace (key, r);
    return r;
}
std::vector<IoWorld::Rock> IoWorld::rocksNear (double x, double z, double fx, double fz, double reach) const
{
    std::vector<Rock> out;
    const int n = (int) std::ceil (reach / rockCell);
    const double c0 = std::floor (x / rockCell), c1 = std::floor (z / rockCell);
    for (int j = -n; j <= n; ++j)
        for (int i = -n; i <= n; ++i)
        {
            const double cx = (c0 + i + 0.5) * rockCell - x, cz = (c1 + j + 0.5) * rockCell - z, D = hyp (cx, cz);
            if (D > reach + 4 || (D > 14 && cx * fx + cz * fz < 0.35 * D - 4)) continue;
            if (const auto r = rockAt (c0 + i, c1 + j)) out.push_back (*r);
        }
    return out;
}
void IoWorld::setPitRadius (double r) noexcept
{
    for (auto& p : calderas_)
        if (p.pit) { if (r > 2 && r < 60) { p.R = r - p.W; p.out = r + 5; } return; }
}
double IoWorld::plumeAct (const Plume& p, double b) noexcept { return jsMax (p.base, smoothstep (p.th, p.th + 0.3, b)); }
double IoWorld::fountainAct (const Fountain& f, double b) noexcept { return smoothstep (f.th, f.th + 0.2, b); }
double IoWorld::lakeAct (const Caldera& c, double b) noexcept { return c.lake > 0 ? clamp (0.12 + 0.88 * smoothstep (c.th - 0.1, c.th + 0.8, b), 0, 1) : 0; }

//==============================================================================
// THE DRIVE AND THE KNOBS

double IoWorld::speedFor (int g) const noexcept { return gearSpeed[(size_t) (std::clamp (g, 1, numGears) - 1)]; }
void IoWorld::setGear (int g) noexcept { gearWanted = std::clamp (g == 0 ? 1 : g, 1, numGears); s.gear = gearWanted; }
void IoWorld::setKnob (Knob k, double v) noexcept { want[(size_t) k] = clamp (v == v ? v : 0, 0, 1); }
void IoWorld::place()
{
    const double th = thetaAt (s.sr);
    const V2 p = loopPoint (th), r = rightAt (th), t = tangentAt (th);
    s.x = p[0] + s.lat * r[0]; s.z = p[1] + s.lat * r[1];
    s.heading = Js::atan2 (t[0], -t[1]) + Js::atan2 (s.latV, jsMax (s.speed, 2.0)) * 0.8;
}
void IoWorld::sky()
{
    s.sun = sunSolve (s.drive, s.blend);
    s.sunK = s.sun.vis * s.sun.gate; s.dark = s.sun.dark;
    s.jupLit = (1 - Js::cos (s.sun.sep)) / 2;
    s.cabinDay = s.sun.cabin;
}
namespace
{
    double actOf (double E) { return IoWorld::act.quiet + (1 - IoWorld::act.quiet) * smoothstep (0, 1, E); }
    double envOf (double a)
    {
        const double y = clamp ((a - IoWorld::act.quiet) / (1 - IoWorld::act.quiet), 0, 1);
        return y <= 0 ? 0 : y >= 1 ? 1 : 0.5 - Js::sin (jsAsin (1 - 2 * y) / 3);
    }
}
void IoWorld::stepAct (double dt)
{
    s.actE = s.actHold + (s.actE - s.actHold) * Js::exp (-dt / act.tau);
    s.act = actOf (s.actE);
    if (s.autoHits && s.time >= s.autoNext)
    {
        ++s.autoN;
        hitNow (0.7 + 0.3 * hs (s.autoN, 4451));
        s.autoNext = s.time + act.autoMin + (act.autoMax - act.autoMin) * hs (s.autoN, 9907);
    }
}
double IoWorld::doseRest (double t) const
{
    double a = 0;
    for (const auto& [k, f, p] : dose.sines) a = a + k * Js::sin (2 * PI * f * t + p);
    return dose.rest + a + dose.jitter * s.doseJ;
}
void IoWorld::stepDose (double dt)
{
    ++s.doseN;
    const double g = (hs (s.doseN, 7177) + hs (s.doseN, 3301) + hs (s.doseN, 9419) - 1.5) * 2.0;
    s.doseJ += -s.doseJ * dt / dose.jitterTau + g * std::sqrt (2 * dt / dose.jitterTau);
    s.doseE *= Js::exp (-dt / dose.tau);
    const double target = doseRest (s.time) + dose.span * (1 - Js::exp (-dose.k * s.doseE));
    const int n = (int) jsMax (1, std::ceil (dt * 240));
    const double h = dt / n, w = dose.w0;
    for (int i = 0; i < n; ++i)
    {
        s.doseV += (w * w * (target - s.dose) - 2 * dose.zeta * w * s.doseV) * h;
        s.dose += s.doseV * h;
        if (s.dose > 1) { s.dose = 1; if (s.doseV > 0) s.doseV *= -dose.bounce; }
        if (s.dose < 0) { s.dose = 0; if (s.doseV < 0) s.doseV *= -dose.bounce; }
    }
}
void IoWorld::writeHist (bool fill)
{
    const int slot = toI32 (std::floor (mod (s.time, 100000) / histDt)) & (histN - 1);
    const auto put = [this] (int sl)
    {
        for (const auto& p : plumes_) hist[(size_t) (p.row * histN + sl)] = u8 (Js::round (plumeAct (p, s.act) * 255));
        for (const auto& f : fountains_) hist[(size_t) (f.row * histN + sl)] = u8 (Js::round (fountainAct (f, s.act) * 255));
    };
    if (fill || histSlot < 0) for (int k = 0; k < histN; ++k) put (k);
    else for (int k = histSlot; k != slot; k = (k + 1) & (histN - 1)) put ((k + 1) & (histN - 1));
    put (slot); histSlot = slot;
}
void IoWorld::step (double dt)
{
    const double L = loopLength();
    s.time += dt; s.dt = dt; s.hitT += dt;
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
    s.drive += (want[0] - s.drive) * ease (dt, driveSeconds);
    s.blend += (want[1] - s.blend) * ease (dt, blendSeconds);
    s.presence += (want[2] - s.presence) * ease (dt, presenceSeconds);
    sky();
    stepAct (dt);
    writeHist (false);
    stepDose (dt);
    s.sr += s.speed * dt; s.travel += s.speed * dt;
    if (s.sr >= L) { s.sr -= L; ++s.lap; }
    place();
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double g0 = floorHeight (s.x, s.z), sp = s.speed / 10;
    const double bob = 0.010 * sp * (Js::sin (s.travel * 1.7) + 0.6 * Js::sin (s.travel * 4.3 + 1));
    const double targetY = g0 + eye + bob;
    if (s.camY == 0) s.camY = targetY;
    s.camY += (targetY - s.camY) * ease (dt, 0.12);
    const double gb = floorHeight (s.x - fx * 5, s.z - fz * 5), gf = floorHeight (s.x + fx * 5, s.z + fz * 5);
    const double pitchT = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 10);
    s.pitch += (pitchT - s.pitch) * ease (dt, 0.35);
    for (auto& p : s.puffs) p.t += dt;
    while (! s.puffs.empty() && s.puffs.front().t > 8) s.puffs.erase (s.puffs.begin());
}
void IoWorld::settle()
{
    s.speed = parked ? 0 : speedFor (gearWanted);
    if (parked) { s.lat = parkLat; s.park = 1; }
    s.drive = want[0]; s.blend = want[1]; s.presence = want[2];
    sky(); s.act = actOf (s.actE); writeHist (true); place();
    if (s.doseE < 1e-3) { s.dose = clamp (doseRest (s.time), 0, 1); s.doseV = 0; }
}
void IoWorld::settleCamera() noexcept
{
    if (! built || s.camY != 0)
        return;
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double g0 = floorHeight (s.x, s.z), sp = s.speed / 10;
    const double bob = 0.010 * sp * (Js::sin (s.travel * 1.7) + 0.6 * Js::sin (s.travel * 4.3 + 1));
    s.camY = g0 + eye + bob;
    const double gb = floorHeight (s.x - fx * 5, s.z - fz * 5), gf = floorHeight (s.x + fx * 5, s.z + fz * 5);
    s.pitch = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 10);
}
void IoWorld::startAt (double sr, int lap)
{
    s.sr = modL (sr == sr ? sr : 0);
    if (lap >= 0) s.lap = lap;
    s.camY = 0; place();
}
void IoWorld::setActivity (double a, bool hold)
{
    const double E = envOf (a == a ? a : 0);
    s.actE = E; s.actHold = hold ? E : 0; s.act = actOf (s.actE); writeHist (true);
}
void IoWorld::setAuto (bool on) noexcept { if (on && ! s.autoHits) s.autoNext = s.time; s.autoHits = on; }
void IoWorld::hit (double level) { hitNow (level); }
void IoWorld::quiet() noexcept { s.hitT = 1e4; s.hitS = 0; s.puffs.clear(); s.doseE = 0; }
void IoWorld::setPitchOffset (double rad) noexcept { s.pitchOffset = clamp (rad == rad ? rad : 0, -0.5, 0.5); }
void IoWorld::hitNow (double level)
{
    const double lv = clamp (level == level ? level : 0, 0, 1);
    s.hitT = 0; s.hitS = lv; ++s.hits;
    s.doseE = jsMin (3, s.doseE + lv);
    s.actE = jsMin (act.cap, s.actE + act.perHit * lv);
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    struct Cand { double x, y, z, k, R, D; };
    std::vector<Cand> cand;
    for (const auto& p : plumes_) cand.push_back ({ p.x, p.y, p.z, 1.0, 12, 0 });
    for (const auto& c : calderas_) if (c.lake > 0) cand.push_back ({ c.x, c.baseY - c.D, c.z, 0.6, c.lakeR, 0 });
    std::vector<Cand> seen;
    for (auto q : cand)
    {
        q.D = hyp (q.x - s.x, q.z - s.z);
        if (q.D < 2500 && (q.x - s.x) * fx + (q.z - s.z) * fz > 0.45 * q.D) seen.push_back (q);
    }
    std::stable_sort (seen.begin(), seen.end(), [] (const Cand& a, const Cand& b) { return a.D - b.D < 0; });
    for (size_t i = 0; i < seen.size() && i < 3; ++i)
        s.puffs.push_back ({ seen[i].x, seen[i].y, seen[i].z, -0.25 - (double) i * 0.12, lv * seen[i].k, seen[i].R, (s.hits * 7 + (int) i) % 97 });
    while (s.puffs.size() > 6) s.puffs.erase (s.puffs.begin());
}

//==============================================================================
// THE JUMP LIST

std::vector<IoWorld::Place> IoWorld::places() const
{
    std::vector<Place> out;
    const Caldera* big = nullptr;
    for (const auto& c : calderas_) if (c.big) { big = &c; break; }
    const Plume* tallest = &plumes_.front();
    for (const auto& p : plumes_) if (p.H > tallest->H) tallest = &p;
    const Caldera* lake = nullptr; Near ln; double lk = 0;
    for (const auto& c : calderas_)
    {
        if (! (c.lake > 0)) continue;
        const Near n = nearestOnLoop (c.x, c.z);
        if (lake == nullptr || n.d - c.lakeR < lk) { lake = &c; ln = n; lk = n.d - c.lakeR; }
    }
    out.push_back ({ "big", "the biggest caldera", nearestOnLoop (big->x, big->z).at, 520 });
    out.push_back ({ "big", "the tallest plume", tallest->view ? *tallest->view : modL (tallest->near.at - 200), tallest->view ? 0.0 : 300.0 });
    out.push_back ({ "big", "the road's closest pass to a lava lake", ln.at, 160 });
    for (const auto& c : calderas_) if (! c.pit) out.push_back ({ "paterae", c.name, nearestOnLoop (c.x, c.z).at, jsMin (600, 140 + c.R * 1.5) });
    for (const auto& p : plumes_)
        out.push_back ({ "plumes", p.name + " \xc2\xb7 " + numberText (p.H) + " m", p.view ? *p.view : modL (p.near.at - jsMin (600, p.near.d * 0.5)), p.view ? 0.0 : 200.0 });
    int fl = 0;
    for (const auto& f : flows_) if (f.fissure) { const Near n = nearestOnLoop (f.pts[0][0], f.pts[0][1]); out.push_back ({ "road", "a fresh flow " + std::to_string (++fl), n.at, 150 }); }
    out.push_back ({ "road", "Jupiter dead ahead", modL (startAt0 + 160), 60 });
    for (size_t i = 0; i < sites_.size(); ++i)
    {
        std::string names;
        for (const auto& q : sites_[i].items) names += (names.empty() ? "" : ", ") + std::string (variants[(size_t) q.variant]);
        out.push_back ({ "hardware", "site " + std::to_string (i + 1) + " (" + names + ")", sites_[i].at, 120 });
    }
    for (const auto& g : stations_) out.push_back ({ "stations", g.name, g.at, g.seen });
    std::stable_sort (out.begin(), out.end(), [] (const Place& a, const Place& b) { return a.at - b.at < 0; });
    return out;
}
IoWorld::Jump IoWorld::jumpTo (const Place& pl)
{
    const double L = loopLength();
    const double at = mod (mod (pl.at - pl.seen - 3 * speedOf(), L) + L, L);
    startAt (at, s.lap);
    return { Js::round (at), s.lap };
}

} // namespace orbitamp::io
