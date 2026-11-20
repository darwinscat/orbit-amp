// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// A port of california-gl.js's world, line for line: the makers of the textures and the meshes, makeWorld (the road
// and land maps, the drive, the pendant, the butterflies, the finds, the signs, the chalk, the saucers, the submarine,
// the oaks, the poles, the shadow map) and the view's readers of the files. Every expression keeps the prototype's
// order of operations, so that the doubles round where the JS's round; f32 where it stores into a Float32Array.

#include "CaliforniaWorld.h"

#include "../../redgroove/view/WalkerWorld.h"     // Js: V8's sin, cos, exp, atan2, hypot and round

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>

namespace orbitamp::california
{

namespace
{
    using Js = redgroove::WalkerWorld::Js;
    using W = CaliforniaWorld;
    using V2 = W::V2;
    using V3 = W::V3;
    using V4 = W::V4;
    constexpr double pi = W::pi;
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    inline double jsMin (double a, double b) noexcept { return (a != a || b != b) ? nan : (a < b ? a : b); }
    inline double jsMax (double a, double b) noexcept { return (a != a || b != b) ? nan : (a > b ? a : b); }
    inline double clamp (double v, double lo, double hi) noexcept { return jsMin (hi, jsMax (lo, v)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline double ease (double dt, double tau) noexcept { return 1 - Js::exp (-dt / tau); }
    /** JS's `%` on doubles: the C library's fmod, which is exact. */
    inline double mod (double a, double b) noexcept { return std::fmod (a, b); }
    inline double wrapW (double v) noexcept { return mod (mod (v, W::worldPeriod) + W::worldPeriod, W::worldPeriod); }
    inline double nearest (double d) noexcept { return d - Js::round (d / W::worldPeriod) * W::worldPeriod; }
    inline double wrapBox (double d, double L) noexcept { return mod (mod (d, L) + L + L / 2, L) - L / 2; }
    /** `x || 1`: 1 where it is 0 or NaN. */
    inline double orOne (double x) noexcept { return std::abs (x) > 0 ? x : 1; }
    inline int64_t imod (int64_t a, int64_t m) noexcept { return ((a % m) + m) % m; }

    /** Tileable value noise on the unit square: fx by fy lattice cells, smooth interpolation. */
    double vnoise (double x, double y, int fx, int fy, int seed) noexcept
    {
        const double px = x * fx, py = y * fy, ix = std::floor (px), iy = std::floor (py);
        const double ux0 = px - ix, uy0 = py - iy, ux = ux0 * ux0 * (3 - 2 * ux0), uy = uy0 * uy0 * (3 - 2 * uy0);
        const auto h = [fx, fy, seed] (double i, double j)
        {
            return W::hcell ((int32_t) imod ((int64_t) i, fx), (int32_t) imod ((int64_t) j, fy), seed);
        };
        return lerp (lerp (h (ix, iy), h (ix + 1, iy), ux), lerp (h (ix, iy + 1), h (ix + 1, iy + 1), ux), uy);
    }
    double fbm (double x, double y, int f0, int octaves, double rough, int seed, int fy0) noexcept
    {
        double a = 1, sum = 0, norm = 0;
        int fx = f0, fy = fy0;
        for (int o = 0; o < octaves; ++o) { sum += a * vnoise (x, y, fx, fy, seed + o * 7919); norm += a; a *= rough; fx *= 2; fy *= 2; }
        return sum / norm;
    }
    double fbm (double x, double y, int f0, int octaves, double rough, int seed) noexcept { return fbm (x, y, f0, octaves, rough, seed, f0); }
    void normalize01 (std::vector<float>& h)
    {
        double lo = h[0], hi = h[0];
        for (const float x : h) { if (x < lo) lo = x; if (x > hi) hi = x; }
        const double k = 1 / (hi - lo);
        for (auto& x : h) x = (float) (((double) x - lo) * k);
    }
    inline uint8_t trunc8 (double v) noexcept { return (uint8_t) (int) std::trunc (v); }

    // THE ROAD'S PLACES (the prototype's tables).
    constexpr std::array<int, 4> loopK { 2, 3, 5, 7 };
    constexpr std::array<double, 4> loopA { 200.0, 40.0, 22.0, 10.0 }, loopP { 2.8623, 2.1, 4.0, 1.3 };
    enum class At { number, climb, descent };
    struct PostAt { At kind; double at, d; };
    struct SignPostSpec { const char* key; std::vector<PostAt> at; };
    const std::vector<SignPostSpec>& signPostSpecs()
    {
        static const std::vector<SignPostSpec> t {
            { "p3", { { At::number, 0.217, 8.4 }, { At::number, 0.515, -8.4 } } },
            { "p4", { { At::climb, 0, -10.5 }, { At::climb, 0, 10.5 } } },
            { "p12", { { At::descent, 0, 10.5 }, { At::descent, 0, -10.5 } } },
            { "p5", { { At::number, 0.883, 0 } } },
            { "p13", { { At::number, 900 / 5147.1, -8.6 }, { At::number, 0.398, 8.6 }, { At::number, 0.641, 8.6 }, { At::number, 0.868, -8.6 } } },
        };
        return t;
    }
    struct Rhyme { std::array<double, 2> at { 0.280, 0.700 }; double step = 28.0; std::array<double, 2> d { -8.6, 8.6 }; std::array<const char*, 5> posts { "p7", "p8", "p9", "p10", "p11" }; };
    const Rhyme rhyme;
    struct PumpSpec { double at, d; };
    constexpr std::array<PumpSpec, 4> pumpSpecs { { { 0.090, -26 }, { 0.175, 30 }, { 0.760, -24 }, { 0.980, 28 } } };
    struct FenceSpec { double from, to, d; };
    constexpr std::array<FenceSpec, 2> fenceSpecs { { { 0.420, 0.452, -10.5 }, { 0.930, 0.962, -10.5 } } };
    constexpr W::Placeholder gable { 11.1, 10, 14.8, 0, { 0.62, 0.16, 0.12 } };
    struct SpriteSpec
    {
        std::string name, label;
        double at = 0;
        int side = 0;
        bool hasOff = false;
        V2 off {};
        double d = 0;
        bool hill = false;
        double turn = 0, pull = 0;
        std::string mesh;
        bool hasPart = false;
        V2 part {};
        double scale = 0;                 // 0: none said
        bool paint = false;
        std::string group;
        double seen = 0;
        W::Placeholder ph;
        bool noBox = false, noJump = false;
    };
    const std::vector<SpriteSpec>& spriteSpecs()
    {
        static const std::vector<SpriteSpec> t = []
        {
            std::vector<SpriteSpec> v;
            const auto off = [] (std::string name, std::string label, double at, int side, double turn, std::string mesh)
            {
                SpriteSpec sp; sp.name = name; sp.label = label; sp.at = at; sp.side = side; sp.hasOff = true; sp.off = { 25, 60 };
                sp.turn = turn; sp.pull = 6; sp.mesh = mesh; sp.ph = gable; return sp;
            };
            const auto by = [] (std::string name, std::string label, double at, double d, double turn, double pull, std::string mesh, W::Placeholder ph)
            {
                SpriteSpec sp; sp.name = name; sp.label = label; sp.at = at; sp.d = d; sp.turn = turn; sp.pull = pull; sp.mesh = mesh; sp.ph = ph; return sp;
            };
            const auto giant = [&by] (std::string name, std::string label, double at, double d, V2 part, W::Placeholder ph)
            {
                auto sp = by (name, label, at, d, 30, 0, "combos3d/combos", ph);
                sp.hasPart = true; sp.part = part; sp.paint = true; sp.group = "giants"; return sp;
            };
            v.push_back (off ("barn_smiley", "barn: the smiley", 0.055, 1, 40, "barn_smiley3d/barn_smiley"));
            v.push_back (by ("barn", "barn: the rainbow", 0.330, -40, 25, 6, "barn3d/barn", { 14.8, 10, 11, 0, { 0.62, 0.16, 0.12 } }));
            v.push_back (off ("barn_salute", "barn: the salute", 0.560, -1, 40, "barn_salute3d/barn_salute"));
            v.push_back (by ("drivein", "drive-in", 0.620, 75, 35, 1, "drivein3d/drivein", { 18, 7, 0.5, 4.5, { 0.92, 0.90, 0.84 } }));
            { auto sp = by ("tower", "water tower", 0.670, 0, 0, 3, "", { 6, 5, 6, 11, { 0.80, 0.78, 0.74 } }); sp.hill = true; v.push_back (sp); }
            v.push_back (off ("barn_victory", "barn: the victory", 0.845, 1, 40, "barn_victory3d/barn_victory"));
            v.push_back (giant ("combo_a", "orange 2x12 combo", 390 / 5147.1, -14, { 5.8, 11.5 }, { 5.6, 5, 3.4, 0, { 0.85, 0.40, 0.10 } }));
            v.push_back (giant ("ampeg", "Ampeg-style rig", 560 / 5147.1, 14, { -2.1, 3.0 }, { 5, 8, 4.3, 0, { 0.12, 0.12, 0.13 } }));
            v.push_back (giant ("combo_b", "orange 2x12 combo, again", 3840 / 5147.1, -14, { 5.8, 11.5 }, { 5.6, 5, 3.4, 0, { 0.85, 0.40, 0.10 } }));
            v.push_back (giant ("stack", "Orange stack", 4680 / 5147.1, 15, { -11.5, -4.9 }, { 6.5, 8, 4.3, 0, { 0.85, 0.40, 0.10 } }));
            {
                auto sp = by ("walker", "the walker", 1920 / 5147.1, -343, 0, 0, "walker3d/walker", { 67, 44, 39, 0, { 0.30, 0.28, 0.30 } });
                sp.scale = 10; sp.paint = true; sp.group = "far"; sp.seen = 600; sp.noBox = true; v.push_back (sp);
            }
            // THE GUITAR GROVES: three 12 m guitars each, left of the road, the farther along the nearer it.
            struct Member { const char* mesh; bool hasPart; V2 part; double along, off, scale, turn; };
            struct Grove { const char* label; double at; int side; double off; std::array<Member, 3> members; };
            const std::array<Grove, 2> groves { {
                { "guitar grove: the Strat", 2620.0, -1, 15, { { { "guitars3d/guitars", true, { -6.0, 0.0 }, 0, 0, 1.0, 0 },
                                                                 { "hippieguitars3d/kestrel", false, {}, -10, 6, 0.86, 28 },
                                                                 { "hippieguitars3d/lantern", false, {}, 9, -4, 1.12, -18 } } } },
                { "guitar grove: the P-Bass", 4870.0, -1, 17, { { { "guitars3d/guitars", true, { 0.0, 6.0 }, 0, 2, 1.08, -10 },
                                                                  { "hippieguitars3d/lander", false, {}, -9, 8, 0.9, 30 },
                                                                  { "hippieguitars3d/heron", false, {}, 10, -3, 1.0, -25 } } } },
            } };
            for (size_t gi = 0; gi < groves.size(); ++gi)
                for (size_t i = 0; i < 3; ++i)
                {
                    const auto& g = groves[gi];
                    const auto& m = g.members[i];
                    SpriteSpec sp;
                    sp.name = "grove" + std::to_string (gi) + "_" + std::to_string (i);
                    sp.label = i == 0 ? g.label : "";
                    sp.noJump = i > 0;
                    sp.at = (g.at + m.along) / 5147.1; sp.d = g.side * (g.off + m.off);
                    sp.turn = 30 + m.turn; sp.mesh = m.mesh; sp.hasPart = m.hasPart; sp.part = m.part; sp.scale = m.scale;
                    sp.paint = true; sp.group = "giants"; sp.pull = 6;
                    sp.ph = { 6 * m.scale, 14 * m.scale, 3 * m.scale, 0, { 0.80, 0.78, 0.74 } };
                    v.push_back (sp);
                }
            return v;
        }();
        return t;
    }
    constexpr V2 chalkStep { 100.0, 200.0 }, chalkSide { 1.5, 2.5 };
    constexpr bool chalkRight = false;
    constexpr double chalkSmallStretch = 2.5;
    const std::array<std::string_view, 5> chalkWords { "love", "peace_word", "groovy", "far_out", "make_love" };
    struct SubSpec { std::array<double, 2> at { 2560.0, 4300.0 }; double ahead = 140.0, side = 190.0; std::array<double, 2> up { 0, 0 }; double screen = 0.31, T = 30.0, follow = 1.0; };
    const SubSpec subSpec;
    struct SaucerSpec { double swoopAt = 1020.0, lead = 5.0, crossAhead = 85.0; V2 crossUp { 15.0, 25.0 }, swoopEvery { 60.0, 110.0 }, pairEvery { 80.0, 140.0 };
                        double pairFor = 14.0; V2 beamEvery { 28.0, 50.0 }; double beamFor = 7.0, dark = 12.0, vmax = 55.0; };
    const SaucerSpec saucer;
    struct DishRow { double at = 3680.0, d = -200.0; int n = 4; double gap = 38.0, view = 300.0, look = 75.0, sink = 1.5; };
    const DishRow dishRow;

    // THE MESHES' MAKERS.
    struct Builder
    {
        std::vector<double> v;                 // 7 a vertex, kept as the JS's numbers until the end
        std::vector<int> idx;
        int vert (const V3& p, const V3& n, double part) { v.insert (v.end(), { p[0], p[1], p[2], n[0], n[1], n[2], part }); return (int) (v.size() / 7) - 1; }
        void tube (const std::vector<V3>& pts, int sides, const std::vector<double>& radii, double part)
        {
            std::vector<std::vector<int>> rings;
            for (size_t k = 0; k < pts.size(); ++k)
            {
                const auto& p = pts[k];
                const auto& q = pts[std::min (k + 1, pts.size() - 1)];
                const auto& o = pts[k > 0 ? k - 1 : 0];
                const auto d = W::normalised ({ q[0] - o[0], q[1] - o[1], q[2] - o[2] });
                const V3 h = std::abs (d[1]) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
                const auto s1 = W::normalised ({ d[1] * h[2] - d[2] * h[1], d[2] * h[0] - d[0] * h[2], d[0] * h[1] - d[1] * h[0] });
                const V3 s2 { d[1] * s1[2] - d[2] * s1[1], d[2] * s1[0] - d[0] * s1[2], d[0] * s1[1] - d[1] * s1[0] };
                std::vector<int> ring;
                for (int i = 0; i < sides; ++i)
                {
                    const double a = (double) i / sides * 2 * pi, ca = Js::cos (a), sa = Js::sin (a);
                    const V3 n { s1[0] * ca + s2[0] * sa, s1[1] * ca + s2[1] * sa, s1[2] * ca + s2[2] * sa };
                    ring.push_back (vert ({ p[0] + n[0] * radii[k], p[1] + n[1] * radii[k], p[2] + n[2] * radii[k] }, n, part));
                }
                rings.push_back (ring);
            }
            for (size_t k = 0; k + 1 < rings.size(); ++k)
                for (int i = 0; i < sides; ++i)
                {
                    const int a = rings[k][(size_t) i], b = rings[k][(size_t) ((i + 1) % sides)], c = rings[k + 1][(size_t) ((i + 1) % sides)], d = rings[k + 1][(size_t) i];
                    idx.insert (idx.end(), { a, b, c, a, c, d });
                }
        }
        void box (const V3& c, const V3& h, double part)
        {
            for (const auto& [ax, sg] : std::array<std::pair<int, int>, 6> { { { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { 2, 1 }, { 2, -1 } } })
            {
                V3 n {}; n[(size_t) ax] = sg;
                V3 u {}, w {}; u[(size_t) ((ax + 1) % 3)] = 1; w[(size_t) ((ax + 2) % 3)] = 1;
                std::array<int, 4> base {};
                int bi = 0;
                for (const auto& [a, b] : std::array<std::pair<int, int>, 4> { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } })
                {
                    const V3 p { c[0] + n[0] * h[0] + u[0] * a * h[0] + w[0] * b * h[0], c[1] + n[1] * h[1] + u[1] * a * h[1] + w[1] * b * h[1],
                                 c[2] + n[2] * h[2] + u[2] * a * h[2] + w[2] * b * h[2] };
                    base[(size_t) bi++] = vert (p, n, part);
                }
                idx.insert (idx.end(), { base[0], base[1], base[2], base[0], base[2], base[3] });
            }
        }
        void blob (const V3& c, const V3& r, int sub, int seed)
        {
            const auto ico = W::icosphere (sub);
            const int base = (int) (v.size() / 7);
            for (size_t i = 0; i < ico.V.size(); ++i)
            {
                const auto& p = ico.V[i];
                const double k = 0.84 + 0.30 * W::hs ((int32_t) i + 1, seed) + 0.08 * Js::sin (p[0] * 5 + p[2] * 4 + seed);
                const V3 q { c[0] + p[0] * r[0] * k, c[1] + p[1] * r[1] * k * (p[1] < 0 ? 0.7 : 1), c[2] + p[2] * r[2] * k };
                vert (q, W::normalised ({ p[0] / r[0], p[1] / r[1], p[2] / r[2] }), 1);
            }
            for (const auto& f : ico.F) idx.insert (idx.end(), { base + f[0], base + f[1], base + f[2] });
        }
        W::Mesh mesh() const
        {
            W::Mesh m;
            m.v.reserve (v.size());
            for (const double x : v) m.v.push_back ((float) x);
            for (const int i : idx) m.idx.push_back ((uint16_t) i);
            return m;
        }
    };

    // THE FILES' WORDS.
    bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
    std::string_view trim (std::string_view s) noexcept
    {
        while (! s.empty() && isSpace (s.front())) s.remove_prefix (1);
        while (! s.empty() && isSpace (s.back())) s.remove_suffix (1);
        return s;
    }
    /** `line.trim ().split (/\s+/)`: an empty line is one empty word. */
    std::vector<std::string> words (std::string_view line)
    {
        const auto t = trim (line);
        std::vector<std::string> out;
        size_t i = 0;
        if (t.empty()) return { std::string() };
        while (i < t.size())
        {
            size_t j = i;
            while (j < t.size() && ! isSpace (t[j])) ++j;
            out.emplace_back (t.substr (i, j - i));
            while (j < t.size() && isSpace (t[j])) ++j;
            i = j;
        }
        return out;
    }
    std::vector<std::string_view> lines (std::string_view text)
    {
        std::vector<std::string_view> out;
        size_t i = 0;
        while (true)
        {
            const size_t j = text.find ('\n', i);
            out.push_back (text.substr (i, j == std::string_view::npos ? std::string_view::npos : j - i));
            if (j == std::string_view::npos) break;
            i = j + 1;
        }
        return out;
    }
    /** `Number (word)` for the files' words: "" is 0; a plain decimal number is itself; anything else (a name, a
        colour) is NaN. JS's hex and Infinity are no file's. */
    double jsNumber (const std::string& w)
    {
        if (w.empty()) return 0;
        if (w.find_first_not_of ("0123456789+-.eE") != std::string::npos) return nan;
        char* end = nullptr;
        const double v = std::strtod (w.c_str(), &end);
        return end == w.c_str() + w.size() ? v : nan;
    }
    double word (const std::vector<std::string>& w, size_t i) { return i < w.size() ? jsNumber (w[i]) : nan; }
    bool isWordChar (char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
    /** Reads digits at i (at least one); false if none. */
    bool digits (std::string_view s, size_t& i, std::string& out, bool dots)
    {
        const size_t i0 = i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || (dots && s[i] == '.'))) ++i;
        out = std::string (s.substr (i0, i - i0));
        return i > i0;
    }
    bool literal (std::string_view s, size_t& i, std::string_view lit) { if (s.substr (i, lit.size()) != lit) return false; i += lit.size(); return true; }
}

//==============================================================================
uint32_t CaliforniaWorld::ih (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
double CaliforniaWorld::hcell (int32_t cx, int32_t cy, int32_t s) noexcept
{
    const uint32_t inner = ih (((uint32_t) cy * 0x85EBCA77u) ^ (uint32_t) s);
    return (double) (float) ((double) ih (((uint32_t) cx * 0x9E3779B1u) ^ inner) / 4294967296.0);
}
double CaliforniaWorld::hs (int32_t i, int32_t s) noexcept
{
    return (double) (float) ((double) ih (((uint32_t) i * 0x9E3779B1u) ^ ih ((uint32_t) s * 0x85EBCA77u)) / 4294967296.0);
}
double CaliforniaWorld::toHalf (double v) noexcept
{
    const double a = std::abs (v);
    if (a < 6.103515625e-05) return Js::round (v / 5.960464477539063e-08) * 5.960464477539063e-08;   // subnormal
    // Math.pow (2, Math.floor (Math.log2 (a)) - 10): the exponent exactly; where V8's log2 rounds up to the next
    // power of two just below one, the value rounds to that power either way
    const double q = std::ldexp (1.0, std::ilogb (a) - 10);
    return clamp (Js::round (v / q) * q, -65504, 65504);
}
CaliforniaWorld::Basis CaliforniaWorld::cameraBasis (double heading, double pitch) noexcept
{
    const double cp = Js::cos (pitch), sp = Js::sin (pitch), f0x = Js::sin (heading), f0z = -Js::cos (heading);
    return { { Js::cos (heading), 0, Js::sin (heading) }, { -f0x * sp, cp, -f0z * sp }, { f0x * cp, sp, f0z * cp } };
}
CaliforniaWorld::V3 CaliforniaWorld::normalised (const V3& v) noexcept
{
    const double l = orOne (Js::hypot (v[0], v[1], v[2]));
    return { v[0] / l, v[1] / l, v[2] / l };
}
std::array<float, 9> CaliforniaWorld::tiltOf (V2 lean, double k) noexcept
{
    const double lx = lean[0], lz = lean[1];
    const double th = std::max (Js::hypot (lx, lz), 1e-6);
    const double kx = lz / th, kz = -lx / th, c = Js::cos (th), sn = Js::sin (th), t = 1 - c;
    const std::array<double, 9> m { c + t * kx * kx, sn * kz, t * kx * kz, -sn * kz, c, sn * kx, t * kx * kz, -sn * kx, c + t * kz * kz };
    std::array<float, 9> o {};
    for (size_t i = 0; i < 9; ++i) o[i] = (float) (m[i] * k);
    return o;
}

//==============================================================================
std::vector<float> CaliforniaWorld::makeHeight()
{
    const int n = texSize, nn = n * n;
    std::vector<float> R ((size_t) nn), Gc ((size_t) nn), B ((size_t) nn), A ((size_t) nn);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n, v = (double) y / n;
            const size_t i = (size_t) (y * n + x);
            R[i] = (float) fbm (u, v, 3, 4, 0.45, 101);
            Gc[i] = (float) fbm (u, v, 2, 3, 0.55, 202);
            B[i] = (float) fbm (u, v, 3, 3, 0.5, 303);
            A[i] = (float) fbm (u, v, 4, 2, 0.5, 404);
        }
    normalize01 (R); normalize01 (Gc); normalize01 (B); normalize01 (A);
    std::vector<float> H ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i) { H[i * 4] = R[i]; H[i * 4 + 1] = Gc[i]; H[i * 4 + 2] = B[i]; H[i * 4 + 3] = A[i]; }
    for (auto& h : H) h = (float) toHalf (h);
    return H;
}

std::vector<uint8_t> CaliforniaWorld::makeDetail()
{
    const int d = detailSize, m = d - 1;
    std::vector<float> v ((size_t) (d * d)), g ((size_t) (d * d));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double u = (double) x / d, w = (double) y / d;
            const double s1 = fbm (u, w, 40, 2, 0.5, 505, 6);
            const double s2 = fbm (fract (u - w), w, 30, 2, 0.5, 515, 8);
            const double grit = fbm (u, w, 24, 2, 0.5, 606);
            v[(size_t) (y * d + x)] = (float) (0.5 * s1 + 0.3 * s2 + 0.2 * grit);
            g[(size_t) (y * d + x)] = (float) fbm (u, w, 48, 3, 0.55, 707);
        }
    normalize01 (v); normalize01 (g);
    std::vector<uint8_t> px ((size_t) (d * d * 4));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double gx = ((double) v[(size_t) (y * d + ((x + 1) & m))] - v[(size_t) (y * d + ((x - 1) & m))]) * 3;
            const double gy = ((double) v[(size_t) (((y + 1) & m) * d + x)] - v[(size_t) (((y - 1) & m) * d + x)]) * 3;
            const size_t i = (size_t) (y * d + x) * 4;
            px[i]     = trunc8 (128 + clamp (gx * 127, -127, 127));
            px[i + 1] = trunc8 (128 + clamp (gy * 127, -127, 127));
            px[i + 2] = trunc8 ((double) v[(size_t) (y * d + x)] * 255);
            px[i + 3] = trunc8 ((double) g[(size_t) (y * d + x)] * 255);
        }
    return px;
}

std::vector<uint8_t> CaliforniaWorld::makeNoise()
{
    const int n = noiseSize, nn = n * n;
    std::array<std::vector<float>, 4> ch;
    for (auto& c : ch) c.resize ((size_t) nn);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n, v = (double) y / n;
            const size_t i = (size_t) (y * n + x);
            ch[0][i] = (float) fbm (u, v, 4, 5, 0.5, 1101);
            ch[1][i] = (float) fbm (u, v, 8, 4, 0.5, 1202);
            ch[2][i] = (float) fbm (u, v, 3, 3, 0.5, 1303);
            ch[3][i] = (float) fbm (u, v, 6, 3, 0.5, 1404);
        }
    for (auto& c : ch) normalize01 (c);
    std::vector<uint8_t> px ((size_t) nn * 4);
    for (size_t i = 0; i < (size_t) nn; ++i) for (size_t k = 0; k < 4; ++k) px[i * 4 + k] = trunc8 ((double) ch[k][i] * 255);
    return px;
}

//==============================================================================
CaliforniaWorld::Radius CaliforniaWorld::loopR (double th) noexcept
{
    double R = loopRadius, dR = 0;
    for (size_t k = 0; k < 4; ++k) { const double a = loopK[k] * th + loopP[k]; R += loopA[k] * Js::cos (a); dR -= loopA[k] * loopK[k] * Js::sin (a); }
    return { R, dR };
}
CaliforniaWorld::V2 CaliforniaWorld::loopPoint (double th) noexcept
{
    const double R = loopR (th).R;
    return { loopCentre[0] + R * Js::cos (th), loopCentre[1] + R * Js::sin (th) };
}
const std::vector<double>& CaliforniaWorld::loopTable()
{
    static const std::vector<double> t = []
    {
        std::vector<double> v ((size_t) loopN + 1, 0.0);
        double acc = 0;
        V2 prev = loopPoint (0);
        for (int i = 1; i <= loopN; ++i) { const auto p = loopPoint ((double) i / loopN * 2 * pi); acc += Js::hypot (p[0] - prev[0], p[1] - prev[1]); v[(size_t) i] = acc; prev = p; }
        return v;
    }();
    return t;
}
double CaliforniaWorld::loopLength() { return loopTable()[(size_t) loopN]; }
double CaliforniaWorld::modL (double v) { const double L = loopLength(); return mod (mod (v, L) + L, L); }
double CaliforniaWorld::thetaAt (double s)
{
    const auto& S = loopTable();
    const double q = modL (s);
    int lo = 0, hi = loopN;
    while (hi - lo > 1) { const int mid = (lo + hi) >> 1; if (S[(size_t) mid] <= q) lo = mid; else hi = mid; }
    return (lo + (q - S[(size_t) lo]) / std::max (S[(size_t) lo + 1] - S[(size_t) lo], 1e-9)) / loopN * 2 * pi;
}
CaliforniaWorld::V2 CaliforniaWorld::tangentAt (double th) noexcept
{
    const auto [R, dR] = loopR (th);
    const double c = Js::cos (th), sn = Js::sin (th), tx = dR * c - R * sn, tz = dR * sn + R * c, l = Js::hypot (tx, tz);
    return { tx / l, tz / l };
}
CaliforniaWorld::V2 CaliforniaWorld::rightAt (double th) noexcept { const auto t = tangentAt (th); return { -t[1], t[0] }; }
double CaliforniaWorld::sOfTheta (double th)
{
    const auto& S = loopTable();
    const double q = mod (mod (th / (2 * pi), 1) + 1, 1) * loopN;
    const int i = (int) std::min (std::floor (q), (double) loopN - 1);
    return S[(size_t) i] + (S[(size_t) i + 1] - S[(size_t) i]) * (q - i);
}
CaliforniaWorld::V2 CaliforniaWorld::roadPointAt (double s, double d)
{
    const double th = thetaAt (s);
    const auto p = loopPoint (th), r = rightAt (th);
    return { p[0] + d * r[0], p[1] + d * r[1] };
}
int CaliforniaWorld::polesN() { static const int n = (int) Js::round (loopLength() / poleStep); return n; }
double CaliforniaWorld::poleDs() { return loopLength() / polesN(); }
int CaliforniaWorld::dashesN() { static const int n = (int) Js::round (loopLength() / 12); return n; }
CaliforniaWorld::V3 CaliforniaWorld::sunDir() noexcept { return { Js::sin (sunAz) * Js::cos (sunElev), Js::sin (sunElev), -Js::cos (sunAz) * Js::cos (sunElev) }; }
namespace
{
    V2 sunXZ() noexcept { const auto S = W::sunDir(); const double l = Js::hypot (S[0], S[2]); return { S[0] / l, S[2] / l }; }
    // Math.tan has no V8 port in the walker's Js: the C library's, which agrees but for the last bit now and then.
    double sunTan() noexcept { return std::tan (W::sunElev); }
    double headingAt (double sr) { const auto t = W::tangentAt (W::thetaAt (sr)); return Js::atan2 (t[0], -t[1]); }
    double cyc (double a, double b) { const double L = W::loopLength(); return std::abs (mod (a - b + L * 1.5, L) - L / 2); }
}
CaliforniaWorld::V2 CaliforniaWorld::wind() noexcept
{
    const auto s = sunXZ();
    const double a = Js::atan2 (-s[1], -s[0]) + 0.35;
    return { Js::cos (a) * windSpeed, Js::sin (a) * windSpeed };
}

//==============================================================================
CaliforniaWorld::Icosphere CaliforniaWorld::icosphere (int sub)
{
    const double t = (1 + std::sqrt (5.0)) / 2;
    Icosphere m;
    for (const V3& p : std::array<V3, 12> { { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                                             { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } } })
        m.V.push_back (normalised (p));
    m.F = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
            { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
    for (int k = 0; k < sub; ++k)
    {
        std::map<int64_t, int> cache;
        const auto mid = [&] (int a, int b)
        {
            const int64_t key = a < b ? (int64_t) a * 65536 + b : (int64_t) b * 65536 + a;
            if (const auto it = cache.find (key); it != cache.end()) return it->second;
            const V3 va = m.V[(size_t) a], vb = m.V[(size_t) b];
            m.V.push_back (normalised ({ (va[0] + vb[0]) / 2, (va[1] + vb[1]) / 2, (va[2] + vb[2]) / 2 }));
            cache[key] = (int) m.V.size() - 1;
            return (int) m.V.size() - 1;
        };
        std::vector<std::array<int, 3>> F2;
        for (const auto& f : m.F)
        {
            const int a = mid (f[0], f[1]), b = mid (f[1], f[2]), c = mid (f[2], f[0]);
            F2.push_back ({ f[0], a, c }); F2.push_back ({ f[1], b, a }); F2.push_back ({ f[2], c, b }); F2.push_back ({ a, b, c });
        }
        m.F = std::move (F2);
    }
    return m;
}

CaliforniaWorld::Mesh CaliforniaWorld::oakMesh (int detail)
{
    const bool near = detail > 0;
    Builder m;
    m.tube ({ { 0, -0.1, 0 }, { 0.02, 0.25, 0 }, { 0.05, 0.48, 0.02 } }, near ? 7 : 5, { 0.11, 0.085, 0.07 }, 0);
    if (detail > 1)
        for (int i = 0; i < 3; ++i)
        {
            const double a = (double) i / 3 * 2 * pi + 0.4;
            m.tube ({ { 0.05, 0.45, 0.02 }, { 0.35 * Js::cos (a), 0.66, 0.35 * Js::sin (a) }, { 0.55 * Js::cos (a), 0.78, 0.55 * Js::sin (a) } }, 4, { 0.06, 0.04, 0.02 }, 0);
        }
    const std::vector<std::array<V3, 2>> blobs = near
        ? std::vector<std::array<V3, 2>> { { { { 0, 0.92, 0 }, { 0.62, 0.42, 0.62 } } }, { { { 0.52, 0.80, 0.12 }, { 0.42, 0.34, 0.42 } } },
                                           { { { -0.46, 0.82, 0.24 }, { 0.44, 0.33, 0.40 } } }, { { { 0.06, 0.78, -0.55 }, { 0.42, 0.33, 0.40 } } },
                                           { { { -0.22, 0.76, 0.58 }, { 0.38, 0.30, 0.38 } } }, { { { 0.40, 0.72, -0.40 }, { 0.36, 0.28, 0.36 } } },
                                           { { { -0.52, 0.70, -0.30 }, { 0.34, 0.28, 0.36 } } } }
        : std::vector<std::array<V3, 2>> { { { { 0, 0.88, 0 }, { 0.92, 0.46, 0.88 } } }, { { { 0.35, 0.78, 0.30 }, { 0.55, 0.32, 0.5 } } } };
    for (size_t k = 0; k < blobs.size(); ++k) m.blob (blobs[k][0], blobs[k][1], detail > 1 ? 2 : 1, 900 + (int) k);
    return m.mesh();
}

CaliforniaWorld::Mesh CaliforniaWorld::poleMesh()
{
    Builder m;
    m.tube ({ { 0, -0.3, 0 }, { 0, 4.5, 0 }, { 0, 9.0, 0 } }, 8, { 0.15, 0.13, 0.11 }, 0);
    m.box ({ 0, 8.35, 0 }, { 1.0, 0.07, 0.06 }, 0);
    m.box ({ -0.10, 8.05, 0.0 }, { 0.03, 0.35, 0.04 }, 0);
    for (const double x : { -0.85, 0.85 }) m.box ({ x, 8.48, 0 }, { 0.04, 0.06, 0.04 }, 2);
    m.box ({ 0, 9.06, 0 }, { 0.04, 0.06, 0.04 }, 2);
    return m.mesh();
}

CaliforniaWorld::Mesh CaliforniaWorld::pumpTowerMesh()
{
    Builder m;
    const auto bar = [&m] (const V3& a, const V3& b, double w, double part) { m.tube ({ a, b }, 4, { w, w }, part); };
    const auto leg = [] (double y, double sx, double sz) { const double k = 1 - y / 10.6; return V3 { sx * (0.3 + 1.6 * k), y, sz * (0.3 + 1.6 * k) }; };
    const std::array<V2, 4> corners { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } };
    for (const auto& [sx, sz] : corners) bar (leg (-0.3, sx, sz), leg (10.2, sx, sz), 0.05, 0);
    for (const double y : { 2.5, 5.0, 7.5, 9.6 })
        for (size_t c = 0; c < 4; ++c) { const auto& [ax, az] = corners[c]; const auto& [bx, bz] = corners[(c + 1) % 4]; bar (leg (y, ax, az), leg (y, bx, bz), 0.03, 0); }
    for (const auto& [y0, y1] : std::array<V2, 4> { { { 0.0, 2.5 }, { 2.5, 5.0 }, { 5.0, 7.5 }, { 7.5, 9.6 } } })
        for (size_t c = 0; c < 4; ++c)
        {
            const auto& [ax, az] = corners[c];
            const auto& [bx, bz] = corners[(c + 1) % 4];
            bar (leg (y0, ax, az), leg (y1, bx, bz), 0.02, 0); bar (leg (y0, bx, bz), leg (y1, ax, az), 0.02, 0);
        }
    m.box ({ 0, 9.7, 0 }, { 0.75, 0.04, 0.75 }, 0);
    m.box ({ 0, 10.4, 0.0 }, { 0.18, 0.22, 0.45 }, 0);
    bar ({ 0, 10.4, 0.3 }, { 0, 10.5, 2.6 }, 0.035, 0);
    m.box ({ 0, 10.6, 2.75 }, { 0.02, 0.55, 0.65 }, 3);
    return m.mesh();
}

CaliforniaWorld::Mesh CaliforniaWorld::pumpFanMesh()
{
    Builder m;
    for (int k = 0; k < 18; ++k)
    {
        const double a = (double) k / 18 * 2 * pi, ca = Js::cos (a), sa = Js::sin (a), w = 0.13, tw = 0.35;
        const auto pt = [&] (double r, double u) { return V3 { ca * r - sa * u * Js::cos (tw), sa * r + ca * u * Js::cos (tw), u * Js::sin (tw) }; };
        const auto nrm = normalised ({ sa * Js::sin (tw), -ca * Js::sin (tw), -Js::cos (tw) });
        std::array<int, 4> q {};
        const std::array<V3, 4> ps { pt (0.35, -w * 0.6), pt (1.5, -w), pt (1.5, w), pt (0.35, w * 0.6) };
        for (size_t i = 0; i < 4; ++i) q[i] = m.vert (ps[i], nrm, 1);
        m.idx.insert (m.idx.end(), { q[0], q[1], q[2], q[0], q[2], q[3] });
    }
    for (const double r : { 0.9, 1.45 })
    {
        std::vector<V3> ring;
        for (int k = 0; k <= 24; ++k) { const double a = (double) k / 24 * 2 * pi; ring.push_back ({ Js::cos (a) * r, Js::sin (a) * r, 0.02 }); }
        m.tube (ring, 3, std::vector<double> (ring.size(), 0.015), 0);
    }
    m.box ({ 0, 0, 0 }, { 0.12, 0.12, 0.12 }, 0);
    return m.mesh();
}

std::array<CaliforniaWorld::Mesh, 3> CaliforniaWorld::floraMeshes()
{
    struct Build
    {
        std::vector<double> v;
        std::vector<int> idx;
        int vert (const V3& p, const V3& n, const V3& c, V2 uv, double flag, double sway)
        {
            v.insert (v.end(), { p[0], p[1], p[2], n[0], n[1], n[2], c[0], c[1], c[2], uv[0], uv[1], flag, sway });
            return (int) (v.size() / 13) - 1;
        }
        void quad (int a, int b, int c, int d) { idx.insert (idx.end(), { a, b, c, a, c, d }); }
        void ribbon (const std::vector<V3>& pts, const std::vector<double>& widths, const V3& side, const std::vector<V3>& cols, double flag,
                     const std::function<double (const V3&)>& swayOf)
        {
            std::vector<std::array<int, 2>> rows;
            for (size_t k = 0; k < pts.size(); ++k)
            {
                const auto& p = pts[k];
                const auto& q = pts[std::min (k + 1, pts.size() - 1)];
                const auto& o = pts[k > 0 ? k - 1 : 0];
                const auto d = W::normalised ({ q[0] - o[0], q[1] - o[1], q[2] - o[2] });
                const auto n = W::normalised ({ d[1] * side[2] - d[2] * side[1], d[2] * side[0] - d[0] * side[2], d[0] * side[1] - d[1] * side[0] });
                const double w = widths[k] / 2, t = (double) k / (double) (pts.size() - 1);
                if (w <= 0) { const int a = vert (p, n, cols[k], { 0.5, t }, flag, swayOf (p)); rows.push_back ({ a, a }); continue; }
                const int a = vert ({ p[0] - side[0] * w, p[1] - side[1] * w, p[2] - side[2] * w }, n, cols[k], { 0, t }, flag, swayOf (p));
                const int b = vert ({ p[0] + side[0] * w, p[1] + side[1] * w, p[2] + side[2] * w }, n, cols[k], { 1, t }, flag, swayOf (p));
                rows.push_back ({ a, b });
            }
            for (size_t k = 0; k + 1 < rows.size(); ++k)
            {
                if (rows[k + 1][0] == rows[k + 1][1]) idx.insert (idx.end(), { rows[k][0], rows[k][1], rows[k + 1][0] });
                else quad (rows[k][0], rows[k][1], rows[k + 1][1], rows[k + 1][0]);
            }
        }
        Mesh mesh() const
        {
            Mesh m;
            for (const double x : v) m.v.push_back ((float) x);
            for (const int i : idx) m.idx.push_back ((uint16_t) i);
            return m;
        }
    };
    std::array<Mesh, 3> out;
    {   // 0: a clump of poppies: stems, a cup of four petals each, a heart, a lacy leaf
        Build m;
        for (int p = 0; p < 3; ++p)
        {
            const double a = (double) p / 3 * 2 * pi + 0.7 * hs (p + 1, 401), r = p == 0 ? 0.05 : 0.30 + 0.35 * hs (p + 1, 402);
            const double bx = r * Js::cos (a), bz = r * Js::sin (a), h = 0.72 + 0.28 * hs (p + 1, 403);
            const double lx = 0.10 * (hs (p + 1, 404) - 0.5), lz = 0.10 * (hs (p + 1, 405) - 0.5);
            const V3 top { bx + lx, h, bz + lz };
            const auto sway = [h] (const V3& q) { return clamp (q[1] / h, 0, 1); };
            const V3 stemC { 0.40, 0.50, 0.30 };
            const std::vector<V3> pts { { bx, 0, bz }, { bx + lx * 0.3, h * 0.5, bz + lz * 0.3 }, top };
            m.ribbon (pts, { 0.030, 0.026, 0.022 }, { 1, 0, 0 }, { stemC, stemC, stemC }, 0, sway);
            m.ribbon (pts, { 0.030, 0.026, 0.022 }, { 0, 0, 1 }, { stemC, stemC, stemC }, 0, sway);
            const double leafA = a + 1.3;
            const V3 leafC { 0.42, 0.54, 0.44 };
            m.ribbon ({ { bx, 0.01, bz }, { bx + 0.22 * Js::cos (leafA), 0.10, bz + 0.22 * Js::sin (leafA) }, { bx + 0.40 * Js::cos (leafA), 0.06, bz + 0.40 * Js::sin (leafA) } },
                      { 0.09, 0.11, 0.0 }, { -Js::sin (leafA), 0, Js::cos (leafA) }, { leafC, leafC, leafC }, 0, [] (const V3&) { return 0.25; });
            const double hue = hs (p + 1, 406);
            const V3 pc { 1.0, 0.36 + 0.14 * hue, 0.02 + 0.03 * hue };
            const double open = (32 + 14 * hs (p + 1, 407)) * pi / 180, L = 0.26, Wd = 0.30;
            for (int k = 0; k < 4; ++k)
            {
                const double psi = (double) k / 4 * 2 * pi + 0.3 * hs (p + 1, 408);
                const V3 outv { Js::cos (psi), 0, Js::sin (psi) }, side { -Js::sin (psi), 0, Js::cos (psi) };
                const V3 axis { outv[0] * Js::sin (open), Js::cos (open), outv[2] * Js::sin (open) };
                const V3 inward { -outv[0] * Js::cos (open), Js::sin (open), -outv[2] * Js::cos (open) };
                std::array<std::array<int, 3>, 2> grid {};
                for (int j = 0; j <= 1; ++j)
                    for (int i = 0; i <= 2; ++i)
                    {
                        const double u = i - 1, vv = j;
                        const double wd = Wd / 2 * (0.35 + 0.65 * vv);
                        const double curl = 0.06 * u * u + 0.02 * vv * vv;
                        const V3 q { top[0] + axis[0] * vv * L + side[0] * u * wd + inward[0] * curl,
                                     top[1] + axis[1] * vv * L + inward[1] * curl,
                                     top[2] + axis[2] * vv * L + side[2] * u * wd + inward[2] * curl };
                        grid[(size_t) j][(size_t) i] = m.vert (q, inward, pc, { i / 2.0, vv }, 2, 1);
                    }
                for (size_t i = 0; i < 2; ++i) m.quad (grid[0][i], grid[0][i + 1], grid[1][i + 1], grid[1][i]);
            }
            const V3 hc { 0.50, 0.52, 0.14 };
            const int c0 = m.vert ({ top[0], top[1] + 0.05, top[2] }, { 0, 1, 0 }, hc, { 0.5, 0.5 }, 3, 1);
            std::array<int, 4> ring {};
            for (int k = 0; k < 4; ++k)
            {
                const double ang = (double) k / 4 * 2 * pi;
                ring[(size_t) k] = m.vert ({ top[0] + 0.045 * Js::cos (ang), top[1] + 0.01, top[2] + 0.045 * Js::sin (ang) }, { Js::cos (ang), 0.5, Js::sin (ang) }, hc, { 0.5, 0.5 }, 3, 1);
            }
            for (size_t k = 0; k < 4; ++k) m.idx.insert (m.idx.end(), { ring[k], c0, ring[(k + 1) % 4] });
        }
        out[0] = m.mesh();
    }
    {   // 1: lupine: a stem, a spike of pea flowers spiralling up its top half, leaves
        Build m;
        const auto sway = [] (const V3& q) { return clamp (q[1], 0, 1); };
        const V3 sc { 0.36, 0.46, 0.30 };
        const std::vector<V3> pts { { 0, 0, 0 }, { 0.02, 0.5, 0.01 }, { 0.03, 1.0, 0.02 } };
        m.ribbon (pts, { 0.035, 0.03, 0.02 }, { 1, 0, 0 }, { sc, sc, sc }, 0, sway);
        m.ribbon (pts, { 0.035, 0.03, 0.02 }, { 0, 0, 1 }, { sc, sc, sc }, 0, sway);
        for (int k = 0; k < 5; ++k)
        {
            const double a = (double) k / 5 * 2 * pi + 0.3;
            const V3 lc { 0.36, 0.50, 0.40 };
            m.ribbon ({ { 0, 0.12, 0 }, { 0.16 * Js::cos (a), 0.17, 0.16 * Js::sin (a) }, { 0.26 * Js::cos (a), 0.13, 0.26 * Js::sin (a) } },
                      { 0.02, 0.07, 0.0 }, { -Js::sin (a), 0, Js::cos (a) }, { lc, lc, lc }, 0, [] (const V3&) { return 0.15; });
        }
        for (int k = 0; k < 12; ++k)
        {
            const double t = (double) k / 11, y = 0.45 + 0.53 * t, a = k * 2.4, r = 0.075 * (1 - 0.6 * t);
            const double cx = 0.03 * y + r * Js::cos (a), cz = 0.02 * y + r * Js::sin (a);
            const V3 outv { Js::cos (a), 0, Js::sin (a) }, side { -Js::sin (a), 0, Js::cos (a) };
            const double s2 = 0.055 * (1 - 0.45 * t);
            const V3 col = t > 0.8 ? V3 { 0.56, 0.44, 0.92 } : V3 { 0.38, 0.28, 0.84 };
            const V3 tipC { 0.62, 0.55, 0.95 };
            const int q0 = m.vert ({ cx - side[0] * s2, y - s2 * 0.6, cz - side[2] * s2 }, outv, col, { 0, 0 }, 2, y);
            const int q1 = m.vert ({ cx + side[0] * s2, y - s2 * 0.6, cz + side[2] * s2 }, outv, col, { 1, 0 }, 2, y);
            const int q2 = m.vert ({ cx + side[0] * s2 + outv[0] * 0.02, y + s2, cz + side[2] * s2 + outv[2] * 0.02 }, outv, tipC, { 1, 1 }, 2, y);
            const int q3 = m.vert ({ cx - side[0] * s2 + outv[0] * 0.02, y + s2, cz - side[2] * s2 + outv[2] * 0.02 }, outv, tipC, { 0, 1 }, 2, y);
            m.quad (q0, q1, q2, q3);
        }
        out[1] = m.mesh();
    }
    {   // 2: a tuft of dry grass: tapering blades bent by their weight, two wild oats with nodding spikelets
        Build m;
        const auto sway = [] (const V3& q) { return clamp (q[1], 0, 1); };
        for (int k = 0; k < 6; ++k)
        {
            const double a = (double) k / 6 * 2 * pi + 0.5 * hs (k + 1, 501), lean = 0.10 + 0.25 * hs (k + 1, 502), h = 0.55 + 0.35 * hs (k + 1, 503);
            const V3 d { Js::cos (a), 0, Js::sin (a) };
            const V3 base { 0.04 * d[0], 0, 0.04 * d[2] };
            const std::vector<V3> pts { base, { base[0] + d[0] * lean * 0.3, h * 0.45, base[2] + d[2] * lean * 0.3 },
                                        { base[0] + d[0] * lean * 0.75, h * 0.82, base[2] + d[2] * lean * 0.75 }, { base[0] + d[0] * lean * 1.3, h, base[2] + d[2] * lean * 1.3 } };
            const V3 c0 { 0.52, 0.40, 0.18 }, c1 { 0.84, 0.64, 0.30 }, c2 { 0.92, 0.76, 0.42 };
            m.ribbon (pts, { 0.045, 0.035, 0.022, 0.0 }, { -Js::sin (a), 0, Js::cos (a) }, { c0, c1, c2, c2 }, 1, sway);
        }
        for (int k = 0; k < 2; ++k)
        {
            const double a = k * pi + 0.8, h = 0.95 + 0.1 * k;
            const V3 d { Js::cos (a), 0, Js::sin (a) }, side { -Js::sin (a), 0, Js::cos (a) };
            const V3 sc { 0.80, 0.70, 0.44 };
            const std::vector<V3> pts { { 0, 0, 0 }, { d[0] * 0.05, h * 0.5, d[2] * 0.05 }, { d[0] * 0.12, h, d[2] * 0.12 } };
            m.ribbon (pts, { 0.018, 0.014, 0.010 }, side, { sc, sc, sc }, 1, sway);
            for (int j = 0; j < 4; ++j)
            {
                const double y = h * (0.72 + 0.07 * j);
                const int sd = j % 2 ? 1 : -1;
                const V3 at { d[0] * (0.12 * y / h) + side[0] * 0.02 * sd, y, d[2] * (0.12 * y / h) + side[2] * 0.02 * sd };
                const V3 tip { at[0] + side[0] * 0.06 * sd + d[0] * 0.02, y - 0.10, at[2] + side[2] * 0.06 * sd + d[2] * 0.02 };
                const V3 oc { 0.93, 0.80, 0.48 };
                m.ribbon ({ at, { (at[0] + tip[0]) / 2, (at[1] + tip[1]) / 2, (at[2] + tip[2]) / 2 }, tip }, { 0.02, 0.035, 0.0 }, d, { oc, oc, oc }, 1, sway);
            }
        }
        out[2] = m.mesh();
    }
    return out;
}

CaliforniaWorld::Mesh CaliforniaWorld::butterflyMesh()
{
    std::vector<double> v;
    std::vector<int> idx;
    const auto vert = [&v] (const V3& p, V2 uv, double part) { v.insert (v.end(), { p[0], p[1], p[2], uv[0], uv[1], part }); return (int) (v.size() / 6) - 1; };
    for (const double s : { -1.0, 1.0 })
    {
        const int a = vert ({ 0, 0, -0.65 }, { 0, -0.65 }, 1), b = vert ({ s, 0, -0.65 }, { 1, -0.65 }, 1), c = vert ({ s, 0, 0.85 }, { 1, 0.85 }, 1), d = vert ({ 0, 0, 0.85 }, { 0, 0.85 }, 1);
        idx.insert (idx.end(), { a, b, c, a, c, d });
    }
    for (const auto& [ux, uy] : std::array<V2, 2> { { { 1, 0 }, { 0, 1 } } })
    {
        const double w = 0.045;
        const int a = vert ({ -ux * w, -uy * w, -0.45 }, { 0, 0 }, 0), b = vert ({ ux * w, uy * w, -0.45 }, { 0, 0 }, 0);
        const int c = vert ({ ux * w, uy * w, 0.42 }, { 0, 0 }, 0), d = vert ({ -ux * w, -uy * w, 0.42 }, { 0, 0 }, 0);
        idx.insert (idx.end(), { a, b, c, a, c, d });
    }
    Mesh m;
    for (const double x : v) m.v.push_back ((float) x);
    for (const int i : idx) m.idx.push_back ((uint16_t) i);
    return m;
}

//==============================================================================
CaliforniaWorld::SpriteTable CaliforniaWorld::parseSpriteTable (std::string_view text)
{
    SpriteTable t;
    for (const auto line : lines (text))
    {
        const auto w = words (line);
        if (! line.empty() && line[0] == '#' && line.find ("px per metre") != std::string_view::npos)
        {
            // /cells (\d+) x (\d+) px, ([\d.]+) px per metre/: the leftmost match
            for (size_t at = line.find ("cells "); at != std::string_view::npos; at = line.find ("cells ", at + 1))
            {
                size_t i = at + 6;
                std::string a, b, c;
                if (digits (line, i, a, false) && literal (line, i, " x ") && digits (line, i, b, false) && literal (line, i, " px, ")
                    && digits (line, i, c, true) && literal (line, i, " px per metre"))
                {
                    t.cell = { jsNumber (a), jsNumber (b) }; t.scale = jsNumber (c);
                    break;
                }
            }
            for (size_t at = line.find ("atlas "); at != std::string_view::npos; at = line.find ("atlas ", at + 1))
            {
                size_t i = at + 6;
                std::string a, b;
                if (digits (line, i, a, false) && literal (line, i, " x ") && digits (line, i, b, false))
                {
                    t.grid = { (int) jsNumber (a), (int) jsNumber (b) };
                    break;
                }
            }
        }
        else if (w[0] == "anchor") t.anchor = { word (w, 1), word (w, 2) };
        else if (w[0] == "cell") ++t.count;
    }
    return t;
}

std::optional<CaliforniaWorld::PendantBox> CaliforniaWorld::parsePendantBox (std::string_view text)
{
    std::optional<V4> box;
    std::optional<V2> pivot;
    for (const auto line : lines (text))
    {
        const auto w = words (line);
        if (w[0] == "box") box = V4 { word (w, 1), word (w, 2), word (w, 3), word (w, 4) };
        else if (w[0] == "pivot") pivot = V2 { word (w, 1), word (w, 2) };
    }
    if (! box || ! pivot) return std::nullopt;
    return PendantBox { *box, *pivot };
}

std::vector<CaliforniaWorld::Sign> CaliforniaWorld::parseSigns (std::string_view text)
{
    std::vector<Sign> t;
    for (const auto line : lines (text))
    {
        const auto w = words (line);
        if (w[0] != "sign" || w.size() < 11) continue;
        t.push_back ({ w[1], w[2], w[10], { word (w, 3), word (w, 4), word (w, 5), word (w, 6) }, { word (w, 7), word (w, 8) }, word (w, 9) });
    }
    return t;
}

namespace
{
    std::vector<double> numbersOf (std::string_view line)
    {
        std::vector<double> nums;
        for (const auto& x : words (line)) { const double v = jsNumber (x); if (! std::isnan (v)) nums.push_back (v); }
        return nums;
    }
}

std::vector<CaliforniaWorld::V4> CaliforniaWorld::parseFlagCells (std::string_view text, int w, int h)
{
    std::vector<V4> cells;
    for (const auto line : lines (text))
    {
        auto nums = numbersOf (line);
        if (nums.size() > 4) nums.erase (nums.begin(), nums.end() - 4);
        const auto t = trim (line);
        if (! (! t.empty() && t[0] == '#') && nums.size() == 4)
            cells.push_back ({ nums[0] / w, nums[1] / h, (nums[0] + nums[2]) / w, (nums[1] + nums[3]) / h });
    }
    return cells;
}

std::vector<CaliforniaWorld::ChalkDrawing> CaliforniaWorld::parseChalk (std::string_view text, int w, int h)
{
    std::vector<ChalkDrawing> t;
    for (const auto line : lines (text))
    {
        const auto tl = trim (line);
        if (! tl.empty() && tl[0] == '#') continue;
        const auto nums = numbersOf (line);
        if (nums.size() < 6) continue;
        bool optional = false;                                    // /\boptional\b/
        for (size_t at = line.find ("optional"); at != std::string_view::npos && ! optional; at = line.find ("optional", at + 1))
            optional = (at == 0 || ! isWordChar (line[at - 1])) && (at + 8 >= line.size() || ! isWordChar (line[at + 8]));
        const auto wl = words (line);
        t.push_back ({ wl.size() > 1 ? wl[1] : std::string(), optional,
                       { nums[0] / w, nums[1] / h, (nums[0] + nums[2]) / w, (nums[1] + nums[3]) / h }, { nums[4], nums[5] } });
    }
    return t;
}

CaliforniaWorld::MeshInfo CaliforniaWorld::parseMeshInfo (std::string_view text)
{
    MeshInfo m;
    for (const auto line : lines (text))
    {
        const auto w = words (line);
        if (w[0] == "texture")
        {
            const double n = word (w, 1) - 1;
            if (n >= 0 && n == std::floor (n) && n < 1024)
            {
                if (m.textures.size() <= (size_t) n) m.textures.resize ((size_t) n + 1);
                m.textures[(size_t) n] = w.size() > 2 ? w[2] : std::string();
            }
        }
        if (w[0] == "headstock" && w.size() >= 8)
            m.censor.push_back ({ w[1], { jsNumber (w[2]), jsNumber (w[3]), jsNumber (w[4]) }, { jsNumber (w[5]), jsNumber (w[6]), jsNumber (w[7]) } });
    }
    return m;
}

std::optional<CaliforniaWorld::CbMesh> CaliforniaWorld::parseCbMesh (const uint8_t* data, size_t size, const std::vector<V2>& parts)
{
    if (data == nullptr || size < 16) return std::nullopt;
    const auto u32 = [data] (size_t at) { uint32_t v; std::memcpy (&v, data + at, 4); return v; };     // little-endian, as every target is
    const uint32_t nv = u32 (8), ni = u32 (12);
    if (std::memcmp (data, "CBMESH1", 7) != 0 || (uint64_t) size < 16 + (uint64_t) nv * 40 + (uint64_t) ni * 4) return std::nullopt;
    CbMesh m;
    m.vertices = nv; m.indices = ni;
    m.vertexBytes.assign (data + 16, data + 16 + (size_t) nv * 40);
    m.idx.resize (ni);
    std::memcpy (m.idx.data(), data + 16 + (size_t) nv * 40, (size_t) ni * 4);
    if (! parts.empty())
    {
        // a row of things in one file: its triangles sorted into the parts by their centre's x, each part a range
        const auto px = [&] (uint32_t i, bool& ok) -> double
        {
            if ((uint64_t) 16 + (uint64_t) i * 40 + 4 > size) { ok = false; return 0; }
            float f; std::memcpy (&f, data + 16 + (size_t) i * 40, 4); return f;
        };
        std::vector<std::vector<uint32_t>> bins (parts.size());
        for (uint32_t t = 0; t + 2 < ni; t += 3)
        {
            bool ok = true;
            const double cx = (px (m.idx[t], ok) + px (m.idx[t + 1], ok) + px (m.idx[t + 2], ok)) / 3;
            if (! ok) return std::nullopt;                                    // the view's reading throws: no mesh
            for (size_t b = 0; b < parts.size(); ++b)
                if (cx >= parts[b][0] && cx <= parts[b][1]) { bins[b].insert (bins[b].end(), { m.idx[t], m.idx[t + 1], m.idx[t + 2] }); break; }
        }
        std::vector<uint32_t> o;
        uint32_t first = 0;
        for (size_t k = 0; k < bins.size(); ++k)
        {
            o.insert (o.end(), bins[k].begin(), bins[k].end());
            m.parts.push_back ({ parts[k], first, (uint32_t) bins[k].size(), (parts[k][0] + parts[k][1]) / 2 });
            first += (uint32_t) bins[k].size();
        }
        m.idx = std::move (o);
    }
    return m;
}

std::optional<CaliforniaWorld::V4> CaliforniaWorld::glassBounds (const uint8_t* rgba, int w, int h, int margin)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (rgba[(size_t) ((y * w + x) * 4 + 3)] < 255) { x0 = std::min (x0, x); x1 = std::max (x1, x); y0 = std::min (y0, y); y1 = std::max (y1, y); }
    if (x1 < 0) return std::nullopt;
    return V4 { (double) std::max (0, x0 - margin) / w, (double) std::max (0, y0 - margin) / h, (double) std::min (w, x1 + 1 + margin) / w, (double) std::min (h, y1 + 1 + margin) / h };
}

std::vector<std::array<int, 4>> CaliforniaWorld::shadowPieces (const std::array<int, 4>& rect)
{
    const auto split = [] (int a, int b, std::vector<std::array<int, 2>>& out)
    {
        const int a0 = (int) imod (a, shN), n = b - a;
        if (n >= shN) out.push_back ({ 0, shN });
        else if (a0 + n <= shN) out.push_back ({ a0, n });
        else { out.push_back ({ a0, shN - a0 }); out.push_back ({ 0, a0 + n - shN }); }
    };
    std::vector<std::array<int, 2>> xs, ys;
    split (rect[0], rect[2], xs); split (rect[1], rect[3], ys);
    std::vector<std::array<int, 4>> out;
    for (const auto& [x, w] : xs) for (const auto& [y, h] : ys) out.push_back ({ x, y, w, h });
    return out;
}

//==============================================================================
CaliforniaWorld::CaliforniaWorld() noexcept = default;
CaliforniaWorld::~CaliforniaWorld() = default;

double CaliforniaWorld::speedFor (int g) const noexcept { return gearSpeed[(size_t) std::clamp (g, 1, 2) - 1]; }

double CaliforniaWorld::tap (int ch, double u, double v) const noexcept
{
    if (heightPx.empty()) return 0;
    const int n = texSize, m = n - 1;
    const double x = u * n - 0.5, y = v * n - 0.5, fx0 = std::floor (x), fy0 = std::floor (y);
    const double fx = x - fx0, fy = y - fy0;
    const int x0 = (int) (int64_t) fx0, y0 = (int) (int64_t) fy0;
    const int i0 = x0 & m, i1 = (x0 + 1) & m, j0 = y0 & m, j1 = (y0 + 1) & m;
    const double a = heightPx[(size_t) ((j0 * n + i0) * 4 + ch)], b = heightPx[(size_t) ((j0 * n + i1) * 4 + ch)];
    const double c = heightPx[(size_t) ((j1 * n + i0) * 4 + ch)], d = heightPx[(size_t) ((j1 * n + i1) * 4 + ch)];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

double CaliforniaWorld::noiseTap (int ch, double u, double v) const noexcept
{
    if (noisePx.empty()) return 0;
    const int n = noiseSize, m = n - 1;
    const double x = u * n - 0.5, y = v * n - 0.5, fx0 = std::floor (x), fy0 = std::floor (y);
    const double fx = x - fx0, fy = y - fy0;
    const int x0 = (int) (int64_t) fx0, y0 = (int) (int64_t) fy0;
    const int i0 = x0 & m, i1 = (x0 + 1) & m, j0 = y0 & m, j1 = (y0 + 1) & m;
    const auto q = [this, ch] (int i, int j) { return noisePx[(size_t) ((j * noiseSize + i) * 4 + ch)] / 255.0; };
    return (q (i0, j0) * (1 - fx) + q (i1, j0) * fx) * (1 - fy) + (q (i0, j1) * (1 - fx) + q (i1, j1) * fx) * fy;
}

double CaliforniaWorld::roadHeight (double th, double R) const noexcept
{
    const double cx = loopCentre[0] + R * Js::cos (th), cz = loopCentre[1] + R * Js::sin (th);
    return 0.30 * hillA * (tap (0, cx / L0, cz / L0) - 0.5) + 0.20 * medA * (tap (2, cx / L1, cz / L1) - 0.5);
}

CaliforniaWorld::V3 CaliforniaWorld::roadDist (double x, double z) const noexcept
{
    const double qx = x - loopCentre[0], qz = z - loopCentre[1], r = Js::hypot (qx, qz), th = Js::atan2 (qz, qx);
    const auto [R, dR] = loopR (th);
    return { (R - r) * R / std::sqrt (R * R + dR * dR), th, R };
}

CaliforniaWorld::V3 CaliforniaWorld::roadTap (double x, double z) const noexcept
{
    if (roadPx.empty()) return { 0, 0, 0 };
    const int n = roadN;
    const double u = clamp ((x - roadOrigin[0]) / roadSpan * n - 0.5, 0, n - 1), v = clamp ((z - roadOrigin[1]) / roadSpan * n - 0.5, 0, n - 1);
    const double fx0 = std::min (std::floor (u), (double) n - 2), fy0 = std::min (std::floor (v), (double) n - 2), fx = u - fx0, fy = v - fy0;
    const int x0 = (int) fx0, y0 = (int) fy0;
    const auto at = [this] (int i, int j, int c) { return (double) roadPx[(size_t) ((j * roadN + i) * 4 + c)]; };
    const auto q = [&] (int c) { return (at (x0, y0, c) * (1 - fx) + at (x0 + 1, y0, c) * fx) * (1 - fy) + (at (x0, y0 + 1, c) * (1 - fx) + at (x0 + 1, y0 + 1, c) * fx) * fy; };
    return { q (0), q (1), q (2) };
}

double CaliforniaWorld::floorHeight (double px, double pz) const noexcept
{
    if (heightPx.empty()) return 0;
    const double ar = tap (0, px / L0, pz / L0), bb = tap (2, px / L1, pz / L1), ca = tap (3, px / L2, pz / L2);
    const auto rd = roadTap (px, pz);
    const double ad = std::abs (rd[0]);
    const double hT = hillA * (ar - 0.5) + medA * (bb - 0.5);
    const double w = smoothstep (6, 75, ad);
    const double ditch = 0.35 * smoothstep (4.7, 5.6, ad) * (1 - smoothstep (6.2, 8.5, ad));
    return lerp (rd[1], hT, w) + fineA * (ca - 0.5) * smoothstep (4.8, 7.0, ad) - ditch;
}

double CaliforniaWorld::sunAt (double x, double z) const noexcept { return roadTap (x, z)[2]; }

double CaliforniaWorld::ridgeTan (double az, int k, double alt) const noexcept
{
    const double m = k == 0 ? 7 : (k == 1 ? 5 : 3), row = 0.11 + 0.29 * k;
    const double n = noiseTap (3, az * m, row) * 0.78 + noiseTap (3, az * m * 4, row + 0.13) * 0.22;
    const double H = k == 0 ? 10 + 150 * n : (k == 1 ? 30 + 300 * n : 60 + 640 * n);
    return (H - alt) / std::array<double, 3> { 2600, 5200, 10000 }[(size_t) std::clamp (k, 0, 2)];
}

void CaliforniaWorld::place() noexcept
{
    const double th = thetaAt (s.sr);
    const auto p = loopPoint (th), r = rightAt (th), t = tangentAt (th);
    s.x = p[0] + s.lat * r[0]; s.z = p[1] + s.lat * r[1];
    s.heading = Js::atan2 (t[0], -t[1]) + Js::atan2 (s.latV, std::max (s.speed, 2.0)) * 0.8;
}

//==============================================================================
void CaliforniaWorld::build()
{
    const double L = loopLength();
    s = State();
    heightPx = makeHeight();
    detailPx = makeDetail();
    noisePx = makeNoise();
    const auto sunxz = sunXZ();
    const double sunT = sunTan();

    // THE ROAD MAP: per texel the signed distance and the road's own height (at its angle), made once...
    roadPx.assign ((size_t) roadN * roadN * 4, 0.0f);
    for (int j = 0; j < roadN; ++j)
        for (int i = 0; i < roadN; ++i)
        {
            const double x = roadOrigin[0] + (i + 0.5) * roadSpan / roadN, z = roadOrigin[1] + (j + 0.5) * roadSpan / roadN;
            const auto rd = roadDist (x, z);
            const size_t o = (size_t) (j * roadN + i) * 4;
            roadPx[o] = (float) rd[0]; roadPx[o + 1] = (float) roadHeight (rd[1], rd[2]); roadPx[o + 2] = 1; roadPx[o + 3] = 0;
        }
    // ...B: the sun's visibility past the hills, marched once (6 steps, out to 740 m) from 0.4 m over the ground, over
    // the map's R and G as made (not yet rounded to half floats: the prototype rounds them all after)
    for (int j = 0; j < roadN; ++j)
        for (int i = 0; i < roadN; ++i)
        {
            const double x = roadOrigin[0] + (i + 0.5) * roadSpan / roadN, z = roadOrigin[1] + (j + 0.5) * roadSpan / roadN;
            const double h = floorHeight (x, z) + 0.4;
            double occ = -1, t = 10;
            for (int k = 0; k < 6; ++k) { occ = jsMax (occ, (floorHeight (x + sunxz[0] * t, z + sunxz[1] * t) - h) / t); t *= 1.85; }
            roadPx[(size_t) (j * roadN + i) * 4 + 2] = (float) (1 - smoothstep (sunT - 0.03, sunT + 0.02, occ));
        }
    for (auto& v : roadPx) v = (float) toHalf (v);
    // THE LAND MAP: RG the ground's normal (x, z), BA the poppies' and the lupine's fields.
    landPx.assign ((size_t) roadN * roadN * 4, 0.0f);
    {
        const double e = 3.0;
        for (int j = 0; j < roadN; ++j)
            for (int i = 0; i < roadN; ++i)
            {
                const double x = roadOrigin[0] + (i + 0.5) * roadSpan / roadN, z = roadOrigin[1] + (j + 0.5) * roadSpan / roadN;
                const double nx = floorHeight (x - e, z) - floorHeight (x + e, z), nz = floorHeight (x, z - e) - floorHeight (x, z + e), l = Js::hypot (nx, 2 * e, nz);
                const double ad = std::abs (roadTap (x, z)[0]);
                const double ab = tap (2, x / L0, z / L0), ag = tap (1, x / L0, z / L0), bg = tap (1, x / L1, z / L1), br = tap (0, x / L1, z / L1);
                double P = smoothstep (0.42, 0.62, lerp (bg, ab, 0.45));
                P = jsMax (P, 0.8 * smoothstep (0.28, 0.52, bg) * (1 - smoothstep (7, 18, ad))) * smoothstep (4.9, 5.6, ad);
                const double Lu = smoothstep (0.66, 0.80, br) * smoothstep (0.35, 0.65, ag) * smoothstep (4.9, 5.6, ad);
                const size_t o = (size_t) (j * roadN + i) * 4;
                landPx[o] = (float) toHalf (nx / l); landPx[o + 1] = (float) toHalf (nz / l); landPx[o + 2] = (float) toHalf (P); landPx[o + 3] = (float) toHalf (Lu);
            }
    }
    gearWanted = 1; parked = false; wasParked = false; flockNext = flockFirst; flockCount = 0; pull.reset(); prevSpeed = s.speed;
    place();

    // THE BUTTERFLIES: anchored in a box the camera wraps through, each on its own wandering path near the flowers.
    bf.clear();
    for (int i = 0; i < bfCount; ++i)
    {
        const auto h = [i] (int k) { return hs (i + 1, 300 + k); };
        Butterfly b;
        b.ax = h (1) * bfBox; b.az = h (2) * bfBox; b.alt = 0.35 + 1.5 * std::pow (h (3), 1.6); b.scale = 0.058 * (0.85 + 0.3 * h (4));
        b.A = 1.5 + 3.5 * h (5); b.w1 = 0.12 + 0.2 * h (6); b.w2 = 0.3 + 0.3 * h (7); b.p1 = h (8) * 2 * pi; b.p2 = h (9) * 2 * pi;
        b.p3 = h (10) * 2 * pi; b.p4 = h (11) * 2 * pi; b.rate = 6 + 4 * h (12); b.glideW = 0.5 + 0.7 * h (13); b.phase = h (14) * 2 * pi;
        bf.push_back (b);
    }
    findPlaces.reset();
    oakCache.clear(); oaks.clear(); oakBase.reset(); poleCache.clear();

    // THE SIGNS' POSTS: each at its place (half way between two poles), its faces turned toward the van that comes.
    std::vector<double> taken;
    for (const auto& sp : signPostSpecs()) for (const auto& a : sp.at) if (a.kind == At::number) taken.push_back (a.at);
    for (const double f : findSlots) taken.push_back (f);
    for (const auto& pp : pumpSpecs) taken.push_back (pp.at);
    for (const auto& sp : spriteSpecs()) taken.push_back (sp.at);
    for (const auto& fe : fenceSpecs) taken.push_back (fe.from);
    for (const auto& fe : fenceSpecs) taken.push_back (fe.to);
    for (const double a : rhyme.at) { taken.push_back (a); taken.push_back (a + 4 * rhyme.step / L); }
    for (auto& f : taken) f = f * L;
    const auto gradeAt = [this] (double a)
    {
        const auto h = [this] (double b) { const double th = thetaAt (b); return roadHeight (th, loopR (th).R); };
        return (h (a + 25) - h (a - 25)) / 50;
    };
    const auto clearOf = [&taken] (double a) { return std::all_of (taken.begin(), taken.end(), [a] (double t) { return cyc (a, t) > 150; }); };
    const auto steepest = [&] (double sign, std::vector<double>& mine)
    {
        double best = -1e9, at = 0;
        for (double a = 0; a < L; a += 10)
        {
            const double g = sign * gradeAt (a);
            if (g > best && clearOf (a) && std::all_of (mine.begin(), mine.end(), [a, L] (double m) { return cyc (a, m) > L / 4; })) { best = g; at = a; }
        }
        taken.push_back (at); mine.push_back (at);
        return at / L;
    };
    struct Spot { std::string key; double at, d; };
    std::vector<Spot> postSpots;
    for (const auto& spec : signPostSpecs())
    {
        std::vector<double> mine;
        for (const auto& sp : spec.at)
        {
            const double f = sp.kind == At::climb ? steepest (1, mine) : sp.kind == At::descent ? steepest (-1, mine) : sp.at;
            postSpots.push_back ({ spec.key, (Js::round (f * L / poleDs()) + 0.5) * poleDs(), sp.d });
        }
    }
    for (size_t j = 0; j < rhyme.at.size(); ++j)
        for (size_t i = 0; i < rhyme.posts.size(); ++i) postSpots.push_back ({ rhyme.posts[i], rhyme.at[j] * L + i * rhyme.step, rhyme.d[j] });
    signPosts.clear();
    for (const auto& [key, at, d] : postSpots)
    {
        const double th = thetaAt (at);
        const auto t = tangentAt (th), r = rightAt (th), p = roadPointAt (at, d);
        const double tw = d == 0 ? 0 : 20 * pi / 180 * (d < 0 ? 1 : -1);
        const double nx = -t[0] * Js::cos (tw) + r[0] * Js::sin (tw), nz = -t[1] * Js::cos (tw) + r[1] * Js::sin (tw);
        SignPost P0 { key, at, p[0], p[1], floorHeight (p[0], p[1]), nx, nz, Js::atan2 (nx, nz), nz, -nx, sunAt (p[0], p[1]), d == 0, r };
        auto it = std::find_if (signPosts.begin(), signPosts.end(), [&key] (const auto& e) { return e.first == key; });
        if (it == signPosts.end()) { signPosts.push_back ({ key, {} }); it = signPosts.end() - 1; }
        it->second.push_back (P0);
    }
    signNames.clear();

    // THE WINDPUMPS, THE FENCES AND THEIR FLAGS.
    pumpList.clear();
    for (size_t i = 0; i < pumpSpecs.size(); ++i)
    {
        const auto p = roadPointAt (pumpSpecs[i].at * L, pumpSpecs[i].d);
        pumpList.push_back ({ p[0], p[1], floorHeight (p[0], p[1]), sunAt (p[0], p[1]), hs ((int) i + 1, 701) * 6.28 });
    }
    fenceBoxes.clear(); flagList.clear();
    for (size_t fi = 0; fi < fenceSpecs.size(); ++fi)
    {
        const auto& fe = fenceSpecs[fi];
        bool hasPrev = false;
        double prevX = 0, prevZ = 0, prevY = 0;
        int k = 0;
        for (double at = fe.from * L; at <= fe.to * L; at += 3.0, ++k)
        {
            const auto p = roadPointAt (at, fe.d);
            const double px = p[0], pz = p[1], y = floorHeight (px, pz) - 0.05, vis = sunAt (px, pz);
            const int32_t key = k + (int32_t) fi * 1000;
            const double lean = (hs (key, 711) - 0.5) * 0.08;
            Box post; post.x = px; post.y = y + 0.6; post.z = pz; post.yaw = lean + Js::atan2 (px, pz); post.hx = 0.06; post.hy = 0.75; post.hz = 0.06; post.vis = vis;
            fenceBoxes.push_back (post);
            if (hasPrev)
            {
                const double dx = px - prevX, dz = pz - prevZ, len = Js::hypot (dx, dz), yaw = Js::atan2 (-dz / len, dx / len);
                const double mx = (px + prevX) / 2, mz = (pz + prevZ) / 2, my = (y + prevY) / 2;
                for (const double h : { 0.55, 1.10 })
                {
                    Box rail; rail.x = mx; rail.y = my + h; rail.z = mz; rail.yaw = yaw; rail.hx = len / 2 + 0.05; rail.hy = 0.035; rail.hz = 0.025; rail.vis = vis;
                    fenceBoxes.push_back (rail);
                }
                Box flag; flag.x = mx; flag.y = my + 1.10 - 0.04 - 0.24; flag.z = mz; flag.yaw = yaw; flag.hx = 0.19; flag.hy = 0.24; flag.hz = 0; flag.part = 2;
                flag.rect = { -1, hs (key, 712), 0, 0 }; flag.vis = vis; flag.flutter = hs (key, 713);
                flagList.push_back (flag);
            }
            hasPrev = true; prevX = px; prevZ = pz; prevY = y;
        }
    }

    // THE SPRITES' PLACES: by the road, on a hill top near, or the best ground off one side; fronts toward the road.
    const auto frontAt = [] (double px, double pz, double at, double turn)
    {
        const double th = thetaAt (at);
        const auto rp = roadPointAt (at, 0), t = tangentAt (th);
        double fx = rp[0] - px, fz = rp[1] - pz;
        const double fl = orOne (Js::hypot (fx, fz));
        fx /= fl; fz /= fl;
        const double tw = turn * pi / 180, side = (fx * t[1] - fz * t[0]) < 0 ? 1 : -1;
        const double c = Js::cos (tw * side), sn = Js::sin (tw * side);
        return V2 { fx * c - fz * sn, fx * sn + fz * c };
    };
    const auto footY = [this] (double px, double pz, double nx, double nz, double w, double d)
    {
        double y = 1e9;
        for (const auto& [a, b] : std::array<V2, 4> { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } })
            y = jsMin (y, floorHeight (px + nz * a * w / 2 + nx * b * d / 2, pz - nx * a * w / 2 + nz * b * d / 2));
        return y - 0.1;
    };
    spriteList.clear();
    for (const auto& sp : spriteSpecs())
    {
        double at = sp.at * L, px = 0, pz = 0;
        if (sp.hill)
        {
            double best = -1e9;
            for (const double ds : { -150.0, -75.0, 0.0, 75.0, 150.0 })
                for (const double side : { -1.0, 1.0 })
                    for (const double d : { 140.0, 190.0, 240.0, 300.0, 360.0 })
                    {
                        const auto q = roadPointAt (at + ds, side * d);
                        const double h = floorHeight (q[0], q[1]) - d * 0.02;
                        if (h > best) { best = h; px = q[0]; pz = q[1]; }
                    }
        }
        else if (sp.hasOff)
        {
            double best = -1e9;
            std::array<double, 3> pick {};
            for (const double ds : { -60.0, -30.0, 0.0, 30.0, 60.0 })
                for (double d = sp.off[0]; d <= sp.off[1]; d += 5)
                {
                    const double a = at + ds;
                    const auto q = roadPointAt (a, sp.side * d);
                    const double qx = q[0], qz = q[1];
                    const auto [nx, nz] = frontAt (qx, qz, a, sp.turn);
                    std::array<double, 5> hs4 {};
                    size_t hi = 0;
                    for (const auto& [u, v] : std::array<V2, 5> { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 }, { 0, 0 } } })
                        hs4[hi++] = floorHeight (qx + nz * u * sp.ph.w / 2 + nx * v * sp.ph.d / 2, qz - nx * u * sp.ph.w / 2 + nz * v * sp.ph.d / 2);
                    double mx = -std::numeric_limits<double>::infinity(), mn = std::numeric_limits<double>::infinity();
                    for (const double h : hs4) { mx = jsMax (mx, h); mn = jsMin (mn, h); }
                    const double spread = mx - mn, y = mn;
                    int seen = 0, inWay = 0;
                    for (const double back : { 60.0, 110.0, 160.0 })
                    {
                        const auto e = roadPointAt (a - back, lane);
                        const double ex = e[0], ez = e[1], ey = floorHeight (ex, ez) + eye;
                        int ok = 1;
                        for (int k = 1; k < 12; ++k)
                        {
                            const double t = k / 12.0, gx = lerp (ex, qx, t), gz = lerp (ez, qz, t);
                            if (floorHeight (gx, gz) > lerp (ey, y + 2, t)) ok = 0;
                            if (k % 3 == 0 && oakNear (gx, gz, 1.5)) inWay = 1;
                        }
                        seen += ok;
                    }
                    const double score = -0.8 * spread - 0.15 * d + 1.5 * seen - 3 * inWay - (oakNear (qx, qz, 12) ? 8 : 0);
                    if (score > best) { best = score; pick = { qx, qz, a }; }
                }
            px = pick[0]; pz = pick[1]; at = pick[2];
        }
        else { const auto q = roadPointAt (at, sp.d); px = q[0]; pz = q[1]; }
        const auto [nx, nz] = frontAt (px, pz, at, sp.turn);
        const double y = ! sp.mesh.empty() ? footY (px, pz, nx, nz, sp.ph.w, sp.ph.d) - (sp.scale > 2 ? 1.5 : 0) : floorHeight (px, pz);
        SpritePlace pl;
        pl.name = sp.name; pl.label = sp.label; pl.at = modL (at); pl.hill = sp.hill; pl.mesh = sp.mesh; pl.x = px; pl.z = pz; pl.y = y; pl.nx = nx; pl.nz = nz;
        pl.pull = sp.pull; pl.ph = sp.ph; pl.vis = sunAt (px, pz); pl.scale = sp.scale > 0 ? sp.scale : 1; pl.paint = sp.paint; pl.group = sp.group;
        pl.seen = sp.seen; pl.hasPart = sp.hasPart; pl.part = sp.part; pl.noBox = sp.noBox; pl.noJump = sp.noJump;
        spriteList.push_back (pl);
    }
    // THE RADIO TELESCOPES: the row across the line of sight from dishRow.view m before, all turned the same way.
    dishList.clear();
    {
        const auto& R = dishRow;
        const auto c = roadPointAt (R.at, R.d), e = roadPointAt (R.at - R.view, lane);
        double vx = c[0] - e[0], vz = c[1] - e[1];
        const double vl = Js::hypot (vx, vz);
        vx /= vl; vz /= vl;
        const double rx = -vz, rz = vx, lk = R.look * pi / 180;
        const double nx = -vx * Js::cos (lk) + rx * Js::sin (lk), nz = -vz * Js::cos (lk) + rz * Js::sin (lk);
        for (int i = 0; i < R.n; ++i)
        {
            const double o = (i - (R.n - 1) / 2.0) * R.gap, x = c[0] + rx * o, z = c[1] + rz * o;
            dishList.push_back ({ x, z, floorHeight (x, z) - R.sink, nx, nz });
        }
    }
    chalkList.clear(); spritesIn.clear();
    shadowAll = false;
    shPix.assign ((size_t) shN * shN * 4, 0);
    shO.reset(); shUpdates.clear();
    boxes = fenceBoxes;
    for (auto& b : placeholderBoxes()) boxes.push_back (std::move (b));

    // THE FLYING SAUCERS and THE YELLOW SUBMARINE.
    for (int i = 0; i < 2; ++i)
    {
        Saucer u;
        u.i = i; u.ph = hs (i, 801) * 2 * pi; u.yaw = hs (i, 802) * 6;
        sau[(size_t) i] = u;
    }
    sc = SaucerControl();
    yellow = Sub();
    built = true;
    resetSaucers();
    if (started) setAt (startSr, startLap);
}

//==============================================================================
void CaliforniaWorld::stepButterflies (double dt)
{
    const double t = s.time;
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double startle = s.startleS * (1 - Js::exp (-s.startleT / 0.3)) * Js::exp (-s.startleT / 2.5);
    for (auto& b : bf)
    {
        const double glide = smoothstep (0.55, 0.85, 0.5 + 0.5 * Js::sin (t * b.glideW + b.p4));
        b.phase += 2 * pi * b.rate * (1 - 0.82 * glide) * (1 + 1.4 * startle) * dt;
        if (b.phase > 2 * pi * 64) b.phase -= 2 * pi * 64;
        const double wx = b.ax + b.A * (Js::sin (b.w1 * t + b.p1) + 0.5 * Js::sin (2.3 * b.w1 * t + b.p2));
        const double wz = b.az + b.A * (Js::cos (b.w2 * t + b.p3) + 0.5 * Js::sin (1.7 * b.w2 * t + b.p1));
        const double vx = b.A * (b.w1 * Js::cos (b.w1 * t + b.p1) + 1.15 * b.w1 * Js::cos (2.3 * b.w1 * t + b.p2));
        const double vz = b.A * (-b.w2 * Js::sin (b.w2 * t + b.p3) + 0.85 * b.w2 * Js::cos (1.7 * b.w2 * t + b.p1));
        double dx = wrapBox (wx - s.x, bfBox), dz = wrapBox (wz - s.z, bfBox);
        // out of the van's way: inside the corridor ahead, pushed aside
        const double ahead = dx * fx + dz * fz, side = dx * -fz + dz * fx;
        if (ahead > -2 && ahead < 9 && std::abs (side) < 2.6)
        {
            const double push = (2.6 - std::abs (side)) * smoothstep (-2, 1, ahead) * (1 - smoothstep (7, 9, ahead));
            const double sg = side < 0 ? -1 : 1;
            dx += -fz * sg * push; dz += fx * sg * push;
        }
        const double gy = floorHeight (s.x + dx, s.z + dz);
        const double dist0 = Js::hypot (dx, dz);
        const double lift = startle * (1.0 + 1.2 * b.alt) * smoothstep (25, 8, dist0);
        const double y = gy + b.alt + 0.35 * Js::sin (0.7 * t + b.p2) + 0.05 * Js::sin (b.phase) + lift;
        b.rel = { dx, y - s.camY, dz };
        b.yaw = Js::atan2 (vx, -vz);
        b.flap = glide > 0.5 ? 0.35 + 0.15 * Js::sin (b.phase) : 0.1 + 0.95 * (0.5 + 0.5 * Js::sin (b.phase));
        b.pitch = 0.25 + 0.1 * Js::sin (b.phase + 1.0);
        b.dist = Js::hypot (dx, b.rel[1], dz);
        b.vis = smoothstep (35, 28, jsMax (std::abs (dx), std::abs (dz)) + 3) * smoothstep (1.0, 2.0, b.dist);
    }
}

void CaliforniaWorld::stepGlass (double dt)
{
    for (size_t i = 0; i < glassSpots.size(); ++i)
    {
        auto& gb = s.glass[i];
        const auto& G0 = glassSpots[i];
        const int32_t ii = (int32_t) i;
        const double sx = G0.x / stage[0] * 2 - 1, sy = 1 - G0.y / stage[1] * 2;
        gb.t += dt;
        if (gb.st == 1 && gb.t >= G0.at) { gb.st = 2; gb.t = 0; }                       // waiting: arriving
        if (gb.st == 2)
        {
            const double u = clamp (gb.t / 2.2, 0, 1), e = u * u * (3 - 2 * u);
            const double x0 = sx * 0.35 + (hs (ii, 71) - 0.5) * 0.3, y0 = -0.75;
            const double cx = (x0 + sx) * 0.5 + (hs (ii, 72) - 0.5) * 0.6, cy = jsMax (y0, sy) + 0.35;
            gb.x = (1 - e) * (1 - e) * x0 + 2 * (1 - e) * e * cx + e * e * sx + 0.02 * Js::sin (gb.t * 7.0) * (1 - e);
            gb.y = (1 - e) * (1 - e) * y0 + 2 * (1 - e) * e * cy + e * e * sy + 0.025 * Js::sin (gb.t * 5.3 + 1) * (1 - e);
            gb.k = lerp (0.22, 1.0, e);
            gb.open = 0.15 + 0.85 * std::abs (Js::sin (gb.t * 2 * pi * 4.5)) * (1 - e) + e * 0.9;
            gb.a = smoothstep (0, 0.35, gb.t);
            gb.rot = lerp (Js::atan2 (sx - x0, sy - y0) * -1.0 * 0.5, G0.a, e);
            if (u >= 1) { gb.st = 3; gb.t = 0; }
        }
        else if (gb.st == 3)
        {
            gb.x = sx; gb.y = sy; gb.k = 1; gb.a = 1; gb.rot = G0.a;
            gb.open = 0.56 + 0.44 * Js::cos (gb.t * 2 * pi / (3.5 + 1.5 * hs (ii, 73)) + hs (ii, 74) * 6);
        }
        else if (gb.st == 4)
        {
            const double u = clamp ((gb.t - 0.5 * hs (ii, 75)) / 1.5, 0, 1), e = u * u;
            if (gb.t < 0.01) { gb.fx = gb.x; gb.fy = gb.y; gb.fk = gb.k; }
            const double ex = gb.fx + (gb.fx < 0 ? -0.8 : 0.8) * (0.5 + hs (ii, 76)), ey = gb.fy + 1.1;
            gb.x = lerp (gb.fx, ex, e) + 0.02 * Js::sin (gb.t * 9); gb.y = lerp (gb.fy, ey, e);
            gb.k = gb.fk * (1 - 0.55 * e);
            gb.open = u > 0 ? 0.15 + 0.85 * std::abs (Js::sin (gb.t * 2 * pi * 5)) : gb.open;
            gb.a = 1 - smoothstep (0.6, 1.0, u);
            if (u >= 1) { gb.st = 0; gb.a = 0; }
        }
    }
}

void CaliforniaWorld::glassPark (bool on)
{
    for (auto& gb : s.glass)
    {
        if (on && gb.st == 0) { gb.st = 1; gb.t = 0; }
        else if (! on && gb.st == 1) { gb.st = 0; }
        else if (! on && (gb.st == 2 || gb.st == 3)) { gb.st = 4; gb.t = 0; gb.fx = gb.x; gb.fy = gb.y; gb.fk = gb.k; }
    }
}

//==============================================================================
CaliforniaWorld::Find CaliforniaWorld::placeFind (int slot, int kind)
{
    const double L = loopLength();
    const double at = findSlots[(size_t) slot] * L;
    const int n = slot * 3 + kind;
    const auto S = sunDir();
    Find out;
    out.kind = kind;
    if (kind == 0)
    {
        double best = -1e9;
        for (const double ds : { 0.0, 90.0, 180.0 })
            for (const double side : { -1.0, 1.0 })
                for (const double d : { 90.0, 115.0, 140.0, 170.0 })
                {
                    const double sf = at + ds;
                    const auto pp = roadPointAt (sf, side * d);
                    const double px = pp[0], pz = pp[1], h = floorHeight (px, pz), e = 6;
                    const auto nrm = normalised ({ floorHeight (px - e, pz) - floorHeight (px + e, pz), 2 * e, floorHeight (px, pz - e) - floorHeight (px, pz + e) });
                    const auto rp = roadPointAt (sf, 0);
                    const double rx = rp[0], rz = rp[1], tl = orOne (Js::hypot (rx - px, rz - pz));
                    const double facing = (nrm[0] * (rx - px) + nrm[2] * (rz - pz)) / tl / jsMax (Js::hypot (nrm[0], nrm[2]), 0.05);
                    const double lit = nrm[0] * S[0] + nrm[1] * S[1] + nrm[2] * S[2];
                    int seen = 0;
                    for (const double back : { 250.0, 450.0, 650.0 })
                    {
                        const auto ep = roadPointAt (sf - back, lane);
                        const auto ee = roadDist (ep[0], ep[1]);
                        const double ey = roadHeight (ee[1], ee[2]) + eye;
                        int ok = 1;
                        for (int k = 1; k < 12; ++k)
                        {
                            const double t = k / 12.0;
                            if (floorHeight (lerp (ep[0], px, t), lerp (ep[1], pz, t)) > lerp (ey, h + 3, t)) { ok = 0; break; }
                        }
                        seen += ok;
                    }
                    const auto rr = roadDist (rx, rz);
                    const double score = 2.2 * facing * smoothstep (0.04, 0.30, Js::hypot (nrm[0], nrm[2])) + clamp ((h - roadHeight (rr[1], rr[2])) / 30, -1, 1) + 0.8 * lit + seen
                                       - (oakNear (px, pz, 30) ? 3 : 0);
                    if (score > best)
                    {
                        best = score;
                        const auto vd = normalised ({ px - rx, 0, pz - rz });
                        V3 U { -vd[2], 0, vd[0] };                                     // the viewer's right
                        const double dn = U[0] * nrm[0] + U[1] * nrm[1] + U[2] * nrm[2];
                        U = normalised ({ U[0] - nrm[0] * dn, U[1] - nrm[1] * dn, U[2] - nrm[2] * dn });
                        V3 V { nrm[1] * U[2] - nrm[2] * U[1], nrm[2] * U[0] - nrm[0] * U[2], nrm[0] * U[1] - nrm[1] * U[0] };
                        if (V[1] < 0) V = { -V[0], -V[1], -V[2] };
                        // laid out to be read from the road: longer up the slope by 1 / the sine the road sees it at, at most 4 times
                        const auto ep = roadPointAt (sf - 350, lane);
                        const auto ee = roadDist (ep[0], ep[1]);
                        const auto ray = normalised ({ px - ep[0], h - (roadHeight (ee[1], ee[2]) + eye), pz - ep[1] });
                        const double sinA = std::abs (ray[0] * nrm[0] + ray[1] * nrm[1] + ray[2] * nrm[2]);
                        out.x = px; out.z = pz; out.y = h; out.u = U; out.v = V; out.R = 40; out.k = clamp (1 / jsMax (sinA, 0.25), 1, 4);
                    }
                }
        return out;
    }
    if (kind == 1)
    {
        const auto pp = roadPointAt (at, 9.2);
        const auto t = tangentAt (thetaAt (at));
        const double way = 1, turn = (hs (n, 82) - 0.5) * 0.3 - 0.12;            // parked with the traffic: its blanket side toward the road
        const double fx0 = t[0] * way, fz0 = t[1] * way, c = Js::cos (turn), sn = Js::sin (turn);
        out.x = pp[0]; out.z = pp[1]; out.y = floorHeight (pp[0], pp[1]); out.nx = fx0 * c - fz0 * sn; out.nz = fx0 * sn + fz0 * c;
        return out;
    }
    // the monolith: high ground off the road that the road sees from 300-500 m before (its body against the sky)
    double best = -1e9;
    std::array<double, 4> pick {};
    for (const double ds : { 0.0, 60.0, 120.0, 180.0 })
        for (const double side : { -1.0, 1.0 })
            for (const double d : { 60.0, 85.0, 110.0, 150.0 })
            {
                const double sf = at + ds;
                const auto pp = roadPointAt (sf, side * d);
                const double px = pp[0], pz = pp[1], h = floorHeight (px, pz);
                const auto r0 = roadPointAt (sf, 0);
                const auto rr = roadDist (r0[0], r0[1]);
                const double hRoad = roadHeight (rr[1], rr[2]);
                int seen = 0;
                for (const double back : { 300.0, 500.0 })
                {
                    const auto ep = roadPointAt (sf - back, lane);
                    const auto ee = roadDist (ep[0], ep[1]);
                    const double ey = roadHeight (ee[1], ee[2]) + eye;
                    int ok = 1;
                    for (int k = 1; k < 14; ++k) { const double t = k / 14.0; if (floorHeight (lerp (ep[0], px, t), lerp (ep[1], pz, t)) > lerp (ey, h + 12, t)) { ok = 0; break; } }
                    seen += ok;
                }
                const double score = (h - hRoad) / 8 + 2.5 * seen - (oakNear (px, pz, 25) ? 6 : 0) + 0.2 * hs ((int32_t) Js::round (ds + d * side) + 2000, 85 + slot);
                if (score > best) { best = score; pick = { px, pz, sf, side }; }
            }
    const auto [px, pz, sf, side] = pick;
    const auto r = rightAt (thetaAt (sf));
    const double tw = (hs (n, 84) - 0.5) * 0.5;
    const double zx = -r[0] * side, zz = -r[1] * side;                     // the thickness axis: toward the road
    const double nx = zx * Js::cos (tw) - zz * Js::sin (tw), nz = zx * Js::sin (tw) + zz * Js::cos (tw);
    // standing on its lowest corner, so it is planted on a slope
    double y = 1e9;
    for (const auto& [a, b] : std::array<V2, 4> { { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } })
        y = jsMin (y, floorHeight (px - nz * a * monoWide / 2 + nx * b * monoThick / 2, pz + nx * a * monoWide / 2 + nz * b * monoThick / 2));
    out.x = px; out.z = pz; out.y = y; out.nx = nx; out.nz = nz;
    return out;
}

std::array<CaliforniaWorld::Find, 3> CaliforniaWorld::findsNow()
{
    if (! findPlaces)
    {
        std::array<std::array<Find, 3>, 3> fp {};
        for (int slot = 0; slot < 3; ++slot) for (int kind = 0; kind < 3; ++kind) fp[(size_t) slot][(size_t) kind] = placeFind (slot, kind);
        findPlaces = fp;
    }
    std::array<Find, 3> out {};
    for (int slot = 0; slot < 3; ++slot) out[(size_t) slot] = (*findPlaces)[(size_t) slot][(size_t) ((slot + s.lap) % 3)];
    return out;
}

namespace { constexpr int64_t oakM = 3300; }     // Math.round (92400 / 28): cells round the world

bool CaliforniaWorld::oakNear (double x, double z, double r)
{
    const int64_t ci = (int64_t) std::floor (x / oakCell), cj = (int64_t) std::floor (z / oakCell);
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
        {
            const auto o = oakAt (imod (ci + i, oakM), imod (cj + j, oakM));
            if (o && Js::hypot (o->x - x, o->z - z) < r + o->R) return true;
        }
    return false;
}

const std::optional<CaliforniaWorld::Oak>& CaliforniaWorld::oakAt (int64_t ci, int64_t cj)
{
    const int64_t key = ci * 65536 + cj;
    if (const auto it = oakCache.find (key); it != oakCache.end()) return it->second;
    if (oakCache.size() >= 60000) oakCache.clear();          // the prototype's bound (after its insert: the same cells kept or not, the same oaks)
    std::optional<Oak> oak;
    const int32_t a = (int32_t) ci, b = (int32_t) cj;
    const double x = ((double) ci + 0.15 + 0.7 * hcell (a, b, 52)) * oakCell, z = ((double) cj + 0.15 + 0.7 * hcell (a, b, 53)) * oakCell;
    const double grove = tap (1, x / L0, z / L0);
    const double dens = 0.05 + 0.55 * smoothstep (0.55, 0.78, grove);
    if (hcell (a, b, 51) < dens && std::abs (roadDist (x, z)[0]) > 14)
    {
        const double y = floorHeight (x, z), R = 2.6 + 3.6 * std::pow (hcell (a, b, 54), 1.5);
        oak = Oak { x, z, y, R, 0.85 + 0.3 * hcell (a, b, 55), 2 * pi * hcell (a, b, 56), hcell (a, b, 57), sunAt (x, z) };
    }
    return oakCache.emplace (key, oak).first->second;
}

const std::vector<CaliforniaWorld::Oak>& CaliforniaWorld::listOaks()
{
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading), shift = oakW * oakCell * 0.3;
    const int64_t bx = (int64_t) std::floor ((s.x + fx * shift) / oakCell) - oakW / 2, bz = (int64_t) std::floor ((s.z + fz * shift) / oakCell) - oakW / 2;
    if (oakBase && (*oakBase)[0] == bx && (*oakBase)[1] == bz) return oaks;
    oakBase = std::array<int64_t, 2> { bx, bz };
    oaks.clear();
    for (int j = 0; j < oakW; ++j)
        for (int i = 0; i < oakW; ++i)
            if (const auto o = oakAt (imod (bx + i, oakM), imod (bz + j, oakM))) oaks.push_back (*o);
    return oaks;
}

CaliforniaWorld::Pole CaliforniaWorld::pole (int k)
{
    const int N = polesN(), kk = (int) imod (k, N);
    const double at = kk * poleDs(), th = thetaAt (at);
    const auto p = roadPointAt (at, poleSide), r = rightAt (th);
    return { kk, p[0], p[1], floorHeight (p[0], p[1]) - 0.1, 8.6 + 0.8 * hs (kk, 21), Js::atan2 (r[1], r[0]), (hs (kk, 22) - 0.5) * 0.045, r };
}
CaliforniaWorld::Pole CaliforniaWorld::poleAt (int k)
{
    const int kk = (int) imod (k, polesN());
    if (const auto it = poleCache.find (kk); it != poleCache.end()) return it->second;
    return poleCache.emplace (kk, pole (kk)).first->second;
}
std::vector<CaliforniaWorld::Pole> CaliforniaWorld::listPoles()
{
    const int k0 = (int) std::floor (s.sr / poleDs());
    std::vector<Pole> out;
    for (int k = k0 - 2; k <= k0 + (int) std::ceil (poleAhead / poleDs()); ++k) out.push_back (poleAt (k));
    return out;
}

//==============================================================================
std::vector<CaliforniaWorld::Box> CaliforniaWorld::placeholderBoxes() const
{
    std::vector<Box> out;
    for (const auto& sp : spriteList)
    {
        if (sp.noBox || std::find (spritesIn.begin(), spritesIn.end(), sp.name) != spritesIn.end()) continue;
        const auto& ph = sp.ph;
        const double yaw = Js::atan2 (sp.nx, sp.nz);
        Box b; b.x = sp.x; b.y = sp.y + ph.up + ph.h / 2; b.z = sp.z; b.yaw = yaw; b.hx = ph.w / 2; b.hy = ph.h / 2; b.hz = ph.d / 2; b.part = 3;
        b.rect = { ph.col[0], ph.col[1], ph.col[2], 0 }; b.vis = sp.vis; b.ph = sp.name;
        out.push_back (b);
        if (ph.up > 0)
            for (const double sx : { -0.4, 0.4 })
            {
                Box leg; leg.x = sp.x + sp.nz * sx * ph.w; leg.y = sp.y + ph.up / 2; leg.z = sp.z - sp.nx * sx * ph.w; leg.yaw = yaw; leg.hx = 0.15; leg.hy = ph.up / 2 + 0.3; leg.hz = 0.15;
                leg.vis = sp.vis; leg.ph = sp.name;
                out.push_back (leg);
            }
    }
    return out;
}

void CaliforniaWorld::buildSigns (const std::vector<Sign>& table, int atlasW, int atlasH)
{
    std::vector<Box> out;
    std::vector<std::pair<std::string, std::vector<const Sign*>>> byPost;
    for (const auto& sg : table)
    {
        auto it = std::find_if (byPost.begin(), byPost.end(), [&sg] (const auto& e) { return e.first == sg.post; });
        if (it == byPost.end()) { byPost.push_back ({ sg.post, {} }); it = byPost.end() - 1; }
        it->second.push_back (&sg);
    }
    for (const auto& [key, list] : byPost)
    {
        std::string joined;
        for (size_t i = 0; i < list.size(); ++i) joined += (i ? " / " : "") + list[i]->name;
        auto it = std::find_if (signNames.begin(), signNames.end(), [&k = key] (const auto& e) { return e.first == k; });
        if (it == signNames.end()) signNames.push_back ({ key, joined }); else it->second = joined;
    }
    const bool atlas = atlasW > 0 && atlasH > 0;
    for (const auto& [key, list] : byPost)
    {
        const auto posts = std::find_if (signPosts.begin(), signPosts.end(), [&k = key] (const auto& e) { return e.first == k; });
        if (posts == signPosts.end()) continue;
        for (const auto& P0 : posts->second)
        {
            const auto box = [&] (double ox, double oy, double oz, double hx, double hy, double hz, int part, std::optional<V4> rect = std::nullopt)
            {
                Box b; b.x = P0.x + P0.rx * ox + P0.nx * oz; b.y = P0.y + oy; b.z = P0.z + P0.rz * ox + P0.nz * oz; b.yaw = P0.yaw; b.hx = hx; b.hy = hy; b.hz = hz;
                b.part = part; b.rect = rect ? *rect : V4 { -1, 0, 0, 0 }; b.vis = P0.vis; b.flutter = P0.at * 0.01;
                out.push_back (b);
            };
            const auto uv = [&] (const Sign& sg)
            {
                return atlas ? V4 { sg.cell[0] / atlasW, sg.cell[1] / atlasH, (sg.cell[0] + sg.cell[2]) / atlasW, (sg.cell[1] + sg.cell[3]) / atlasH } : V4 { -1, 0, 0, 0 };
            };
            double top = -std::numeric_limits<double>::infinity();
            for (const auto* sg : list) top = jsMax (top, sg->bottom + sg->size[1]);
            if (P0.banner)
            {
                const auto& sg = *list[0];
                const double hw = sg.size[0] / 2;
                for (const double sd : { -1.0, 1.0 }) box (sd * (hw + 0.14), (top + 0.5 - 0.3) / 2, 0, 0.13, (top + 0.5 + 0.3) / 2, 0.13, 0);
                box (0, top + 0.08, 0, hw + 0.14, 0.03, 0.03, 0);                                     // the rope across
                box (0, sg.bottom + sg.size[1] / 2, 0, hw, sg.size[1] / 2, 0, 2, uv (sg));
                continue;
            }
            const bool wide = std::any_of (list.begin(), list.end(), [n = list.size()] (const Sign* sg) { return sg->size[0] > 3.0 && sg->shape == "board" && n == 1; });
            std::vector<double> postsX;
            if (wide) for (const double f : { -0.3, 0.3 }) postsX.push_back (f * list[0]->size[0]); else postsX.push_back (0);
            const double postTop = list.size() > 1 ? top + 0.35 : list[0]->bottom + list[0]->size[1] * (list[0]->shape == "disc" ? 0.55 : 0.85);
            for (const double x : postsX) box (x, (postTop - 0.4) / 2, -0.16, 0.12, (postTop + 0.4) / 2, 0.12, 0);
            for (const auto* sg : list)
            {
                box (0, sg->bottom + sg->size[1] / 2, 0, sg->size[0] / 2, sg->size[1] / 2, 0.035, 1, uv (*sg));
                if (sg->shape == "board" && sg->size[0] > 2.5)                                          // a batten behind a wide board
                    box (0, sg->bottom + sg->size[1] * 0.5, -0.07, sg->size[0] * 0.46, 0.06, 0.03, 0);
            }
        }
    }
    boxes = std::move (out);
    boxes.insert (boxes.end(), fenceBoxes.begin(), fenceBoxes.end());
    for (auto& b : placeholderBoxes()) boxes.push_back (std::move (b));
    shadowAll = true;
}

void CaliforniaWorld::setFlagCells (const std::vector<V4>& cells)
{
    if (cells.empty()) return;
    for (size_t i = 0; i < flagList.size(); ++i) flagList[i].rect = cells[i % cells.size()];
}

void CaliforniaWorld::spriteCame (const std::string& name)
{
    if (std::find (spritesIn.begin(), spritesIn.end(), name) == spritesIn.end()) spritesIn.push_back (name);
    boxes.erase (std::remove_if (boxes.begin(), boxes.end(), [] (const Box& b) { return ! b.ph.empty(); }), boxes.end());
    for (auto& b : placeholderBoxes()) boxes.push_back (std::move (b));
}

void CaliforniaWorld::placeChalk (const std::vector<ChalkDrawing>& table, bool wink)
{
    const double L = loopLength();
    chalkList.clear();
    const auto put = [this] (const ChalkDrawing& dr, double sAt, double d)
    {
        const auto t = tangentAt (thetaAt (sAt)), p = roadPointAt (sAt, d);
        const double along = dr.size[0] * (dr.size[0] <= 2.5 ? chalkSmallStretch : 1);
        chalkList.push_back ({ p[0], p[1], t[0], t[1], dr.size[1] / 2, along / 2, dr.cell, modL (sAt), d, dr.name.empty() ? std::string ("chalk") : dr.name });
    };
    const auto isWord = [] (const ChalkDrawing& dr) { return std::find (chalkWords.begin(), chalkWords.end(), dr.name) != chalkWords.end(); };
    // beside the van's way: left (-1) or right (+1), its near edge clear of the van's middle by `clear` m where it can
    const auto beside = [] (const ChalkDrawing& dr, double side, double clear) -> std::optional<double>
    {
        const double hw = dr.size[1] / 2, room = side > 0 ? 4.6 - lane - hw : chalkSide[1];
        if (room < chalkSide[0]) return std::nullopt;
        return lane + side * clamp (hw + clear, chalkSide[0], room);
    };
    const auto gapAt = [] (double at) { const double k = std::floor (thetaAt (at) / (2 * pi) * dashesN()); return sOfTheta ((k + 0.64) / dashesN() * 2 * pi); };
    const auto straightest = [L] (const std::vector<V2>& avoid)
    {
        double best = 1e9, at = 0;
        for (double a = 0; a < L; a += 10)
        {
            if (std::any_of (avoid.begin(), avoid.end(), [a] (const V2& bm) { return cyc (a, bm[0]) < bm[1]; })) continue;
            const double turn = std::abs (mod (headingAt (a + 60) - headingAt (a - 60) + 3 * pi, 2 * pi) - pi);
            if (turn < best) { best = turn; at = a; }
        }
        return at;
    };
    // the drawn d: JS's null (no room) is 0 on the road, kept as NaN on the drawing
    const auto dOf = [] (std::optional<double> d) { return d ? *d : nan; };
    const auto putAt = [&] (const ChalkDrawing& dr, double sAt, std::optional<double> d)
    {
        put (dr, sAt, d ? *d : 0.0);
        chalkList.back().d = dOf (d);
    };
    std::vector<const ChalkDrawing*> once, cycle;
    for (const auto& dr : table) if (dr.name == "make_love") once.push_back (&dr);
    for (const auto& dr : table) if (dr.name != "make_love" && ! dr.optional) cycle.push_back (&dr);
    std::vector<double> special;
    double loveAt = -1e9;
    if (! once.empty()) { loveAt = gapAt (straightest ({})); put (*once[0], loveAt, 0.0); special.push_back (loveAt); }
    if (wink)
    {
        int i = 0;
        for (const auto& dr : table)
        {
            if (! dr.optional) continue;
            double at = L - 25;
            if (i > 0)
            {
                std::vector<V2> avoid { { L - 25, L / 4 }, { loveAt, 300 } };
                for (const double b : special) avoid.push_back ({ b, 300 });
                at = straightest (avoid);
            }
            putAt (dr, at, beside (dr, -1, 0.8));
            special.push_back (at);
            ++i;
        }
    }
    if (cycle.empty()) return;
    double side = -1;
    int i = 0;
    for (double at = 60; at < L - 120; ++i)
    {
        const auto& dr = *cycle[(size_t) i % cycle.size()];
        double sAt = at;
        std::optional<double> d;
        if (isWord (dr)) { sAt = gapAt (at); d = 0.0; }
        else if (! chalkRight) { d = beside (dr, -1, side < 0 ? 0.6 : 9); side = -side; }    // left: near, far, near...
        else
        {
            d = beside (dr, side, 0.8);
            if (! d) { side = -side; d = beside (dr, side, 0.8); }
            side = -side;
        }
        if (std::all_of (special.begin(), special.end(), [sAt] (double b) { return cyc (sAt, b) > 40; })) putAt (dr, sAt, d);
        at = sAt + chalkStep[0] + (chalkStep[1] - chalkStep[0]) * hs (i + 1, 731);
    }
}

std::vector<CaliforniaWorld::Place> CaliforniaWorld::places() const
{
    std::vector<Place> out;
    const double L = loopLength();
    const std::array<const char*, 3> kinds { "peace sign of flowers", "Feynman van", "monolith" };
    const std::array<double, 3> seenK { 520, 260, 650 };
    for (int lap = 0; lap < 3; ++lap)
        for (int slot = 0; slot < 3; ++slot)
        {
            const int k = (slot + lap) % 3;
            out.push_back ({ "finds", std::string (kinds[(size_t) k]) + " \xC2\xB7 lap " + std::to_string (lap), findSlots[(size_t) slot] * L, seenK[(size_t) k], lap, k });
        }
    for (const auto& [key, spots] : signPosts)
    {
        const bool isRhyme = std::find_if (rhyme.posts.begin(), rhyme.posts.end(), [&k = key] (const char* p) { return k == p; }) != rhyme.posts.end();
        if (isRhyme && key != rhyme.posts[0]) continue;                     // the rhyme: one entry a set, at its first board
        const auto nm = std::find_if (signNames.begin(), signNames.end(), [&k = key] (const auto& e) { return e.first == k; });
        const std::string signName = nm != signNames.end() && ! nm->second.empty() ? nm->second : key;
        for (size_t i = 0; i < spots.size(); ++i)
            out.push_back ({ isRhyme ? "rhyme" : key == "p5" ? "road" : "signs",
                             isRhyme ? std::string ("the rhyme, ") + (i ? "the second time (right)" : "the first time (left)") : signName + (key == "p5" ? " (banner)" : ""),
                             spots[i].at, isRhyme ? 140.0 : 300.0 });
    }
    for (const auto& sp : spriteList)
        if (! sp.noJump)
            out.push_back ({ ! sp.group.empty() ? sp.group : (sp.hill ? "far" : sp.name.rfind ("barn", 0) == 0 ? "barns" : "road"), ! sp.label.empty() ? sp.label : sp.name,
                             sp.at, sp.seen > 0 ? sp.seen : (sp.hill ? 700.0 : 300.0) });
    for (size_t i = 0; i < pumpSpecs.size(); ++i) out.push_back ({ "road", "windpump " + std::to_string (i + 1), pumpSpecs[i].at * L, 350 });
    for (size_t i = 0; i < fenceSpecs.size(); ++i) out.push_back ({ "road", "fence and flags " + std::to_string (i + 1), fenceSpecs[i].from * L, 250 });
    for (const auto& c : chalkList) { auto nm = c.name; std::replace (nm.begin(), nm.end(), '_', ' '); out.push_back ({ "chalk", nm, c.at, 55 }); }
    out.push_back ({ "far", "radio telescopes", dishRow.at, 560 });        // where they first stand whole against the sky ahead
    out.push_back ({ "sky", "saucers", saucer.swoopAt, 150 });             // before the stretch where they swoop
    for (size_t i = 0; i < subSpec.at.size(); ++i) out.push_back ({ "sky", "yellow submarine " + std::to_string (i + 1), subSpec.at[i], 20 });
    const auto lapOf = [] (const Place& q) { return q.group == "finds" ? q.lap : -1; };
    std::stable_sort (out.begin(), out.end(), [&] (const Place& a, const Place& b) { return lapOf (a) != lapOf (b) ? lapOf (a) < lapOf (b) : a.at < b.at; });
    std::map<std::string, int> count, seenN;
    const auto numbered = [] (const Place& q) { return q.group == "chalk" || q.group == "signs"; };
    for (const auto& q : out) if (numbered (q)) ++count[q.name];
    for (auto& q : out) if (numbered (q) && count[q.name] > 1) { const auto base = q.name; q.name += " " + std::to_string (++seenN[base]); }
    return out;
}

//==============================================================================
void CaliforniaWorld::setGear (int g) noexcept { gearWanted = std::clamp (g == 0 ? 1 : g, 1, 2); }
void CaliforniaWorld::setPitchOffset (double rad) noexcept { s.pitchOffset = clamp (rad == rad ? rad : 0, -0.5, 0.5); }

void CaliforniaWorld::settle() noexcept
{
    s.speed = parked ? 0 : speedFor (gearWanted);
    prevSpeed = s.speed;
    if (parked) { s.lat = parkLat; s.park = 1; }
    place();
}

namespace
{
    /** A pothole's knock now and then, and the camera's shake: as step works them out. */
    double potHitAt (double travel, double sp) noexcept
    {
        const double pot = std::floor (travel / 23), potK = W::hs ((int32_t) pot, 91) < 0.35 ? (W::hs ((int32_t) pot, 92) - 0.5) * 6 * sp : 0;
        const double q = travel / 23 - pot - 0.5;
        return potK * Js::exp (-(q * q) / 0.0008);
    }
}

void CaliforniaWorld::settleCamera() noexcept
{
    if (! built) return;
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading), sp = s.speed / 14, potHit = potHitAt (s.travel, sp);
    const double bob = 0.008 * sp * (Js::sin (s.travel * 2.3) + 0.6 * Js::sin (s.travel * 5.7 + 1)) + 0.02 * potHit / 6;
    s.camY = floorHeight (s.x, s.z) + eye + bob;
    const double gb = floorHeight (s.x - fx * 4, s.z - fz * 4), gf = floorHeight (s.x + fx * 4, s.z + fz * 4);
    s.pitch = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 8) - 0.004 * s.aFwd;
}

void CaliforniaWorld::setAt (double sr, int lap)
{
    const double L = loopLength();
    s.sr = modL (sr == sr ? sr : 0);
    if (lap >= 0) s.lap = lap;
    s.camY = 0;
    place();
    s.findFade = smoothstep (findSwapFade[0], findSwapFade[1], std::min (s.sr, L - s.sr));
    resetSaucers();
    yellow.on = false; yellow.pass = -1;
}

double CaliforniaWorld::nextFindAt() const
{
    const double L = loopLength();
    double ahead = std::numeric_limits<double>::infinity();
    for (const double f : findSlots) ahead = jsMin (ahead, modL (f * L - 350 - s.sr));
    return modL (s.sr + ahead);
}

CaliforniaWorld::Jump CaliforniaWorld::jumpTo (const Place& p)
{
    const double L = loopLength();
    const double at = mod (mod (p.at - p.seen - 3 * speedOf(), L) + L, L);
    const int lap = p.lap >= 0 ? p.lap : s.lap;
    setAt (at, lap);
    return { Js::round (at), lap };
}

void CaliforniaWorld::hit (double level) noexcept
{
    const double lv = clamp (level == level ? level : 0, 0, 1);
    s.hitT = 0; s.hitS = lv; s.startleT = 0; s.startleS = lv;
    auto& b = s.bursts[(size_t) (s.hits % petalSlots)];
    b.t = 0; b.x = s.x; b.z = s.z; b.fx = Js::sin (s.heading); b.fz = -Js::cos (s.heading); b.level = lv; b.seed = s.hits % 64;
    s.pend.thV += 0.35 * lv * (s.hits % 2 == 0 ? 1 : -1);
    ++s.hits;
}

void CaliforniaWorld::quiet() noexcept
{
    s.hitT = 1e4; s.hitS = 0; s.startleT = 1e4; s.startleS = 0;
    for (auto& b : s.bursts) { b.t = 1e4; b.level = 0; }
}

void CaliforniaWorld::step (double dt)
{
    const double L = loopLength();
    s.time += dt; s.dt = dt; s.hitT += dt; s.startleT += dt;
    for (auto& b : s.bursts) b.t += dt;
    // The speed: the gears eased; PARK pulls over to the shoulder and stops; leaving PARK, a heavy old van pulls away
    // (a soft S-curve from where it stands to the gear's speed, back into the lane over the same time). A gear changed
    // while it pulls away hands over to the ordinary easing.
    if (parked != wasParked)
    {
        glassPark (parked); wasParked = parked;
        if (! parked) pull = Pull { 0, s.speed, s.lat, gearWanted, pullSeconds + pullPerMs * jsMax (0, speedFor (gearWanted) - gearSpeed[0]) };
        else pull.reset();
    }
    if (pull && pull->gear != gearWanted) pull.reset();
    prevSpeed = s.speed;
    const double prevLatV = s.latV;
    if (pull)
    {
        pull->t += dt;
        const double u = clamp (pull->t / pull->T, 0, 1), e = u * u * u * (u * (u * 6 - 15) + 10);
        s.speed = lerp (pull->v0, speedFor (gearWanted), e);
        const double lat = lerp (pull->lat0, lane, e);
        s.latV = dt > 0 ? (lat - s.lat) / dt : 0; s.lat = lat;
        s.latA = dt > 0 ? (s.latV - prevLatV) / dt : 0;
        if (u >= 1) pull.reset();
    }
    else
    {
        const double target = parked ? 0 : speedFor (gearWanted);
        s.speed += (target - s.speed) * ease (dt, parked ? parkSeconds : speedSeconds);
        if (parked && s.speed < 0.02) s.speed = 0;
        // Across the road: the right lane, PARK to the shoulder; a critically damped spring, no sliding standing still.
        const double latT = parked ? parkLat : lane;
        const double k = parked ? 1.7 : 2.2;
        const double mobile = smoothstep (0.0, 1.5, s.speed);
        s.latA = (k * k * (latT - s.lat) - 2 * k * s.latV) * mobile - s.latV * (1 - mobile) * 4;
        s.latV += s.latA * dt; s.lat += s.latV * dt;
    }
    s.aFwd = dt > 0 ? (s.speed - prevSpeed) / dt : 0;
    s.park += ((parked ? 1 : 0) - s.park) * ease (dt, parked ? 1.5 : 1.0);
    s.parkT = parked ? s.parkT + dt : 0;
    // Along it: round the loop, lap after lap.
    s.sr += s.speed * dt;
    s.travel += s.speed * dt;
    if (s.sr >= L) { s.sr -= L; ++s.lap; }
    place();
    const double sdist = std::min (s.sr, L - s.sr);
    s.findFade = smoothstep (findSwapFade[0], findSwapFade[1], sdist);
    // The forces the pendant feels: the curve (v^2 times the curvature), the lane change, the road's roughness.
    const double kappa = (mod (headingAt (s.sr + 2) - headingAt (s.sr - 2) + 3 * pi, 2 * pi) - pi) / 4;
    const double sp = s.speed / 14;
    const double rough = sp * sp * 0.9 * (Js::sin (s.travel * 0.71) + 0.6 * Js::sin (s.travel * 1.93 + 1.1) + 0.4 * Js::sin (s.travel * 4.1 + 2.3));
    const double potHit = potHitAt (s.travel, sp);
    s.aLeft = -s.speed * s.speed * kappa - s.latA * (pull ? 0.3 : 1) + rough + potHit;     // pulling away the pendant swings little
    // The camera: on the road, a little vibration with the speed; the pitch follows the road's grade.
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double g0 = floorHeight (s.x, s.z);
    const double bob = 0.008 * sp * (Js::sin (s.travel * 2.3) + 0.6 * Js::sin (s.travel * 5.7 + 1)) + 0.02 * potHit / 6;
    const double targetY = g0 + eye + bob;
    if (s.camY == 0) s.camY = targetY;
    s.camY += (targetY - s.camY) * ease (dt, 0.08);
    const double gb = floorHeight (s.x - fx * 4, s.z - fz * 4), gf = floorHeight (s.x + fx * 4, s.z + fz * 4);
    const double pitchT = basePitch + s.pitchOffset + Js::atan2 (gf - gb, 8) - 0.004 * s.aFwd;
    s.pitch += (pitchT - s.pitch) * ease (dt, 0.3);

    // THE PENDANT: a damped pendulum (0.9 s), sideways and fore-aft, kicked by the curves, the lane changes, the
    // bumps, the speed's changes (a quarter of them coupled sideways) and the hits.
    {
        const double w0 = 2 * pi / pendPeriod, zeta = parked && s.speed < 0.3 ? pendZetaParked : pendZeta;
        const double aF = clamp (s.aFwd, -5, 5);
        const double aL = s.aLeft + (pull ? 0.08 : 0.25) * aF;
        auto& p = s.pend;
        const int nsub = (int) jsMax (1, std::ceil (dt * 240));
        const double h = dt / nsub;
        for (int i = 0; i < nsub; ++i)
        {
            p.thV += (-w0 * w0 * (Js::sin (p.th) - aL / gravity * Js::cos (p.th)) - 2 * zeta * w0 * p.thV) * h; p.th += p.thV * h;
            p.phV += (-w0 * w0 * (Js::sin (p.ph) + aF / gravity * Js::cos (p.ph)) - 2 * zeta * w0 * p.phV) * h; p.ph += p.phV * h;
        }
    }

    // The breeze carries the pollen and the fluff (box units, wrapped at 4096).
    const auto wd = wind();
    s.pollenFlow[0] += wd[0] * 0.8 * dt / pollenBox[0]; s.pollenFlow[1] += 0.04 * dt / pollenBox[1]; s.pollenFlow[2] += wd[1] * 0.8 * dt / pollenBox[2];
    s.fluffFlow[0] += wd[0] * dt / fluffBox[0]; s.fluffFlow[1] += 0.10 * dt / fluffBox[1]; s.fluffFlow[2] += wd[1] * dt / fluffBox[2];
    for (auto* fl : { &s.pollenFlow, &s.fluffFlow }) for (auto& q : *fl) { if (q > 4096) q -= 4096; else if (q < -4096) q += 4096; }

    stepButterflies (dt);
    stepGlass (dt);
    stepSaucers (dt);
    stepSub (dt);

    // A flock of birds far off, now and then: across the view from one side, a hundred-odd metres out.
    auto& fl = s.flock;
    if (! fl.on && s.time >= flockNext)
    {
        const int m = flockCount++;
        const double side = hs (m, 61) < 0.5 ? -1 : 1;
        const double bear = (35 + 20 * hs (m, 62)) * pi / 180 * side, dist = 120 + 70 * hs (m, 63);
        const double bx = Js::sin (s.heading + bear), bz = -Js::cos (s.heading + bear);
        const double gx = wrapW (s.x + bx * dist), gz = wrapW (s.z + bz * dist);
        const double cross = s.heading - side * (pi / 2 + 0.4 * (hs (m, 64) - 0.5));
        fl = { true, m, gx, floorHeight (gx, gz) + 30 + 25 * hs (m, 65), gz, Js::sin (cross), -Js::cos (cross), 0, 0 };
    }
    if (fl.on)
    {
        fl.t += dt;
        fl.x = wrapW (fl.x + fl.fx * 11 * dt); fl.z = wrapW (fl.z + fl.fz * 11 * dt);
        fl.vis = smoothstep (0, 4, fl.t) * (1 - smoothstep (flockLife - 5, flockLife, fl.t));
        if (fl.t > flockLife) { fl.on = false; flockNext = s.time + flockEvery - flockLife; }
    }
}

//==============================================================================
double CaliforniaWorld::sh (int k) { return hs (sc.n++, 810 + k); }      // the saucers' own dice, in turn

void CaliforniaWorld::anchor (Saucer& u, Anchor kind)
{
    const double side = sh (1) < 0.5 ? -1 : 1;
    double A = 0, lat = 0, up = 0;
    if (kind == Anchor::front) { A = 120 + 120 * sh (2); lat = side * (15 + 70 * sh (3)); up = 28 + 32 * sh (4); }
    else if (kind == Anchor::side) { A = -60 + 200 * sh (2); lat = side * (230 + 150 * sh (3)); up = 80 + 70 * sh (4); }
    else if (kind == Anchor::park) { A = 85 + 55 * sh (2); lat = side * (10 + 40 * sh (3)); up = 22 + 16 * sh (4); }
    else { A = saucer.swoopAt - s.sr + 30; lat = side * 45; up = 40; }
    const double at = kind == Anchor::prep ? s.sr + modL (saucer.swoopAt + 30 - s.sr) : s.sr + A;
    u.as = at; u.lat = lat; u.up = up; u.front = kind != Anchor::side; u.anchorT = 0; u.kind = kind;
    u.follow = kind == Anchor::front ? 0.5 : kind == Anchor::side ? 0.95 : 0;
    u.loopR = kind == Anchor::park ? 6 + 6 * sh (5) : 14 + 22 * sh (5);
    u.loopW = 2 * pi / (16 + 10 * sh (6));
}

CaliforniaWorld::V3 CaliforniaWorld::roamTarget (const Saucer& u, double t) const
{
    const auto ap = roadPointAt (u.as, u.lat);
    const double a = u.ph + u.loopW * t, k = u.beamOn ? 0.1 : 1;
    const double x = ap[0] + u.loopR * k * Js::cos (a), z = ap[1] + u.loopR * k * 0.7 * Js::sin (a * 1.5), g = floorHeight (x, z);
    const auto r0 = roadPointAt (u.as, 0);
    const double y = u.front ? jsMax (g + 25, floorHeight (r0[0], r0[1]) + u.up) : g + u.up;
    return { x, y + 6 * Js::sin (a * 1.7 + u.i), z };
}

void CaliforniaWorld::resetSaucers()
{
    sc.n = (int) Js::round (s.sr) * 7 + s.lap * 131;
    const bool prep = modL (saucer.swoopAt - s.sr) < 450;
    for (auto& u : sau)
    {
        anchor (u, parked ? Anchor::park : u.i == 0 ? (prep ? Anchor::prep : Anchor::front) : Anchor::side);
        const auto p = roamTarget (u, s.time);
        u.x = p[0]; u.y = p[1]; u.z = p[2]; u.vx = u.vy = u.vz = 0; u.mode = Mode::roam; u.mt = 0; u.path.reset(); u.beam = 0; u.beamOn = false; u.lean = { 0, 0 };
    }
    sc.sinceSeen = 0; sc.called = -1; sc.pair.reset();
    sc.nextSwoop = s.time + 30 + 30 * sh (7); sc.nextPair = s.time + 40 + 40 * sh (8); sc.nextBeam = s.time + 12 + 10 * sh (9);
    sc.swoopPass = prep ? -1 : sc.swoopPass;
}

void CaliforniaWorld::startSwoop (Saucer& u)
{
    const double v = jsMax (s.speed, 1), lead0 = saucer.lead;
    double sC = s.sr + saucer.crossAhead + v * lead0;
    const double th = thetaAt (sC);
    const auto r = rightAt (th), cp = roadPointAt (sC, 0);
    const double side = (u.x - cp[0]) * r[0] + (u.z - cp[1]) * r[1] < 0 ? -1 : 1;
    const auto P = [this] (double a, double lat, double up) { const auto q = roadPointAt (a, lat); return V3 { q[0], floorHeight (q[0], q[1]) + up, q[1] }; };
    const auto E = P (sC - 12, side * 65, 28);
    const double dist = Js::hypot (E[0] - u.x, E[1] - u.y, E[2] - u.z);
    const double lead = jsMax (lead0, dist / 45 + 1.5);
    if (lead > lead0) sC = s.sr + saucer.crossAhead + v * lead;
    const double up = saucer.crossUp[0] + (saucer.crossUp[1] - saucer.crossUp[0]) * sh (10);
    Swoop pa;
    pa.pts = { V3 { u.x, u.y, u.z }, P (sC - 12, side * 65, 28), P (sC, 0, up), P (sC + 16, -side * 60, 30), P (sC + 50, -side * 230, 120) };
    pa.ts = { 0, lead - 1.5, lead, lead + 1.6, lead + 6.5 };
    pa.v0 = { u.vx, u.vy, u.vz };
    u.path = pa;
    u.mode = Mode::swoop; u.mt = 0; u.beamOn = false;
}

namespace
{
    V3 pathAt (const W::Swoop& pa, double t)
    {
        const auto& pts = pa.pts;
        const auto& ts = pa.ts;
        const size_t n = pts.size();
        t = clamp (t, 0, ts[n - 1]);
        size_t i = 0;
        while (i < n - 2 && t > ts[i + 1]) ++i;
        const auto m = [&] (size_t j)
        {
            V3 o {};
            for (size_t c = 0; c < 3; ++c)
                o[c] = j == 0 ? pa.v0[c] : j == n - 1 ? (pts[j][c] - pts[j - 1][c]) / (ts[j] - ts[j - 1]) * 0.5 : (pts[j + 1][c] - pts[j - 1][c]) / (ts[j + 1] - ts[j - 1]);
            return o;
        };
        const double h = ts[i + 1] - ts[i], q = (t - ts[i]) / h, q2 = q * q, q3 = q2 * q;
        const double h00 = 2 * q3 - 3 * q2 + 1, h10 = q3 - 2 * q2 + q, h01 = -2 * q3 + 3 * q2, h11 = q3 - q2;
        const auto m0 = m (i), m1 = m (i + 1);
        V3 o {};
        for (size_t c = 0; c < 3; ++c) o[c] = h00 * pts[i][c] + h10 * h * m0[c] + h01 * pts[i + 1][c] + h11 * h * m1[c];
        return o;
    }
}

void CaliforniaWorld::stepSaucers (double dt)
{
    if (dt <= 0) return;
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading), v = s.speed;
    const double toStretch = modL (saucer.swoopAt - s.sr);
    const int pass = s.lap * 100000 + (int) Js::round (saucer.swoopAt);
    const bool busy = std::any_of (sau.begin(), sau.end(), [] (const Saucer& u) { return u.mode != Mode::roam; });
    // the swooping stretch: one waits over it, then swoops so it crosses ahead of the van
    if (! parked && toStretch < 450 && sc.swoopPass != pass)
    {
        auto& u = sau[0];
        if (u.mode == Mode::roam && u.kind != Anchor::prep) anchor (u, Anchor::prep);
        if (u.mode == Mode::roam && toStretch < saucer.crossAhead + jsMax (v, 1) * saucer.lead) { startSwoop (u); sc.swoopPass = pass; }
    }
    else if (! parked && ! busy && s.time > sc.nextSwoop && toStretch > 700 && ! yellow.on)
    {
        auto& u = sau[sh (11) < 0.5 ? 0 : 1];
        startSwoop (u);
        sc.nextSwoop = s.time + saucer.swoopEvery[0] + (saucer.swoopEvery[1] - saucer.swoopEvery[0]) * sh (12);
    }
    // a pair hovering side by side, rocking, one of them beaming
    if (! busy && s.time > sc.nextPair && toStretch > 600)
    {
        const double A = parked ? 100 : 190;
        const double sgn = sh (13) < 0.5 ? -1 : 1;
        const double lat = sgn * (20 + 35 * sh (14));
        Pair P;
        P.as = s.sr + A; P.lat = lat; P.up = parked ? 30 : 42; P.beamer = sh (15) < 0.5 ? 0 : 1;
        sc.pair = P;
        for (auto& u : sau) { u.mode = Mode::pair; u.mt = 0; u.beamOn = false; }
        sc.nextPair = s.time + saucer.pairEvery[0] + (saucer.pairEvery[1] - saucer.pairEvery[0]) * sh (16);
    }
    // a beam: one in view, roaming, comes down a little and holds still
    if (s.time > sc.nextBeam)
    {
        const auto it = std::find_if (sau.begin(), sau.end(), [] (const Saucer& q) { return q.mode == Mode::roam && q.front && q.kind != Anchor::prep && ! q.beamOn; });
        if (it != sau.end()) { it->beamOn = true; it->beamT = 0; it->up = jsMin (it->up, 48); }
        sc.nextBeam = s.time + saucer.beamEvery[0] + (saucer.beamEvery[1] - saucer.beamEvery[0]) * sh (17);
    }
    if (sc.pair && sau[0].mt > saucer.pairFor + 6)                 // the pair over: both roam again
    {
        for (auto& u : sau) { u.mode = Mode::roam; u.beamOn = false; anchor (u, u.i == 0 ? Anchor::front : Anchor::side); }
        sc.pair.reset();
    }
    if (sc.pair)                                                     // the pair's place, moving on at 0.6 of the van's pace
    {
        auto& P = *sc.pair;
        P.as += v * 0.6 * dt;
        const auto q = roadPointAt (P.as, P.lat);
        P.x = q[0]; P.z = q[1];
        const auto r0 = roadPointAt (P.as, 0);
        P.y = floorHeight (r0[0], r0[1]) + P.up;
        P.y = jsMax (P.y, floorHeight (P.x, P.z) + 22);
        const double ux = P.x - s.x, uz = P.z - s.z, ul = orOne (Js::hypot (ux, uz));
        P.rx = -uz / ul; P.rz = ux / ul;
    }
    bool seen = false;
    for (auto& u : sau)
    {
        u.mt += dt; u.anchorT += dt;
        const double pvx = u.vx, pvz = u.vz;
        if (u.mode == Mode::swoop)
        {
            const auto& pa = *u.path;
            const double T = pa.ts.back();
            const auto p1 = pathAt (pa, u.mt), p0 = pathAt (pa, u.mt - dt);
            u.x = p1[0]; u.y = p1[1]; u.z = p1[2]; u.vx = (p1[0] - p0[0]) / dt; u.vy = (p1[1] - p0[1]) / dt; u.vz = (p1[2] - p0[2]) / dt;
            if (u.mt >= T) { u.mode = Mode::roam; u.path.reset(); anchor (u, Anchor::side); }
        }
        else
        {
            double tx = 0, ty = 0, tz = 0;
            if (u.mode == Mode::pair)
            {
                const auto& P = *sc.pair;
                const double sd = u.i == 0 ? -1 : 1;
                tx = P.x + P.rx * sd * 9; tz = P.z + P.rz * sd * 9; ty = P.y + 1.5 * Js::sin (s.time * 0.8 + u.i * 2);
                const bool there = Js::hypot (tx - u.x, ty - u.y, tz - u.z) < 12;
                if (u.i == P.beamer) u.beamOn = there && u.mt > 5 && u.mt < saucer.pairFor - 2;
            }
            else
            {
                // anchors: refreshed when the van comes up to a front one, after a while for the others; PARK brings them near
                u.as += v * u.follow * dt;
                const double along = u.as - s.sr;
                if (parked && u.kind != Anchor::park && ! u.beamOn) anchor (u, Anchor::park);
                else if (! parked && u.kind == Anchor::park) anchor (u, u.i == 0 ? Anchor::front : Anchor::side);
                else if (! u.beamOn && u.kind != Anchor::prep && ((u.front && along < 75) || u.anchorT > (u.front ? 45 : 30)))
                    anchor (u, parked ? Anchor::park : sh (18) < (u.i == 0 ? 0.7 : 0.4) ? Anchor::front : Anchor::side);
                else if (u.kind == Anchor::prep && (along < 0 || along > 520)) anchor (u, Anchor::front);
                const auto tg = roamTarget (u, s.time);
                tx = tg[0]; ty = tg[1]; tz = tg[2];
                if (u.beamOn && (u.beamT += dt) > saucer.beamFor) { u.beamOn = false; u.up = 40 + 40 * sh (19); }
            }
            // steered: a wanted velocity toward the target (at most vmax), eased in
            const double tau = u.mode == Mode::pair || u.beamOn ? 1.6 : 2.4;
            double wx = (tx - u.x) / tau, wy = (ty - u.y) / tau, wz = (tz - u.z) / tau;
            const double wl = Js::hypot (wx, wy, wz), vm = saucer.vmax;
            if (wl > vm) { wx *= vm / wl; wy *= vm / wl; wz *= vm / wl; }
            const double k = ease (dt, 0.9);
            u.vx += (wx - u.vx) * k; u.vy += (wy - u.vy) * k; u.vz += (wz - u.vz) * k;
            u.x += u.vx * dt; u.y += u.vy * dt; u.z += u.vz * dt;
            u.y = jsMax (u.y, floorHeight (u.x, u.z) + 8);
        }
        // leaning into the acceleration (and a little into the way it goes); a pair rocks toward each other
        const double ahx = (u.vx - pvx) / dt, ahz = (u.vz - pvz) / dt;
        double lx = clamp (0.03 * ahx + 0.004 * u.vx, -0.45, 0.45), lz = clamp (0.03 * ahz + 0.004 * u.vz, -0.45, 0.45);
        if (u.mode == Mode::pair && sc.pair) { const double r = 0.2 * Js::sin (u.mt * 0.9) * (u.i == 0 ? 1 : -1); lx += sc.pair->rx * r; lz += sc.pair->rz * r; }
        u.lean[0] += (lx - u.lean[0]) * ease (dt, 0.35); u.lean[1] += (lz - u.lean[1]) * ease (dt, 0.35);
        u.yaw += dt * 0.35;
        u.beam = clamp (u.beam + (u.beamOn ? dt / 0.8 : -dt / 0.6), 0, 1);
        // in the front view: ahead within 38 degrees, from just under the horizon to 22 degrees up, within 500 m
        const double dx = u.x - s.x, dz = u.z - s.z, dy = u.y - s.camY, D = Js::hypot (dx, dz);
        if (D < 500 && (dx * fx + dz * fz) > D * 0.788 && dy > -0.05 * D && dy < 0.40 * D) seen = true;
    }
    sc.sinceSeen = seen ? 0 : sc.sinceSeen + dt;
    if (seen) sc.called = -1;
    // none in the front view for too long: call one in (again after 12 s if it did not come)
    if (sc.sinceSeen > saucer.dark && sc.called < 0)
    {
        auto it = std::find_if (sau.begin(), sau.end(), [] (const Saucer& q) { return q.mode == Mode::roam && ! q.front; });
        if (it == sau.end()) it = std::find_if (sau.begin(), sau.end(), [] (const Saucer& q) { return q.mode == Mode::roam; });
        if (it != sau.end()) { anchor (*it, parked ? Anchor::park : Anchor::front); sc.called = it->i; }
    }
    else if (sc.called >= 0 && sc.sinceSeen > saucer.dark + 12) sc.called = -1;
}

void CaliforniaWorld::stepSub (double dt)
{
    auto& sb = yellow;
    if (sb.on)
    {
        sb.t += dt;
        sb.as += subSpec.follow * s.speed * dt;
        const double e = clamp (sb.t / subSpec.T, 0, 1);                       // steadily, not lingering at the sides
        const double lat = sb.from * subSpec.side * (1 - 2 * e);               // from beyond one side of the view to beyond the other
        const auto q = roadPointAt (sb.as, lat);
        const double x = q[0], z = q[1];
        // on a line of the screen that bends down toward the sides (the glass's top corners are round)
        const double D = Js::hypot (x - s.x, z - s.z), hf = Js::sin (s.heading), hz = -Js::cos (s.heading);
        const double az = Js::atan2 ((x - s.x) * -hz + (z - s.z) * hf, (x - s.x) * hf + (z - s.z) * hz), qs = jsMin (std::abs (az) / 0.84, 1.2);
        // (a flat screen lifts a thing at the same angle up toward its sides by 1 / cos: taken out)
        // Math.tan and Math.atan: the C library's (no V8 port of them in the walker's Js)
        const double scrWant = subSpec.screen - 0.08 * qs * qs;
        const double want = s.camY + D * std::tan (s.pitch + std::atan (scrWant * Js::cos (az)));
        sb.yb = sb.t < dt * 1.5 ? want : sb.yb + (want - sb.yb) * ease (dt, 1.2);
        const double y = jsMax (sb.yb, floorHeight (x, z) + 15) + 1.2 * Js::sin (sb.t * 0.55) + 0.5 * Js::sin (sb.t * 1.3);
        // a hill under its way off to the side would lift it into the glass's top corner: there it fades away instead
        const double scr = std::tan (Js::atan2 (y - s.camY, D) - s.pitch) / Js::cos (az);
        const bool lifted = std::abs (az) > 0.35 && scr - scrWant > 0.06;
        sb.lift = clamp (sb.lift + (lifted ? -dt / 1.2 : dt / 1.2), 0, 1);
        if (sb.t < dt * 1.5) sb.lift = lifted ? 0 : 1;
        {
            // side-on to the van: the bow across the road toward the side it goes to, a slow sway as if swimming
            const auto r = rightAt (thetaAt (sb.as));
            const double wantYaw = Js::atan2 (sb.from * r[0], -sb.from * r[1]) + 0.12 * Js::sin (sb.t * 0.4);
            double d = wantYaw - sb.yaw;
            d = Js::atan2 (Js::sin (d), Js::cos (d));
            sb.yaw += d * ease (dt, 1.2);
        }
        const double fxs = -Js::sin (sb.yaw), fzs = Js::cos (sb.yaw), pitch = 0.04 * Js::sin (sb.t * 0.45 + 1.2), roll = 0.05 * Js::sin (sb.t * 0.32);
        sb.lean = { fxs * pitch - fzs * roll, fzs * pitch + fxs * roll };
        sb.x = x; sb.y = y; sb.z = z;
        sb.fade = smoothstep (0, 3, sb.t) * (1 - smoothstep (subSpec.T - 3, subSpec.T, sb.t)) * smoothstep (0, 1, sb.lift);
        if (sb.t >= subSpec.T || (sb.lift <= 0 && sb.t > subSpec.T / 2)) sb.on = false;
        return;
    }
    // twice a lap, as the van passes either place (not while a saucer swoops; then it waits)
    for (size_t i = 0; i < subSpec.at.size(); ++i)
    {
        const int pass = s.lap * 10 + (int) i;
        const double ahead = modL (s.sr - subSpec.at[i]);
        if (sb.on || sb.pass == pass || ahead > 120 || parked || std::any_of (sau.begin(), sau.end(), [] (const Saucer& u) { return u.mode == Mode::swoop; })) continue;
        sb.pass = pass; sb.on = true; sb.t = 0; sb.k++;
        sb.from = hs (sb.k + pass, 901) < 0.5 ? -1 : 1;
        sb.up = subSpec.up[0] + (subSpec.up[1] - subSpec.up[0]) * hs (sb.k + pass, 902);
        sb.as = s.sr + subSpec.ahead;
        const auto q = roadPointAt (sb.as, sb.from * subSpec.side);
        const auto r = rightAt (thetaAt (sb.as));                              // its bow across the road, toward the other side
        const auto r0 = roadPointAt (sb.as, 0);
        sb.x = q[0]; sb.z = q[1]; sb.y = floorHeight (r0[0], r0[1]) + sb.up; sb.yaw = Js::atan2 (sb.from * r[0], -sb.from * r[1]);
    }
}

//==============================================================================
std::vector<CaliforniaWorld::Caster> CaliforniaWorld::casters (double x0, double z0, double x1, double z1) const
{
    std::vector<Caster> out;
    const auto sxz = sunXZ();
    const V2 D { -sxz[0], -sxz[1] };
    const double sinv = 1 / sunTan();
    const auto add = [&] (double ax, double az, double bx, double bz, double r, double dark, double soft)
    {
        if (jsMax (ax, bx) + r + soft < x0 || jsMin (ax, bx) - r - soft > x1 || jsMax (az, bz) + r + soft < z0 || jsMin (az, bz) - r - soft > z1) return;
        out.push_back ({ ax, az, bx, bz, r, dark, soft });
    };
    const double wx = (x0 + x1) / 2, wz = (z0 + z1) / 2;
    for (const auto& pm : pumpList) add (pm.x, pm.z, pm.x + D[0] * 11 * sinv, pm.z + D[1] * 11 * sinv, 0.9, 0.35, 0.8);
    for (const auto& sp : spriteList)
    {
        if (sp.noBox) continue;                                   // the far giant: no shadow near
        const double top = sp.ph.up + sp.ph.h, lo = sp.ph.up;
        add (sp.x + D[0] * lo * sinv, sp.z + D[1] * lo * sinv, sp.x + D[0] * top * sinv, sp.z + D[1] * top * sinv, sp.ph.w * 0.45, 0.62, 1.2);
    }
    for (const auto& b : boxes)
        if (b.part == 1 || b.part == 2)
        {
            const double lo = (b.y - floorHeight (b.x, b.z) + 0.0) - b.hy, hi = lo + 2 * b.hy;
            const double bx = b.x + D[0] * jsMax (lo, 0) * sinv, bz = b.z + D[1] * jsMax (lo, 0) * sinv;
            add (bx, bz, b.x + D[0] * hi * sinv, b.z + D[1] * hi * sinv, b.hx * 0.75, 0.6, 1.0);
        }
    for (const auto& o : oaks)
    {
        const double ox = wx + nearest (o.x - wx), oz = wz + nearest (o.z - wz);
        const double hb = 0.42 * o.R * o.s, ht = 1.3 * o.R * o.s;
        add (ox, oz, ox + D[0] * hb * sinv, oz + D[1] * hb * sinv, 0.45, 0.7, 0.5);
        add (ox + D[0] * hb * sinv, oz + D[1] * hb * sinv, ox + D[0] * ht * sinv, oz + D[1] * ht * sinv, o.R * 0.82, 0.74, 1.2);
    }
    return out;
}

void CaliforniaWorld::rasterRect (int i0, int j0, int i1, int j1)
{
    const double x0 = i0 * shTexel, z0 = j0 * shTexel, x1 = i1 * shTexel, z1 = j1 * shTexel;
    const auto at = [] (int i, int j) { return (size_t) ((imod (j, shN) * shN + imod (i, shN)) * 4); };
    for (int j = j0; j < j1; ++j)
        for (int i = i0; i < i1; ++i) { const size_t o = at (i, j); shPix[o] = 0; shPix[o + 1] = 255; shPix[o + 2] = 0; shPix[o + 3] = 255; }
    for (const auto& c : casters (x0, z0, x1, z1))
    {
        const int bi0 = std::max (i0, (int) std::floor ((jsMin (c.ax, c.bx) - c.r - c.soft) / shTexel)), bi1 = std::min (i1, (int) std::ceil ((jsMax (c.ax, c.bx) + c.r + c.soft) / shTexel));
        const int bj0 = std::max (j0, (int) std::floor ((jsMin (c.az, c.bz) - c.r - c.soft) / shTexel)), bj1 = std::min (j1, (int) std::ceil ((jsMax (c.az, c.bz) + c.r + c.soft) / shTexel));
        const double ex = c.bx - c.ax, ez = c.bz - c.az, ee = jsMax (ex * ex + ez * ez, 1e-6);
        for (int j = bj0; j < bj1; ++j)
            for (int i = bi0; i < bi1; ++i)
            {
                const double px = (i + 0.5) * shTexel, pz = (j + 0.5) * shTexel;
                const double t = clamp (((px - c.ax) * ex + (pz - c.az) * ez) / ee, 0, 1);
                const double d = Js::hypot (px - c.ax - ex * t, pz - c.az - ez * t);
                const double soft = c.soft * (1 + 1.5 * t);
                const double cov = (1 - smoothstep (c.r - soft, c.r + soft, d)) * c.dark * (1 - 0.35 * t);
                if (cov <= 0) continue;
                const size_t o = at (i, j);
                shPix[o] = (uint8_t) std::max ((double) shPix[o], Js::round (cov * 255));
            }
    }
    shUpdates.push_back ({ i0, j0, i1, j1 });
}

CaliforniaWorld::V2 CaliforniaWorld::updateShadow()
{
    listOaks();
    const double fx = Js::sin (s.heading), fz = -Js::cos (s.heading);
    const double cx = s.x + fx * shAhead, cz = s.z + fz * shAhead;
    const std::array<int, 2> want { (int) (Js::round ((cx / shTexel - shN / 2.0) / shStep) * shStep), (int) (Js::round ((cz / shTexel - shN / 2.0) / shStep) * shStep) };
    if (! shO || shadowAll || std::abs (want[0] - (*shO)[0]) >= shN / 2 || std::abs (want[1] - (*shO)[1]) >= shN / 2)
    {
        shadowAll = false;
        shO = want;
        rasterRect ((*shO)[0], (*shO)[1], (*shO)[0] + shN, (*shO)[1] + shN);
    }
    else
    {
        auto& o = *shO;
        int moves = 0;
        while (o[0] != want[0] && moves++ < 4)
        {
            if (want[0] > o[0]) { rasterRect (o[0] + shN, o[1], o[0] + shN + shStep, o[1] + shN); o[0] += shStep; }
            else { rasterRect (o[0] - shStep, o[1], o[0], o[1] + shN); o[0] -= shStep; }
        }
        while (o[1] != want[1] && moves++ < 8)
        {
            if (want[1] > o[1]) { rasterRect (o[0], o[1] + shN, o[0] + shN, o[1] + shN + shStep); o[1] += shStep; }
            else { rasterRect (o[0], o[1] - shStep, o[0] + shN, o[1]); o[1] -= shStep; }
        }
    }
    return { ((*shO)[0] + shN / 2.0) * shTexel, ((*shO)[1] + shN / 2.0) * shTexel };
}

double CaliforniaWorld::lightAtVan()
{
    if (! shO) return 1;
    const int i = (int) std::floor (s.x / shTexel), j = (int) std::floor (s.z / shTexel);
    const size_t o = (size_t) ((imod (j, shN) * shN + imod (i, shN)) * 4);
    double lit = (1 - shPix[o] / 255.0) * sunAt (s.x, s.z);
    const auto sxz = sunXZ();
    const V2 D { -sxz[0], -sxz[1] };
    const int k0 = (int) std::floor (s.sr / poleDs());
    for (int q = -2; q <= 2; ++q)
    {
        const auto p = poleAt (k0 + q);
        const double L = p.h / sunTan();
        const double qx = nearest (s.x - p.x), qz = nearest (s.z - p.z), t = clamp (qx * D[0] + qz * D[1], 0, L);
        const double d = Js::hypot (qx - D[0] * t, qz - D[1] * t), w = 0.10 + 0.0048 * t;
        lit *= 1 - (1 - smoothstep (w * 0.4, w * 1.6, d)) * (0.62 - 0.40 * t / L);
    }
    return lit;
}

} // namespace orbitamp::california
