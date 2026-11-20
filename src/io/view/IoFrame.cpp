// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// What io-gl.js works out on the CPU, line for line: the surface's light (ringLevel, jupShineOf, surfaceLight), the
// skyline toward a low Sun, a frame's uploads (drawScene's CPU part, in its order), the dosimeter's needle, the
// plates' glass, the rocks' mesh, the finds' readers; and io-world.js's stand-in meshes for the hardware
// (structureBuilder, structureMeshes).

#include "IoWorld.h"
#include "IoJs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>

namespace orbitamp::io
{

using namespace js;
using V2 = IoWorld::V2;
using V3 = IoWorld::V3;

namespace
{
    constexpr double PI = IoWorld::pi, DEG = IoWorld::deg;
    inline double hyp (double a, double b) noexcept { return Js::hypot (a, b); }
    V3 norm3 (const V3& v) noexcept { const double l = orOne (Js::hypot (v[0], v[1], v[2])); return { v[0] / l, v[1] / l, v[2] / l }; }
    V3 cross3 (const V3& a, const V3& b) noexcept { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    double dot3 (const V3& a, const V3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    V3 add3 (const V3& a, const V3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
    V3 sub3 (const V3& a, const V3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    V3 mul3 (const V3& a, double k) noexcept { return { a[0] * k, a[1] * k, a[2] * k }; }
    V3 mixV (const V3& a, const V3& b, double t) noexcept { return { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; }
    double lum3 (const V3& c) noexcept { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; }
    V3 unit3 (const V3& c) noexcept { const double l = orOne (lum3 (c)); return { c[0] / l, c[1] / l, c[2] / l }; }

    // THE SURFACE'S LIGHT's constants (io-gl.js). jupNight: the full Jupiter's shine on the land at night against its
    // unexposed strength, jupShineOf keeping it there, exposed, by day too; jupFloor: its share at BLEND 0 against the full
    // Jupiter's, 1 since 20.11 (Jupiter lights the land as if full whatever its phase; it was a fifth).
    constexpr double ringAtIngress = 0.16, groundFloor = 0.035, ringNight = 0.85, jupNight = 2.6, jupFloor = 1.0, fillShare = 0.18, ringGoldAtIngress = 0.85;
    constexpr V3 fillCol { 0.66, 0.74, 0.92 }, ringRed { 1.0, 0.30, 0.10 }, ringGold { 1.0, 0.86, 0.64 };
    const V3& fillDir() { static const V3 d = IoWorld::dirOf (IoWorld::jupAz() - 80 * DEG, 30 * DEG); return d; }
    // the work lights' reach by variant and their strength
    constexpr std::array<double, IoWorld::numVariants> siteReach { 12, 14, 16, 16, 22, 12, 26, 26, 26, 26 };
    constexpr double siteK = 1.1;
}

//==============================================================================
// THE SURFACE'S LIGHT

IoWorld::Ring IoWorld::ringLevel (const Sun& sp)
{
    double level, t = 0;
    if (sp.sep > jupRho()) level = ringAtIngress * smoothstep (0.5, 1.0, sp.graze);
    else { t = smoothstep (0, 0.75, sp.u); level = Js::exp (jsLog (ringAtIngress) + (jsLog (groundFloor) - jsLog (ringAtIngress)) * t); }
    level = jsMin (level, groundFloor) + jsMax (level - groundFloor, 0) * (1 - ringNight * sp.night);
    return { level, t };
}
V3 IoWorld::jupShineOf (const Sun& sp)
{
    const double jl = jupFloor + (1 - jupFloor) * (1 - Js::cos (sp.sep)) / 2, k = jl * jupNight * 1.6 / sp.expo;
    return { 0.105 * k, 0.090 * k, 0.070 * k };
}
IoWorld::Light IoWorld::surfaceLight (const Sun& sp)
{
    const V3 J = jupFrame().J;
    V3 keyS {};
    for (size_t k = 0; k < 3; ++k) keyS[k] = sp.hue[k] * sp.light * sunI * sp.vis * sp.gate;
    const V3 ambJ = jupShineOf (sp);
    const Ring rl = ringLevel (sp);
    const double nightF = groundFloor * sp.night * (1 - sp.eclDark);
    if (rl.level <= 0 && nightF <= 0) return { sp.dir, keyS, 1, J, ambJ };
    const double P = rl.level / sp.expo, PF = nightF / sp.expo;
    const V3 ringDir = norm3 (mixV (sp.dir, J, jsMax (smoothstep (0, 1, sp.depth), sp.night)));
    V3 keyR = unit3 (mixV (ringRed, ringGold, ringGoldAtIngress * (1 - rl.t)));
    for (auto& v : keyR) v = v * (1 - fillShare) * P / jsMax (ringDir[1], 0.03);
    V3 fill = unit3 (fillCol);
    for (auto& v : fill) v = v * (fillShare * P + PF) / (0.35 + 0.65 * jsMax (fillDir()[1], 0));
    const double ws = lum3 (keyS) * jsMax (sp.dir[1], 0), wr = lum3 (keyR) * jsMax (ringDir[1], 0);
    V3 d {};
    for (size_t k = 0; k < 3; ++k) d[k] = sp.dir[k] * ws + ringDir[k] * wr + sp.dir[k] * 1e-9;
    const V3 dir = norm3 (d);
    const V3 sum = add3 (keyS, keyR);
    const double sc = (ws + wr) / jsMax (lum3 (sum) * jsMax (dir[1], 0.03), 1e-9);
    const double wj = lum3 (ambJ), wf = lum3 (fill);
    V3 a {};
    for (size_t k = 0; k < 3; ++k) a[k] = J[k] * wj + fillDir()[k] * wf + J[k] * 1e-9;
    return { dir, mul3 (sum, sc), 1, norm3 (a), add3 (ambJ, fill) };
}

//==============================================================================
// THE SKYLINE TOWARD THE SUN

double IoWorld::skylineAt (double az, double camY, double x, double z) const
{
    const auto nzTap = [this] (double u, double v, int ch)
    {
        const double N = noiseSize, X = u * N - 0.5, Y = v * N - 0.5, i = std::floor (X), j = std::floor (Y), fx = X - i, fy = Y - j;
        const auto at = [&] (double a, double b) { return noisePx[(size_t) ((mod (mod (b, N) + N, N) * N + mod (mod (a, N) + N, N)) * 4 + ch)] / 255.0; };
        return lerp (lerp (at (i, j), at (i + 1, j), fx), lerp (at (i, j + 1), at (i + 1, j + 1), fx), fy);
    };
    const double dx = Js::sin (az), dz = -Js::cos (az);
    double top = -0.0018;
    for (double k = 0, d = 20; k < 44 && d < farCut; ++k, d *= 1.11)
        top = jsMax (top, jsAtan ((floorHeight (x + dx * d, z + dz * d) - d * d * 0.5 / ioR - camY) / d));
    const double a = az / (2 * PI);
    for (int k = 0; k < 2; ++k)
    {
        const double D = k == 0 ? 6000 : 14000, u0 = a * (k == 0 ? 3 : 2) + 0.37 * k;
        const double hA = nzTap (u0, 0.81 + 0.07 * k, 0), hB = nzTap (u0 * 4, 0.33 + 0.07 * k, 1), peak = smoothstep (0.55, 0.95, hA);
        const double H = (k == 0 ? 40 + 520 * peak * peak : 80 + 1600 * peak * peak) * (0.85 + 0.3 * hB);
        top = jsMax (top, jsAtan ((H - camY - D * D * 0.5 / ioR) / D));
    }
    return top;
}
V2 IoWorld::sunOverSkyline (const Sun& sp, double camY, double x, double z) const
{
    if (sp.el > 0.1) return { 1, 1 };
    const double over = sp.el - skylineAt (sp.az, camY, x, z);
    return { smoothstep (-sunRho, sunRho, over), smoothstep (-sunRho, sunRho + 0.25 * DEG, over) };
}
V2 IoWorld::needle (double doseValue, double fromDeg, double toDeg) noexcept
{
    const double from = fromDeg * DEG, to = toDeg * DEG, a = from + (to - from) * clamp (doseValue, 0, 1);
    return { Js::cos (a), -Js::sin (a) };
}
std::optional<std::array<double, 4>> IoWorld::glassBounds (const uint8_t* rgba, int w, int h, int margin)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgba[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] < 255) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) return {};
    return std::array<double, 4> { (double) std::max (0, x0 - margin) / w, (double) std::max (0, y0 - margin) / h,
                                   (double) std::min (w, x1 + 1 + margin) / w, (double) std::min (h, y1 + 1 + margin) / h };
}

//==============================================================================
// A FRAME (io-gl.js drawScene, its CPU part, in its order)

IoWorld::Frame IoWorld::frame()
{
    Frame F;
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    F.ccx = ccx; F.ccz = ccz;
    F.camFrac = { f32 (s.x - ccx), f32 (s.z - ccz) };
    F.camRel = { F.camFrac[0], f32 (s.camY), F.camFrac[1] };
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    F.fwd = { fx, fz };
    F.off0 = { fract (ccx / l0), fract (ccz / l0) };
    F.off1 = { fract (ccx / l1) + off1[0], fract (ccz / l1) + off1[1] };
    F.off2 = { fract (ccx / l2) + off2[0], fract (ccz / l2) + off2[1] };
    F.time = mod (s.time, 100000);
    F.hitAge = jsMin (s.hitT, 1e4);

    // THE LIGHT
    F.sun = s.sun.dir;
    for (size_t k = 0; k < 3; ++k) F.sunCol[k] = s.sun.hue[k] * s.sun.light * sunI;
    F.jupSunCol = { 1.0 * sunI, 0.975 * sunI, 0.94 * sunI };
    const double rg = s.sun.ring * sunI;
    const V3 js = jupShineOf (s.sun);
    F.jupShine = { js[0] + 1.0 * rg, js[1] + 0.30 * rg, js[2] + 0.11 * rg };
    F.sunK = s.sunK; F.dark = s.dark; F.air = s.sun.airglow; F.flat = s.sun.flat; F.expo = s.sun.expo;
    F.airglow = { 0.010 * F.air, 0.0035 * F.air, 0.0030 * F.air };
    F.ground = surfaceLight (s.sun);
    const auto& G = F.ground;
    const double gh = orOne (hyp (G.dir[0], G.dir[2]));
    F.term = { jsAsin (clamp (G.dir[1], -1, 1)), G.dir[0] / gh, G.dir[2] / gh, 0.0015 };
    const V2 skyV = sunOverSkyline (s.sun, s.camY, s.x, s.z);
    F.sunUp = { skyV[0] * s.sunK, skyV[1] * s.sunK, skyV[1] * s.sun.gate };
    const V3 farK { 0.80, 0.70, 0.32 };
    for (size_t k = 0; k < 3; ++k) F.far[k] = farK[k] * (G.key[k] * G.k * jsMax (0, G.dir[1]) * 0.9 + G.amb[k] * 0.6 + F.airglow[k]);

    // the lava's lights
    struct Lava { V3 p; double reach, k, D; V3 col; };
    std::vector<Lava> lights;
    for (const auto& c : calderas_)
    {
        if (c.lake <= 0) continue;
        const double a = lakeAct (c, s.act), D = hyp (c.x - s.x, c.z - s.z);
        if (D > 1500) continue;
        lights.push_back ({ { c.x, c.baseY - c.D + 0.3 * c.lakeR + 2, c.z }, c.lakeR * 0.95 + 8, (0.2 + 0.8 * a) * a * 0.7, D, { 1.0, 0.36, 0.09 } });
    }
    for (const auto& f : fountains_)
    {
        const double a = fountainAct (f, s.act), D = hyp (f.x - s.x, f.z - s.z);
        if (a < 0.02 || D > 900) continue;
        lights.push_back ({ { f.x, f.y + f.H * 0.35, f.z }, f.H * 0.8 + 8, 2.2 * a, D, { 1.0, 0.45, 0.12 } });
    }
    const auto atCam = [] (const Lava& L) { return L.k / (1 + sq (jsMax (0, L.D - L.reach * 0.5) / L.reach)); };
    std::stable_sort (lights.begin(), lights.end(), [&] (const Lava& a, const Lava& b) { return atCam (b) - atCam (a) < 0; });
    for (const auto& L : lights)
    {
        if (F.plN >= 2) break;
        if (! (L.k > 0.01 && L.D < L.reach * 6)) continue;
        const size_t o = (size_t) F.plN * 4;
        F.pl[o] = f32 (L.p[0] - ccx); F.pl[o + 1] = f32 (L.p[1]); F.pl[o + 2] = f32 (L.p[2] - ccz); F.pl[o + 3] = f32 (L.reach);
        F.plc[o] = f32 (L.col[0]); F.plc[o + 1] = f32 (L.col[1]); F.plc[o + 2] = f32 (L.col[2]); F.plc[o + 3] = f32 (L.k);
        ++F.plN;
    }
    // the hardware's work lights in the dark (the lists as the frame before left them)
    for (size_t i = 0; i < 6; ++i) { F.sl[i * 4] = 0; F.sl[i * 4 + 1] = -1e6f; F.sl[i * 4 + 2] = 0; F.sl[i * 4 + 3] = 1; }
    F.slBox = { 1e9, 1e9, -1e9, -1e9 };
    if (s.dark > 0.02)
    {
        struct Cand { const Structure* st; size_t vi; double D; };
        std::vector<Cand> cand;
        for (size_t vi = 0; vi < near_.size(); ++vi)
            for (const auto& st : near_[vi])
            {
                const double dx = st.x - s.x, dz = st.z - s.z, D = hyp (dx, dz);
                if (D > 12 && D < 450 && dx * fx + dz * fz > 0.3 * D) cand.push_back ({ &st, vi, D });
            }
        std::stable_sort (cand.begin(), cand.end(), [] (const Cand& a, const Cand& b) { return a.D - b.D < 0; });
        for (size_t c = 0; c < cand.size() && c < 6; ++c)
        {
            const auto& st = *cand[c].st;
            const double D = cand[c].D, R = siteReach[cand[c].vi] * orOne (st.k);
            const double tx = (s.x - st.x) / jsMax (D, 1), tz = (s.z - st.z) / jsMax (D, 1);
            const double lx = st.x + tx * R * 0.3 - ccx, lz = st.z + tz * R * 0.3 - ccz;
            const size_t o = (size_t) F.slN * 4;
            F.sl[o] = f32 (lx); F.sl[o + 1] = f32 (st.y + R * 0.9); F.sl[o + 2] = f32 (lz); F.sl[o + 3] = f32 (R);
            ++F.slN;
            F.slBox[0] = jsMin (F.slBox[0], lx - 2.5 * R); F.slBox[1] = jsMin (F.slBox[1], lz - 2.5 * R);
            F.slBox[2] = jsMax (F.slBox[2], lx + 2.5 * R); F.slBox[3] = jsMax (F.slBox[3], lz + 2.5 * R);
        }
    }
    F.slc = { siteK * s.dark, siteK * 0.84 * s.dark, siteK * 0.62 * s.dark };
    for (size_t i = 0; i < lights.size() && i < 4; ++i)
    {
        const double k = atCam (lights[i]) * 0.5;
        for (size_t c = 0; c < 3; ++c) F.lavaEnv[c] += lights[i].col[c] * k;
    }
    // the paterae for the shaders
    for (size_t i = 0; i < calderas_.size() && i < (size_t) maxCald; ++i)
    {
        const auto& c = calderas_[i];
        const std::array<double, 4> a { c.x - ccx, c.z - ccz, c.R, c.W }, b { c.D, c.rim, c.out, c.baseY }, cc { c.lakeR, lakeAct (c, s.act), 0, 0 };
        for (size_t k = 0; k < 4; ++k) { F.caldA[i * 4 + k] = f32 (a[k]); F.caldB[i * 4 + k] = f32 (b[k]); F.caldC[i * 4 + k] = f32 (cc[k]); }
    }
    F.caldN = (int) calderas_.size();

    // the hardware round the rover, rebuilt as it crosses a cell, and its instances this frame
    {
        const std::string key = std::to_string ((long long) std::floor (s.x / structCell)) + "," + std::to_string ((long long) std::floor (s.z / structCell));
        if (key != nearKey)
        {
            nearKey = key;
            for (auto& b : near_) b.clear();
            for (const auto& st : structuresNear (s.x, s.z)) near_[(size_t) st.variant].push_back (st);
        }
    }
    const auto inView = [&] (double x, double z, double r) { const double dx = x - s.x, dz = z - s.z, D = hyp (dx, dz); return D - r < farCut && (D < r + 30 || dx * fx + dz * fz > 0.55 * D - r); };
    const double c0x = s.x + fx * sh0Ahead, c0z = s.z + fz * sh0Ahead;
    for (size_t vi = 0; vi < near_.size(); ++vi)
    {
        const auto& list = near_[vi];
        auto& b = F.hardware[vi];
        const auto put = [&] (const Structure& st)
        {
            for (const double v : { st.x - ccx, st.y, st.z - ccz, st.yaw, st.k, st.lean[0], st.lean[1], st.seed }) b.data.push_back (f32 (v));
            ++b.n;
        };
        std::vector<char> seen (list.size(), 0);
        for (size_t i = 0; i < list.size(); ++i) seen[i] = inView (list[i].x, list[i].z, 25) && hyp (list[i].x - s.x, list[i].z - s.z) < 2200;
        for (size_t i = 0; i < list.size(); ++i) if (seen[i]) { if (b.n >= 256) break; put (list[i]); }
        const int n = b.n;
        for (size_t i = 0; i < list.size(); ++i)
            if (b.n < 256 && ! seen[i] && std::abs (list[i].x - c0x) < sh0Half + 30 && std::abs (list[i].z - c0z) < sh0Half + 30) put (list[i]);
        b.nSh = b.n; b.n = n;
    }
    for (const auto& r : rocksNear (s.x, s.z, fx, fz, rockReach))
    {
        auto& R = F.rocks[r.sx < 0.45 ? 1 : 0];
        if ((int) (R.size() / 8) >= maxRocks) continue;
        for (const double v : { r.x - ccx, r.y, r.z - ccz, r.yaw, r.sx, r.sy, r.sz, r.tint }) R.push_back (f32 (v));
    }

    // the sky's: Jupiter's exposure, the attack's aurora
    F.jupK = jsPow (s.sun.expo, -0.7); F.sep = s.sun.sep; F.jupT = mod (s.time * 0.5, 1e6);
    for (size_t i = 0; i < 2; ++i)
    {
        const auto& m = moons()[i];
        for (size_t k = 0; k < 3; ++k) { F.moon[i * 4 + k] = f32 (m.dir[k]); F.moonAlb[i * 3 + k] = f32 (m.alb[k]); }
        F.moon[i * 4 + 3] = f32 (Js::sin (m.rho));
    }
    {
        const double ta = F.hitAge;
        F.aurora = s.hitS > 0 && ta > -0.05 && ta < 9 ? s.hitS * (1.6 * Js::exp (-jsMax (ta, 0) / 0.25) + 0.7 * Js::exp (-jsMax (ta, 0) / 1.5)) * smoothstep (-0.05, 0.02, ta) : 0;
        F.flux = F.aurora * 0.8;
    }

    // THE PLUMES' BODIES, THE EMITTERS
    const auto rowMax = [this] (int row) { int m = 0; for (int i = 0; i < histN; ++i) m = std::max (m, (int) hist[(size_t) (row * histN + i)]); return m; };
    const auto nearLakeOf = [this] (const Plume& q) { for (const auto& c : calderas_) if (c.lake > 0 && hyp (c.x - q.x, c.z - q.z) < c.R) return true; return false; };
    {
        struct Vol { double D; bool inside; std::array<float, 12> e; };
        std::vector<Vol> vols;
        for (const auto& q : plumes_)
        {
            if (rowMax (q.row) < 2) continue;
            const double D = hyp (q.x - s.x, q.z - s.z);
            if (D > 9000 || ! inView (q.x, q.z, q.ring * 1.05 + 100)) continue;
            const double nearLake = nearLakeOf (q) ? 1 : 0.3;
            const bool inside = std::abs (q.x - s.x) < q.ring * 1.04 + 2 && std::abs (q.z - s.z) < q.ring * 1.04 + 2 && s.camY < q.y + q.H * 1.06 + 2;
            Vol v { D, inside, {} };
            const std::array<double, 12> e { q.x - ccx, q.y, q.z - ccz, q.H, q.ring, q.pele ? 0.0 : 1.0, (double) q.row, mod (q.i * 0.377, 1), nearLake, 2 * std::sqrt (2 * q.H / 1.796), 0, 0 };
            for (size_t k = 0; k < 12; ++k) v.e[k] = f32 (e[k]);
            vols.push_back (v);
        }
        std::stable_sort (vols.begin(), vols.end(), [] (const Vol& a, const Vol& b)
        {
            const double d = (double) ((int) b.inside - (int) a.inside);
            return (d != 0 ? d : b.D - a.D) < 0;
        });
        for (size_t i = 0; i < vols.size() && i < (size_t) maxEmit; ++i) { F.volumes.push_back (vols[i].e); F.volumesInside += vols[i].inside ? 1 : 0; }
    }
    {
        const auto add = [&] (int list, std::initializer_list<double> e)
        {
            auto& L = F.emitters[(size_t) list];
            if ((int) L.size() >= maxEmit) return;
            std::array<float, 12> v {};
            size_t k = 0;
            for (const double x : e) v[k++] = f32 (x);
            L.push_back (v);
        };
        for (const auto& q : plumes_)
        {
            const double D = hyp (q.x - s.x, q.z - s.z);
            if (rowMax (q.row) < 2) continue;
            if (D > 9000 || ! inView (q.x, q.z, q.ring * 1.2 + 200)) continue;
            const double nearLake = nearLakeOf (q) ? 1 : 0.25;
            add (0, { q.x - ccx, q.y, q.z - ccz, 0, q.H, q.pele ? 0.0 : 1.0, mod (q.i * 0.137 + 0.05, 1), (double) q.row, nearLake, 0, 1, 0 });
        }
        for (const auto& q : fountains_)
        {
            if (rowMax (q.row) < 2) continue;
            if (! inView (q.x, q.z, q.H * 2)) continue;
            add (1, { q.x - ccx, q.y, q.z - ccz, 1, q.H, 0, mod (q.i * 0.293 + 0.11, 1), (double) q.row, 1, 0, 1, 0 });
        }
        for (const auto& q : s.puffs)
        {
            if (q.t < 0 || q.t > 7.5) continue;
            add (2, { q.x - ccx, q.y, q.z - ccz, 2, 0, q.R != 0 && q.R == q.R ? q.R : 10, mod (q.seed * 0.0517, 1), 0, 0, q.t, q.k, 0 });
        }
    }

    // THE SULPHUR SNOW
    F.snow = 0.015 + 0.985 * s.presence * s.presence;
    F.travel = mod (s.travel, 10000); F.speed = s.speed;
    const Basis basis = cameraBasis (s.heading, s.pitch);
    F.sunV = { dot3 (F.sun, basis.r), dot3 (F.sun, basis.u), -dot3 (F.sun, basis.f) };
    return F;
}
std::optional<V3> IoWorld::inkAt (const uint8_t* rgba, int w, int h, double hx, double hy, double R)
{
    double r = 0, g = 0, b = 0; int n = 0;
    for (int y = (int) std::floor (hy - R); y <= (int) std::ceil (hy + R); ++y)
        for (int x = (int) std::floor (hx - R); x <= (int) std::ceil (hx + R); ++x)
            if (x >= 0 && y >= 0 && x < w && y < h && hyp (x + 0.5 - hx, y + 0.5 - hy) <= R)
            {
                const size_t i = ((size_t) y * (size_t) w + (size_t) x) * 4;
                r += rgba[i]; g += rgba[i + 1]; b += rgba[i + 2]; ++n;
            }
    if (n == 0) return {};
    return V3 { r / n / 255, g / n / 255, b / n / 255 };
}

//==============================================================================
// THE ROCKS' MESH (io-gl.js rockMesh): an icosahedron split once, its corners pushed in and out by a hash

std::vector<float> IoWorld::rockMesh (bool fine)
{
    const double t = (1 + std::sqrt (5.0)) / 2;
    std::vector<V3> V;
    for (const V3& p : std::initializer_list<V3> { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
                                                   { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } })
        V.push_back (norm3 (p));
    const std::vector<std::array<int, 3>> Fc { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                               { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    std::map<int, int> mid;
    const auto split = [&] (int a, int b)
    {
        const int k = a < b ? a * 64 + b : b * 64 + a;
        if (mid.find (k) == mid.end())
        {
            V.push_back (norm3 ({ (V[(size_t) a][0] + V[(size_t) b][0]) / 2, (V[(size_t) a][1] + V[(size_t) b][1]) / 2, (V[(size_t) a][2] + V[(size_t) b][2]) / 2 }));
            mid[k] = (int) V.size() - 1;
        }
        return mid[k];
    };
    std::vector<std::array<int, 3>> F2;
    for (const auto& [a, b, c] : Fc)
    {
        const int ab = split (a, b), bc = split (b, c), ca = split (c, a);
        F2.push_back ({ a, ab, ca }); F2.push_back ({ b, bc, ab }); F2.push_back ({ c, ca, bc }); F2.push_back ({ ab, bc, ca });
    }
    if (! fine) F2 = Fc;
    const auto h = [] (int i)
    {
        uint32_t x = (uint32_t) (i + 17) * 0x9E3779B1u;
        x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 13;
        return (double) x / 4294967296.0;
    };
    for (size_t i = 0; i < V.size(); ++i)
    {
        const V3 p = V[i];
        const double k = 0.78 + 0.38 * h ((int) i) - (p[1] < -0.3 ? 0.25 : 0);
        V[i] = { p[0] * k, jsMax (p[1] * k, -0.45), p[2] * k };
    }
    std::vector<float> out;
    for (const auto& [a, b, c] : F2)
    {
        const V3 A = V[(size_t) a], B = V[(size_t) b], C = V[(size_t) c], n = norm3 (cross3 (sub3 (B, A), sub3 (C, A)));
        for (const V3* P : { &A, &B, &C })
        {
            const V3 m = norm3 ({ n[0] * 0.55 + (*P)[0] * 0.45, n[1] * 0.55 + (*P)[1] * 0.45, n[2] * 0.55 + (*P)[2] * 0.45 });
            for (const double v : { (*P)[0], (*P)[1], (*P)[2], m[0], m[1], m[2] }) out.push_back (f32 (v));
        }
    }
    return out;
}

//==============================================================================
// THE HARDWARE'S STAND-IN MESHES (io-world.js structureBuilder, structureMeshes): 11 floats a vertex

namespace
{
    struct Builder
    {
        std::vector<double> v;
        void tri (const V3& A, const V3& B, const V3& C, const V3& n, const V3& ea, const V3& eb, const V3& ec, double mat, double flag)
        {
            for (const auto& [P, E] : { std::pair<const V3*, const V3*> { &A, &ea }, { &B, &eb }, { &C, &ec } })
                for (const double x : { (*P)[0], (*P)[1], (*P)[2], n[0], n[1], n[2], (*E)[0], (*E)[1], (*E)[2], mat, flag }) v.push_back (x);
        }
        /** A square-section bar of thickness t along a -> b. */
        void bar (const V3& a, const V3& b, double t, double mat = 0, double flag = 0)
        {
            const V3 u = norm3 (sub3 (b, a)), ref = std::abs (u[1]) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
            const V3 vv = norm3 (cross3 (u, ref)), w = cross3 (u, vv);
            const double h = t / 2, r2 = 0.7071067811865476;
            std::vector<V3> C, E;
            for (const int end : { 0, 1 }) for (const double sv : { -1.0, 1.0 }) for (const double sw : { -1.0, 1.0 })
            {
                C.push_back (add3 (add3 (end ? b : a, mul3 (vv, sv * h)), mul3 (w, sw * h)));
                E.push_back (mul3 (add3 (mul3 (vv, sv), mul3 (w, sw)), r2));
            }
            const auto quad = [&] (int i0, int i1, int i2, int i3, const V3& n)
            {
                tri (C[(size_t) i0], C[(size_t) i1], C[(size_t) i2], n, E[(size_t) i0], E[(size_t) i1], E[(size_t) i2], mat, flag);
                tri (C[(size_t) i0], C[(size_t) i2], C[(size_t) i3], n, E[(size_t) i0], E[(size_t) i2], E[(size_t) i3], mat, flag);
            };
            quad (2, 3, 7, 6, vv); quad (0, 4, 5, 1, mul3 (vv, -1)); quad (1, 5, 7, 3, w); quad (0, 2, 6, 4, mul3 (w, -1)); quad (4, 6, 7, 5, u); quad (0, 1, 3, 2, mul3 (u, -1));
        }
        /** A box: its centre and half sizes, a yaw about y. */
        void box (const V3& c, const V3& h, double mat, double yaw = 0)
        {
            const double cy = Js::cos (yaw), sy = Js::sin (yaw);
            const V3 z { 0, 0, 0 };
            const auto R = [&] (const V3& p) { return V3 { cy * p[0] + sy * p[2], p[1], -sy * p[0] + cy * p[2] }; };
            for (size_t ax = 0; ax < 3; ++ax)
                for (const double sg : { -1.0, 1.0 })
                {
                    V3 n { 0, 0, 0 }; n[ax] = sg;
                    V3 u { 0, 0, 0 }, w { 0, 0, 0 }; u[(ax + 1) % 3] = 1; w[(ax + 2) % 3] = 1;
                    const auto pt = [&] (double a, double b)
                    {
                        const V3 q { n[0] + u[0] * a + w[0] * b, n[1] + u[1] * a + w[1] * b, n[2] + u[2] * a + w[2] * b };
                        return add3 (c, R ({ q[0] * h[0], q[1] * h[1], q[2] * h[2] }));
                    };
                    const V3 p0 = pt (-1, -1), p1 = pt (1, -1), p2 = pt (1, 1), p3 = pt (-1, 1), nn = R (n);
                    if (dot3 (cross3 (sub3 (p1, p0), sub3 (p2, p0)), nn) < 0) { tri (p0, p2, p1, nn, z, z, z, mat, 0); tri (p0, p3, p2, nn, z, z, z, mat, 0); }
                    else { tri (p0, p1, p2, nn, z, z, z, mat, 0); tri (p0, p2, p3, nn, z, z, z, mat, 0); }
                }
        }
        /** A paraboloid shell (both faces) of radius R and depth along its axis, segs round. */
        void dish (const V3& c, const V3& axis, double R, double depth, int segs, double mat = 4)
        {
            const V3 u = norm3 (axis), ref = std::abs (u[1]) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 }, vv = norm3 (cross3 (u, ref)), w = cross3 (u, vv), z { 0, 0, 0 };
            const auto pt = [&] (double r, double ang) { return add3 (add3 (add3 (c, mul3 (vv, r * Js::cos (ang))), mul3 (w, r * Js::sin (ang))), mul3 (u, depth * (r / R) * (r / R))); };
            const std::array<double, 3> rings { 0, R * 0.5, R };
            for (int sI = 0; sI < segs; ++sI)
            {
                const double a0 = (double) sI / segs * 2 * PI, a1 = (double) (sI + 1) / segs * 2 * PI;
                for (size_t r = 0; r < 2; ++r)
                {
                    const V3 P0 = pt (rings[r], a0), P1 = pt (rings[r + 1], a0), P2 = pt (rings[r + 1], a1), P3 = pt (rings[r], a1);
                    const V3 n = norm3 (cross3 (sub3 (P1, P0), sub3 (P2, P0))), nn = mul3 (n, -1);
                    tri (P0, P1, P2, n, z, z, z, mat, 0); if (r > 0) tri (P0, P2, P3, n, z, z, z, mat, 0);
                    tri (P0, P2, P1, nn, z, z, z, mat, 0); if (r > 0) tri (P0, P3, P2, nn, z, z, z, mat, 0);
                }
            }
        }
        /** A vertical prism (a tank, a drum) of segs sides with its lid, or a cone (Rtop 0). */
        void prism (const V3& c, double R, double h, int segs, double mat, double Rtop)
        {
            const V3 z { 0, 0, 0 };
            for (int sI = 0; sI < segs; ++sI)
            {
                const double a0 = (double) sI / segs * 2 * PI, a1 = (double) (sI + 1) / segs * 2 * PI, am = (a0 + a1) / 2;
                const V3 P0 { c[0] + R * Js::cos (a0), c[1], c[2] + R * Js::sin (a0) }, P1 { c[0] + R * Js::cos (a1), c[1], c[2] + R * Js::sin (a1) };
                const V3 Q0 { c[0] + Rtop * Js::cos (a0), c[1] + h, c[2] + Rtop * Js::sin (a0) }, Q1 { c[0] + Rtop * Js::cos (a1), c[1] + h, c[2] + Rtop * Js::sin (a1) };
                const V3 n = norm3 ({ Js::cos (am) * h, R - Rtop, Js::sin (am) * h });
                tri (P0, Q1, P1, n, z, z, z, mat, 0); tri (P0, Q0, Q1, n, z, z, z, mat, 0);
                if (Rtop > 0) { const V3 T { c[0], c[1] + h, c[2] }; tri (Q0, T, Q1, { 0, 1, 0 }, z, z, z, mat, 0); }
            }
        }
        void prism (const V3& c, double R, double h, int segs, double mat = 0) { prism (c, R, h, segs, mat, R); }
        /** A beacon: a small lamp (flag 2), kept wide enough to see far off. */
        void lamp (const V3& at, double size) { bar (at, add3 (at, { 0, size, 0 }), size, 5, 2); }
        /** A wheel: a cylinder along z (its tread and its two sides), segs round. */
        void wheel (const V3& c, double R, double w, int segs, double mat = 3)
        {
            const V3 z { 0, 0, 0 };
            for (int sI = 0; sI < segs; ++sI)
            {
                const double a0 = (double) sI / segs * 2 * PI, a1 = (double) (sI + 1) / segs * 2 * PI, am = (a0 + a1) / 2;
                const auto P = [&] (double a, double sz) { return V3 { c[0] + R * Js::cos (a), c[1] + R * Js::sin (a), c[2] + sz * w / 2 }; };
                const V3 n { Js::cos (am), Js::sin (am), 0 };
                tri (P (a0, -1), P (a1, -1), P (a1, 1), n, z, z, z, mat, 0); tri (P (a0, -1), P (a1, 1), P (a0, 1), n, z, z, z, mat, 0);
                for (const double sz : { -1.0, 1.0 })
                {
                    const V3 C { c[0], c[1], c[2] + sz * w / 2 };
                    if (sz > 0) tri (C, P (a0, 1), P (a1, 1), { 0, 0, 1 }, z, z, z, mat, 0); else tri (C, P (a1, -1), P (a0, -1), { 0, 0, -1 }, z, z, z, mat, 0);
                }
            }
        }
        /** A sulphur mound over a buried module (the base's stand-in). */
        void dome (const V3& c, double R, double H, double mat = 7)
        {
            const int n = 6;
            for (int k = 0; k < n; ++k)
            {
                const double y0 = H * k / n, y1 = H * (k + 1) / n, r0 = R * std::sqrt (1 - sq ((double) k / n)), r1 = R * std::sqrt (1 - sq ((double) (k + 1) / n));
                prism ({ c[0], c[1] + y0, c[2] }, r0, y1 - y0, 16, mat, jsMax (r1, 0.0));
            }
        }
    };
}

std::vector<std::vector<float>> IoWorld::structureMeshes()
{
    std::vector<std::vector<double>> out;
    const std::array<V2, 4> corners { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } };
    const std::array<std::array<V2, 2>, 4> faces { { { { { -1, -1 }, { 1, -1 } } }, { { { 1, -1 }, { 1, 1 } } }, { { { 1, 1 }, { -1, 1 } } }, { { { -1, 1 }, { -1, -1 } } } } };
    {   // 0: the lattice mast
        Builder m; const double H = 22, b0 = 1.5, b1 = 0.5; const int nl = 5;
        const auto at = [&] (double h, double sx, double sz) { const double b = b0 + (b1 - b0) * (h / H); return V3 { sx * b, h, sz * b }; };
        for (const auto& [sx, sz] : corners) m.bar (at (0, sx, sz), at (H, sx, sz), 0.24);
        for (int l = 0; l <= nl; ++l)
        {
            const double h = H * l / nl;
            for (const auto& [p, q] : faces) m.bar (at (h, p[0], p[1]), at (h, q[0], q[1]), 0.13, 0, (l == 0 || l == nl) ? 0 : 1);
            if (l < nl) { const double h2 = H * (l + 1) / nl; for (const auto& [p, q] : faces) { m.bar (at (h, p[0], p[1]), at (h2, q[0], q[1]), 0.09, 0, 1); m.bar (at (h2, p[0], p[1]), at (h, q[0], q[1]), 0.09, 0, 1); } }
        }
        m.bar ({ 0, H, 0 }, { 0, H + 4.0, 0 }, 0.15); m.bar ({ -0.9, H + 2.0, 0 }, { 0.9, H + 2.0, 0 }, 0.09, 0, 1);
        m.box ({ 0, 0.9, b0 + 0.9 }, { 0.55, 0.9, 0.4 }, 1);
        m.dish ({ 0, H * 0.72, b0 * 0.85 }, { 0, 0.25, 1 }, 1.2, 0.33, 10);
        m.lamp ({ 0, H + 4.0, 0 }, 0.32);
        out.push_back (m.v);
    }
    {   // 1: a dish on a tripod
        Builder m; const double H = 5.5;
        for (int k = 0; k < 3; ++k)
        {
            const double a = k / 3.0 * 2 * PI, a2 = (k + 1) / 3.0 * 2 * PI;
            m.bar ({ 2.2 * Js::cos (a), 0, 2.2 * Js::sin (a) }, { 0, H, 0 }, 0.22);
            m.bar ({ 1.1 * Js::cos (a), H / 2, 1.1 * Js::sin (a) }, { 1.1 * Js::cos (a2), H / 2, 1.1 * Js::sin (a2) }, 0.10, 0, 1);
        }
        m.bar ({ 0, H, 0 }, { 0, H + 1.3, 0 }, 0.24);
        const V3 ax = norm3 ({ 0.15, 0.62, 0.77 }), feed { ax[0] * 2.5, H + 1.3 + ax[1] * 2.5, ax[2] * 2.5 };
        m.dish ({ 0, H + 1.3, 0 }, ax, 3.4, 0.95, 14);
        m.bar ({ 0, H + 1.3, 0 }, feed, 0.10, 0, 1);
        {
            const V3 vv = norm3 (cross3 (ax, { 0, 1, 0 })), w = cross3 (ax, vv);
            for (int k = 0; k < 3; ++k)
            {
                const double a = k / 3.0 * 2 * PI + 0.6, r = 3.2;
                m.bar ({ vv[0] * r * Js::cos (a) + w[0] * r * Js::sin (a) + ax[0] * 0.85, H + 1.3 + vv[1] * r * Js::cos (a) + w[1] * r * Js::sin (a) + ax[1] * 0.85,
                         vv[2] * r * Js::cos (a) + w[2] * r * Js::sin (a) + ax[2] * 0.85 }, feed, 0.05, 0, 1);
            }
        }
        m.box ({ 1.6, 0.45, -1.4 }, { 0.5, 0.45, 0.35 }, 3, 0.4);
        out.push_back (m.v);
    }
    {   // 2: the frame module
        Builder m; const double S = 4.2, H0 = 1.1, Ht = 4.0;
        const auto c = [&] (double sx, double h, double sz) { return V3 { sx * S / 2, h, sz * S / 2 }; };
        for (const auto& [sx, sz] : corners) m.bar (c (sx, 0, sz), c (sx, H0 + Ht, sz), 0.22);
        for (const double h : { H0, H0 + Ht }) for (const auto& [p, q] : faces) m.bar (c (p[0], h, p[1]), c (q[0], h, q[1]), 0.18);
        m.bar (c (-1, H0, -1), c (1, H0 + Ht, -1), 0.10, 0, 1); m.bar (c (1, H0, 1), c (-1, H0 + Ht, 1), 0.10, 0, 1);
        m.prism ({ 0, H0 + 0.1, 0 }, 1.3, Ht - 0.4, 10, 1);
        m.bar (c (1, H0 + Ht, 0), { S / 2 + 2.0, H0 + Ht + 1.5, 0 }, 0.12, 0, 1);
        m.box ({ -S / 2 - 0.08, H0 + Ht * 0.55, 0 }, { 0.04, Ht * 0.42, S * 0.42 }, 3);
        out.push_back (m.v);
    }
    {   // 3: the old lander
        Builder m; const double D = 1.25;
        for (int k = 0; k < 4; ++k)
        {
            const double a = (k + 0.5) / 4 * 2 * PI, ca = Js::cos (a), sa = Js::sin (a);
            const V3 top { ca * 1.2, D, sa * 1.2 }, foot { ca * 2.6, 0.12, sa * 2.6 };
            m.bar (top, foot, 0.16); m.bar ({ ca * 1.0, D - 0.55, sa * 1.0 }, foot, 0.10, 0, 1);
            m.prism ({ foot[0], 0, foot[2] }, 0.42, 0.12, 8, 0);
        }
        m.prism ({ 0, D - 0.65, 0 }, 1.0, 0.7, 6, 2);
        m.prism ({ 0, D, 0 }, 1.55, 0.18, 6, 1);
        m.box ({ 0, D + 0.18 + 0.55, 0 }, { 0.75, 0.55, 0.62 }, 3);
        m.box ({ 0.85, D + 0.18 + 0.35, 0.65 }, { 0.28, 0.35, 0.22 }, 2);
        m.bar ({ -0.9, D + 0.18, -0.4 }, { -0.9, D + 2.4, -0.4 }, 0.10);
        m.box ({ -0.9, D + 2.5, -0.4 }, { 0.16, 0.10, 0.12 }, 1);
        m.bar ({ 0.4, D + 0.18, -0.9 }, { 0.4, D + 1.5, -0.9 }, 0.12);
        m.dish ({ 0.4, D + 1.5, -0.9 }, norm3 ({ 0.2, 0.7, -0.68 }), 0.95, 0.24, 12);
        out.push_back (m.v);
    }
    {   // 4: the volcano-monitoring station
        Builder m; const double H = 7.0;
        m.bar ({ 0, 0, 0 }, { 0, H, 0 }, 0.16);
        for (const V2& q : std::initializer_list<V2> { { 1, 0 }, { -0.5, 0.87 }, { -0.5, -0.87 } }) m.bar ({ q[0] * 2.2, 0, q[1] * 2.2 }, { 0, H * 0.62, 0 }, 0.06, 0, 1);
        m.bar ({ 0, H - 0.6, 0 }, { 2.1, H - 0.6, 0 }, 0.08, 0, 1);
        m.box ({ 2.1, H - 0.6, 0 }, { 0.18, 0.12, 0.12 }, 1);
        m.box ({ 0, H - 1.7, 0.22 }, { 0.22, 0.30, 0.10 }, 2);
        m.dish ({ 0, H * 0.5, -0.35 }, norm3 ({ 0.0, 0.45, -0.9 }), 0.55, 0.14, 10);
        m.box ({ 1.6, 0.18, 1.3 }, { 0.9, 0.18, 0.6 }, 0, 0.3);
        m.prism ({ 1.6, 0.36, 1.3 }, 0.32, 0.95, 10, 3);
        for (int k = 0; k < 8; ++k) { const double a = k / 8.0 * 2 * PI; m.box ({ 1.6 + Js::cos (a) * 0.42, 0.85, 1.3 + Js::sin (a) * 0.42 }, { 0.02, 0.42, 0.14 }, 3, -a); }
        m.prism ({ -1.4, 0, 1.6 }, 0.45, 0.25, 10, 1); m.prism ({ -1.4, 0.25, 1.6 }, 0.45, 0.24, 10, 1, 0.0);
        m.bar ({ 0, 0.05, 0 }, { -1.4, 0.05, 1.6 }, 0.04, 3, 1);
        m.lamp ({ 0, H, 0 }, 0.22);
        out.push_back (m.v);
    }
    {   // 5: the abandoned rover, turned so its long axis runs along z, its nose to +z
        Builder m; const double Rw = 0.55, Hb = 1.45;
        for (const double x : { -2.1, 0.0, 2.1 }) for (const double zs : { -1.0, 1.0 })
        {
            m.wheel ({ x, Rw, zs * 1.45 }, Rw, 0.42, 14, 3);
            m.bar ({ x, Rw, zs * 1.2 }, { x * 0.55, Hb - 0.25, zs * 1.05 }, 0.12, 0);
        }
        for (const double zs : { -1.0, 1.0 }) m.bar ({ -2.1, Rw + 0.35, zs * 1.22 }, { 2.1, Rw + 0.35, zs * 1.22 }, 0.14, 0);
        m.box ({ 0, Hb, 0 }, { 2.45, 0.42, 1.0 }, 1);
        m.box ({ 0.3, Hb + 0.48, 0 }, { 1.6, 0.08, 0.95 }, 1);
        m.box ({ -0.9, Hb + 0.75, 0.45 }, { 0.55, 0.25, 0.32 }, 2);
        m.prism ({ -2.85, Hb - 0.25, 0 }, 0.32, 0.95, 10, 3);
        for (int k = 0; k < 8; ++k) { const double a = k / 8.0 * 2 * PI; m.box ({ -2.85 + Js::cos (a) * 0.42, Hb + 0.2, Js::sin (a) * 0.42 }, { 0.02, 0.42, 0.14 }, 3, -a); }
        m.bar ({ 1.9, Hb + 0.4, 0.62 }, { 1.9, Hb + 2.35, 0.62 }, 0.12, 0);
        m.box ({ 1.9, Hb + 2.5, 0.62 }, { 0.16, 0.14, 0.34 }, 1);
        m.bar ({ -0.3, Hb + 0.55, -0.55 }, { -0.3, Hb + 1.05, -0.55 }, 0.1, 0);
        m.dish ({ -0.3, Hb + 1.05, -0.55 }, norm3 ({ 0.35, 0.8, -0.3 }), 0.62, 0.17, 12);
        m.bar ({ 2.45, Hb - 0.1, -0.5 }, { 2.95, Hb + 0.35, -0.5 }, 0.11, 0); m.bar ({ 2.95, Hb + 0.35, -0.5 }, { 2.55, Hb + 0.65, -0.2 }, 0.09, 0);
        m.box ({ 2.55, Hb + 0.7, -0.2 }, { 0.14, 0.1, 0.14 }, 0);
        auto& v = m.v;
        for (size_t i = 0; i < v.size(); i += 11) for (const size_t o : { 0, 3, 6 }) { const double x = v[i + o], z = v[i + o + 2]; v[i + o] = -z; v[i + o + 2] = x; }
        out.push_back (v);
    }
    {   // 6: the buried base
        Builder m;
        m.dome ({ -14, 0, -6 }, 20, 11); m.dome ({ 16, 0, -12 }, 15, 9); m.dome ({ 22, 0, 14 }, 10, 6); m.dome ({ -32, 0, 14 }, 9, 5.5);
        for (const auto& [x, z, h, r] : std::initializer_list<std::array<double, 4>> { { -14, 13, 3.4, 0 }, { 10, 2, 3.0, 0.3 }, { 24, 23, 2.4, 0 }, { -32, 22, 2.2, 0 } })
        {
            m.box ({ x, h, z - 1.5 }, { 3.2, h, 3.2 }, 1, r); m.box ({ x, 1.6, z + 2.3 }, { 1.4, 1.6, 1.2 }, 1, r);
            m.box ({ x, 1.4, z + 3.55 }, { 0.9, 1.2, 0.05 }, 3, r); m.box ({ x, 3.6, z + 3.4 }, { 0.3, 0.14, 0.14 }, 6, r);
        }
        m.box ({ -25, 0.6, 15 }, { 3.6, 0.6, 7.0 }, 0); m.box ({ -25, 1.6, 9 }, { 3.2, 1.8, 0.4 }, 3);
        m.box ({ -25, 3.6, 9.3 }, { 0.5, 0.15, 0.15 }, 6);
        m.bar ({ 4, 0, -2 }, { 4, 16, -2 }, 0.3); m.dish ({ 4, 14, -2 }, { 0.3, 0.8, 0.5 }, 1.6, 0.4, 12); m.lamp ({ 4, 16, -2 }, 0.3);
        for (int k = 0; k < 3; ++k) m.prism ({ 30 + k * 5.5, 0, -24 }, 2.2, 5, 10, 1);
        out.push_back (m.v);
    }
    {   // 7: the lava-tube skylight base
        Builder m; const double R = 12.5, H = 18;
        for (const double x : { -15.0, 15.0 }) { m.bar ({ x, 0, -6 }, { x, H, 0 }, 0.5); m.bar ({ x, 0, 6 }, { x, H, 0 }, 0.5); m.bar ({ x, H * 0.45, -3.3 }, { x, H * 0.45, 3.3 }, 0.25, 0, 1); }
        m.bar ({ -15, H, 0 }, { 15, H, 0 }, 0.7); m.bar ({ -15, H - 1.6, 0 }, { 15, H - 1.6, 0 }, 0.4);
        for (int i = 0; i < 6; ++i) { const double x0 = -15 + 5 * i, x1 = x0 + 5; m.bar ({ x0, H, 0 }, { x1, H - 1.6, 0 }, 0.18, 0, 1); }
        m.box ({ 2, H - 2.3, 0 }, { 1.2, 0.7, 1.0 }, 1); m.bar ({ 2, H - 3, 0 }, { 2, -18, 0 }, 0.08, 0, 1); m.box ({ 2, -18.5, 0 }, { 1.0, 1.2, 1.0 }, 0);
        for (int k = 0; k < 24; ++k)
        {
            const double a0 = 0.4 + k * 0.17, a1 = a0 + 0.17, y0 = -k, y1 = -(k + 1), r = R - 1.6;
            m.bar ({ Js::cos (a0) * r, y0, Js::sin (a0) * r }, { Js::cos (a1) * r, y1, Js::sin (a1) * r }, 1.2, 0);
            if (k % 4 == 0) m.box ({ Js::cos (a0) * (r - 0.4), y0 + 1.6, Js::sin (a0) * (r - 0.4) }, { 0.2, 0.12, 0.2 }, 6);
        }
        m.box ({ 0, 1.6, 17 }, { 4, 1.6, 3 }, 1); m.box ({ 0, 3.4, 17 }, { 4.4, 0.2, 3.4 }, 3); m.box ({ -4.1, 1.2, 17 }, { 0.05, 0.8, 0.6 }, 6);
        for (const V2& q : std::initializer_list<V2> { { 0, 0 }, { -6, 4 }, { 5, -5 }, { 3, 7 } }) m.box ({ q[0], -23.6, q[1] }, { 0.6, 0.35, 0.6 }, 6);
        m.prism ({ -4, -24, -3 }, 2.5, 2.6, 8, 1); m.box ({ 6, -23, 2 }, { 2.5, 1.0, 1.5 }, 0);
        out.push_back (m.v);
    }
    {   // 8: the lava-lake observatory
        Builder m;
        m.box ({ 0, 2.4, 2 }, { 7, 2.4, 5 }, 1); m.prism ({ 0, 0, 2 }, 9.5, 3.4, 8, 7, 7.2);
        m.box ({ 0, 5.2, 2 }, { 3, 0.4, 3 }, 3); m.box ({ 0, 2.0, 7.05 }, { 1.2, 1.6, 0.05 }, 3); m.box ({ 0, 4.2, 7.0 }, { 0.4, 0.15, 0.15 }, 6);
        const V3 tip { 0, 9, -25 };
        for (const double x : { -0.9, 0.9 }) { m.bar ({ x, 5.6, -2 }, { x * 0.5, tip[1], tip[2] }, 0.28); m.bar ({ x, 4.0, -2 }, { x * 0.5, tip[1] - 1.2, tip[2] }, 0.22); }
        for (int i = 0; i < 8; ++i)
        {
            const double t0 = i / 8.0, t1 = (i + 1) / 8.0, z0 = -2 + (tip[2] + 2) * t0, z1 = -2 + (tip[2] + 2) * t1, y0 = 5.6 + (tip[1] - 5.6) * t0, y1 = 5.6 + (tip[1] - 5.6) * t1;
            m.bar ({ 0, y0, z0 }, { 0, y1 - 1.3, z1 }, 0.12, 0, 1);
        }
        m.bar ({ 0, 5.6, 2 }, { 0, 8.5, 6 }, 0.2); m.box ({ 0, 8.5, 6.5 }, { 1.2, 1.0, 1.0 }, 3);
        m.bar (tip, { 0, tip[1] - 8, tip[2] }, 0.05, 0, 1); m.box ({ 0, tip[1] - 8.6, tip[2] }, { 0.5, 0.6, 0.5 }, 0); m.box ({ 0, tip[1] - 9.3, tip[2] }, { 0.2, 0.15, 0.2 }, 6);
        m.bar ({ 4, 4.8, 4 }, { 4, 13, 4 }, 0.15); m.lamp ({ 4, 13, 4 }, 0.25);
        out.push_back (m.v);
    }
    {   // 9: the SO2 plant
        Builder m;
        m.box ({ -8, 6, -4 }, { 15, 6, 8 }, 1); m.box ({ -14, 13.5, -6 }, { 6, 1.5, 4 }, 1); m.box ({ -8, 12.2, -4 }, { 15.4, 0.25, 8.4 }, 3);
        m.box ({ -8, 3, 4.05 }, { 3.5, 3, 0.06 }, 3); m.box ({ -8, 6.6, 4.0 }, { 0.6, 0.15, 0.15 }, 6);
        for (int k = 0; k < 4; ++k)
        {
            m.prism ({ 12.0 + (k % 2) * 8, 0, -12.0 + std::floor (k / 2.0) * 9 }, 3.3, 9, 12, 1);
            m.bar ({ 7, 7, -8 }, { 12.0 + (k % 2) * 8, 7.5, -12.0 + std::floor (k / 2.0) * 9 }, 0.3, 0, 1);
        }
        m.box ({ 22, 3, 12 }, { 3, 3, 3 }, 0); m.prism ({ 22, 6, 12 }, 3.4, 2.4, 4, 0, 1.2);
        for (const double z : { 11.2, 12.8 }) m.bar ({ 22, 7, z }, { 6, 12, z - 8 }, 0.3);
        for (int i = 0; i < 8; ++i) { const double t = i / 8.0, t2 = (i + 1) / 8.0; m.bar ({ 22 - 16 * t, 7 + 5 * t, 12 - 8 * t }, { 22 - 16 * t2, 7 + 5 * t2, 12 - 8 * t2 + 1.6 }, 0.1, 0, 1); }
        const V3 ex { -26, 0, 12 };
        m.box ({ ex[0], 0.8, ex[2] - 1.6 }, { 3.2, 0.8, 0.6 }, 3); m.box ({ ex[0], 0.8, ex[2] + 1.6 }, { 3.2, 0.8, 0.6 }, 3);
        m.box ({ ex[0], 2.3, ex[2] }, { 2.6, 0.7, 2.0 }, 1); m.box ({ ex[0] + 1.2, 3.8, ex[2] + 0.9 }, { 1.0, 0.8, 0.9 }, 1);
        m.bar ({ ex[0] + 2, 3, ex[2] }, { ex[0] + 6.5, 8, ex[2] }, 0.6); m.bar ({ ex[0] + 6.5, 8, ex[2] }, { ex[0] + 8.5, 4, ex[2] }, 0.45);
        m.box ({ ex[0] + 8.6, 3.3, ex[2] }, { 0.9, 0.7, 1.1 }, 3);
        m.bar ({ 10, 0, 8 }, { 10, 14, 8 }, 0.25); m.lamp ({ 10, 14, 8 }, 0.3);
        out.push_back (m.v);
    }
    std::vector<std::vector<float>> f;
    for (const auto& v : out) { std::vector<float> o (v.size()); for (size_t i = 0; i < v.size(); ++i) o[i] = f32 (v[i]); f.push_back (std::move (o)); }
    return f;
}

//==============================================================================
// THE FINDS' FILES

namespace
{
    uint32_t u32le (const uint8_t* b) noexcept { return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24); }
    bool jsSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
    std::string trim (std::string t)
    {
        size_t a = 0, b = t.size();
        while (a < b && jsSpace (t[a])) ++a;
        while (b > a && jsSpace (t[b - 1])) --b;
        return t.substr (a, b - a);
    }
}

IoWorld::MeshFile IoWorld::readMesh (const uint8_t* b, size_t size)
{
    MeshFile m;
    if (b == nullptr || size < 16) return m;
    m.nv = u32le (b + 8); m.ni = u32le (b + 12);
    if (std::memcmp (b, "CBMESH1", 7) != 0 || (uint64_t) size < 16 + (uint64_t) m.nv * 40 + (uint64_t) m.ni * 4) return m;
    m.ok = true; m.vertexAt = 16; m.indexAt = 16 + (size_t) m.nv * 40;
    return m;
}
int IoWorld::roleOf (std::string_view folder)
{
    std::string n (folder);
    for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
    for (size_t i = 0; i < variants.size(); ++i) if (n.rfind (variants[i], 0) == 0) return (int) i;
    return -1;
}
std::optional<std::string> IoWorld::creditOf (std::string_view text)
{
    static const std::regex re (R"(credit:?\s*([^\n]*))");
    std::match_results<std::string_view::const_iterator> m;
    if (! std::regex_search (text.begin(), text.end(), m, re)) return {};
    const std::string c = trim (std::string (m[1].first, m[1].second));
    std::string low = c.substr (0, 5);
    for (auto& ch : low) ch = (char) std::tolower ((unsigned char) ch);
    const bool none = low.rfind ("none", 0) == 0 && (low.size() == 4 || ! (std::isalnum ((unsigned char) low[4]) || low[4] == '_'));
    if (none) return {};
    return c;
}
std::optional<double> IoWorld::pitRadiusOf (std::string_view text)
{
    static const std::regex re (R"((?:pit|radius)\s*[:=]?\s*([\d.]+))", std::regex::ECMAScript | std::regex::icase);
    std::match_results<std::string_view::const_iterator> m;
    if (! std::regex_search (text.begin(), text.end(), m, re)) return {};
    const std::string w (m[1].first, m[1].second);
    char* end = nullptr;
    const double v = std::strtod (w.c_str(), &end);
    if (end != w.c_str() + w.size()) return std::numeric_limits<double>::quiet_NaN();   // Number ("1.2.") is NaN: setPitRadius ignores it
    return v;
}

} // namespace orbitamp::io
