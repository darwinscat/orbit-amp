// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "TitanWorld.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace orbitamp::amberpaw
{

namespace
{
    constexpr double pi = TitanWorld::pi;

    inline double clamp (double x, double a, double b) noexcept { return std::max (a, std::min (b, x)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0.0, 1.0); return t * t * (3 - 2 * t); }
    inline double jsRound (double x) noexcept { return std::floor (x + 0.5); }   // as the prototype's Math.round

    /** The prototype's integer hash of a cycle index: the schedules' lengths and the strikes' shapes. */
    double hashOf (uint32_t k, uint32_t seed) noexcept
    {
        uint32_t x = ((k ^ 0x9E3779B9u) * 0x85EBCA6Bu) ^ seed;
        x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
        return (double) x / 4294967296.0;
    }

    /** A double to the 32 bits the prototype's generators were seeded with, wrapping as ToInt32 does. */
    inline uint32_t toInt32 (double v) noexcept { return (uint32_t) (int64_t) v; }

    /** Periodic gradient noise: a P x P lattice of unit gradients sampled at N x N, added into `out`.
        The lattice and the sum are single precision, as the prototype's typed arrays keep them. */
    template <typename Next>
    void perlinOctave (int N, int P, Next&& rng, std::vector<float>& out, double amp, bool ridged)
    {
        std::vector<float> gx ((size_t) (P * P)), gy ((size_t) (P * P));
        for (int i = 0; i < P * P; ++i) { const double a = rng() * pi * 2; gx[(size_t) i] = (float) std::cos (a); gy[(size_t) i] = (float) std::sin (a); }
        const auto fade = [] (double t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        for (int j = 0; j < N; ++j)
        {
            const double y = (j + 0.5) / N * P, y0 = std::floor (y), fy = y - y0, v = fade (fy);
            const int j0 = (int) y0 % P, j1 = ((int) y0 + 1) % P;
            for (int i = 0; i < N; ++i)
            {
                const double x = (i + 0.5) / N * P, x0 = std::floor (x), fx = x - x0, u = fade (fx);
                const int i0 = (int) x0 % P, i1 = ((int) x0 + 1) % P;
                const size_t k00 = (size_t) (j0 * P + i0), k10 = (size_t) (j0 * P + i1), k01 = (size_t) (j1 * P + i0), k11 = (size_t) (j1 * P + i1);
                const double n00 = gx[k00] * fx + gy[k00] * fy, n10 = gx[k10] * (fx - 1) + gy[k10] * fy;
                const double n01 = gx[k01] * fx + gy[k01] * (fy - 1), n11 = gx[k11] * (fx - 1) + gy[k11] * (fy - 1);
                const double nx0 = n00 + (n10 - n00) * u, nx1 = n01 + (n11 - n01) * u;
                double n = (nx0 + (nx1 - nx0) * v) * 1.414;
                if (ridged) { n = 1 - std::abs (n); n *= n; }
                auto& o = out[(size_t) (j * N + i)];
                o = (float) ((double) o + n * amp);
            }
        }
    }
}

double TitanWorld::Rng::next() noexcept     // mulberry32, the prototype's generator: its stream is the world
{
    a += 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}

TitanWorld::Schedule::At TitanWorld::Schedule::at (double t) const noexcept
{
    if (t < t0) { k = 0; t0 = 0; }
    for (;;)
    {
        const double h = hashOf ((uint32_t) k, seed);
        const double L = base + spread * (power == 1.0 ? h : std::pow (h, power));
        if (t < t0 + L) return { k, t - t0, L };
        t0 += L; ++k;
    }
}

//==============================================================================
std::array<double, 2> TitanWorld::glassST (double nx, double ny) noexcept
{
    const double tv = vfovHalfTan, th = tv * winAspect, sb = std::sin (glassBeta), cb = std::cos (glassBeta);
    const double dx = nx * th, dy = (ny - shear) * tv;
    const double lam = (glassYb * sb - glassZb * cb) / (dy * sb - cb);
    return { lam * dx, (lam * dy - glassYb) * cb - (-lam + glassZb) * sb };
}

std::array<double, 2> TitanWorld::glassToUV (double gs, double gt, double th) noexcept
{
    const double depth = glassZb + gt * std::sin (glassBeta), y = glassYb + gt * std::cos (glassBeta);
    return { 0.5 + 0.5 * gs / (depth * th), 0.5 + 0.5 * (y / (depth * vfovHalfTan) + shear) };
}

std::array<double, 2> TitanWorld::cabinXY (double gs, double gt) noexcept
{
    const auto uv = glassToUV (gs, gt);
    return { uv[0] * cabinWidth, (1 - uv[1]) * cabinHeight };
}

double TitanWorld::glassHalfW (double gt) noexcept { return vfovHalfTan * winAspect * (glassZb + gt * std::sin (glassBeta)); }
double TitanWorld::glassTop() noexcept             { return glassST (0, 1)[1]; }

const std::array<TitanWorld::Wiper, 3>& TitanWorld::wipers() noexcept
{
    // Given in the cabin's pixels: the pivot, a point as far as the blade reaches, where it parks, where
    // it turns, and the pane.
    static const std::array<Wiper, 3> w = []
    {
        const auto at = [] (double x, double y) { return glassST (x / cabinWidth * 2 - 1, 1 - 2 * y / cabinHeight); };
        const auto blade = [&at] (std::array<double, 2> pv, std::array<double, 2> reach, std::array<double, 2> park, std::array<double, 2> end, std::array<double, 4> pane)
        {
            const auto p = at (pv[0], pv[1]), q = at (reach[0], reach[1]);
            const double r = std::hypot (q[0] - p[0], q[1] - p[1]);
            const auto dir = [&] (std::array<double, 2> xy) { const auto g = at (xy[0], xy[1]); return std::atan2 (g[1] - p[1], g[0] - p[0]); };
            return Wiper { { p[0], p[1] }, dir (park), dir (end), r * 0.11 / 0.86, r, { pane[0], pane[1], pane[2], pane[3] } };
        };
        return std::array<Wiper, 3> { blade ({ 836, 400 }, { 836, 66 }, { 1372, 399 }, { 300, 399 }, { 348, 0.4, 1322, -0.4 }),
                                      blade ({ 392, 430 }, { 392, 70 }, { 0, 429 }, { 330, 60 }, { -1e9, 0, 348, 0.4 }),
                                      blade ({ 1280, 430 }, { 1280, 70 }, { 1672, 429 }, { 1342, 60 }, { 1322, -0.4, 1e9, 0 }) };
    }();
    return w;
}

//==============================================================================
TitanWorld::TitanWorld (int g) noexcept : gears (std::clamp (g, 1, maxGears)) {}

void TitanWorld::build()
{
    if (built)
        return;

    // The noise: four channels of periodic gradient noise, octaves summed, normalised to bytes.
    {
        const int N = noiseSize;
        Rng rng { 90210 };
        struct Spec { int base, oct; double gain; bool ridged; };
        const Spec specs[4] { { 3, 6, 0.5, false },     // R: coastlines, slicks
                              { 4, 6, 0.55, false },    // G: mist banks, coast detail, dune fields, film
                              { 4, 5, 0.5, true },      // B: hills, frost streaks
                              { 8, 5, 0.6, false } };   // A: detail, clouds, froth, vapour
        noisePx.assign ((size_t) (N * N * 4), 0);
        for (int ch = 0; ch < 4; ++ch)
        {
            const auto& sp = specs[ch];
            std::vector<float> f ((size_t) (N * N), 0.0f);
            double amp = 1;
            for (int o = 0; o < sp.oct; ++o) { perlinOctave (N, sp.base << o, [&rng] { return rng.next(); }, f, amp, sp.ridged); amp *= sp.gain; }
            const auto [mn, mx] = std::minmax_element (f.begin(), f.end());
            const double lo = *mn, hi = *mx;
            for (size_t i = 0; i < f.size(); ++i)
                noisePx[i * 4 + (size_t) ch] = (uint8_t) jsRound (((double) f[i] - lo) / (hi - lo) * 255);
        }
    }

    // The ripples' slope map: a sum of wave trains on integer wave vectors (so it tiles), equal slope a wave.
    {
        const int N = rippleSize;
        Rng rng { 4711 };
        struct Wave { int m, n; double k, s, ph; };
        std::vector<Wave> waves;
        std::set<std::pair<int, int>> used;
        while (waves.size() < 46)
        {
            const bool back = rng.next() < 0.18;                                 // a few cross / counter waves
            double ang = (rng.next() - 0.5) * 2.2;
            if (back) ang += pi * (rng.next() < 0.5 ? 0.6 : -0.6);
            const double km = 3 + std::pow (rng.next(), 1.5) * 36;
            const int m = (int) jsRound (std::cos (ang) * km), n = (int) jsRound (std::sin (ang) * km);
            if ((m == 0 && n == 0) || used.count ({ m, n }) != 0) continue;
            used.insert ({ m, n });
            const double s0 = 0.35 + 0.65 * rng.next();
            waves.push_back ({ m, n, std::hypot ((double) m, (double) n), s0, rng.next() * pi * 2 });
        }
        std::vector<float> sx ((size_t) (N * N), 0.0f), sz ((size_t) (N * N), 0.0f), h ((size_t) (N * N), 0.0f);
        for (const auto& w : waves)
        {
            const double ux = w.m / w.k, uz = w.n / w.k, ah = w.s / (2 * pi * w.k);
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i)
                {
                    const double arg = 2 * pi * (w.m * i + w.n * j) / N + w.ph, c = std::cos (arg);
                    const size_t o = (size_t) (j * N + i);
                    sx[o] = (float) (sx[o] + w.s * ux * c); sz[o] = (float) (sz[o] + w.s * uz * c); h[o] = (float) (h[o] + ah * std::sin (arg));
                }
        }
        double mx = 0, hmn = 1e300, hmx = -1e300, ss = 0;
        for (size_t o = 0; o < sx.size(); ++o)
        {
            mx = std::max ({ mx, (double) std::abs (sx[o]), (double) std::abs (sz[o]) });
            hmn = std::min (hmn, (double) h[o]); hmx = std::max (hmx, (double) h[o]);
            ss += (double) sx[o] * sx[o] + (double) sz[o] * sz[o];
        }
        ripplePx.assign ((size_t) (N * N * 4), 0);
        for (size_t o = 0; o < sx.size(); ++o)
        {
            const double a = sx[o] / mx, b = sz[o] / mx;
            ripplePx[o * 4] = (uint8_t) jsRound ((0.5 + 0.5 * a) * 255); ripplePx[o * 4 + 1] = (uint8_t) jsRound ((0.5 + 0.5 * b) * 255);
            ripplePx[o * 4 + 2] = (uint8_t) jsRound ((a * a + b * b) * 0.5 * 255); ripplePx[o * 4 + 3] = (uint8_t) jsRound ((h[o] - hmn) / (hmx - hmn) * 255);
        }
        rmsAxis = std::sqrt (ss / (N * N) / 2) / mx;
    }

    built = true;
    findStart();
    if (! placed)
        place (startXZ[0], startXZ[1], 0, gearWanted, turbo, parked);
}

// Bilinear on the 8-bit noise exactly as the GPU's base level: the autopilot and the start search read the shore.
double TitanWorld::texS (int ch, double u, double v) const noexcept
{
    const int N = noiseSize;
    const double x = (u - std::floor (u)) * N - 0.5, y = (v - std::floor (v)) * N - 0.5;
    const double x0 = std::floor (x), y0 = std::floor (y), fx = x - x0, fy = y - y0;
    const int i0 = ((int) x0 + N) % N, i1 = ((int) x0 + 1) % N, j0 = ((int) y0 + N) % N, j1 = ((int) y0 + 1) % N;
    const auto px = [&] (int i, int j) { return (double) noisePx[(size_t) ((j * N + i) * 4 + ch)]; };
    const double a = px (i0, j0), b = px (i1, j0), c = px (i0, j1), e = px (i1, j1);
    return ((a + (b - a) * fx) * (1 - fy) + (c + (e - c) * fx) * fy) / 255;
}

double TitanWorld::coast (double x, double z) const noexcept
{
    const double n = 0.72 * texS (0, x / land.LA + land.o0[0], z / land.LA + land.o0[1]) + 0.28 * texS (1, x / land.LB + land.o1[0], z / land.LB + land.o1[1]);
    return smoothstep (land.C0, land.C1, n);
}

double TitanWorld::coastAt (double x, double z, double heading, double azDeg, double d) const noexcept
{
    const double a = heading + azDeg * TitanWorld::deg;
    return coast (x + std::sin (a) * d, z - std::cos (a) * d);
}

// The autopilot keeps to open water: it leans away from the side with more shore ahead, and turns off a
// shore dead ahead — gently, and sooner the faster it goes.
double TitanWorld::landLean() const noexcept
{
    if (! built)
        return 0;
    double L = 0, R = 0, C = 0;
    for (const double d : { 3000.0, 3800.0 })
    {
        L += coastAt (s.x, s.z, s.heading, -22, d) + 0.5 * coastAt (s.x, s.z, s.heading, -45, d);
        R += coastAt (s.x, s.z, s.heading, 22, d) + 0.5 * coastAt (s.x, s.z, s.heading, 45, d);
        C += coastAt (s.x, s.z, s.heading, 0, d);
    }
    double yaw = clamp ((L - R) * 2.5, -4, 4);
    if (C > 0.15) yaw += (L <= R ? -1 : 1) * 6 * std::min (1.0, C);
    return yaw * clamp (s.speed / 8, 0.25, 1) * TitanWorld::deg;
}

void TitanWorld::findStart()
{
    Rng rng { 20261018 };
    bool found = false; double bestScore = 0;
    for (int i = 0; i < 300; ++i)
    {
        const double x = 60000 + rng.next() * 400000, z = 60000 + rng.next() * 400000;
        double near = 0;
        for (int a = -80; a <= 80; a += 10) for (const double d : { 600.0, 1500.0, 2500.0, 3500.0 }) near = std::max (near, coastAt (x, z, 0, a, d));
        for (int a = -45; a <= 45; a += 15) near = std::max (near, coastAt (x, z, 0, a, 3900));
        if (near > 0.02) continue;
        double cov = 0, n = 0, sunSea = 0;
        for (int a = -50; a <= 50; a += 5)
        {
            double m = 0;
            for (const double d : { 4200.0, 5500.0, 7000.0, 9000.0, 12000.0 }) m = std::max (m, coastAt (x, z, 0, a, d));
            cov += m > 0.5 ? 1 : 0; n++;
        }
        cov /= n;
        for (const double d : { 4200.0, 6000.0, 9000.0 })
            for (const double a : { sunAz - 6, sunAz, sunAz + 6, volcanoAz, stormAz })
                sunSea = std::max (sunSea, coastAt (x, z, 0, a, d));
        const double score = -std::abs (cov - 0.4) - 0.3 * sunSea;
        if (! found || score > bestScore) { found = true; bestScore = score; startXZ = { x, z }; }
    }
}

//==============================================================================
void TitanWorld::setGear (int g) noexcept   { gearWanted = std::clamp (g, 1, gears); }
void TitanWorld::setSteer (float v) noexcept { steer = std::clamp (v, -1.0f, 1.0f); }

double TitanWorld::speedFor (int g, bool t) const noexcept
{
    return gearSpeed[(size_t) std::clamp (g, 1, gears) - 1] * (t ? turboFactor : 1.0);
}

void TitanWorld::place (double x, double z, double heading, int g, bool t, bool p)
{
    placed = true;
    gearWanted = std::clamp (g, 1, gears);
    turbo = t; parked = p;
    s.x = x; s.z = z; s.heading = heading;
    s.speed = parked ? 0.0 : speedFor (gearWanted, turbo);
    s.boost = turbo && ! parked ? 1 : 0;
    s.yawRate = 0; lastSteerTime = s.time; s.foam = 0; s.rotorPhase = s.time * 0.9;
    resetEvents();
    resetGlass();
    step (0);
}

void TitanWorld::settle (int g, bool t, bool p)
{
    gearWanted = std::clamp (g, 1, gears);
    turbo = t; parked = p;
    s.speed = parked ? 0.0 : speedFor (gearWanted, turbo);
    for (size_t k = 0; k < 2; ++k)
        lightLevel[k] = lightWanted[k] ? 1.0 : 0.0;
    s.boost = turbo && ! parked ? 1 : 0;
    step (0);
}

void TitanWorld::setClock (double time, double travel)
{
    s.time = time;
    place (s.x, s.z, s.heading, gearWanted, turbo, parked);
    s.travel = travel;
}

//==============================================================================
// The timed events.
double TitanWorld::saturnVisibility (double t) const noexcept   // the haze thins: Saturn rises out of it, holds, sinks back; a pale disc between
{
    const auto c = saturnCycle.at (t);
    const double a0 = 0.12 * c.L, hold = 3 + 0.18 * c.L;
    return 0.62 + 0.38 * smoothstep (a0, a0 + 4.5, c.a) * (1 - smoothstep (a0 + 4.5 + hold, a0 + 9.5 + hold, c.a));
}

std::array<double, 2> TitanWorld::rotorShadow (double t) const noexcept   // a soft sweep across the glass now and then
{
    const auto c = shadowCycle.at (t);
    if (c.a > 1) return { 0, 0 };
    return { -1.5 + 3.0 * c.a, 0.10 * std::sin (pi * c.a) };
}

double TitanWorld::eruption (double t) const noexcept   // the last 30 s of each cycle: swell 5 s, hold 7, spread and fade 16
{
    const auto c = eruptCycle.at (t);
    const double a0 = c.L - 30;
    return smoothstep (a0, a0 + 5, c.a) * (1 - smoothstep (a0 + 12, a0 + 28, c.a));
}

TitanWorld::Flash TitanWorld::flash (double t) const noexcept   // two or three flickers a strike, sometimes a bolt to the sea
{
    const auto c = flashCycle.at (t);
    const auto k = (uint32_t) c.k;
    const double h1 = hashOf (k, 11), h2 = hashOf (k, 12), h3 = hashOf (k, 13), h4 = hashOf (k, 14);
    const auto g = [&c] (double centre, double w) { const double x = (c.a - centre) / w; return std::exp (-x * x); };
    const double f = g (0.03, 0.04) + 0.75 * g (0.15, 0.05) + (h3 > 0.5 ? 0.55 * g (0.34, 0.07) : 0);
    return { f * (0.6 + 0.4 * h4), (h1 - 0.5) * 0.22 + 0.01, 0.03 + 0.05 * h2, h4 < 0.5 };
}

//==============================================================================
// Placed: the nitrogen fields and the buoy gates set out ahead on the course.
void TitanWorld::resetEvents()
{
    eventRng = { (toInt32 (std::floor (s.x) * 73856093) ^ toInt32 (std::floor (s.z) * 19349663)) ^ toInt32 (std::floor (s.time * 1000)) };
    fieldList.clear(); buoyList.clear();
    nextFieldT = s.time + 3; nextGateT = s.time + 7; nextBuoyT = s.time + 16;
}

double TitanWorld::fieldStrength (const Field& f) const noexcept { return smoothstep (f.born, f.born + 4, s.time); }

void TitanWorld::addField (double ahead, double lat)
{
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading), rx = std::cos (s.heading), rz = std::sin (s.heading);
    const double r = 28 + 30 * eventRng.next();
    fieldList.push_back ({ s.x + fx * ahead + rx * lat, s.z + fz * ahead + rz * lat, r, s.time, eventRng.next() });
    if (fieldList.size() > 2) fieldList.erase (fieldList.begin());
}

void TitanWorld::addGate (double ahead)
{
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading), rx = std::cos (s.heading), rz = std::sin (s.heading);
    const double half = 15 + 10 * eventRng.next(), lat = (eventRng.next() - 0.5) * 8;
    for (const int side : { -1, 1 })
        buoyList.push_back ({ s.x + fx * ahead + rx * (lat + side * half), s.z + fz * ahead + rz * (lat + side * half), side < 0 ? 0 : 1, eventRng.next() });
    while (buoyList.size() > 8) buoyList.erase (buoyList.begin());
}

void TitanWorld::stepEvents (double dt)
{
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading), rx = std::cos (s.heading), rz = std::sin (s.heading);
    const bool moving = s.speed > 0.4 && ! parked;
    if (moving && s.time >= nextFieldT && fieldList.size() < 2)
    {
        const double lat = (eventRng.next() - 0.5) * 14;
        addField (clamp (s.speed * 18, 110, 480), lat);
        nextFieldT = s.time + 22 + 22 * eventRng.next();
    }
    if (moving && s.time >= nextGateT && buoyList.size() <= 6)
    {
        addGate (clamp (s.speed * 26, 150, 650));
        nextGateT = s.time + 28 + 30 * eventRng.next();
    }
    if (moving && s.time >= nextBuoyT && buoyList.size() <= 7)
    {
        const double ahead = clamp (s.speed * 30, 200, 900);
        const double sign = eventRng.next() < 0.5 ? -1 : 1;
        const double lat = sign * (45 + 90 * eventRng.next());
        buoyList.push_back ({ s.x + fx * ahead + rx * lat, s.z + fz * ahead + rz * lat, 2, eventRng.next() });
        nextBuoyT = s.time + 20 + 25 * eventRng.next();
    }
    const auto keep = [&] (double ox, double oz, double back)
    {
        const double dx = ox - s.x, dz = oz - s.z;
        return dx * fx + dz * fz > -back && dx * dx + dz * dz < 3500.0 * 3500.0;
    };
    fieldList.erase (std::remove_if (fieldList.begin(), fieldList.end(), [&] (const Field& f) { return ! keep (f.x, f.z, f.r + 200); }), fieldList.end());
    buoyList.erase (std::remove_if (buoyList.begin(), buoyList.end(), [&] (const Buoy& b) { return ! keep (b.x, b.z, 80); }), buoyList.end());

    // Flying through a field's spray? Measured just ahead.
    double foam = 0;
    const double bx = s.x + fx * 4, bz = s.z + fz * 4;
    for (const auto& f : fieldList)
        foam = std::max (foam, (1 - smoothstep (0.5 * f.r, 0.95 * f.r, std::hypot (f.x - bx, f.z - bz))) * fieldStrength (f));
    if (dt == 0.0) s.foam = foam;
    else s.foam += (foam - s.foam) * (1 - std::exp (-dt / (foam > s.foam ? 0.2 : 1.1)));
    s.down = 0.25 + 0.75 * (1 - smoothstep (0, 12, s.speed));      // the downwash on the liquid: strongest in a hover
    s.white = s.foam * clamp (s.speed / 4, 0.2, 1);
}

//==============================================================================
// The canopy: the drops, the wipers' timing.
void TitanWorld::resetGlass()
{
    glassRng = { toInt32 (std::floor (s.x * 7)) ^ toInt32 (std::floor (s.z * 13)) ^ toInt32 (std::floor (s.time * 977)) };
    dropList.clear(); trailList.clear(); dropAcc = 0; glassT0 = s.time;
    wipeCur = wipePrev = -1e9; nextWipe = s.time + wipeEvery;
}

bool TitanWorld::wiperFraction (double t, double& f) const noexcept
{
    const double a = t - wipeCur, out = strokeCur.out, pause = strokeCur.pause;
    if (a < 0 || a > strokeCur.length()) return false;
    f = a < out ? smoothstep (0, 1, a / out)
      : a < out + pause ? 1
      : 1 - smoothstep (0, 1, (a - out - pause) / out);
    return true;
}

void TitanWorld::startWipe() noexcept
{
    if (s.time - wipeCur < strokeCur.length()) return;
    wipePrev = wipeCur; strokePrev = strokeCur;
    wipeCur = s.time; nextWipe = s.time + wipeEvery;
    strokeCur = wiperSpeed == wipersFast ? Stroke { fastStroke, fastPause } : Stroke { strokeSeconds, pauseSeconds };
}

// When the wipers' speed has the next wipe start of its own accord: a minute after the last began, a
// rest after it ended, straight after it — or never.
double TitanWorld::wipeDue() const noexcept
{
    switch (wiperSpeed)
    {
        case wipersOff:  return 1e300;
        case wipersSlow: return wipeCur + strokeCur.length() + slowRest;
        case wipersFast: return wipeCur + strokeCur.length();
        default:         return nextWipe;
    }
}

void TitanWorld::spawnDrop (int kind)   // positions and radii in glass metres
{
    auto& r = glassRng;
    const double gt = (kind == 1 ? std::pow (r.next(), 1.7) * 0.95 : r.next()) * glassTop();
    const double gs = (r.next() * 2 - 1) * glassHalfW (gt);
    const double big = kind == 1 ? 0.0022 + 0.0075 * std::pow (r.next(), 1.6) : 0.003 + 0.0035 * r.next();
    const double life = 5 + 8 * r.next(), ph = r.next() * 6.28, k = r.next();
    dropList.push_back ({ gs, gt, big, big, 0, life, ph, k, gs, gt });
    if ((int) dropList.size() > maxDrops) dropList.erase (dropList.begin());
}

void TitanWorld::stepGlass (double dt)
{
    if (dt <= 0) return;
    if (s.time >= wipeDue()) startWipe();
    const double sp = s.speed, top = glassTop();
    // Landing: a few from the drizzle (more when moving into it), the downwash's spray (most in a hover),
    // much more on a TURBO dash, a burst over a fizzing field.
    const double rDrizzle = 0.5 + 0.09 * sp, rSpray = 1.6 * s.down + 85 * s.foam + 8 * s.boost * clamp (sp / 15, 0, 1);
    dropAcc += (rDrizzle + rSpray) * dt;
    while (dropAcc >= 1) { dropAcc -= 1; spawnDrop (glassRng.next() * (rDrizzle + rSpray) < rSpray ? 1 : 0); }
    // The airflow: up the glass and outward, faster with speed and with size; small drops stay pinned; still on PARK.
    for (auto& d : dropList)
    {
        d.age += dt;
        const double f = d.age / d.life;
        d.r = d.r0 * (f < 0.6 ? 1 : std::sqrt (std::max (0.0, 1 - (f - 0.6) / 0.4)));     // evaporating
        const double vm = std::max (0.0, (sp / 27) * (d.r / 0.0046) - 0.22) * 0.125;      // metres a second
        if (vm > 0)
        {
            const double out = d.s / glassHalfW (d.t);
            const double dx = (out > 0 ? 1 : out < 0 ? -1 : 0) * (0.3 + 0.9 * std::abs (out)), dy = 1, l = std::hypot (dx, dy);
            d.ph += dt * (3 + 4 * d.k);
            const double wob = 0.45 + 0.55 * std::max (0.0, std::sin (d.ph));               // stick and slip
            d.s += dx / l * vm * wob * dt; d.t += dy / l * vm * wob * dt;
            if (std::hypot (d.s - d.ls, d.t - d.lt) > 0.005)
            {
                trailList.push_back ({ d.ls, d.lt, d.s, d.t, d.r * 0.22, s.time });
                d.ls = d.s; d.lt = d.t;
            }
        }
    }
    // Merging: touching drops become one, the volume kept.
    for (size_t i = 0; i < dropList.size(); ++i)
        for (size_t j = i + 1; j < dropList.size(); ++j)
        {
            auto& a = dropList[i]; auto& b = dropList[j];
            if (a.r == 0.0 || b.r == 0.0) continue;
            const double dx = a.s - b.s, dy = a.t - b.t, rr = (a.r + b.r) * 0.8;
            if (dx * dx + dy * dy < rr * rr)
            {
                const double va = a.r * a.r * a.r, vb = b.r * b.r * b.r, v = va + vb, k = vb / v;
                a.s += (b.s - a.s) * k; a.t += (b.t - a.t) * k;
                a.r0 = std::cbrt (a.r0 * a.r0 * a.r0 + b.r0 * b.r0 * b.r0); a.r = std::cbrt (v);
                a.age = std::min (a.age, b.age); a.life = std::max (a.life, b.life); b.r = 0;
            }
        }
    // The blades clear what lies under them as they pass — tested on the glass, where they are, each on its own pane.
    double f1 = 0, f0 = 0;
    const bool has1 = wiperFraction (s.time, f1), has0 = wiperFraction (s.time - dt, f0);
    const auto swept = [&] (double gs, double gt)
    {
        if (! has1 && ! has0) return false;
        const double a = has0 ? f0 : f1, b = has1 ? f1 : f0, lo = std::min (a, b), hi = std::max (a, b);
        bool haveX = false; std::array<double, 2> X {};
        for (const auto& w : wipers())
        {
            const double px = gs - w.p[0], py = gt - w.p[1], rho = std::hypot (px, py), span = w.th1 - w.th0;
            const double x = (std::atan2 (py, px) - w.th0) / span, m = 0.01 / std::abs (span);   // 0.01 rad either side
            if (! (rho > w.rIn - 0.012 && rho < w.rOut + 0.012 && x >= lo - m && x <= hi + m)) continue;
            if (! haveX) { X = cabinXY (gs, gt); haveX = true; }
            if (X[0] > w.pane[0] + w.pane[1] * X[1] && X[0] < w.pane[2] + w.pane[3] * X[1]) return true;
        }
        return false;
    };
    dropList.erase (std::remove_if (dropList.begin(), dropList.end(), [&] (const Drop& d)
    {
        return ! (d.r > 0.0006 && d.age < d.life && d.t < top + 0.01 && d.t > -0.01 && std::abs (d.s) < glassHalfW (d.t) + 0.01 && ! swept (d.s, d.t));
    }), dropList.end());
    trailList.erase (std::remove_if (trailList.begin(), trailList.end(), [&] (const Trail& q)
    {
        return ! (s.time - q.born < trailLife && ! swept ((q.s0 + q.s1) / 2, (q.t0 + q.t1) / 2));
    }), trailList.end());
    if ((int) trailList.size() > maxTrails) trailList.erase (trailList.begin(), trailList.end() - maxTrails);
}

//==============================================================================
void TitanWorld::step (double dt)
{
    s.time += dt;
    const double target = parked ? 0.0 : speedFor (gearWanted, turbo);
    const double kS = 1 - std::exp (-dt / (parked ? parkSeconds : speedSeconds));
    s.speed += (target - s.speed) * kS;
    s.boost += ((turbo && ! parked ? 1 : 0) - s.boost) * (1 - std::exp (-dt / boostSeconds));
    for (size_t k = 0; k < 2; ++k)
        lightLevel[k] += ((lightWanted[k] ? 1.0 : 0.0) - lightLevel[k]) * (1 - std::exp (-dt / 0.12));

    // Yaw: the hand wins; after 1.5 s without it a gentle meander resumes, plus the lean away from the
    // shore. Parked, the hand turns the view in place and nothing meanders.
    double yawTarget;
    if (parked) { yawTarget = steer * parkedYawDegrees * deg; lastSteerTime = s.time; }
    else if (std::abs (steer) > 0.001f) { yawTarget = steer * steerYawDegrees * deg; lastSteerTime = s.time; }
    else
    {
        const double fade = smoothstep (1.5, 4.5, s.time - lastSteerTime);
        yawTarget = fade * (3.5 * deg * std::sin (s.time * 0.09 + meanderPhase) + landLean());
    }
    s.yawRate += (yawTarget - s.yawRate) * (1 - std::exp (-dt / yawSeconds));
    s.heading += s.yawRate * dt;
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);
    s.x += fx * s.speed * dt; s.z += fz * s.speed * dt; s.travel += s.speed * dt;

    // Flying low: a slow bob and sway; the nose dips with speed (harder on a dash); the craft banks into turns.
    const double sp = s.speed / (topSpeed * turboFactor);
    s.camY = camHeight + 0.06 * std::sin (s.time * 0.9) + 0.03 * std::sin (s.time * 2.1 + 1.3) + 0.012 * sp * std::sin (s.time * 4.1) + 0.05 * s.foam * std::sin (s.time * 3.3);
    const double pitchT = -(0.028 * std::min (s.speed, topSpeed) / topSpeed + 0.038 * std::max (0.0, s.speed - topSpeed) / topSpeed) + 0.004 * std::sin (s.time * 0.71 + 0.4);
    const double rollT = -s.yawRate * 0.17 + 0.006 * std::sin (s.time * 0.53) + 0.003 * std::sin (s.time * 1.7 + 0.8);
    s.rotorPhase += dt * (0.95 - 0.2 * (1 - smoothstep (0, 3, s.speed)));            // the rotor's glint; a touch slower in a hover
    if (dt == 0.0) { s.pitch = pitchT; s.roll = rollT; }
    else { const double k = 1 - std::exp (-dt / 0.6); s.pitch += (pitchT - s.pitch) * k; s.roll += (rollT - s.roll) * k; }
    stepEvents (dt);
    stepGlass (dt);
}

} // namespace orbitamp::amberpaw
