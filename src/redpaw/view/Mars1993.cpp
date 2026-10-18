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
    constexpr double pi = 3.14159265358979;

    // The discs' dither: a 4 x 4 ordered matrix, its thresholds in sixteenths.
    constexpr uint8_t bayer[16] { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    inline double dither (int x, int y) noexcept { return (bayer[((y & 3) << 2) | (x & 3)] + 0.5) / 16.0; }

    /** 0..1 to one of `levels` steps, the pixel's threshold choosing between the two nearest. */
    inline int step (double f, int levels, double threshold) noexcept
    {
        const double s = std::clamp (f, 0.0, 1.0) * (levels - 1);
        const int i = (int) s;
        return std::min (levels - 1, i + (s - i > threshold ? 1 : 0));
    }

    inline double smoothstep (double e0, double e1, double x) noexcept
    {
        const double t = std::clamp ((x - e0) / (e1 - e0), 0.0, 1.0);
        return t * t * (3 - 2 * t);
    }

    // Saturn's darkest: its night side at the limb, the light's step 0.
    constexpr double satDark = 0.08 * 0.55;

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
    else if (look == Look::titan)
        makeTitanPalette();

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
    if (look == Look::titan)
        makeSea();
    else
    {
        // The ice moon's snow lies flatter: its heights at a quarter, its shading at a half. Mars's
        // ground a little lower: at two thirds, its shading as strong as it was.
        const bool ice = look == Look::ice;
        for (auto& v : hm)
            v = (uint8_t) (0x80 + ((int) v - 0x80) * (ice ? 1 : 2) / (ice ? 4 : 3));
        // Colour = w(u) - w(u + 3) + 32, clamped to 0..63: the sun on the horizon, low along u.
        for (uint32_t i = 0; i < 65536; ++i)
        {
            const int slope = (int) hm[i] - (int) hm[(uint16_t) (i + 3)];
            const int c = (ice ? slope * 2 : slope * 3 / 2) + 0x20;
            colours[i] = (uint8_t) (c < 0 ? 0 : (c > 0x3F ? 0x3F : c));
        }
    }
    // Blur 2, in place: the 2 x 2 box on the heights.
    for (uint32_t i = 0; i < 65536; ++i)
        hm[i] = (uint8_t) ((hm[i] + hm[(uint16_t) (i + 1)] + hm[(uint16_t) (i + 0x100)] + hm[(uint16_t) (i + 0x101)]) >> 2);
}

/** THE ICE MOON'S DAC. The ground's shade is w(u) - w(u + 3) + 32 — the sun low along u — so the 64
    entries run from a blue shadow through lit ice to white snow; the sky's 32 are black; 97 is the mist
    on the horizon. From 98, Jupiter: 24 latitudes of the ice world's banding, each in six steps of
    light from the night side to the full sun, then the Spot's six. */
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
    const auto light = [] (int l) { return 1.25f * (float) l / (float) (jupLevels - 1) + 0.045f; };
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
        for (int l = 0; l < jupLevels; ++l)
            put (jupShades + row * jupLevels + l, { c.r * light (l), c.g * light (l), c.b * light (l) });
    }
    for (int l = 0; l < jupLevels; ++l)
        put (jupSpot + l, { 0.80f * light (l), 0.48f * light (l), 0.36f * light (l) });
}

/** TITAN'S DAC. The sea's 64 shades from 98, a dark trough through the amber-brown liquid to the haze's
    sheen on a wave; the islands' 32 from 162, the lit shore to a violet-brown shadow; and the first 64,
    which only the haze rows under the horizon take, the far sea fading into the pale violet mist. The
    sky's 32 are the haze, a darker brown to the amber of the horizon; 97 is the mist on the horizon line.
    From 194, Saturn: 12 latitudes of its bands, each in four steps of light, then its rings' three — all
    reddened by the haze and mixed, in light, into its middle, a little more of the haze than the GPU's
    picture shows where the haze is thinnest, this sky being the darker. */
void Mars1993::makeTitanPalette()
{
    struct C { float r, g, b; };
    const auto mix = [] (C a, C b, float t) { return C { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; };
    const auto ss = [] (float e0, float e1, float x) { const float t = std::clamp ((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3 - 2 * t); };
    const auto put = [this] (int i, C c)
    {
        const auto q = [] (float v) { return (uint32_t) std::lround (std::clamp (v, 0.0f, 1.0f) * 255); };
        pal[(size_t) i] = 0xFF000000u | (q (c.r) << 16) | (q (c.g) << 8) | q (c.b);
    };
    const C trough { 0.12f, 0.08f, 0.05f }, liquid { 0.42f, 0.28f, 0.14f }, sheen { 0.60f, 0.42f, 0.23f };
    const auto sea = [&] (int i) { return i < 32 ? mix (trough, liquid, (float) i / 32.0f) : mix (liquid, sheen, (float) (i - 32) / 31.0f); };
    for (int i = 0; i < 64; ++i)
        put (seaShades + i, sea (i));
    const C shore { 0.12f, 0.08f, 0.07f }, lit { 0.50f, 0.35f, 0.20f };
    for (int i = 0; i < 32; ++i)
        put (landShades + 31 - i, mix (shore, lit, (float) i / 31.0f));
    const C mist { 0.60f, 0.50f, 0.52f };
    for (int i = 0; i < 64; ++i)
        put (i, mix (sea (farSea), mist, ss (18.0f, 40.0f, (float) i)));
    const C hazeHigh { 0.40f, 0.25f, 0.12f }, hazeLow { 0.81f, 0.54f, 0.25f };
    for (int i = 64; i < 96; ++i)
        put (i, mix (hazeHigh, hazeLow, (float) (i - 64) / 31.0f));
    put (97, { 0.65f, 0.55f, 0.70f });
    horizonIndex = 97;

    const auto toLight = [] (C c) { return C { std::pow (c.r, 2.2f), std::pow (c.g, 2.2f), std::pow (c.b, 2.2f) }; };
    const auto toShown = [] (C c) { return C { std::pow (std::max (c.r, 0.0f), 1 / 2.2f), std::pow (std::max (c.g, 0.0f), 1 / 2.2f), std::pow (std::max (c.b, 0.0f), 1 / 2.2f) }; };
    const auto times = [] (C c, float k) { return C { c.r * k, c.g * k, c.b * k }; };
    const C haze = toLight (mix (hazeHigh, hazeLow, 0.5f)), tint { 1.0f, 0.85f, 0.65f };   // the haze's middle; what it lets through, reddened
    const float clear = 0.42f;                                                                 // Saturn's own light that reaches the eye
    for (int row = 0; row < satRows; ++row)
    {
        const float lat = ((float) row + 0.5f) / (float) satRows * 2 - 1;
        const float band = std::clamp (0.5f + 0.17f * std::sin (lat * 8.5f + 0.7f) + 0.08f * std::sin (lat * 21.0f + 2.0f), 0.0f, 1.0f);
        const C pc = mix (mix ({ 0.66f, 0.56f, 0.40f }, { 0.82f, 0.74f, 0.56f }, band), { 0.55f, 0.56f, 0.62f }, ss (0.75f, 0.95f, std::abs (lat)));
        for (int l = 0; l < satLevels; ++l)
        {
            const float b = (float) satDark + (1 - (float) satDark) * (float) l / (float) (satLevels - 1);
            put (satShades + row * satLevels + l, toShown (mix (haze, mix (times ({ pc.r * tint.r, pc.g * tint.g, pc.b * tint.b }, b), haze, 0.2f), clear)));
        }
    }
    for (int l = 0; l < satRingShades; ++l)
        put (satRing + l, toShown (mix (haze, times ({ 0.70f * tint.r, 0.62f * tint.g, 0.48f * tint.b }, 0.75f + 0.1f * (float) l), 0.5f * clear)));
}

/** TITAN'S SEA out of the program's terrain: the highest sixteenth of it stays, at half its height, low
    islands and a far shore rising from the level of the rest, shaded as the program shades its ground.
    The rest is the sea, flat, its texels keeping only a swell — long crests across the way ahead and the
    terrain's own small detail — as a signed code round 96, which the ground's rows turn into a shade by
    their distance. */
void Mars1993::makeSea()
{
    auto& hm = heights;
    std::array<int, 256> count {};
    for (auto v : hm)
        ++count[v];
    int level = 254, above = 0;
    while (level > 0 && above + count[(size_t) level] <= 65536 / 16)
        above += count[(size_t) level--];
    const std::vector<uint8_t> ground (hm.begin(), hm.end());
    for (uint32_t i = 0; i < 65536; ++i)
    {
        const int g = ground[i], up = g - level;
        if (up > 0)
        {
            hm[i] = (uint8_t) (seaLevel + up / 2);
            colours[i] = (uint8_t) std::clamp (g - (int) ground[(uint16_t) (i + 3)] + 16, 0, 31);
        }
        else
        {
            const int u = (int) (i & 0xFF), v = (int) (i >> 8);
            const double swell = 11.0 * std::sin (6.283185307179586 * (37 * v + 3 * u) / 256.0)
                               + 5.0 * std::sin (6.283185307179586 * (61 * v - 11 * u) / 256.0);
            const int s = g - (int) ground[(uint16_t) (i + 0x0200)] + (int) std::lround (swell);
            hm[i] = seaLevel;
            colours[i] = (uint8_t) (seaCode + std::clamp (s, -31, 31));
        }
    }
}

/** A row of Titan's ground at map distance d: the islands' codes to their shades, the sea's to the
    sea's — brighter and its swell full near, darker and calmer toward the haze. */
void Mars1993::seaRow (uint8_t* shade, int d) const noexcept
{
    for (int c = 0; c < 32; ++c)
        shade[c] = (uint8_t) (landShades + 31 - c);
    const double t = d / (d + 20.0);
    const double base = 34 - 20 * t, swell = 1 - 0.85 * t;
    for (int c = 64; c < 128; ++c)
        shade[c] = (uint8_t) (seaShades + std::clamp ((int) std::lround (base + (c - seaCode) * swell), 0, 63));
}

void Mars1993::setRows (int rows, double glass)
{
    rows = std::max (2, rows & ~1);
    h = rows;
    w = 2 * (int) std::lround (rows * aspect / 2.0);
    shown = glass;
    squeeze = glass > 0 ? (double) w / h / glass : 1.0;
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
    else if (look == Look::titan)
        drawSaturn();
    drawGround (alt, camX, camY);
}

/** JUPITER in the black sky, where the ice world's sky has it: 27 degrees right of straight ahead, 4
    up, 9 across to its edge — on this screen, whose horizon is the middle row, the same fractions of
    the glass as the GPU's picture gives them. Bands by latitude from the palette, the Spot in its
    place; a sphere lit as the GPU lights it, by the same sun, its terminator soft and its limb darker,
    in six steps of light dithered between. The sky rows only, the ground is drawn over it. No
    festoons, no edge: it is 1993. */
void Mars1993::drawJupiter()
{
    const double cx = 0.7063 * w, cy = G - 0.152 * h, r = 0.343 * h, rx = r * squeeze;
    const int y0 = std::max (0, (int) std::floor (cy - r)), y1 = std::min (skyRows - 1, (int) std::ceil (cy + r));
    // The light: n = u R + v U - z J against the sun, with J, R, U the ice world's sky frame.
    const auto dir = [] (double azDeg, double elDeg) { const double az = azDeg * pi / 180, el = elDeg * pi / 180; return std::array<double, 3> { std::sin (az) * std::cos (el), std::sin (el), -std::cos (az) * std::cos (el) }; };
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
        const double half = std::sqrt (std::max (0.0, 1.0 - v * v)) * rx;
        const int x0 = std::max (0, (int) std::ceil (cx - half)), x1 = std::min (w - 1, (int) std::floor (cx + half));
        const double lat = std::asin (std::clamp (v, -1.0, 1.0));
        const int row = std::clamp ((int) ((lat / pi + 0.5) * jupRows), 0, jupRows - 1);
        for (int x = x0; x <= x1; ++x)
        {
            const double u = (x - cx) / rx, z = std::sqrt (std::max (0.0, 1.0 - u * u - v * v));
            const double l = u * sR + v * sU - z * sJ;
            const double lit = smoothstep (-0.06, 0.34, l) * (0.9 + 0.1 * std::max (l, 0.0)), limb = 0.55 + 0.45 * z;
            const int light = step (lit * limb, jupLevels, dither (x, y));
            const double lon = std::asin (std::clamp (u / std::max (std::cos (lat), 1e-6), -1.0, 1.0));
            const double e = (lon - 0.35) * (lon - 0.35) / (0.21 * 0.21) + (lat - 0.36) * (lat - 0.36) / (0.085 * 0.085);
            fb[(size_t) y * (size_t) w + (size_t) x] = (uint8_t) (e < 0.8 ? jupSpot + light : jupShades + row * jupLevels + light);
        }
    }
}

/** SATURN in Titan's haze, where the GPU's sky has it: 17 degrees left of straight ahead, 6 up, 2.85
    across to its edge, its rings 13 degrees off level and open by 0.075 of their width — on this
    screen, whose horizon is the middle row, the same fractions of the glass as the GPU's picture gives
    them. The far half of the rings, then the banded disc lit from the right, its terminator soft, its
    limb darker and the rings' shadow across it, in four steps of light; then the near half of the
    rings. Their colours are mixed into the haze (the palette's), their edges
    and the rings' thin line dithered into it. The sky rows only. */
void Mars1993::drawSaturn()
{
    const double ry = 0.06951 * h, rx = ry * squeeze, cx = 0.39328 * w, cy = G - 0.15346 * h;
    const double tc = std::cos (13 * pi / 180), ts = std::sin (13 * pi / 180), open = 0.075;
    // The light, from the right and gibbous in the rings' frame, turned with them.
    const double l0 = 0.62, l1 = 0.22, l2 = 0.75, ll = std::sqrt (l0 * l0 + l1 * l1 + l2 * l2);
    const double Lx = (tc * l0 - ts * l1) / ll, Ly = (ts * l0 + tc * l1) / ll, Lz = l2 / ll;
    const double aa = 1 / ry + 0.07;                       // the haze softens its edge
    const int y0 = std::max (0, (int) std::floor (cy - 1.1 * ry)), y1 = std::min (skyRows - 1, (int) std::ceil (cy + 1.1 * ry));
    const int x0 = std::max (0, (int) std::floor (cx - 2.35 * rx)), x1 = std::min (w - 1, (int) std::ceil (cx + 2.35 * rx));
    uint8_t* fb = frame.data();
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const double qx = (x - cx) / rx, qy = (cy - y) / ry;              // planet radii, y up
            const double ux = tc * qx - ts * qy, uy = ts * qx + tc * qy;      // along the rings' line, across it
            const double er = std::hypot (ux, uy / open);
            double ring = smoothstep (1.22, 1.30, er) * (1 - smoothstep (2.18, 2.27, er));
            ring *= 1 - 0.8 * smoothstep (1.90, 1.93, er) * (1 - smoothstep (1.99, 2.02, er));     // the Cassini division
            const int ringShade = satRing + std::clamp ((int) ((0.15 + 0.15 * std::sin (er * 31)) / 0.3 * satRingShades), 0, satRingShades - 1);
            const double threshold = dither (x, y);
            uint8_t& px = fb[(size_t) y * (size_t) w + (size_t) x];
            if (uy > 0 && ring > threshold)
                px = (uint8_t) ringShade;                                     // the far half, behind the disc
            const double pqx = ux, pqy = uy / 0.9, pr = std::hypot (pqx, pqy);     // the disc, a tenth flatter at the poles
            if (1 - smoothstep (1 - aa, 1 + aa, pr) > threshold)
            {
                const double z = std::sqrt (std::max (1 - pr * pr, 0.0));
                const double lit = smoothstep (-0.15, 0.45, pqx * Lx + pqy * Ly + z * Lz);
                double b = (0.08 + 0.92 * lit) * (0.55 + 0.45 * z);
                b *= 1 - 0.6 * std::exp (-((uy + 0.16) / 0.05) * ((uy + 0.16) / 0.05)) * lit;   // the rings' shadow
                const int row = std::clamp ((int) ((pqy + 1) / 2 * satRows), 0, satRows - 1);
                const int light = step ((b - satDark) / (1 - satDark), satLevels, dither (x + 2, y + 2));
                px = (uint8_t) (satShades + row * satLevels + light);
            }
            if (uy <= 0 && ring > threshold)
                px = (uint8_t) ringShade;                                     // the near half, in front
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
    uint8_t shade[256];                                              // a texel's colour code to its shade: itself, but on Titan
    for (int c = 0; c < 256; ++c)
        shade[c] = (uint8_t) c;
    for (int k = 60; k >= 1; --k)
    {
        if (look == Look::titan)
            seaRow (shade, distances[k]);
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
            const int c0 = shade[colours[idx]];
            const int col = (int) ((uint32_t) (((((int) shade[colours[idx1]] - c0) * (int) (frac >> 8)) << 1) + (c0 << 8)) & 0xFFFFu);   // 8.8
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
