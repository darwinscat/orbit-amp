// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Mars1993.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::redpaw
{

namespace
{
    // The 0x300 bytes at ds:020a the program sent to the VGA DAC, 6 bits a channel. The first 96 entries
    // are the picture's; entry 96 is the white of the "TJC" initials. Entry 4 really is 03 01 00.
    constexpr uint8_t pal6[] = {
        0,0,0, 1,0,0, 2,0,0, 3,0,0, 3,1,0, 4,0,0, 5,0,0, 6,0,0, 7,0,0, 8,0,0, 9,0,0, 10,0,0, 11,0,0, 12,0,0,
        13,0,0, 14,0,0, 15,0,0, 16,0,0, 17,0,0, 18,0,0, 19,0,0, 20,0,0, 21,0,0, 22,0,0, 23,0,0, 24,0,0, 25,0,0,
        26,0,0, 27,0,0, 28,0,0, 29,1,0, 30,2,0, 31,3,0, 32,4,0, 33,5,0, 34,6,0, 35,7,1, 36,8,2, 37,9,3, 38,10,4,
        39,11,5, 40,12,6, 41,13,7, 42,14,8, 43,15,9, 44,16,10, 45,17,11, 46,18,12, 47,19,13, 48,20,14, 49,21,15,
        50,22,16, 51,23,17, 52,24,18, 53,25,19, 54,26,20, 55,27,21, 56,28,22, 57,29,23, 58,30,24, 59,31,25,
        60,32,26, 61,33,27, 62,34,28,
        // 64..95: the sky
        50,10,10, 50,11,11, 50,12,12, 50,13,13, 50,14,14, 50,15,15, 50,16,16, 50,17,17, 50,18,18, 50,19,19,
        50,20,20, 50,21,21, 50,22,22, 50,23,23, 50,24,24, 50,25,25, 50,26,26, 50,27,27, 50,28,28, 50,29,29,
        50,30,30, 50,31,31, 50,32,32, 50,33,33, 50,34,34, 50,35,35, 50,36,36, 50,37,37, 50,38,38, 50,39,39,
        50,40,40, 50,41,41,
        63,63,63
    };

    // ds:0190: the map-row distances of the 60 constant-v scan lines; index 60 is the farthest, drawn first.
    constexpr int distances[] = {
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
        33,34,35,36,37,38,39,40,42,44,46,48,50,52,54,56,58,60,64,68,72,76,80,84,88,92,96,100 };

    // The random generator (0105..0116, and inside the fractal): s = (s * 0xAB + 0x2BCD) mod 0xCF85.
    constexpr uint32_t lcgMul = 0xAB, lcgAdd = 0x2BCD, lcgMod = 0xCF85, lcgMid = 0x67C2;
    inline uint32_t lcg (uint32_t s) noexcept { return (s * lcgMul + lcgAdd) % lcgMod; }

    constexpr double aspect = 5.3451;    // the windscreen box, 1440.5 x 269.5

    /** 0219: the recursive-subdivision fractal, depth first, on a 256 x 256 map that wraps through the
        8-bit halves of its address; 0xFF marks a point not yet written. The four edge midpoints are
        written only if unwritten, the centre always. */
    struct Plasma
    {
        uint8_t* map;
        uint32_t state;

        uint8_t value (int avg, int half) noexcept
        {
            state = lcg (state);
            // Noise proportional to the edge, kept as a SIGNED BYTE (cbw), added to the average and clamped
            // to 0..0xFE. On the two largest levels the byte overflows and wraps — so did the original.
            const int32_t prod = half * ((int32_t) state - (int32_t) lcgMid);
            const int n = (int8_t) (uint8_t) (prod >> 13);
            const int v = avg + n;
            return (uint8_t) (v < 0 ? 0 : (v > 0xFE ? 0xFE : v));
        }

        void square (int u, int v, int size) noexcept
        {
            const int hf = size >> 1;
            const int u2 = (u + size) & 255, v2 = (v + size) & 255, uh = (u + hf) & 255, vh = (v + hf) & 255;
            const int A = (v << 8) | u, B = (v << 8) | u2, C = (v2 << 8) | u2, D = (v2 << 8) | u;
            int m;
            m = (v  << 8) | uh; if (map[m] == 0xFF) map[m] = value ((map[A] + map[B]) >> 1, hf);   // top
            m = (vh << 8) | u2; if (map[m] == 0xFF) map[m] = value ((map[B] + map[C]) >> 1, hf);   // right
            m = (v2 << 8) | uh; if (map[m] == 0xFF) map[m] = value ((map[C] + map[D]) >> 1, hf);   // bottom
            m = (vh << 8) | u;  if (map[m] == 0xFF) map[m] = value ((map[D] + map[A]) >> 1, hf);   // left
            map[(vh << 8) | uh] = value ((map[A] + map[B] + map[C] + map[D]) >> 2, hf);           // centre
            if (hf == 1)
                return;
            square (u, v, hf); square (uh, v, hf); square (uh, vh, hf); square (u, vh, hf);
        }
    };
}

void Mars1993::makeMaps (uint16_t seed, Look lookWanted)
{
    look = lookWanted;
    for (int i = 0; i < 97; ++i)
    {
        const auto widen = [] (uint32_t c) { return (c << 2) | (c >> 4); };    // 6-bit DAC to 8
        pal[(size_t) i] = 0xFF000000u | (widen (pal6[i * 3]) << 16) | (widen (pal6[i * 3 + 1]) << 8) | widen (pal6[i * 3 + 2]);
    }
    for (int i = 97; i < 256; ++i)
        pal[(size_t) i] = 0xFF000000u;
    horizonIndex = 0x50;
    if (look == Look::ice)
        makeIcePalette();

    // 00f3: the sky. Four fixed seeds give it its large-scale pattern; its heights become palette 64..95.
    const uint32_t s1 = lcg (seed & 0x7FFF);
    sky.fill (0xFF);
    sky[0x0000] = 0x00; sky[0x0080] = 0xFE; sky[0x8000] = 0xFE; sky[0x8080] = 0x00;
    Plasma { sky.data(), s1 }.square (0, 0, 256);
    for (auto& v : sky)
        v = (uint8_t) ((v >> 3) + 0x40);

    // 0153: the terrain.
    const uint32_t s2 = lcg (s1);
    heights.fill (0xFF);
    heights[0] = 0x80;
    Plasma { heights.data(), s2 }.square (0, 0, 256);
    auto& hm = heights;
    // Blur 1, in place over the 16-bit linear address (u + k carries into v, as there):
    // w = (w + w[+4] + w[+0202h] + w[-0101h]) / 4.
    for (uint32_t i = 0; i < 65536; ++i)
        hm[i] = (uint8_t) ((hm[i] + hm[(uint16_t) (i + 0x0004)] + hm[(uint16_t) (i + 0x0202)] + hm[(uint16_t) (i + 0xFEFF)]) >> 2);
    // Colour = w(u) - w(u + 3) + 32, clamped to 0..63: the sun on the horizon, low along u.
    for (uint32_t i = 0; i < 65536; ++i)
    {
        const int c = (int) hm[i] - (int) hm[(uint16_t) (i + 3)] + 0x20;
        colours[i] = (uint8_t) (c < 0 ? 0 : (c > 0x3F ? 0x3F : c));
    }
    // Blur 2, in place: the 2 x 2 box on the heights.
    for (uint32_t i = 0; i < 65536; ++i)
        hm[i] = (uint8_t) ((hm[i] + hm[(uint16_t) (i + 1)] + hm[(uint16_t) (i + 0x100)] + hm[(uint16_t) (i + 0x101)]) >> 2);
}

/** THE ICE MOON'S DAC. The ground's shade is w(u) - w(u + 3) + 32 — the sun low along u — so the 64
    entries run from a blue shadow through lit ice to white snow; the sky's 32 are black; 97 is the mist
    on the horizon. From 98, Jupiter: 24 latitudes of the ice world's banding, each lit, at the
    terminator, and on the night side, then the Spot's three. */
void Mars1993::makeIcePalette()
{
    struct C { float r, g, b; };
    const auto mix = [] (C a, C b, float t) { return C { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; };
    const auto ss = [] (float e0, float e1, float x) { const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
    const auto put = [this] (int i, C c)
    {
        const auto q = [] (float v) { return (uint32_t) std::lround (std::clamp (v, 0.0f, 1.0f) * 255); };
        pal[(size_t) i] = 0xFF000000u | (q (c.r) << 16) | (q (c.g) << 8) | q (c.b);
    };
    const C shadow { 0.15f, 0.23f, 0.40f }, lit { 0.70f, 0.77f, 0.87f }, snow { 1.0f, 1.0f, 1.0f };
    for (int i = 0; i < 64; ++i)
        put (i, i < 32 ? mix (shadow, lit, (float) i / 32.0f) : mix (lit, snow, (float) (i - 32) / 31.0f));
    for (int i = 64; i < 96; ++i)
        put (i, { 0, 0, 0 });
    put (97, { 0.59f, 0.67f, 0.80f });
    horizonIndex = 97;

    // Jupiter's bands by latitude, as the ice world's sky shades them, without the festoons.
    const C zone { 0.95f, 0.90f, 0.80f }, belt { 0.56f, 0.36f, 0.24f };
    const float light[3] { 1.25f + 0.045f, 0.5f * 1.25f + 0.045f, 0.045f };
    for (int row = 0; row < 24; ++row)
    {
        const float la = (((float) row + 0.5f) / 24.0f - 0.5f) * 3.14159265f;
        const float band = 0.5f + 0.5f * std::sin (la * 14 + 0.6f), band2 = 0.5f + 0.5f * std::sin (la * 31 + 1.7f);
        float bm = ss (0.2f, 0.8f, band) * 0.7f + 0.3f * band2;
        bm += (0.85f - bm) * ss (0.08f, 0.0f, std::abs (la));
        C c = mix (belt, zone, bm);
        c = mix (c, { 0.74f, 0.62f, 0.50f }, 0.5f * ss (0.35f, 0.45f, std::abs (la + 0.12f)) * (1 - ss (0.45f, 0.6f, std::abs (la + 0.12f))));
        c = mix (c, { 0.78f, 0.70f, 0.58f }, ss (0.9f, 1.3f, std::abs (la)));
        c = mix (c, { 0.62f, 0.60f, 0.68f }, ss (1.15f, 1.45f, std::abs (la)));
        for (int l = 0; l < 3; ++l)
            put (98 + row * 3 + l, { c.r * light[l], c.g * light[l], c.b * light[l] });
    }
    for (int l = 0; l < 3; ++l)
        put (170 + l, { 0.80f * light[l], 0.48f * light[l], 0.36f * light[l] });
}

void Mars1993::setRows (int rows)
{
    rows = std::max (2, rows & ~1);
    h = rows;
    w = 2 * (int) std::lround (rows * aspect / 2.0);
    P = rows / 200.0;
    G = rows / 2;
    const int line = std::max (1, (int) std::lround (P));      // the horizon line, row 99 of the original
    skyRows = G - line;
    hazeRows = std::min (h - G, (int) std::lround (40 * P));
    frame.assign ((size_t) (w * h), 0);
    yBuf.assign ((size_t) w, 0);
    cBuf.assign ((size_t) w, 0);
}

// 105c + 043b: bilinear ground height under the 12.4 position, 8.8 result, + 1900h (25 units up),
// saturating at FFFFh.
uint16_t Mars1993::altitudeAt (uint16_t camX, uint16_t camY) const noexcept
{
    const int u = (camX >> 4) & 0xFF, fu = camX & 15, v = (camY >> 4) & 0xFF, fv = camY & 15;
    const int u1 = (u + 1) & 0xFF, v1 = (v + 1) & 0xFF;               // inc bl / inc bh: 8-bit wraps
    const int a0 = heights[(size_t) ((v << 8) | u)],  a1 = heights[(size_t) ((v << 8) | u1)];
    const int b0 = heights[(size_t) ((v1 << 8) | u)], b1 = heights[(size_t) ((v1 << 8) | u1)];
    const int top = (a1 - a0) * fu + (a0 << 4);
    const int bot = (b1 - b0) * fu + (b0 << 4);
    const uint32_t hh = (uint32_t) ((bot - top) * fv + (top << 4)) & 0xFFFFu;
    const uint32_t alt = hh + 0x1900u;
    return (uint16_t) (alt > 0xFFFFu ? 0xFFFFu : alt);
}

void Mars1993::render (uint16_t camX, uint16_t camY)
{
    if (w == 0)
        return;
    const uint16_t alt = altitudeAt (camX, camY);
    drawSky (alt, camX, camY);
    if (look == Look::ice)
        drawJupiter();
    drawGround (alt, camX, camY);
}

/** A FLAT JUPITER in the black sky, where the ice world's sky has it: 27 degrees right of straight
    ahead, 4 up, 9 across to its edge — on this screen, whose horizon is the middle row, the same
    fractions of the glass as the GPU's picture gives them. Bands by latitude from the palette,
    lit by the same sun through a terminator in three steps, the Spot in its place; the sky rows
    only, the ground is drawn over it. No festoons, no limb, no edge: it is 1993. */
void Mars1993::drawJupiter()
{
    const double cx = 0.7063 * w, cy = G - 0.152 * h, r = 0.343 * h;
    const int y0 = std::max (0, (int) std::floor (cy - r)), y1 = std::min (skyRows - 1, (int) std::ceil (cy + r));
    // The light: n = u R + v U - z J against the sun, with J, R, U the ice world's sky frame.
    const auto dir = [] (double azDeg, double elDeg) { const double az = azDeg * 3.14159265358979 / 180, el = elDeg * 3.14159265358979 / 180; return std::array<double, 3> { std::sin (az) * std::cos (el), std::sin (el), -std::cos (az) * std::cos (el) }; };
    const auto cross = [] (std::array<double, 3> a, std::array<double, 3> b) { return std::array<double, 3> { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; };
    const auto norm = [] (std::array<double, 3> a) { const double l = std::sqrt (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); return std::array<double, 3> { a[0] / l, a[1] / l, a[2] / l }; };
    const auto dot = [] (std::array<double, 3> a, std::array<double, 3> b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto sun = dir (-118, 16), J = dir (27, 4);
    const auto R = norm (cross ({ 0, 1, 0 }, J)), U = norm (cross (J, R));
    const double sR = dot (R, sun), sU = dot (U, sun), sJ = dot (J, sun);
    uint8_t* fb = frame.data();
    for (int y = y0; y <= y1; ++y)
    {
        const double v = (cy - y) / r;
        const double half = std::sqrt (std::max (0.0, 1.0 - v * v)) * r;
        const int x0 = std::max (0, (int) std::ceil (cx - half)), x1 = std::min (w - 1, (int) std::floor (cx + half));
        const double lat = std::asin (std::clamp (v, -1.0, 1.0));
        const int row = std::clamp ((int) ((lat / 3.14159265358979 + 0.5) * 24), 0, 23);
        for (int x = x0; x <= x1; ++x)
        {
            const double u = (x - cx) / r, z = std::sqrt (std::max (0.0, 1.0 - u * u - v * v));
            const double l = u * sR + v * sU - z * sJ;
            const int light = l > 0.25 ? 0 : (l > 0.0 ? 1 : 2);
            const double lon = std::asin (std::clamp (u / std::max (std::cos (lat), 1e-6), -1.0, 1.0));
            const double e = (lon - 0.35) * (lon - 0.35) / (0.21 * 0.21) + (lat - 0.36) * (lat - 0.36) / (0.085 * 0.085);
            fb[(size_t) y * (size_t) w + (size_t) x] = (uint8_t) (e < 0.8 ? 170 + light : 98 + row * 3 + light);
        }
    }
}

// 0459: the sky. Row r above the horizon looks at a plane at distance K / r: rows of constant v, u
// spanning +-D across the original's 256 columns. The address pair si:bx is 32-bit and linear, so u
// carries into v as it wraps — the sky shears near the horizon, as it did.
void Mars1993::drawSky (uint16_t alt, uint16_t camX, uint16_t camY)
{
    uint8_t* fb = frame.data();
    const int64_t K = (int64_t) ((((uint32_t) (-(int32_t) alt) & 0xFFFFu) >> 3) + 0x4000u) * 8192;
    const int64_t X9 = (int64_t) camX * 512, Y9 = (int64_t) camY * 512;
    const int lead = (int) std::lround (w / 2.0 - 128 * P);       // columns left of the original's window
    size_t p = 0;
    for (int y = 0; y < skyRows; ++y)
    {
        const int64_t D = (int64_t) std::floor ((double) K / ((G - y) / P - 1.0));   // K / (99 - y) at P = 1
        const uint32_t esi = (uint32_t) (X9 - D), ebp = (uint32_t) (Y9 + D);
        const uint32_t step = (uint32_t) (int64_t) std::floor ((double) D / (128 * P));  // shr eax, 7
        uint32_t L = (((ebp >> 16) & 0xFFu) << 24) | (((esi >> 16) & 0xFFu) << 16) | (esi & 0xFFFFu);
        L -= (uint32_t) lead * step;
        for (int x = 0; x < w; ++x) { fb[p++] = sky[L >> 16]; L += step; }
    }
    // The horizon line, colour 50h.
    const size_t horizonEnd = (size_t) G * (size_t) w;
    std::fill (fb + p, fb + horizonEnd, horizonIndex);
    p = horizonEnd;
    // "Blank off ~50 lines below the horizon": a ramp by the altitude (09e9..0a1c).
    const uint32_t num = (alt >> 1) + 10;
    for (int y = 0; y < hazeRows; ++y)
    {
        const uint32_t bx = 4 + (uint32_t) std::floor (y / P);
        uint32_t c = (num / bx) >> 7;
        if (c > 63) c = 63;
        std::fill (fb + p, fb + p + (size_t) w, (uint8_t) c);
        p += (size_t) w;
    }
}

// 0a1f: the ground, rows of constant v from far (d = 100) to near (d = 1), each scanned left to right.
// A column's vertical span from the previous row's y down to this row's y is filled, Gouraud from the
// old colour to the new — and only when it goes down.
void Mars1993::drawGround (uint16_t alt, uint16_t camX, uint16_t camY)
{
    uint8_t* fb = frame.data();
    int32_t* ybuf = yBuf.data();
    int32_t* cbuf = cBuf.data();
    std::fill (yBuf.begin(), yBuf.end(), 32000);
    std::fill (cBuf.begin(), cBuf.end(), 0);
    const int fy = camY & 15, vCam = (camY >> 4) & 0xFF;
    const int64_t X12 = (int64_t) camX * 4096;
    const int halfW = w >> 1;
    for (int k = 60; k >= 1; --k)
    {
        const int D = (distances[k] << 4) + (fy ^ 15);              // distance in 1/16 texel
        int y0; uint32_t kq;
        if (k == 1) { y0 = 32000; kq = 0; }                          // the nearest row is pinned to the bottom
        else { y0 = G + (int) std::floor (alt * P / D); kq = (uint32_t) std::floor (65536 * P / D); }
        const uint32_t step = (uint32_t) (int64_t) std::floor (D * 64 / P);   // u step per column, 16.16
        const uint32_t ecx = (uint32_t) (X12 - (int64_t) halfW * (int64_t) step);
        uint32_t bl = (ecx >> 16) & 0xFFu;
        uint32_t frac = (ecx & 0xFFFFu) >> 1;                        // the original keeps 15 fraction bits
        const uint32_t rowBase = (uint32_t) (((vCam + (D >> 4)) & 0xFF) << 8);
        const uint32_t stepLo = step & 0xFFFFu, stepHi = (step >> 16) & 0xFFu;
        for (int x = 0; x < w; ++x)
        {
            const uint32_t f = (frac << 1) + stepLo;                 // shl cx,1 ; add cx ; adc bl ; shr cx,1
            bl = (bl + stepHi + (f >> 16)) & 0xFFu;
            frac = (f & 0xFFFFu) >> 1;
            const uint32_t idx = rowBase | bl, idx1 = (idx + 1) & 0xFFFFu;
            const int h0 = heights[idx];
            const uint32_t hh = (uint32_t) (((((int) heights[idx1] - h0) * (int) frac) >> 7) + (h0 << 8)) & 0xFFFFu;   // 8.8
            int yn = y0 - (int) ((hh * kq) >> 16);
            if (yn < 0) yn = -1; else if (yn >= h) yn = h - 1;
            const int yo = ybuf[x]; ybuf[x] = yn;
            const int c0 = colours[idx];
            const int col = (int) ((uint32_t) (((((int) colours[idx1] - c0) * (int) (frac >> 8)) << 1) + (c0 << 8)) & 0xFFFFu);   // 8.8
            if (yo >= yn) { cbuf[x] = col; continue; }
            const int old = cbuf[x]; cbuf[x] = col;
            const int n = yn - yo;
            const int slope = (old - col) / -n;                       // cwd ; idiv: truncates toward zero
            uint32_t c = (uint32_t) old;
            size_t q = (size_t) (yo + 1) * (size_t) w + (size_t) x;
            for (int i = 0; i < n; ++i) { fb[q] = (uint8_t) (c >> 8); c = (c + (uint32_t) slope) & 0xFFFFu; q += (size_t) w; }
        }
    }
}

} // namespace orbitamp::redpaw
