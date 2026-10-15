// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IceWorld.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::frostpaw
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr float camHeight = 2.0f;

    /** A small, good 32-bit generator; its stream is the landscape, so it never changes. */
    struct Rng
    {
        uint32_t a;
        float next() noexcept
        {
            a += 0x6D2B79F5u;
            uint32_t t = (a ^ (a >> 15)) * (1u | a);
            t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
            return (float) ((double) (t ^ (t >> 14)) / 4294967296.0);
        }
    };

    /** The recursive-subdivision fractal on a wrapping n x n grid: the square's centre, then the edge
        midpoints, each the average of its neighbours plus noise that shrinks by `rough` a level. */
    std::vector<float> plasma (int n, uint32_t seed, float rough)
    {
        std::vector<float> h ((size_t) n * (size_t) n, 0.0f);
        Rng rnd { seed };
        const int m = n - 1;
        float amp = 1.0f;
        for (int step = n; step > 1; step >>= 1)
        {
            const int half = step >> 1;
            for (int y = 0; y < n; y += step)
                for (int x = 0; x < n; x += step)
                {
                    const int x1 = (x + step) & m, y1 = (y + step) & m;
                    h[(size_t) ((y + half) * n + x + half)] = 0.25f * (h[(size_t) (y * n + x)] + h[(size_t) (y * n + x1)] + h[(size_t) (y1 * n + x)] + h[(size_t) (y1 * n + x1)]) + (rnd.next() * 2 - 1) * amp;
                }
            for (int y = 0; y < n; y += half)
                for (int x = ((y / half) & 1) ? 0 : half; x < n; x += step)
                {
                    const int xm = (x - half) & m, xp = (x + half) & m, ym = (y - half) & m, yp = (y + half) & m;
                    h[(size_t) (y * n + x)] = 0.25f * (h[(size_t) (y * n + xm)] + h[(size_t) (y * n + xp)] + h[(size_t) (ym * n + x)] + h[(size_t) (yp * n + x)]) + (rnd.next() * 2 - 1) * amp;
                }
            amp *= rough;
        }
        return h;
    }

    /** The asymmetric blur w = k1 w(u,v) + k2 w(u+a,v-b) + k3 w(u-c,v+d), its taps scaled to the grid. */
    std::vector<float> blurAsymmetric (std::vector<float> src, int n, int passes)
    {
        const int m = n - 1;
        const double sc = n / 256.0;
        const int o1x = (int) std::lround (3 * sc), o1y = (int) std::lround (-2 * sc), o2x = (int) std::lround (-2 * sc), o2y = (int) std::lround (4 * sc);
        for (int p = 0; p < passes; ++p)
        {
            std::vector<float> dst ((size_t) n * (size_t) n);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    dst[(size_t) (y * n + x)] = 0.5f * src[(size_t) (y * n + x)]
                                              + 0.25f * src[(size_t) (((y + o1y) & m) * n + ((x + o1x) & m))]
                                              + 0.25f * src[(size_t) (((y + o2y) & m) * n + ((x + o2x) & m))];
            src = std::move (dst);
        }
        return src;
    }

    std::vector<float>& normalize01 (std::vector<float>& h)
    {
        float lo = h[0], hi = h[0];
        for (float v : h) { lo = std::min (lo, v); hi = std::max (hi, v); }
        const float k = 1.0f / (hi - lo);
        for (float& v : h) v = (v - lo) * k;
        return h;
    }

    inline float smoothstep (float a, float b, float x) noexcept
    {
        const float t = std::clamp ((x - a) / (b - a), 0.0f, 1.0f);
        return t * t * (3 - 2 * t);
    }

    inline uint32_t ih (uint32_t x) noexcept
    {
        x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
    }
    inline float hcell (int cx, int cy, uint32_t s) noexcept
    {
        return (float) ((double) ih (((uint32_t) cx * 0x9E3779B1u) ^ ih (((uint32_t) cy * 0x85EBCA77u) ^ s)) / 4294967296.0);
    }
    inline int wrapMod (int a, int m) noexcept { return ((a % m) + m) % m; }
    inline float sq (float x) noexcept { return x * x; }

    constexpr double off1[] { 0.37, 0.61 }, off2[] { 0.71, 0.13 }, off3[] { 0.23, 0.87 };   // the layers' fixed texture offsets

    /** A family of lineae: parallel lines `s = dot (p, dir) + warp`, a period apart, each hashed for its
        height, width and phase; the ridge fades in and out along its length with the sine of the
        along-line coordinate. The profile is two gaussians either side of a central trough. */
    struct LineaFamily { double dx, dz, px, pz, period, wavelength; uint32_t seed; };
    constexpr LineaFamily lineae[2] { { 0.904752, 0.425939, -0.425939, 0.904752, 1100.0, 1600.0, 101u },
                                      { -0.370181, 0.928960, -0.928960, -0.370181, 1700.0, 2300.0, 202u } };
    float lineaHeight (double px, double pz, float warp, const LineaFamily& F) noexcept
    {
        const double sv = px * F.dx + pz * F.dz + warp;
        const double fi = std::floor (sv / F.period);
        const int i = (int) fi;
        const float ds = (float) (sv - (fi + 0.5) * F.period);
        const float r1 = hcell (i, 7, F.seed), r2 = hcell (i, 8, F.seed), r3 = hcell (i, 9, F.seed);
        const float H = 3.0f + 8.0f * r1 * r1, w = 9 + 9 * r2, sg = 0.55f * w;
        const double tv = px * F.px + pz * F.pz;
        const float amp = smoothstep (0.15f, 0.55f, 0.5f + 0.5f * (float) std::sin (tv / F.wavelength * 6.2831853 + r3 * 6.2831853));
        const float e1 = std::exp (-sq ((ds - w) / sg)), e2 = std::exp (-sq ((ds + w) / sg)), e3 = std::exp (-sq (ds / (0.7f * sg)));
        return amp * H * (e1 + e2 - 0.45f * e3);
    }

    /** The chaos terrain inside a patch: tilted plateaus on the 14 m grid over a lowered rubble floor. */
    float chaosHeight (double px, double pz, float mask, float cg) noexcept
    {
        if (mask < 0.005f) return 0.0f;
        constexpr double CHB = IceWorld::chaosCell; constexpr int CHM = (int) (IceWorld::worldPeriod / IceWorld::chaosCell);
        const double qx = px / CHB, qz = pz / CHB;
        const double fx0 = std::floor (qx), fz0 = std::floor (qz);
        const float fx = (float) (qx - fx0), fz = (float) (qz - fz0);
        const int cx = wrapMod ((int) fx0, CHM), cz = wrapMod ((int) fz0, CHM);
        const float ex = hcell (cx, cz, 31) <= 0.78f ? 1.0f : 0.0f;
        const float hgt = 0.6f + 3.6f * sq (hcell (cx, cz, 32));
        const float tx = (hcell (cx, cz, 33) - 0.5f) * 0.45f, tz = (hcell (cx, cz, 34) - 0.5f) * 0.45f;
        const float m = 0.08f + 0.10f * hcell (cx, cz, 35);
        const float pl = smoothstep (m, m + 0.16f, std::min (fx, 1 - fx)) * smoothstep (m, m + 0.16f, std::min (fz, 1 - fz));
        const float top = hgt + tx * (fx - 0.5f) * (float) CHB + tz * (fz - 0.5f) * (float) CHB;
        return mask * (ex * pl * top + (cg - 0.5f) * 1.2f - 0.8f);
    }
}

IceWorld::IceWorld (int g) noexcept : gears (std::clamp (g, 1, maxGears)) {}

//==============================================================================
void IceWorld::build()
{
    if (built)
        return;
    const int n = texSize;
    const size_t nn = (size_t) n * (size_t) n;
    auto R = blurAsymmetric (plasma (n, 1101, 0.56f), n, 2); normalize01 (R);          // uplands
    auto G = blurAsymmetric (plasma (n, 2202, 0.50f), n, 1); normalize01 (G);          // the hills' modulation, the rubble
    auto Bp = plasma (n, 3303, 0.60f);
    for (auto& v : Bp) v = 1 - std::abs (2 * v - 1);                                    // ridged: the lineae's warp, the upland mask
    normalize01 (Bp);
    auto B = blurAsymmetric (std::move (Bp), n, 1); normalize01 (B);
    auto A = blurAsymmetric (plasma (n, 4404, 0.42f), n, 2); normalize01 (A);          // the smooth swell, the chaos patches
    height.resize (nn * 4);
    for (size_t i = 0; i < nn; ++i) { height[i * 4] = R[i]; height[i * 4 + 1] = G[i]; height[i * 4 + 2] = B[i]; height[i * 4 + 3] = A[i]; }

    // The mips, 2 x 2 box: a float texture's mips are made here, not by the driver.
    mips.clear();
    mips.push_back (height);
    for (int size = n; size > 1; size >>= 1)
    {
        const auto& cur = mips.back();
        const int s2 = size >> 1;
        std::vector<float> nxt ((size_t) s2 * (size_t) s2 * 4);
        for (int y = 0; y < s2; ++y)
            for (int x = 0; x < s2; ++x)
                for (int c = 0; c < 4; ++c)
                    nxt[(size_t) ((y * s2 + x) * 4 + c)] = 0.25f * (cur[(size_t) (((2 * y) * size + 2 * x) * 4 + c)] + cur[(size_t) (((2 * y) * size + 2 * x + 1) * 4 + c)]
                                                                  + cur[(size_t) (((2 * y + 1) * size + 2 * x) * 4 + c)] + cur[(size_t) (((2 * y + 1) * size + 2 * x + 1) * 4 + c)]);
        mips.push_back (std::move (nxt));
    }

    // The gradients: central differences of each channel per uv unit, scaled by the 99.9th percentile
    // of their magnitude (the plasma has rare large steps) and clamped beyond, into two RGBA8 textures.
    {
        const int m = n - 1;
        const int chans[] { 0, 2, 1, 3 };     // texture 1: R, B   texture 2: G, A
        std::vector<std::vector<float>> grads;
        for (int ci = 0; ci < 4; ++ci)
        {
            const int ch = chans[ci];
            std::vector<float> g (nn * 2);
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                {
                    g[(size_t) ((y * n + x) * 2)]     = (height[(size_t) ((y * n + ((x + 1) & m)) * 4 + ch)] - height[(size_t) ((y * n + ((x - 1) & m)) * 4 + ch)]) * 0.5f * (float) n;
                    g[(size_t) ((y * n + x) * 2 + 1)] = (height[(size_t) ((((y + 1) & m) * n + x) * 4 + ch)] - height[(size_t) ((((y - 1) & m) * n + x) * 4 + ch)]) * 0.5f * (float) n;
                }
            std::vector<float> a;
            for (size_t k = 0; k < g.size(); k += 97) a.push_back (std::abs (g[k]));
            std::sort (a.begin(), a.end());
            const float q = a[(size_t) std::floor ((double) a.size() * 0.999)];
            gmax[(size_t) ci] = q > 0 ? q : 1.0f;
            grads.push_back (std::move (g));
        }
        const auto encode = [] (const std::vector<float>& ga, const std::vector<float>& gb, float sa, float sb)
        {
            std::vector<uint8_t> px (ga.size() * 2);
            const size_t count = ga.size() / 2;
            const auto q = [] (float v) { return (uint8_t) std::lround (127.5f + 127.5f * std::clamp (v, -1.0f, 1.0f)); };
            for (size_t i = 0; i < count; ++i)
            {
                px[i * 4]     = q (ga[i * 2] / sa);
                px[i * 4 + 1] = q (ga[i * 2 + 1] / sa);
                px[i * 4 + 2] = q (gb[i * 2] / sb);
                px[i * 4 + 3] = q (gb[i * 2 + 1] / sb);
            }
            return px;
        };
        grad1 = encode (grads[0], grads[1], gmax[0], gmax[1]);
        grad2 = encode (grads[2], grads[3], gmax[2], gmax[3]);
    }

    // The detail: sastrugi and crust grain near the rover, a gradient in RG and the value in B. The
    // value is averaged along x over five texels, so the ripples run across the wind.
    {
        const int d = detailSize, m = d - 1;
        auto v = plasma (d, 5505, 0.62f); normalize01 (v);
        std::vector<float> w ((size_t) d * (size_t) d);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
            {
                float sum = 0;
                for (int k = -2; k <= 2; ++k) sum += v[(size_t) (y * d + ((x + k) & m))];
                w[(size_t) (y * d + x)] = sum / 5;
            }
        detailPx.resize ((size_t) d * (size_t) d * 4);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
            {
                const float gx = (w[(size_t) (y * d + ((x + 1) & m))] - w[(size_t) (y * d + ((x - 1) & m))]) * 4;
                const float gy = (w[(size_t) (((y + 1) & m) * d + x)] - w[(size_t) (((y - 1) & m) * d + x)]) * 4;
                const size_t i = (size_t) (y * d + x) * 4;
                detailPx[i]     = (uint8_t) (int) (128 + std::clamp (gx * 127, -127.0f, 127.0f));
                detailPx[i + 1] = (uint8_t) (int) (128 + std::clamp (gy * 127, -127.0f, 127.0f));
                detailPx[i + 2] = (uint8_t) (int) (w[(size_t) (y * d + x)] * 255);
                detailPx[i + 3] = 255;
            }
    }
    built = true;
    place (s.x, s.z, s.heading, gearWanted, turbo, parked);
}

//==============================================================================
float IceWorld::tap (int ch, double u, double v) const noexcept
{
    const int n = texSize, m = n - 1;
    const double x = u * n - 0.5, y = v * n - 0.5;
    const double x0 = std::floor (x), y0 = std::floor (y);
    const float fx = (float) (x - x0), fy = (float) (y - y0);
    const int i0 = (int) x0 & m, i1 = ((int) x0 + 1) & m, j0 = (int) y0 & m, j1 = ((int) y0 + 1) & m;
    const float a = height[(size_t) ((j0 * n + i0) * 4 + ch)], b = height[(size_t) ((j0 * n + i1) * 4 + ch)];
    const float c = height[(size_t) ((j1 * n + i0) * 4 + ch)], d = height[(size_t) ((j1 * n + i1) * 4 + ch)];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

float IceWorld::chaosMask (double px, double pz) const noexcept
{
    return built ? smoothstep (0.66f, 0.74f, tap (3, px / 1250 + off1[0], pz / 1250 + off1[1])) : 0.0f;
}

float IceWorld::terrainHeight (double px, double pz, float dist) const noexcept
{
    if (! built)
        return 0.0f;
    float a[4], b[4], c[4];
    for (int ch = 0; ch < 4; ++ch)
    {
        a[ch] = tap (ch, px / 7000, pz / 7000);
        b[ch] = tap (ch, px / 1250 + off1[0], pz / 1250 + off1[1]);
        c[ch] = tap (ch, px / 190 + off2[0], pz / 190 + off2[1]);
    }
    const float swell = (a[3] - 0.5f) * 70, hills = (b[0] - 0.5f) * 30 * (0.3f + 0.7f * a[1]);
    const float ridges = (a[0] - 0.5f) * 24 * smoothstep (0.55f, 0.8f, a[2]);
    const float fade = 1 - smoothstep (600, 2000, dist);
    float dune = ((c[0] - 0.5f) * 1.6f + (c[2] - 0.5f) * 0.6f) * fade;
    const float fade3 = 1 - smoothstep (250, 600, dist);
    if (fade3 > 0)
    {
        const float d0 = tap (0, px / 50 + off3[0], pz / 50 + off3[1]), d1 = tap (1, px / 50 + off3[0], pz / 50 + off3[1]), d2 = tap (2, px / 50 + off3[0], pz / 50 + off3[1]);
        dune += ((d0 - 0.5f) * 1.1f + (d2 - 0.5f) * 0.9f + (d1 - 0.5f) * 0.4f) * fade3;
    }
    const float warp = (b[2] - 0.5f) * 160;
    const float lin = lineaHeight (px, pz, warp, lineae[0]) + lineaHeight (px, pz, warp, lineae[1]);
    const float cm = smoothstep (0.66f, 0.74f, b[3]) * (1 - smoothstep (700, 900, dist));
    float h = swell + hills + ridges + dune + lin;
    if (dist < 900) h += chaosHeight (px, pz, cm, c[1]);
    if (dist < 400) h += structDrift (px, pz, cm) * (1 - smoothstep (300, 400, dist));
    return h;
}

//==============================================================================
bool IceWorld::structureAt (int ci, int cj, Structure& out) const noexcept
{
    constexpr int SM = (int) (worldPeriod / structCell);
    const int cx = wrapMod (ci, SM), cz = wrapMod (cj, SM);
    const int gx = (ci + SM * 64) / 4, gz = (cj + SM * 64) / 4;    // the super-cell: positive, so the division floors
    const float prob = hcell (gx, gz, 47) < 0.3f ? structProbIn : structProbOut;
    if (hcell (cx, cz, 41) > prob)
        return false;
    constexpr float radii[numVariants] { 2.4f, 6.0f, 2.8f, 3.4f };
    out.variant = std::min (numVariants - 1, (int) (hcell (cx, cz, 42) * 4));
    out.scale = 0.8f + 0.5f * hcell (cx, cz, 45);
    out.radius = radii[out.variant] * out.scale;
    out.x = (ci + 0.22 + 0.56 * hcell (cx, cz, 43)) * structCell;
    out.z = (cj + 0.22 + 0.56 * hcell (cx, cz, 44)) * structCell;
    out.ci = ci; out.cj = cj;
    return true;
}

void IceWorld::structuresNear (double x, double z, int cells, std::vector<Structure>& out) const
{
    out.clear();
    const int ci = (int) std::floor (x / structCell), cj = (int) std::floor (z / structCell);
    Structure st;
    for (int j = cj - cells; j <= cj + cells; ++j)
        for (int i = ci - cells; i <= ci + cells; ++i)
            if (structureAt (i, j, st) && chaosMask (st.x, st.z) <= 0.02f)   // one stands in chaos terrain only on the GPU's say-so: the same mask
                out.push_back (st);
}

void IceWorld::structuresInWindow (int bx, int bz, std::array<std::vector<float>, numVariants>& cells) const
{
    for (auto& v : cells) v.clear();
    Structure st;
    for (int j = 0; j < structWindow; ++j)
        for (int i = 0; i < structWindow; ++i)
            if (structureAt (bx + i, bz + j, st) && chaosMask (st.x, st.z) <= 0.02f)
            {
                cells[(size_t) st.variant].push_back ((float) (bx + i));
                cells[(size_t) st.variant].push_back ((float) (bz + j));
            }
}

float IceWorld::structDrift (double px, double pz, float chaos) const noexcept
{
    Structure st;
    if (! structureAt ((int) std::floor (px / structCell), (int) std::floor (pz / structCell), st))
        return 0.0f;
    const float r2 = sq ((float) (px - st.x)) + sq ((float) (pz - st.z));
    return 0.55f * st.scale * std::exp (-r2 / (2 * sq (st.radius + 1.5f))) * (1 - chaos);
}

/** The steel ahead: a yaw away from any structure the rover would pass within its radius + 8 m in
    the next ~110 m, growing as it nears; up to 50 degrees a second. */
double IceWorld::avoidStructures (double fx, double fz)
{
    const double rx = -fz, rz = fx;
    double yaw = 0;
    structuresNear (s.x, s.z, 2, near);
    for (const auto& st : near)
    {
        const double dx = st.x - s.x, dz = st.z - s.z, along = dx * fx + dz * fz, lat = dx * rx + dz * rz;
        if (along < -2 || along > 110) continue;
        const double clear = st.radius + 8;
        if (std::abs (lat) > clear + 6) continue;
        const double urgency = 1 - std::max (0.0, along) / 110;
        const double side = lat >= 0 ? -1 : 1;                                   // the structure on the right: turn left
        yaw += side * urgency * (0.4 + 0.6 * (1 - std::min (1.0, std::abs (lat) / (clear + 6)))) * 45 * pi / 180;
    }
    return std::clamp (yaw * 180 / pi, -50.0, 50.0) * pi / 180;
}

//==============================================================================
void IceWorld::setGear (int g) noexcept   { gearWanted = std::clamp (g, 1, gears); }
void IceWorld::setSteer (float v) noexcept { steer = std::clamp (v, -1.0f, 1.0f); }

float IceWorld::speedFor (int g, bool t) const noexcept
{
    return gearSpeed[(size_t) std::clamp (g, 1, gears) - 1] * (t ? turboFactor : 1.0f);
}

float IceWorld::snowFor (int g, bool t, bool p) const noexcept
{
    const auto i = (size_t) (maxGears - gears + std::clamp (g, 1, gears) - 1);    // the top gear the blizzard
    return p ? parkedSnow : (t ? turboSnow[i] : gearSnow[i]);
}

void IceWorld::place (double x, double z, double heading, int g, bool t, bool p)
{
    gearWanted = std::clamp (g, 1, gears);
    turbo = t; parked = p;
    s.x = x; s.z = z; s.heading = heading;
    s.speed = parked ? 0.0f : speedFor (gearWanted, turbo);
    s.snow = snowFor (gearWanted, turbo, parked);
    yawRate = 0.0;
    s.camY = 0.0; s.pitch = 0.0;
    lastSteerTime = s.time;
    step (0.0);
}

void IceWorld::step (double dt)
{
    s.time += dt;
    const float target = parked ? 0.0f : speedFor (gearWanted, turbo);
    const float kS = (float) (1 - std::exp (-dt / (parked ? parkSeconds : speedSeconds)));
    s.speed += (target - s.speed) * kS;
    s.snow += (snowFor (gearWanted, turbo, parked) - s.snow) * (float) (1 - std::exp (-dt / snowEaseSeconds));

    // Yaw: the player's hand wins, with the steel's push added so a mast steered at is still passed
    // around; a gentle meander resumes after 1.5 s of no input, plus a lean away from higher ground
    // 300 m ahead and from broken ice (three chaos samples, 140 / 260 / 300 m out). Parked, the hand
    // turns the view in place and nothing meanders.
    double yawTarget;
    if (parked)
    {
        yawTarget = steer * parkedYawDegrees * pi / 180;
        lastSteerTime = s.time;
    }
    else
    {
        const double fx0 = std::sin (s.heading), fz0 = -std::cos (s.heading);
        const double avoid = avoidStructures (fx0, fz0);
        if (std::abs (steer) > 0.001f)
        {
            yawTarget = steer * steerYawDegrees * pi / 180 + avoid;
            lastSteerTime = s.time;
        }
        else
        {
            const float fadeIn = smoothstep (1.5f, 4.5f, (float) (s.time - lastSteerTime));
            const double ca = std::cos (0.61), sa = std::sin (0.61);
            const double pRx = s.x + 300 * (fx0 * ca - fz0 * sa), pRz = s.z + 300 * (fz0 * ca + fx0 * sa);
            const double pLx = s.x + 300 * (fx0 * ca + fz0 * sa), pLz = s.z + 300 * (fz0 * ca - fx0 * sa);
            const float hR = terrainHeight (pRx, pRz, 300), hL = terrainHeight (pLx, pLz, 300);
            const double lean = std::clamp ((hL - hR) * 0.08, -6.0, 6.0) * pi / 180;    // left higher: turn right
            const float cR = chaosMask (pRx, pRz) + chaosMask (s.x + 140 * (fx0 * ca - fz0 * sa), s.z + 140 * (fz0 * ca + fx0 * sa));
            const float cL = chaosMask (pLx, pLz) + chaosMask (s.x + 140 * (fx0 * ca + fz0 * sa), s.z + 140 * (fz0 * ca - fx0 * sa));
            const float cC = chaosMask (s.x + 260 * fx0, s.z + 260 * fz0);
            const double chaosLean = ((cL - cR) * 10 + cC * (cL >= cR ? 12 : -12)) * pi / 180;
            yawTarget = fadeIn * (4.5 * pi / 180 * std::sin (s.time * 0.11 + meanderPhase) + lean) + chaosLean + avoid;
        }
        s.avoidYaw = (float) avoid;
    }
    yawRate += (yawTarget - yawRate) * (1 - std::exp (-dt / 0.35));
    s.heading += yawRate * dt;

    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);
    s.x += fx * s.speed * dt; s.z += fz * s.speed * dt; s.travel += s.speed * dt;

    // The hard rule: never inside a structure's footprint — a slide along its edge, never through.
    structuresNear (s.x, s.z, 1, near);
    for (const auto& st : near)
    {
        const double dx = s.x - st.x, dz = s.z - st.z, d = std::hypot (dx, dz), r = st.radius + 1.5;
        if (d < r && d > 1e-6) { s.x = st.x + dx / d * r; s.z = st.z + dz / d * r; }
    }
    s.x = std::fmod (std::fmod (s.x, worldPeriod) + worldPeriod, worldPeriod);
    s.z = std::fmod (std::fmod (s.z, worldPeriod) + worldPeriod, worldPeriod);

    // The camera's height: low-passed ground height with a small look-ahead; never below ground + 1.2.
    const float g0 = terrainHeight (s.x, s.z, 0), g1 = terrainHeight (s.x + fx * 4, s.z + fz * 4, 4), g2 = terrainHeight (s.x + fx * 9, s.z + fz * 9, 9);
    const double targetY = std::max ({ g0, g1, g2 }) + camHeight;
    if (s.camY == 0.0) s.camY = targetY;
    s.camY += (targetY - s.camY) * (1 - std::exp (-dt / 0.55));
    s.camY = std::max (s.camY, (double) g0 + 1.2);

    // The pitch: the rover's body follows the ground's slope, slowly (tau 1 s) and clamped, so a climb
    // does not fill the windscreen with snow.
    const float gb = terrainHeight (s.x - fx * 5, s.z - fz * 5, 5), gf = terrainHeight (s.x + fx * 12, s.z + fz * 12, 12);
    const double pitchT = std::clamp (std::atan2 (gf - gb, 17.0) * 0.8, -0.17, 0.17);
    if (dt == 0.0) s.pitch = pitchT; else s.pitch += (pitchT - s.pitch) * (1 - std::exp (-dt / 1.0));
}

} // namespace orbitamp::frostpaw
