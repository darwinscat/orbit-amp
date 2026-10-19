// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PlainsWorld.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::redpaw
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

    /** Craters hashed per 350 m world cell: a bowl and a rim, where the mask allows. */
    float craters (double px, double pz, float mask) noexcept
    {
        constexpr double CRC = 350.0; constexpr int CRM = 1900;
        const double fcx = std::floor (px / CRC), fcz = std::floor (pz / CRC);
        const int cx = ((int) fcx + CRM * 64) % CRM, cz = ((int) fcz + CRM * 64) % CRM;
        if (hcell (cx, cz, 1) > 0.42f || mask < 0.01f) return 0.0f;
        const double ctx = (fcx + 0.25 + 0.5 * hcell (cx, cz, 2)) * CRC, ctz = (fcz + 0.25 + 0.5 * hcell (cx, cz, 3)) * CRC;
        const float k = hcell (cx, cz, 4), R = (float) CRC * (0.045f + 0.16f * k * k);
        const float d = (float) std::hypot (px - ctx, pz - ctz) / R;
        if (d > 1.6f) return 0.0f;
        const float bowl = d < 1 ? -(1 - d * d) : 0.0f, rim = std::exp (-std::pow ((d - 1) / 0.22f, 2.0f));
        return mask * R * (0.24f * bowl + 0.065f * rim);
    }

    constexpr double off1[] { 0.37, 0.61 }, off2[] { 0.71, 0.13 };   // the layers' fixed texture offsets
}

PlainsWorld::PlainsWorld (int g) noexcept : gears (std::clamp (g, 1, maxGears)) {}

//==============================================================================
void PlainsWorld::build()
{
    if (built)
        return;
    const int n = texSize;
    const size_t nn = (size_t) n * (size_t) n;
    auto R = blurAsymmetric (plasma (n, 1101, 0.56f), n, 2); normalize01 (R);          // mountains / regions
    auto G = blurAsymmetric (plasma (n, 2202, 0.50f), n, 1); normalize01 (G);          // canyon contour, hills
    auto Bp = plasma (n, 3303, 0.60f);
    for (auto& v : Bp) v = 1 - std::abs (2 * v - 1);                                    // ridged
    normalize01 (Bp);
    auto B = blurAsymmetric (std::move (Bp), n, 1); normalize01 (B);
    auto A = blurAsymmetric (plasma (n, 4404, 0.42f), n, 2); normalize01 (A);          // smooth swell
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

    // The detail: grit and ripples near the rover, a gradient in RG and the value in B.
    {
        const int d = detailSize, m = d - 1;
        auto v = plasma (d, 5505, 0.62f); normalize01 (v);
        detailPx.resize ((size_t) d * (size_t) d * 4);
        for (int y = 0; y < d; ++y)
            for (int x = 0; x < d; ++x)
            {
                const float gx = (v[(size_t) (y * d + ((x + 1) & m))] - v[(size_t) (y * d + ((x - 1) & m))]) * 4;
                const float gy = (v[(size_t) (((y + 1) & m) * d + x)] - v[(size_t) (((y - 1) & m) * d + x)]) * 4;
                const size_t i = (size_t) (y * d + x) * 4;
                detailPx[i]     = (uint8_t) (int) (128 + std::clamp (gx * 127, -127.0f, 127.0f));
                detailPx[i + 1] = (uint8_t) (int) (128 + std::clamp (gy * 127, -127.0f, 127.0f));
                detailPx[i + 2] = (uint8_t) (int) (v[(size_t) (y * d + x)] * 255);
                detailPx[i + 3] = 255;
            }
    }
    built = true;
    place (s.x, s.z, s.heading, gearWanted, turbo);
}

//==============================================================================
float PlainsWorld::tap (int ch, double u, double v) const noexcept
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

float PlainsWorld::terrainHeight (double px, double pz, float dist) const noexcept
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
    const float mtnMask = smoothstep (0.50f, 0.92f, a[0]);
    const float mtn = 430 * std::pow (mtnMask, 1.25f) * (0.5f + 0.9f * b[2]);
    const float swell = (a[3] - 0.5f) * 90, hills = (b[0] - 0.5f) * 48 * (0.4f + 0.6f * a[1]);
    const float plainM = 1 - mtnMask;
    const float mesaM = smoothstep (0.60f, 0.64f, b[1]) * smoothstep (0.35f, 0.6f, a[2]) * plainM;
    const float mesa = mesaM * 24 * (1 + 0.35f * (c[1] - 0.5f));
    const float ridge = 1 - std::abs (2 * a[1] - 1);
    const float canyon = -smoothstep (0.80f, 0.97f, ridge) * 34 * plainM * smoothstep (0.45f, 0.62f, a[2]);
    const float fade = 1 - smoothstep (800, 2500, dist);
    const float dune = ((c[0] - 0.5f) * 5 + (c[2] - 0.5f) * 1.3f) * fade;
    const float crm = plainM * (1 - mesaM) * smoothstep (0.90f, 0.75f, ridge);
    return swell + mtn + hills + mesa + canyon + dune + craters (px, pz, crm);
}

//==============================================================================
bool PlainsWorld::relicAt (int ci, int cj, Relic& out) const noexcept
{
    constexpr int RM = (int) (worldPeriod / relicCell);
    const int cx = ((ci % RM) + RM) % RM, cz = ((cj % RM) + RM) % RM;
    if (hcell (cx, cz, 50) > relicProbability)
        return false;
    out.x = (ci + 0.2 + 0.6 * hcell (cx, cz, 51)) * relicCell;
    out.z = (cj + 0.2 + 0.6 * hcell (cx, cz, 52)) * relicCell;
    // The ground's say: nothing on a mountain, a mesa, or in a canyon — the terms of the height function.
    if (built)
    {
        const float ar = tap (0, out.x / 7000, out.z / 7000), ag = tap (1, out.x / 7000, out.z / 7000), ab = tap (2, out.x / 7000, out.z / 7000);
        const float bg = tap (1, out.x / 1250 + off1[0], out.z / 1250 + off1[1]);
        const float mtnMask = smoothstep (0.50f, 0.92f, ar), ridge = 1 - std::abs (2 * ag - 1);
        const float mesaM = smoothstep (0.60f, 0.64f, bg) * smoothstep (0.35f, 0.6f, ab) * (1 - mtnMask);
        const float canyonM = smoothstep (0.80f, 0.97f, ridge) * (1 - mtnMask) * smoothstep (0.45f, 0.62f, ab);
        if (mtnMask > 0.02f || mesaM > 0.02f || canyonM > 0.02f)
            return false;
    }
    const float r = hcell (cx, cz, 53);
    out.variant = r < 0.35f ? 0 : r < 0.55f ? 1 : r < 0.75f ? 2 : 3;
    constexpr float radii[numRelics] { 1.7f, 0.5f, 3.2f, 1.4f };
    out.radius = radii[out.variant];
    out.ci = ci; out.cj = cj;
    return true;
}

void PlainsWorld::relicsNear (double x, double z, int cells, std::vector<Relic>& out) const
{
    out.clear();
    const int ci = (int) std::floor (x / relicCell), cj = (int) std::floor (z / relicCell);
    Relic r;
    for (int j = cj - cells; j <= cj + cells; ++j)
        for (int i = ci - cells; i <= ci + cells; ++i)
            if (relicAt (i, j, r))
                out.push_back (r);
}

void PlainsWorld::relicsInWindow (int bx, int bz, std::array<std::vector<float>, numRelics>& cells) const
{
    for (auto& v : cells) v.clear();
    Relic r;
    for (int j = 0; j < relicWindow; ++j)
        for (int i = 0; i < relicWindow; ++i)
            if (relicAt (bx + i, bz + j, r))
            {
                cells[(size_t) r.variant].push_back ((float) (bx + i));
                cells[(size_t) r.variant].push_back ((float) (bz + j));
            }
}

/** A relic ahead: a yaw away from any the rover would pass within its footprint + 8 m in the next
    ~110 m, growing as it nears; up to 50 degrees a second. Nothing near, nothing added. */
double PlainsWorld::avoidRelics (double fx, double fz)
{
    const double rx = -fz, rz = fx;
    double yaw = 0;
    relicsNear (s.x, s.z, 1, near);
    for (const auto& r : near)
    {
        const double dx = r.x - s.x, dz = r.z - s.z, along = dx * fx + dz * fz, lat = dx * rx + dz * rz;
        if (along < -2 || along > 110) continue;
        const double clear = r.radius + 8;
        if (std::abs (lat) > clear + 6) continue;
        const double urgency = 1 - std::max (0.0, along) / 110;
        const double side = lat >= 0 ? -1 : 1;                                   // the relic on the right: turn left
        yaw += side * urgency * (0.4 + 0.6 * (1 - std::min (1.0, std::abs (lat) / (clear + 6)))) * 45 * pi / 180;
    }
    return std::clamp (yaw * 180 / pi, -50.0, 50.0) * pi / 180;
}

//==============================================================================
void PlainsWorld::setGear (int g) noexcept   { gearWanted = std::clamp (g, 1, gears); }
void PlainsWorld::setSteer (float v) noexcept { steer = std::clamp (v, -1.0f, 1.0f); }

float PlainsWorld::speedFor (int g, bool t) const noexcept
{
    return gearSpeed[(size_t) std::clamp (g, 1, gears) - 1] * (t ? turboFactor : 1.0f);
}

float PlainsWorld::dustFor (int g, bool t) const noexcept
{
    return t ? turboDust[(size_t) (maxGears - gears + std::clamp (g, 1, gears) - 1)] : 0.0f;    // the top gear the storm
}

void PlainsWorld::place (double x, double z, double heading, int g, bool t)
{
    gearWanted = std::clamp (g, 1, gears);
    turbo = t;
    s.x = x; s.z = z; s.heading = heading;
    s.speed = speedFor (gearWanted, turbo);
    s.storm = dustFor (gearWanted, turbo);
    yawRate = 0.0;
    s.camY = 0.0; s.pitch = 0.0;
    step (0.0);
}

void PlainsWorld::settle() noexcept
{
    s.speed = parked ? 0.0f : speedFor (gearWanted, turbo);
    s.storm = parked ? 0.0f : dustFor (gearWanted, turbo);
}

void PlainsWorld::step (double dt)
{
    s.time += dt;
    const float target = parked ? 0.0f : speedFor (gearWanted, turbo);
    const float kS = (float) (1 - std::exp (-dt / (parked ? parkSeconds : speedSeconds)));
    s.speed += (target - s.speed) * kS;
    s.storm += ((parked ? 0.0f : dustFor (gearWanted, turbo)) - s.storm) * (float) (1 - std::exp (-dt / dustEaseSeconds));

    // Yaw: the player's hand wins, with the push away from a relic ahead added so one steered at is
    // still passed around; a gentle meander resumes after 1.5 s of no input, plus a lean away from
    // higher ground 300 m ahead (two height samples, 35 degrees either side). Parked, the hand turns
    // the view in place and nothing meanders.
    double yawTarget;
    if (parked)
    {
        yawTarget = steer * parkedYawDegrees * pi / 180;
        lastSteerTime = s.time;
    }
    else
    {
        const double fx0 = std::sin (s.heading), fz0 = -std::cos (s.heading);
        const double push = avoidRelics (fx0, fz0);
        if (std::abs (steer) > 0.001f)
        {
            yawTarget = steer * 25 * pi / 180 + push;
            lastSteerTime = s.time;
        }
        else
        {
            const float fadeIn = smoothstep (1.5f, 4.5f, (float) (s.time - lastSteerTime));
            const double ca = std::cos (0.61), sa = std::sin (0.61);
            const float hR = terrainHeight (s.x + 300 * (fx0 * ca - fz0 * sa), s.z + 300 * (fz0 * ca + fx0 * sa), 300);
            const float hL = terrainHeight (s.x + 300 * (fx0 * ca + fz0 * sa), s.z + 300 * (fz0 * ca - fx0 * sa), 300);
            const double avoid = std::clamp ((hL - hR) * 0.08, -6.0, 6.0) * pi / 180;    // left higher: turn right
            yawTarget = fadeIn * (4.5 * pi / 180 * std::sin (s.time * 0.11 + meanderPhase) + avoid) + push;
        }
    }
    yawRate += (yawTarget - yawRate) * (1 - std::exp (-dt / 0.35));
    s.heading += yawRate * dt;

    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);
    s.x += fx * s.speed * dt; s.z += fz * s.speed * dt; s.travel += s.speed * dt;

    // The hard rule: never inside a relic's footprint — a slide along its edge, never through.
    relicsNear (s.x, s.z, 1, near);
    for (const auto& r : near)
    {
        const double dx = s.x - r.x, dz = s.z - r.z, d = std::hypot (dx, dz), rr = r.radius + 1.5;
        if (d < rr && d > 1e-6) { s.x = r.x + dx / d * rr; s.z = r.z + dz / d * rr; }
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
    // does not fill the windscreen with dirt.
    const float gb = terrainHeight (s.x - fx * 5, s.z - fz * 5, 5), gf = terrainHeight (s.x + fx * 12, s.z + fz * 12, 12);
    const double pitchT = std::clamp (std::atan2 (gf - gb, 17.0) * 0.8, -0.17, 0.17);
    if (dt == 0.0) s.pitch = pitchT; else s.pitch += (pitchT - s.pitch) * (1 - std::exp (-dt / 1.0));
}

} // namespace orbitamp::redpaw
