// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// MetalWorld's meshes and readers: metal-world.js's propBuilder, propMeshes and hillMesh, line for line, and what
// metal-gl.js makes of the finds' files (the CBMESH1 meshes, the dragon's CBANIM1 poses, the credit lines, the
// headstock boxes, guitars.txt and how its guitars are dealt round the groves, banners.txt, the letters' table and
// atlas) and of the cabin's plate (glassBounds).

#include "MetalWorld.h"
#include "MetalJs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <regex>
#include <tuple>
#include <utility>

namespace orbitamp::metal
{

using namespace js;
using V2 = MetalWorld::V2;
using V3 = MetalWorld::V3;

namespace
{
    V3 norm (const V3& a) noexcept { const double l = orOne (Js::hypot (a[0], a[1], a[2])); return { a[0] / l, a[1] / l, a[2] / l }; }
    V3 cross (const V3& a, const V3& b) noexcept { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }

    // The JS's text: String.prototype.trim, split (/\s+/) and Number.
    bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
    std::string trim (std::string_view t)
    {
        size_t a = 0, b = t.size();
        while (a < b && isSpace (t[a])) ++a;
        while (b > a && isSpace (t[b - 1])) --b;
        return std::string (t.substr (a, b - a));
    }
    std::vector<std::string> lines (std::string_view t)
    {
        std::vector<std::string> o;
        size_t a = 0;
        for (;;)
        {
            const size_t e = t.find ('\n', a);
            o.emplace_back (t.substr (a, e == std::string_view::npos ? std::string_view::npos : e - a));
            if (e == std::string_view::npos) break;
            a = e + 1;
        }
        return o;
    }
    /** trim ().split (/\s+/): one empty word for a blank line. */
    std::vector<std::string> words (std::string_view line)
    {
        const std::string t = trim (line);
        std::vector<std::string> o;
        size_t a = 0;
        while (true)
        {
            size_t e = a;
            while (e < t.size() && ! isSpace (t[e])) ++e;
            o.push_back (t.substr (a, e - a));
            if (e >= t.size()) break;
            a = e;
            while (a < t.size() && isSpace (t[a])) ++a;
        }
        return o;
    }
    double number (const std::string& w)
    {
        const std::string t = trim (w);
        if (t.empty()) return 0;
        static const std::regex dec (R"(^[+-]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?$)");
        static const std::regex hex (R"(^0[xX][0-9a-fA-F]+$)");
        if (std::regex_match (t, dec)) return std::strtod (t.c_str(), nullptr);
        if (std::regex_match (t, hex)) return (double) std::strtoull (t.c_str() + 2, nullptr, 16);
        if (t == "Infinity" || t == "+Infinity") return std::numeric_limits<double>::infinity();
        if (t == "-Infinity") return -std::numeric_limits<double>::infinity();
        return nan;
    }
    std::string lower (std::string t) { for (auto& c : t) c = (char) std::tolower ((unsigned char) c); return t; }
    bool endsWith (const std::string& t, const std::string& e) { return t.size() >= e.size() && t.compare (t.size() - e.size(), e.size(), e) == 0; }
    uint32_t u32le (const uint8_t* p) noexcept { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    float f32le (const uint8_t* p) noexcept { const uint32_t u = u32le (p); float f; std::memcpy (&f, &u, 4); return f; }
    int16_t i16le (const uint8_t* p) noexcept { return (int16_t) (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8)); }
}

//==============================================================================
// THE MESHES (procedural, until the owner's come): 7 floats a vertex (position, normal, material), y up, metres.

uint32_t MetalWorld::Builder::vert (const V3& p, const V3& n, double m)
{
    v.insert (v.end(), { p[0], p[1], p[2], n[0], n[1], n[2], m });
    return (uint32_t) (v.size() / 7 - 1);
}
void MetalWorld::Builder::tube (const std::vector<V3>& pts, int sides, const std::vector<double>& radii, double m, bool cap, double rot)
{
    const int np = (int) pts.size();
    std::vector<std::vector<uint32_t>> rings;
    for (int k = 0; k < np; ++k)
    {
        const V3 p = pts[(size_t) k], q = pts[(size_t) std::min (k + 1, np - 1)], o = pts[(size_t) std::max (k - 1, 0)];
        const V3 d = norm ({ q[0] - o[0], q[1] - o[1], q[2] - o[2] });
        const V3 h = std::abs (d[1]) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
        const V3 s1 = norm (cross (d, h)), s2 = cross (d, s1);
        std::vector<uint32_t> ring;
        for (int i = 0; i < sides; ++i)
        {
            const double a = (double) i / sides * 2 * pi + rot, ca = Js::cos (a), sa = Js::sin (a);
            const V3 n { s1[0] * ca + s2[0] * sa, s1[1] * ca + s2[1] * sa, s1[2] * ca + s2[2] * sa };
            const double r = radii[(size_t) k];
            ring.push_back (vert ({ p[0] + n[0] * r, p[1] + n[1] * r, p[2] + n[2] * r }, n, m));
        }
        rings.push_back (ring);
    }
    for (size_t k = 0; k + 1 < rings.size(); ++k)
        for (int i = 0; i < sides; ++i)
        {
            const uint32_t a = rings[k][(size_t) i], b = rings[k][(size_t) ((i + 1) % sides)], c = rings[k + 1][(size_t) ((i + 1) % sides)], d = rings[k + 1][(size_t) i];
            idx.insert (idx.end(), { a, b, c, a, c, d });
        }
    if (cap)
        for (const auto& [k, sg] : { std::pair<int, int> { 0, -1 }, std::pair<int, int> { np - 1, 1 } })
        {
            const V3 p = pts[(size_t) k], q = pts[(size_t) std::max (0, std::min (np - 1, k - sg))];
            const V3 n = norm ({ p[0] - q[0], p[1] - q[1], p[2] - q[2] });
            const uint32_t c = vert (p, n, m);
            std::vector<uint32_t> ring;
            for (int i = 0; i < sides; ++i) { const size_t o = (size_t) rings[(size_t) k][(size_t) i] * 7; ring.push_back (vert ({ v[o], v[o + 1], v[o + 2] }, n, m)); }
            for (int i = 0; i < sides; ++i) idx.insert (idx.end(), { c, ring[(size_t) i], ring[(size_t) ((i + 1) % sides)] });
        }
}
void MetalWorld::Builder::boxFaces (const V3& c, const V3& h, const double* m, bool perFace, double yaw, double tilt)
{
    const double cy = Js::cos (yaw), sy = Js::sin (yaw), ct = Js::cos (tilt), st = Js::sin (tilt);
    const auto tr = [&] (const V3& p) { const double y1 = p[1] * ct - p[2] * st, z1 = p[1] * st + p[2] * ct; return V3 { cy * p[0] + sy * z1, y1, -sy * p[0] + cy * z1 }; };
    const int faces[6][2] = { { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { 2, 1 }, { 2, -1 } };
    for (int fi = 0; fi < 6; ++fi)
    {
        const int ax = faces[fi][0];
        const double sg = faces[fi][1];
        V3 n { 0, 0, 0 }, u { 0, 0, 0 }, w { 0, 0, 0 };
        n[(size_t) ax] = sg; u[(size_t) ((ax + 1) % 3)] = 1; w[(size_t) ((ax + 2) % 3)] = 1;
        const double mm = perFace ? m[fi] : m[0];
        uint32_t base[4];
        const double corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int q = 0; q < 4; ++q)
        {
            const double a = corners[q][0], b = corners[q][1];
            V3 p {};
            for (size_t k = 0; k < 3; ++k) p[k] = n[k] * h[k] + u[k] * a * h[k] + w[k] * b * h[k];
            const V3 t = tr (p);
            base[q] = vert ({ c[0] + t[0], c[1] + t[1], c[2] + t[2] }, tr (n), mm);
        }
        idx.insert (idx.end(), { base[0], base[1], base[2], base[0], base[2], base[3] });
    }
}
void MetalWorld::Builder::box (const V3& c, const V3& h, double m, double yaw, double tilt) { boxFaces (c, h, &m, false, yaw, tilt); }
void MetalWorld::Builder::box (const V3& c, const V3& h, const std::array<double, 6>& m, double yaw, double tilt) { boxFaces (c, h, m.data(), true, yaw, tilt); }
void MetalWorld::Builder::blob (const V3& c, const V3& r, double m, int rings, int sides)
{
    const uint32_t base = (uint32_t) (v.size() / 7);
    for (int j = 0; j <= rings; ++j)
    {
        const double ph = (double) j / rings * pi, sp = Js::sin (ph), cp = Js::cos (ph);
        for (int i = 0; i <= sides; ++i)
        {
            const double a = (double) i / sides * 2 * pi;
            const V3 n { sp * Js::cos (a), cp, sp * Js::sin (a) };
            vert ({ c[0] + n[0] * r[0], c[1] + n[1] * r[1], c[2] + n[2] * r[2] }, norm ({ n[0] / r[0], n[1] / r[1], n[2] / r[2] }), m);
        }
    }
    for (int j = 0; j < rings; ++j)
        for (int i = 0; i < sides; ++i)
        {
            const uint32_t a = base + (uint32_t) (j * (sides + 1) + i), b = a + 1, c2 = a + (uint32_t) sides + 1, d = c2 + 1;
            idx.insert (idx.end(), { a, c2, b, b, c2, d });
        }
}
void MetalWorld::Builder::torus (const V3& c, double R, double r, double m, int seg, int sides)
{
    std::vector<V3> pts;
    for (int k = 0; k <= seg; ++k) { const double a = (double) k / seg * 2 * pi; pts.push_back ({ c[0] + R * Js::cos (a), c[1], c[2] + R * Js::sin (a) }); }
    tube (pts, sides, std::vector<double> (pts.size(), r), m);
}
MetalWorld::Mesh MetalWorld::Builder::done() const
{
    Mesh o;
    o.v.reserve (v.size());
    for (const double x : v) o.v.push_back (f32 (x));
    o.idx = idx;
    if (! o.wide())
        for (auto& i : o.idx) i &= 0xffffu;          // a Uint16Array keeps the low 16 bits
    return o;
}

std::map<std::string, MetalWorld::Mesh> MetalWorld::propMeshes()
{
    std::map<std::string, Mesh> M;
    using R = std::vector<double>;
    // THE SKULL GATE (a stand-in: a steel arch over the road, a brow with two lava eyes, a jaw of teeth each side)
    {
        Builder m;
        for (const double sx : { -1.0, 1.0 })
        {
            m.box ({ sx * 11, 9, 0 }, { 2.6, 9, 3.2 }, 0);
            for (int k = 0; k < 4; ++k) m.box ({ sx * (8.2 - k * 0.05), 16.5 - k * 3.6, 2.2 }, { 0.5, 1.3, 0.5 }, 0);
        }
        m.blob ({ 0, 25, 0 }, { 15, 10, 9 }, 0, 10, 16);
        m.box ({ 0, 17.6, 0 }, { 13.6, 1.6, 4.2 }, 0);
        for (const double sx : { -1.0, 1.0 }) m.blob ({ sx * 5.2, 23.5, 7.2 }, { 3.2, 2.6, 1.6 }, 5, 6, 10);
        m.blob ({ 0, 20.6, 8.0 }, { 1.4, 1.6, 1.0 }, 1, 4, 6);
        M["gate"] = m.done();
    }
    // THE CITADEL: towers (octagonal, broken crenellated tops, a spire), walls with merlons, one breached
    for (int t = 0; t < 3; ++t)
    {
        Builder m;
        const double r = 5.0 + 0.6 * t, h = 50;
        m.tube ({ { 0, -3, 0 }, { 0, h * 0.5, 0 }, { 0, h, 0 } }, 8, R { r * 1.08, r, r * 0.94 }, 0, false, pi / 8);
        m.tube ({ { 0, h - 0.5, 0 }, { 0, h + 1.0, 0 } }, 8, R { r * 1.25, r * 1.25 }, 1, true, pi / 8);
        for (int k = 0; k < 8; ++k)
        {
            if ((k + t) % 5 == 3) continue;
            const double a = (k + 0.5) / 8 * 2 * pi, hh = 1.8 * (k % 3 == t % 3 ? 0.4 : 1);
            m.box ({ Js::cos (a) * r * 1.15, h + 1 + hh, Js::sin (a) * r * 1.15 }, { 1.1, hh, 0.6 }, 0, -a + pi / 2);
        }
        m.tube ({ { 0, h + 1, 0 }, { 0, h + 8, 0 }, { 0, h + 16, 0 } }, 8, R { r * 0.55, r * 0.22, 0.0 }, 0, false, pi / 8);
        for (const double y : { h * 0.3, h * 0.62 })
            for (int k = 0; k < 4; ++k) { const double a = (double) k / 4 * 2 * pi + t; m.box ({ Js::cos (a) * r * 0.98, y, Js::sin (a) * r * 0.98 }, { 0.35, 1.6, 0.2 }, 7, -a + pi / 2); }
        M["tower" + std::to_string (t)] = m.done();
    }
    for (const bool broken : { false, true })
    {
        Builder m;
        const double L = 30, H = 15;
        if (! broken) m.box ({ 0, H / 2 - 2, 0 }, { L / 2, H / 2 + 2, 1.8 }, 1);
        else { m.box ({ -L / 2 + 6, H / 2 - 2, 0 }, { 6, H / 2 + 2, 1.8 }, 1); m.box ({ L / 2 - 5, H / 2 - 4, 0 }, { 5, H / 2, 1.8 }, 1); m.box ({ 0, 1, 0 }, { 4, 3, 1.8 }, 1); }
        for (int k = 0; k < 10; ++k) { const double x = -L / 2 + 1.5 + k * 3; if (broken && std::abs (x) < 5) continue; m.box ({ x, H + 0.8 - (broken && x > 6 ? 2 : 0), 0 }, { 0.8, 1.0, 1.9 }, 0); }
        M[broken ? "wallb" : "wall"] = m.done();
    }
    // THE TESLA TOWER: a steel column, the coil's rings, a torus on top
    {
        Builder m;
        m.tube ({ { 0, -1, 0 }, { 0, 3, 0 } }, 10, R { 3.0, 2.2 }, 1, true);
        m.tube ({ { 0, 3, 0 }, { 0, 22, 0 } }, 10, R { 1.3, 1.1 }, 0, true);
        for (int k = 0; k < 26; ++k) m.torus ({ 0, 4 + k * 0.7, 0 }, 1.55 - k * 0.008, 0.18, 0, 14, 4);
        m.tube ({ { 0, 22, 0 }, { 0, 25, 0 } }, 8, R { 0.6, 0.6 }, 1);
        m.torus ({ 0, 26.4, 0 }, 3.4, 1.05, 0, 28, 10);
        m.blob ({ 0, 26.4, 0 }, { 1.4, 1.0, 1.4 }, 0, 5, 8);
        M["tesla"] = m.done();
    }
    // THE AMPLIFIER CABINETS: black tolex, the grille on the front (+z), steel corners, piping; a head with its pilot lamp
    for (const bool head : { false, true })
    {
        Builder m;
        const double w = 2.25, h = head ? 1.4 : 2.25, d = 1.3, y = head ? 1.4 : 2.3;
        m.box ({ 0, y, 0 }, { w, h, d }, std::array<double, 6> { 2, 2, 2, 2, head ? 2.0 : 3.0, 2 });
        if (head) { m.box ({ 0, y + 0.1, d + 0.01 }, { w * 0.86, h * 0.45, 0.02 }, 3); m.blob ({ w * 0.72, y - h * 0.62, d + 0.06 }, { 0.16, 0.16, 0.08 }, 4, 4, 6); }
        for (const double sx : { -1.0, 1.0 }) for (const double sy : { -1.0, 1.0 }) m.box ({ sx * (w - 0.18), y + sy * (h - 0.18), d - 0.12 }, { 0.24, 0.24, 0.16 }, 0);
        for (const double sy : { -1.0, 1.0 }) m.box ({ 0, y + sy * (h - 0.04), d + 0.02 }, { w - 0.4, 0.035, 0.03 }, 1);
        m.box ({ 0, y + h + 0.06, 0 }, { 0.5, 0.06, 0.08 }, 1);
        M[head ? "head" : "cab"] = m.done();
    }
    // THE TRILITHON (two uprights and a lintel), and one fallen; the altar stone
    {
        Builder m;
        for (const double sx : { -1.0, 1.0 }) m.box ({ sx * 3.4, 6, 0 }, { 1.6, 6.6, 1.1 }, 0, sx * 0.02);
        m.box ({ 0, 13.3, 0 }, { 5.4, 1.1, 1.2 }, 0);
        M["trilithon"] = m.done();
        Builder f;
        f.box ({ -3.4, 6, 0 }, { 1.6, 6.6, 1.1 }, 0, 0.03);
        f.box ({ 3.8, 4.4, 0.6 }, { 1.6, 6.6, 1.1 }, 0, 0.0, 0.0);
        f.box ({ 5.4, 1.2, 4.5 }, { 5.4, 1.1, 1.2 }, 0, 0.9, 0.1);
        M["trilithonF"] = f.done();
        Builder a;
        a.box ({ 0, 0.7, 0 }, { 3.6, 1.0, 1.8 }, 0);
        M["altar"] = a.done();
    }
    // THE ANVIL, the size of a house: a foot, a waist, the body, the face, the horn along -x
    {
        Builder m;
        m.box ({ 0, 0.9, 0 }, { 4.6, 1.2, 3.2 }, 1);
        m.box ({ 0, 2.6, 0 }, { 3.4, 0.6, 2.4 }, 1);
        m.box ({ 0, 4.2, 0 }, { 2.4, 1.2, 1.7 }, 0);
        m.box ({ 0.6, 6.6, 0 }, { 5.6, 1.4, 2.2 }, 0);
        m.box ({ 0.6, 8.1, 0 }, { 5.6, 0.15, 2.2 }, 0);
        m.tube ({ { -5, 6.9, 0 }, { -7.5, 7.2, 0 }, { -10.5, 7.8, 0 }, { -12.6, 8.3, 0 } }, 12, R { 2.0, 1.6, 0.9, 0.05 }, 0);
        m.box ({ 6.6, 7.0, 0 }, { 0.9, 1.0, 1.6 }, 0);
        M["anvil"] = m.done();
    }
    // DARWIN'S CAT, a stand-in: a barbarian cat in steel, 38 m
    {
        Builder m;
        const double k = 38.0 / 30;
        const auto S = [k] (const V3& p) { return V3 { p[0] * k, p[1] * k, p[2] * k }; };
        m.box (S ({ 0, 0.8, 0 }), S ({ 6, 0.8, 4 }), 6);
        for (const double sx : { -1.0, 1.0 }) m.tube ({ S ({ sx * 2.6, 1.6, 0 }), S ({ sx * 2.9, 6, 0.4 }), S ({ sx * 2.0, 10, 0 }) }, 10, R { 1.3 * k, 1.5 * k, 1.6 * k }, 0, true);
        m.blob (S ({ 0, 13.5, 0 }), S ({ 4.0, 4.8, 3.0 }), 0);
        m.blob (S ({ 0, 17.5, 0.2 }), S ({ 5.2, 4.2, 3.4 }), 0);
        for (const double sx : { -1.0, 1.0 }) m.blob (S ({ sx * 2.4, 18.6, 2.4 }), S ({ 2.2, 1.8, 1.2 }), 0, 6, 8);
        m.blob (S ({ 0, 23.2, 0.4 }), S ({ 3.0, 2.8, 2.8 }), 0);
        for (const double sx : { -1.0, 1.0 }) m.tube ({ S ({ sx * 1.8, 24.6, 0.2 }), S ({ sx * 2.6, 27.0, 0.0 }), S ({ sx * 2.9, 28.0, 0 }) }, 6, R { 0.9 * k, 0.5 * k, 0.0 }, 0);
        m.tube ({ S ({ 0, 23.4, 0.4 }), S ({ 0, 24.8, 0.4 }) }, 12, R { 3.25 * k, 3.1 * k }, 1, true);
        for (const double sx : { -1.0, 1.0 })
            m.tube ({ S ({ sx * 3.0, 25.4, 0.4 }), S ({ sx * 5.4, 26.6, 0.4 }), S ({ sx * 6.6, 29.0, 0.6 }), S ({ sx * 6.4, 31.4, 0.2 }) }, 8, R { 0.8 * k, 0.6 * k, 0.4 * k, 0.0 }, 0);
        m.blob (S ({ 0, 22.3, 3.0 }), S ({ 1.4, 1.0, 0.8 }), 0, 5, 8);
        for (const double sx : { -1.0, 1.0 }) m.blob (S ({ sx * 1.2, 23.6, 2.95 }), S ({ 0.55, 0.45, 0.25 }), 5, 4, 6);
        for (const double sx : { -1.0, 1.0 }) m.box (S ({ sx * 2.6, 13.5, -3.6 }), S ({ 3.0, 8.0, 0.4 }), 1, sx * 0.22, 0.12);
        m.tube ({ S ({ 4.8, 19.5, 0 }), S ({ 6.6, 23.5, 0.4 }), S ({ 7.2, 28.0, 0.6 }) }, 10, R { 1.4 * k, 1.2 * k, 1.0 * k }, 0, true);
        m.blob (S ({ 7.2, 28.6, 0.6 }), S ({ 1.3, 1.3, 1.3 }), 0, 5, 8);
        m.box (S ({ 7.3, 26.0, 0.9 }), S ({ 2.2, 3.0, 0.45 }), 1, 0, 0.0);
        m.box (S ({ 6.0, 24.4, 0.9 }), S ({ 0.6, 2.2, 0.4 }), 1, 0.6);
        m.box (S ({ 8.6, 24.4, 0.9 }), S ({ 0.6, 2.2, 0.4 }), 1, -0.6);
        m.tube ({ S ({ 7.3, 28.5, 0.9 }), S ({ 7.3, 37.0, 0.9 }) }, 6, R { 0.35 * k, 0.3 * k }, 0, true);
        m.box (S ({ 7.3, 37.6, 0.9 }), S ({ 0.6, 0.9, 0.3 }), 0);
        m.tube ({ S ({ -4.8, 19.5, 0.4 }), S ({ -6.6, 17.8, 3.4 }), S ({ -6.4, 20.0, 6.2 }) }, 10, R { 1.4 * k, 1.2 * k, 1.0 * k }, 0, true);
        m.blob (S ({ -6.4, 20.6, 6.4 }), S ({ 1.1, 1.1, 1.0 }), 0, 5, 8);
        for (const double sx : { -0.55, 0.55 }) m.tube ({ S ({ -6.4 + sx, 21.2, 6.4 }), S ({ -6.4 + sx * 1.3, 23.6, 6.6 }) }, 6, R { 0.36 * k, 0.26 * k }, 0, true);
        m.tube ({ S ({ 0, 11, -2.6 }), S ({ 0, 7, -6 }), S ({ 1.5, 4, -9 }), S ({ 3, 4.5, -11 }) }, 8, R { 0.9 * k, 0.8 * k, 0.6 * k, 0.3 * k }, 0);
        M["cat"] = m.done();
    }
    // THE FESTIVAL STAGE: a black deck, a roof of truss on four towers with light bars, speaker towers, a backdrop
    {
        Builder m;
        const double W = stageSpec.w / 2;
        m.box ({ 0, 3, 0 }, { W * 0.62, 3, 16 }, 7);
        m.box ({ 0, 20, -15 }, { W * 0.62, 17, 0.6 }, std::array<double, 6> { 7, 7, 7, 7, 14, 7 });
        for (const double sx : { -1.0, 1.0 }) for (const double sz : { -1.0, 1.0 }) m.box ({ sx * W * 0.6, 20, sz * 14 }, { 0.8, 20, 0.8 }, 1);
        m.box ({ 0, 40, 14 }, { W * 0.62, 1.0, 1.0 }, 1); m.box ({ 0, 40, -14 }, { W * 0.62, 1.0, 1.0 }, 1);
        for (const double sx : { -1.0, 1.0 }) m.box ({ sx * W * 0.6, 40, 0 }, { 1.0, 1.0, 14 }, 1);
        for (int k = 0; k < 7; ++k) m.box ({ (k - 3) * W * 0.18, 38.6, 13.0 }, { W * 0.07, 0.45, 0.45 }, 8);
        for (int k = 0; k < 7; ++k) m.box ({ (k - 3) * W * 0.18, 38.6, -2 }, { W * 0.07, 0.45, 0.45 }, 8);
        for (const double sx : { -1.0, 1.0 }) { m.box ({ sx * (W * 0.62 + 7), 14, 6 }, { 6, 14, 5 }, 7); m.box ({ sx * (W * 0.62 + 7), 14, 11.05 }, { 5.2, 13, 0.05 }, 3); }
        for (int k = 0; k < 5; ++k) m.box ({ (k - 2) * 9.0, 6.4, 12 }, { 3.4, 0.5, 0.5 }, 8);
        M["stage"] = m.done();
    }
    // A PYRO NOZZLE: a steel stand with a dish
    {
        Builder m;
        m.tube ({ { 0, -0.3, 0 }, { 0, 1.2, 0 } }, 8, R { 0.55, 0.4 }, 1, true);
        m.tube ({ { 0, 1.2, 0 }, { 0, 1.6, 0 } }, 10, R { 0.5, 0.75 }, 0, true);
        M["nozzle"] = m.done();
    }
    // A LAVA POOL (the fountain's): a rim of basalt round a glowing disc
    {
        Builder m;
        m.torus ({ 0, 0.3, 0 }, 5.2, 1.3, 6, 18, 6);
        m.tube ({ { 0, 0.2, 0 }, { 0, 0.45, 0 } }, 18, R { 4.6, 4.6 }, 5, true);
        M["pool"] = m.done();
    }
    // THE RARE FINDS' STAND-INS: the tour bus, the cassette half buried, the battle jacket's pole, the guitar in the
    // stone, the longship with its dragon prow
    {
        Builder m;
        m.box ({ 0, 2.4, 0 }, { 6.2, 1.9, 1.3 }, 9);
        m.box ({ 0.4, 2.9, 1.31 }, { 5.2, 0.7, 0.01 }, 10); m.box ({ 0.4, 2.9, -1.31 }, { 5.2, 0.7, 0.01 }, 10); m.box ({ 6.21, 2.9, 0 }, { 0.01, 0.8, 1.1 }, 10);
        for (const double x : { -4.2, 3.8 }) for (const double z : { -1.2, 1.2 }) m.tube ({ { x, 0.55, z * 0.95 }, { x, 0.55, z * 1.05 } }, 10, R { 0.55, 0.55 }, 7, true);
        M["find0"] = m.done();
    }
    {
        Builder m;
        m.box ({ 0, 2.2, 0 }, { 5.0, 3.1, 0.6 }, 9, 0, 0.25);
        m.box ({ 0, 3.0, 0.62 }, { 3.4, 1.4, 0.02 }, 11, 0, 0.25);
        for (const double x : { -1.7, 1.7 }) m.tube ({ { x, 2.6, 0.55 }, { x, 2.6, 0.75 } }, 12, R { 0.75, 0.75 }, 0, true);
        m.box ({ 0, 0.0, 0 }, { 6.5, 0.5, 2.5 }, 6);
        M["find1"] = m.done();
    }
    {
        Builder m;
        m.tube ({ { 0, -0.5, 0 }, { 0, 9.5, 0 } }, 8, R { 0.14, 0.1 }, 0, true);
        m.blob ({ 0, 9.6, 0 }, { 0.25, 0.25, 0.25 }, 0, 4, 6);
        M["find2"] = m.done();
    }
    {
        Builder m;
        m.blob ({ 0, 0.6, 0 }, { 3.4, 2.4, 2.8 }, 6, 6, 9);
        m.tube ({ { 0, 1.0, 0 }, { 0, 5.8, 0 } }, 6, R { 0.18, 0.15 }, 0, true);
        m.box ({ 0, 6.3, 0 }, { 0.25, 0.55, 0.06 }, 0);
        m.box ({ 0.85, 2.9, 0 }, { 1.0, 0.35, 0.12 }, 12, 0, 0); m.box ({ -0.85, 2.9, 0 }, { 1.0, 0.35, 0.12 }, 12, 0, 0);
        m.box ({ 0.9, 3.6, 0 }, { 0.22, 1.3, 0.12 }, 12, -0.7); m.box ({ -0.9, 3.6, 0 }, { 0.22, 1.3, 0.12 }, 12, 0.7);
        M["find3"] = m.done();
    }
    {
        Builder m;
        std::vector<V3> pts;
        R rad;
        for (int k = 0; k <= 10; ++k) { const double t = (double) k / 10 * 2 - 1; pts.push_back ({ t * 11, 1.4 + 0.9 * t * t, 0 }); rad.push_back (2.4 * std::sqrt (jsMax (0.02, 1 - t * t * 0.92))); }
        m.tube (pts, 12, rad, 9, true);
        m.box ({ 0, 2.6, 0 }, { 9.5, 0.12, 1.8 }, 1);
        m.tube ({ { 11, 2.4, 0 }, { 12.6, 5.0, 0 }, { 13.2, 7.6, 0 }, { 12.2, 9.0, 0 } }, 8, R { 0.7, 0.6, 0.55, 0.5 }, 1, true);
        m.blob ({ 12.0, 9.2, 0 }, { 1.4, 0.8, 0.7 }, 1, 4, 6); m.blob ({ 11.2, 9.4, 0.5 }, { 0.2, 0.2, 0.2 }, 5, 3, 4); m.blob ({ 11.2, 9.4, -0.5 }, { 0.2, 0.2, 0.2 }, 5, 3, 4);
        m.tube ({ { -11, 2.4, 0 }, { -12.6, 4.6, 0 }, { -12.4, 6.6, 0 } }, 6, R { 0.5, 0.4, 0.1 }, 1);
        m.tube ({ { 0, 2.5, 0 }, { 0, 13, 0 } }, 8, R { 0.3, 0.22 }, 1, true);
        for (int k = 0; k < 7; ++k) for (const double sz : { -1.0, 1.0 }) m.blob ({ -6.0 + k * 2, 2.6, sz * 2.0 }, { 0.75, 0.75, 0.12 }, 12, 4, 8);
        M["find4"] = m.done();
    }
    return M;
}

//==============================================================================
// THE HILL AND ITS TUNNEL behind the skull gate: a mesh built along the road's own curve, relative to the gate's foot.

MetalWorld::Mesh MetalWorld::hillMesh() const
{
    Builder m;
    const double ox = gate_.x, oy = gate_.y, oz = gate_.z;
    const double from = 3.0, to = 100.0, hw = 6.5, high = 9.5, wide = 80.0;
    const int NU = 26;
    std::vector<double> vs;
    for (int k = -20; k <= 20; ++k) vs.push_back (sign (k) * wide * jsPow (std::abs (k) / 20.0, 1.35));
    const auto topH = [&] (double u, double v)
    {
        const double f = (u - from) / (to - from);
        const double H = 34 + 6 * Js::sin (f * 3.1 + 0.4) - 8 * smoothstep (0.7, 1.0, f);
        const double rock = 2.5 * (hs (Js::round (u * 0.5) * 131 + Js::round (v * 0.5) + 4000, 47) - 0.5) + 3 * Js::sin (u * 0.11 + v * 0.07);
        return jsMax (0, (H + rock * (1 - smoothstep (0.6, 1.0, std::abs (v) / wide))) * (1 - smoothstep (wide * 0.22, wide, std::abs (v))));
    };
    const auto at = [&] (double u, double v, double up) { const V2 p = roadPointAt (gate_.at + u, v); return V3 { p[0] - ox, floorHeight (p[0], p[1]) - oy + up, p[1] - oz }; };
    std::vector<std::vector<V3>> grid;
    for (int i = 0; i <= NU; ++i)
    {
        const double u = from + (to - from) * i / NU;
        std::vector<V3> row;
        for (const double v : vs) row.push_back (at (u, v, topH (u, v) - (std::abs (v) > wide * 0.98 ? 2.5 : 0)));
        grid.push_back (row);
    }
    const int nv = (int) vs.size();
    const uint32_t base = (uint32_t) (m.v.size() / 7);
    for (int i = 0; i <= NU; ++i)
        for (int j = 0; j < nv; ++j)
        {
            const V3 p = grid[(size_t) i][(size_t) j], a = grid[(size_t) std::max (0, i - 1)][(size_t) j], b = grid[(size_t) std::min (NU, i + 1)][(size_t) j];
            const V3 c = grid[(size_t) i][(size_t) std::max (0, j - 1)], d = grid[(size_t) i][(size_t) std::min (nv - 1, j + 1)];
            const V3 e1 { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2 { d[0] - c[0], d[1] - c[1], d[2] - c[2] };
            V3 n { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            if (n[1] < 0) n = { -n[0], -n[1], -n[2] };
            const double l = orOne (Js::hypot (n[0], n[1], n[2]));
            m.vert (p, { n[0] / l, n[1] / l, n[2] / l }, 6);
        }
    for (int i = 0; i < NU; ++i)
        for (int j = 0; j + 1 < nv; ++j)
        {
            const uint32_t a = base + (uint32_t) (i * nv + j), b = a + 1, c = a + (uint32_t) nv, d = c + 1;
            m.idx.insert (m.idx.end(), { a, c, b, b, c, d });
        }
    // the arch: from the left foot up over and down to the right foot, as many points as a row of the top
    const auto arch = [&] (int k) { const double ph = pi * (1 - (double) k / (nv - 1)); return V2 { hw * Js::cos (ph), high * jsPow (Js::sin (ph), 0.55) }; };
    // the cliff faces at both ends: a strip from the top's row down to the arch
    for (const auto& [u, ri, sg] : { std::tuple<double, int, double> { from, 0, -1.0 }, std::tuple<double, int, double> { to, NU, 1.0 } })
    {
        const auto& row = grid[(size_t) ri];
        const V2 t = tangentAt (thetaAt (gate_.at + u));
        const V3 n { t[0] * sg, 0, t[1] * sg };
        const uint32_t b0 = (uint32_t) (m.v.size() / 7);
        for (int j = 0; j < nv; ++j)
        {
            m.vert (row[(size_t) j], n, 6);
            const V2 ar = arch (j);
            m.vert (at (u, ar[0], ar[1] - 0.6), n, 6);
        }
        for (int j = 0; j + 1 < nv; ++j)
        {
            const uint32_t a = b0 + (uint32_t) (j * 2), b = a + 1, c = a + 2, d = a + 3;
            m.idx.insert (m.idx.end(), { a, b, c, c, b, d });
        }
    }
    // the bore: the arch swept along the road, its normals inward
    const int NB = 34, nb = 17;
    const uint32_t b1 = (uint32_t) (m.v.size() / 7);
    for (int i = 0; i <= NB; ++i)
    {
        const double u = from - 0.5 + (to - from + 0.5) * i / NB;
        for (int k = 0; k < nb; ++k)
        {
            const double ph = pi * (1 - (double) k / (nb - 1)), ax = hw * Js::cos (ph), ay = high * jsPow (Js::sin (ph), 0.55) - 0.6;
            const V3 p = at (u, ax, ay);
            const V2 r = rightAt (thetaAt (gate_.at + u));
            const double nx = -Js::cos (ph), ny = -Js::sin (ph) * 0.9;
            const double l = orOne (Js::hypot (nx, ny));
            m.vert (p, { r[0] * nx / l, ny / l, r[1] * nx / l }, 13 + mod (u / 15, 1) * 0.0);
        }
    }
    for (int i = 0; i < NB; ++i)
        for (int k = 0; k + 1 < nb; ++k)
        {
            const uint32_t a = b1 + (uint32_t) (i * nb + k), b = a + 1, c = a + (uint32_t) nb, d = c + 1;
            m.idx.insert (m.idx.end(), { a, b, c, b, d, c });
        }
    // the lamps in the walls: lava in little niches, every 15 m both sides
    for (double u = from + 7; u < to - 3; u += 15)
        for (const double sd : { -1.0, 1.0 }) m.blob (at (u, sd * (hw - 0.3), 3.2), { 0.35, 0.5, 0.35 }, 5, 4, 6);
    return m.done();
}

//==============================================================================
// THE FINDS' FILES

MetalWorld::MeshFile MetalWorld::readMesh (const uint8_t* b, size_t size)
{
    MeshFile m;
    if (b == nullptr || size < 16) return m;
    m.nv = u32le (b + 8); m.ni = u32le (b + 12);
    if (std::memcmp (b, "CBMESH1", 7) != 0 || (uint64_t) size < 16 + (uint64_t) m.nv * 40 + (uint64_t) m.ni * 4) return m;
    for (uint32_t i = 0; i < m.nv; ++i)
    {
        const double z = f32le (b + 16 + (size_t) i * 40 + 8);
        if (z < m.zmin) m.zmin = z;
        if (z > m.zmax) m.zmax = z;
    }
    m.ok = true; m.vertexAt = 16; m.indexAt = 16 + (size_t) m.nv * 40;
    return m;
}
std::optional<double> MetalWorld::landmarkTop (const std::string& role, const MeshFile& m)
{
    if (! m.ok) return {};
    for (const auto& lm : landmarkSpecs)
        if (role == lm.role)
            return m.zmax * (lm.height > 0 ? lm.height / (m.zmax - jsMax (m.zmin, 0)) : 1);
    return {};
}
std::string MetalWorld::roleOf (std::string_view folder)
{
    const std::string n = lower (std::string (folder));
    const auto has = [&n] (std::initializer_list<const char*> any) { for (const char* a : any) if (n.find (a) != std::string::npos) return true; return false; };
    if (has ({ "horns", "hand" })) return "horns";
    if (has ({ "mammoth" })) return "mammoth";
    if (has ({ "brachy", "ceratops" })) return "brachy";
    if (has ({ "guitars" })) return "guitars";
    if (has ({ "crawler" })) return "crawler";
    if (has ({ "warrior", "guardian" })) return "warrior";
    if (has ({ "skull", "gate" })) return "gate";
    if (has ({ "fossil" })) return "fossil";
    if (has ({ "cat" })) return "cat";
    if (has ({ "bus" })) return "find0";
    if (has ({ "cassette", "tape" })) return "find1";
    if (has ({ "jacket", "flag" })) return "find2";
    if (has ({ "guitar" })) return "find3";
    if (has ({ "ship", "viking", "boat" })) return "find4";
    if (has ({ "anvil" })) return "anvil";
    if (has ({ "dragon" })) return "dragonFile";
    return std::string (folder);
}
void MetalWorld::furnish (const Finds& f)
{
    if (f.dragonHead) setDragonHead (*f.dragonHead);
    if (! f.letters.empty()) buildLetters (f.letters);
    if (! f.guitars.empty())
    {
        placeGuitars (f.guitars);
        for (size_t i = 0; i < f.guitarNames.size(); ++i) nameGuitar ((int) i, f.guitarNames[i]);
    }
    for (const auto& [role, top] : f.landmarkTops) landmarkCame (role, top);
}
std::optional<MetalWorld::DragonHead> MetalWorld::readDragonHead (const uint8_t* b, size_t size, uint32_t meshNv)
{
    if (b == nullptr || size < 24 || std::memcmp (b, "CBANIM1", 7) != 0) return {};
    const uint32_t nv = u32le (b + 8), nf = u32le (b + 12);
    const double unit = f32le (b + 20);
    if ((meshNv != 0 && meshNv != nv) || nv == 0 || nf == 0 || (uint64_t) size < 24 + (uint64_t) nf * nv * 10) return {};
    const auto P3 = [&] (uint32_t f, uint32_t i) { V3 o {}; for (size_t k = 0; k < 3; ++k) o[k] = i16le (b + 24 + ((size_t) f * nv + i) * 10 + k * 2) * unit; return o; };
    std::vector<uint32_t> head;
    int64_t tip = -1;
    double ty = 1e9;
    for (uint32_t i = 0; i < nv; ++i) { const V3 p = P3 (0, i); if (p[1] < -2.0 && std::abs (p[0]) < 1.2) { head.push_back (i); if (p[1] < ty) { ty = p[1]; tip = i; } } }
    if (head.empty()) return {};
    DragonHead h;
    for (uint32_t f = 0; f < nf; ++f)
    {
        h.mouth.push_back (P3 (f, (uint32_t) tip));
        V3 acc { 0, 0, 0 };
        for (const uint32_t i : head) { const V3 p = P3 (f, i); acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; }
        h.mid.push_back ({ acc[0] / (double) head.size(), acc[1] / (double) head.size(), acc[2] / (double) head.size() });
    }
    return h;
}
std::optional<std::string> MetalWorld::creditOf (std::string_view text)
{
    static const std::regex re (R"(credit:?\s*([^\n]*))");
    std::match_results<std::string_view::const_iterator> m;
    if (! std::regex_search (text.begin(), text.end(), m, re)) return {};
    return trim (std::string (m[1].first, m[1].second));
}
std::vector<MetalWorld::Headstock> MetalWorld::readHeadstocks (std::string_view text)
{
    std::vector<Headstock> o;
    for (const auto& l : lines (text))
    {
        const auto w = words (l);
        if (w[0] != "headstock" || w.size() < 8) continue;
        o.push_back ({ w[1], { number (w[2]), number (w[3]), number (w[4]) }, { number (w[5]), number (w[6]), number (w[7]) } });
    }
    return o;
}
std::vector<MetalWorld::GuitarEntry> MetalWorld::readGuitars (std::string_view text)
{
    std::vector<GuitarEntry> set;
    for (const auto& l : lines (text))
    {
        const auto w = words (l);
        if (w[0] != "guitar" || w.size() < 4) continue;
        const auto indexOf = [&] (const char* s) { for (size_t i = 0; i < w.size(); ++i) if (w[i] == s) return (int) i; return -1; };
        const auto at = [&] (int i) { return i >= 0 && i < (int) w.size() ? number (w[(size_t) i]) : nan; };
        const int hi = indexOf ("headstock"), pi_ = indexOf ("paint");
        GuitarEntry g;
        g.name = w[1];
        g.file = endsWith (w[2], ".mesh") ? w[2].substr (0, w[2].size() - 5) : w[2];
        if (hi > 0 && ! (hi + 1 < (int) w.size() && w[(size_t) hi + 1] == "none"))
            g.box = Headstock { w[1], { at (hi + 1), at (hi + 2), at (hi + 3) }, { at (hi + 4), at (hi + 5), at (hi + 6) } };
        if (pi_ > 0 && pi_ + 2 < (int) w.size())
        {
            const std::string& hex = w[(size_t) pi_ + 2];
            static const std::regex six (R"(^[0-9a-fA-F]{6}$)");
            if (std::regex_match (hex, six))
                g.paint = V3 { (double) std::strtol (hex.substr (0, 2).c_str(), nullptr, 16) / 255, (double) std::strtol (hex.substr (2, 2).c_str(), nullptr, 16) / 255,
                               (double) std::strtol (hex.substr (4, 2).c_str(), nullptr, 16) / 255 };
        }
        set.push_back (g);
    }
    return set;
}
MetalWorld::GroveDeal MetalWorld::dealGroves (const std::vector<GuitarEntry>& set)
{
    GroveDeal deal;
    if (set.empty()) return deal;
    const char* starNames[4] = { "strat_candy_apple_red", "pbass_lake_placid_blue", "rg_flame", "btb5" };
    const char* ourNames[4] = { "ears_blackchrome", "claw_gunmetal", "fang_bloodred", "bolt_brushed" };
    const auto find = [&] (const char* n) { for (size_t i = 0; i < set.size(); ++i) if (set[i].name == n) return (int) i; return -1; };
    std::vector<int> stars, ours;
    for (const char* n : starNames) if (const int i = find (n); i >= 0) stars.push_back (i);
    for (const char* n : ourNames) if (const int i = find (n); i >= 0) ours.push_back (i);
    if (ours.empty()) for (size_t i = 0; i < set.size(); ++i) if (! set[i].box) ours.push_back ((int) i);
    const int G = std::max (1, (int) stars.size());
    for (int g = 0; g < G; ++g)
    {
        const int turns[4] = { 0, 2, 1, 3 };
        const int turn = turns[g % 4];
        std::vector<int> own;
        for (size_t k = 0; k < ours.size(); ++k) own.push_back (ours[(k + (size_t) turn) % ours.size()]);
        if (g % 2) std::reverse (own.begin(), own.end());
        const size_t half = own.size() / 2;
        const int star = g < (int) stars.size() ? stars[(size_t) g] : -1;
        std::vector<int> order (own.begin(), own.begin() + (long) half);
        if (star >= 0) order.push_back (star);
        order.insert (order.end(), own.begin() + (long) half, own.end());
        for (const int q : order)
        {
            deal.chosen.push_back (q); deal.items.push_back ({ g, q == star });
            std::string shown = set[(size_t) q].name;
            std::replace (shown.begin(), shown.end(), '_', ' ');
            deal.names.push_back (shown);
        }
    }
    return deal;
}
std::vector<std::array<double, 4>> MetalWorld::readBanners (std::string_view text, double w, double h)
{
    std::vector<std::array<double, 4>> o;
    for (const auto& l : lines (text))
    {
        const std::string t = trim (l);
        if (t.rfind ("#", 0) == 0) continue;
        std::vector<double> nums;
        for (const auto& x : words (t)) { const double v = number (x); if (v == v) nums.push_back (v); }
        if (nums.size() >= 4) o.push_back ({ nums[0] / w, nums[1] / h, (nums[0] + nums[2]) / w, (nums[1] + nums[3]) / h });
    }
    return o;
}
std::array<int, 2> MetalWorld::pngSize (const uint8_t* b, size_t size)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    if (b == nullptr || size < 24 || std::memcmp (b, sig, 8) != 0 || std::memcmp (b + 12, "IHDR", 4) != 0) return { 0, 0 };
    const auto be = [b] (size_t o) { return (int) (((uint32_t) b[o] << 24) | ((uint32_t) b[o + 1] << 16) | ((uint32_t) b[o + 2] << 8) | (uint32_t) b[o + 3]); };
    return { be (16), be (20) };
}
MetalWorld::LetterAtlas MetalWorld::packLetters (std::string_view lettersTxt, const std::vector<Png>& pngs)
{
    LetterAtlas atlas;
    if (pngs.empty()) return atlas;
    struct Row { std::string name, file; double width = 0; std::optional<double> up; int w = 0, h = 0; std::optional<std::array<double, 4>> cell; };
    std::vector<Row> rows;
    const auto rowOf = [&] (const std::string& file) { for (size_t i = 0; i < rows.size(); ++i) if (rows[i].file == file) return (int) i; return -1; };
    static const std::regex rowRe (R"(^(\S+)\s+(\S+\.png)\s+([0-9]+)\s*x\s*([0-9]+)\s*px\s+([0-9.]+)\s*m\s*wide)", std::regex::ECMAScript | std::regex::icase);
    static const std::regex upRe (R"(([0-9.]+)\s*m\s*(up|over|high))", std::regex::ECMAScript | std::regex::icase);
    for (const auto& line : lines (lettersTxt))
    {
        const std::string t = trim (line);
        std::smatch m;
        if (! std::regex_search (t, m, rowRe)) continue;
        std::smatch u;
        Row r { m[1].str(), m[2].str(), number (m[5].str()), {}, 0, 0, {} };
        if (std::regex_search (line, u, upRe)) r.up = number (u[1].str());
        if (const int i = rowOf (r.file); i >= 0) rows[(size_t) i] = r; else rows.push_back (r);
    }
    for (const auto& p : pngs)
        if (rowOf (p.file) < 0)
            rows.push_back ({ endsWith (p.file, ".png") ? p.file.substr (0, p.file.size() - 4) : p.file, p.file, 14, {}, 0, 0, {} });
    // the pictures that load: the rows whose PNG is there, with its size; then the word CENSORED's canvas
    std::vector<Row> got;
    for (auto r : rows)
        for (const auto& p : pngs)
            if (p.file == r.file && p.width > 0 && p.height > 0) { r.w = p.width; r.h = p.height; got.push_back (r); break; }
    if (got.empty()) return atlas;
    got.push_back ({ "__censored", "", 0, 0.0, 1024, 220, {} });
    const int W = 2048, col = 1024;
    double sum = 0;
    for (const auto& g : got) sum = sum + (double) col * g.h / g.w;
    const int H = (int) jsMin (4096, std::ceil (sum / 2 / 64) * 64 + 512);
    std::stable_sort (got.begin(), got.end(), [] (const Row& a, const Row& b) { return (double) b.h / b.w - (double) a.h / a.w < 0; });
    double ys[2] = { 0, 0 };
    for (auto& g : got)
    {
        const double h = Js::round ((double) col * g.h / g.w);
        const int k = ys[0] + h <= H ? (ys[0] <= ys[1] || ys[1] + h > H ? 0 : 1) : 1;
        if (ys[k] + h > H) continue;
        g.cell = std::array<double, 4> { (double) (k * col + 4), ys[k] + 4, (double) (col - 8), h - 8 };
        ys[k] += h;
    }
    atlas.width = W; atlas.height = H;
    std::vector<const Row*> placed;
    for (const auto& g : got) if (g.cell) { placed.push_back (&g); atlas.pieces.push_back ({ g.name, g.file, *g.cell }); }
    for (const Row* g : placed) if (g->name == "__censored") { atlas.censored = g->cell; break; }
    for (const Row* g : placed)
    {
        const std::string ln = lower (g->name);
        if (endsWith (ln, "_color") || endsWith (ln, "_colour") || g->name == "__censored") continue;
        const Row* colour = nullptr;
        for (const Row* q : placed) { const std::string lq = lower (q->name); if (lq == ln + "_color" || lq == ln + "_colour") { colour = q; break; } }
        const bool logo = ln == "darwin_cat" || ln == "darwins_cat";
        const bool asIs = ln.find ("e530") != std::string::npos || ln.find ("inspired") != std::string::npos || ln.find ("speed") != std::string::npos
                       || ln.find ("limit") != std::string::npos;
        const double k = asIs ? 1.0 : logo ? 1.15 : 1.35;
        const double wm = g->width * k, hgt = wm * g->h / g->w;
        LetterText t;
        t.name = g->name; t.cell = *g->cell; t.size = { wm, hgt }; t.up = g->up ? *g->up : logo ? 12 : hgt > 10 ? 3 : 4.5; t.once = logo;
        if (colour) t.colour = colour->cell;
        atlas.table.push_back (t);
    }
    return atlas;
}

//==============================================================================
// THE CABIN'S PLATE

std::optional<std::array<double, 4>> MetalWorld::glassBounds (const uint8_t* rgba, int w, int h, int margin)
{
    if (rgba == nullptr || w <= 0 || h <= 0) return {};
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgba[((size_t) y * (size_t) w + (size_t) x) * 4 + 3] < 255) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x1 < 0) return {};
    return std::array<double, 4> { (double) std::max (0, x0 - margin) / w, (double) std::max (0, y0 - margin) / h,
                                   (double) std::min (w, x1 + 1 + margin) / w, (double) std::min (h, y1 + 1 + margin) / h };
}
MetalWorld::Glass MetalWorld::Glass::of (const uint8_t* rgba, int w, int h, int step)
{
    Glass g;
    if (rgba == nullptr || w <= 0 || h <= 0 || step <= 0) return g;
    g.width = (w + step - 1) / step;
    g.height = (h + step - 1) / step;
    g.alpha.resize ((size_t) g.width * (size_t) g.height);
    for (int y = 0; y < g.height; ++y)
        for (int x = 0; x < g.width; ++x)
            g.alpha[(size_t) (y * g.width + x)] = rgba[((size_t) (y * step) * (size_t) w + (size_t) (x * step)) * 4 + 3];
    return g;
}
bool MetalWorld::Glass::open (float x01, float y01) const noexcept
{
    if (alpha.empty()) return true;
    const int x = std::clamp ((int) (x01 * (float) width), 0, width - 1), y = std::clamp ((int) (y01 * (float) height), 0, height - 1);
    return alpha[(size_t) (y * width + x)] < 128;    // more than half see-through: the window, not the metal
}

} // namespace orbitamp::metal
