// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for redgroove::WalkerWorld — the walker's world against its prototype, walker-world.js, number for
// number. The fixture (tests/fixtures/walker/walker-world.txt) is the prototype's own state, dumped by node: the hash
// and the noise, the transcendentals the drive feeds at its constant arguments, the meshes, the textures, a few
// cells' mushrooms, the start window's instance lists, the walker after 1, 5, 20 and 120 s at gears 1 and 2 free and
// at 100 bpm, gear 3 and TURBO, a shift of gear and TURBO, a held note with an onset and two attacks, a drag across
// the glass, three stretches through dense clusters traced every half second (so a different choice of way shows
// where it is made), a walk across the world's wrap, and the world before build(). This makes the same entries the same way and compares them: a double within 1e-6, an f32
// value, a hash or a count exactly, and the `x` entries (the transcendentals) to the last bit.
//
// `walker_world_test [fixture]` compares; `walker_world_test --dump` prints this side's entries in the fixture's
// format instead. Returns non-zero on any failure. JUCE-free, like the code under test.

#include "redgroove/view/WalkerWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef WALKER_FIXTURE
 #define WALKER_FIXTURE "tests/fixtures/walker/walker-world.txt"
#endif

using orbitamp::redgroove::WalkerWorld;

namespace
{
    struct Entry { std::string label; char kind; std::vector<double> v; };
    std::vector<Entry> made;

    void out (const std::string& label, char kind, std::vector<double> v) { made.push_back ({ label, kind, std::move (v) }); }

    uint32_t fnvStep (uint32_t h, uint32_t w) { h ^= w; return h * 0x01000193u; }
    uint32_t fnvWords (const float* p, size_t n)
    {
        uint32_t h = 0x811c9dc5u;
        for (size_t i = 0; i < n; ++i) { uint32_t w; std::memcpy (&w, p + i, 4); h = fnvStep (h, w); }
        return h;
    }
    template <typename T>
    uint32_t fnvValues (const T* p, size_t n)
    {
        uint32_t h = 0x811c9dc5u;
        for (size_t i = 0; i < n; ++i) h = fnvStep (h, (uint32_t) p[i]);
        return h;
    }
    double sumOf (const float* p, size_t n) { double t = 0; for (size_t i = 0; i < n; ++i) t += p[i]; return t; }
    std::vector<double> floats (const float* p, size_t n) { return std::vector<double> (p, p + n); }

    constexpr double dt = 1.0 / 60;

    void dumpState (const std::string& label, const WalkerWorld& w, bool full = false)
    {
        const auto& s = w.state();
        out (label + ".s", 'd', { s.x, s.z, s.heading, s.heading0, s.pitch, s.roll, s.nod, s.time, s.travel, s.rate, s.stepLen,
                                  s.speed, s.psi, s.heave, s.sway, s.eyeX, s.eyeZ, s.camY, s.groundY, s.pace, s.brake, s.squeeze,
                                  s.spores, s.storm, s.breath, s.bass, s.hit, s.hitGlow, s.beat, s.bpm, s.footfall });
        for (int i = 0; i < 2; ++i)
        {
            const auto& L = s.legs[(size_t) i];
            std::vector<double> v { L.side, L.off };
            for (const auto* a : { &L.foot, &L.from, &L.to }) v.insert (v.end(), a->begin(), a->end());
            v.push_back (L.lift);
            for (const auto* a : { &L.hip, &L.knee, &L.pos }) v.insert (v.end(), a->begin(), a->end());
            out (label + ".leg" + std::to_string (i), 'd', v);
        }
        out (label + ".n", 'i', { (double) s.burstAt, (double) s.printAt, s.legs[0].swing ? 1.0 : 0.0, s.legs[1].swing ? 1.0 : 0.0,
                                  (double) fnvWords (s.bursts.data(), s.bursts.size()), (double) fnvWords (s.prints.data(), s.prints.size()) });
        out (label + ".sums", 'd', { sumOf (s.bursts.data(), s.bursts.size()), sumOf (s.prints.data(), s.prints.size()) });
        if (full)
        {
            out (label + ".bursts", 'f', floats (s.bursts.data(), s.bursts.size()));
            out (label + ".prints", 'f', floats (s.prints.data(), s.prints.size()));
        }
    }

    struct Opts { int gear = 1; double bpm = 0; bool turbo = false; bool at = false; double x = 0, z = 0; };
    std::unique_ptr<WalkerWorld> make (const Opts& o)
    {
        auto w = std::make_unique<WalkerWorld>();
        if (o.at) w->startAt (o.x, o.z);
        w->setGear (o.gear); w->setTurbo (o.turbo); w->setBpm (o.bpm);
        w->build(); w->settle();
        return w;
    }
    template <typename Each>
    void run (WalkerWorld& w, int steps, Each each) { for (int i = 0; i < steps; ++i) { each (w); w.step (dt); } }
    void run (WalkerWorld& w, int steps) { run (w, steps, [] (WalkerWorld&) {}); }

    void meshOut (const std::string& label, const WalkerWorld::Mesh& m)
    {
        out (label, 'i', { (double) m.verts.size(), (double) m.idx.size(), (double) fnvWords (m.verts.data(), m.verts.size()),
                           (double) fnvValues (m.idx.data(), m.idx.size()) });
        out (label + ".sum", 'd', { sumOf (m.verts.data(), m.verts.size()) });
    }

    void makeAll()
    {
        // The hashes and the noise's corners.
        {
            std::vector<double> v;
            for (int64_t x : { 0LL, 1LL, 12345LL, 0x7fffffffLL, 0xffffffffLL, -5LL }) v.push_back (WalkerWorld::ih ((uint32_t) x));
            out ("hash.ih", 'i', v);
            const int32_t a[6][3] { { 0, 0, 0 }, { 3, 7, 101 }, { -5, 7, 117 }, { 4095, 4095, 133 }, { 204, -1, 1101 }, { INT32_MIN, INT32_MAX, 5505 } };
            v.clear();
            for (const auto& c : a) v.push_back (WalkerWorld::hcell (c[0], c[1], c[2]));
            out ("hash.hcell", 'f', v);
        }
        // Transcendentals at the constant arguments the drive feeds.
        {
            std::vector<double> v;
            for (int k = 0; k < 3; ++k) for (double t : { 0.3, 0.0, 1.0 }) v.push_back (WalkerWorld::stalkRadius (k, t));
            out ("math.stalk", 'x', v);
            v.clear();
            for (double tau : { 0.45, 0.6, 0.7, 0.4, 1.1, 0.06, 0.8, 1.2, WalkerWorld::hitFlash, WalkerWorld::hitGlow }) v.push_back (1 - WalkerWorld::Js::exp (-dt / tau));
            out ("math.ease", 'x', v);
            v.clear();
            for (double u : { 0.0, 0.05, 0.1, 0.3, 0.5, 0.77, 0.999 }) { v.push_back (WalkerWorld::stepRise (u)); v.push_back (WalkerWorld::stepThud (u)); }
            out ("math.shapes", 'd', v);
            const auto k1 = WalkerWorld::kneeOf ({ 2.1, 3.0, 0.6 }, { 2.7, 0.4, -5.2 }, WalkerWorld::femur, WalkerWorld::tibia, { 0.15, 0.55, -1 });
            out ("math.knee", 'd', { k1[0], k1[1], k1[2] });
            const auto k2 = WalkerWorld::kneeOf ({ 2.1, 3.0, 0.6 }, { 2.1, 3.0, 0.6 }, WalkerWorld::femur, WalkerWorld::tibia, { 0.15, 0.55, -1 });
            out ("math.knee0", 'd', { k2[0], k2[1], k2[2] });
            const auto k3 = WalkerWorld::kneeOf ({ -2.1, 3.0, 0.6 }, { -9, -6, -12 }, WalkerWorld::femur, WalkerWorld::tibia, { -0.15, 0.55, -1 });
            out ("math.kneeFar", 'd', { k3[0], k3[1], k3[2] });
        }
        // The meshes the renderer makes (the limbs' profiles are the renderer's).
        for (int k = 0; k < WalkerWorld::numClasses; ++k)
            for (int v = 0; v < WalkerWorld::variants; ++v)
                meshOut ("mesh.mush" + std::to_string (k) + std::to_string (v), WalkerWorld::mushroomMesh (v, WalkerWorld::classes[(size_t) k].segs));
        {
            const double pi = WalkerWorld::pi;
            std::vector<std::array<double, 2>> joint;
            for (int i = 1; i < 12; ++i) { const double a = -pi / 2 + pi * i / 12; joint.push_back ({ 0.56 * WalkerWorld::Js::cos (a), 0.56 * WalkerWorld::Js::sin (a) }); }
            const std::pair<const char*, std::vector<std::array<double, 2>>> pieces[] {
                { "femur", { { 0.30, 0.0 }, { 0.44, 0.06 }, { 0.42, 0.45 }, { 0.36, 0.92 }, { 0.28, 1.0 } } },
                { "tibia", { { 0.30, 0.0 }, { 0.31, 0.05 }, { 0.25, 0.45 }, { 0.19, 0.93 }, { 0.15, 1.0 } } },
                { "joint", joint },
                { "foot", { { 0.60, 0.0 }, { 0.95, 0.05 }, { 0.95, 0.20 }, { 0.70, 0.34 }, { 0.38, 0.46 }, { 0.30, 0.58 } } },
            };
            for (const auto& p : pieces) meshOut (std::string ("mesh.") + p.first, WalkerWorld::lathe (p.second, 28));
            std::vector<double> jp;
            for (const auto& q : joint) { jp.push_back (q[0]); jp.push_back (q[1]); }
            out ("mesh.jointProfile", 'd', jp);
            meshOut ("mesh.ground", WalkerWorld::polarGrid (68, 96, 0.5, 420.0));
        }

        // The textures and the ground under the start.
        {
            auto w = make ({});
            const auto& mips = w->heightMips();
            for (size_t l = 0; l < mips.size(); ++l) out ("tex.mip" + std::to_string (l), 'i', { (double) mips[l].size(), (double) fnvWords (mips[l].data(), mips[l].size()) });
            std::vector<double> v;
            for (const auto& xy : { std::array<int, 2> { 0, 0 }, { 17, 300 }, { 511, 511 }, { 256, 128 }, { 400, 3 } })
                for (int c = 0; c < 4; ++c) v.push_back (mips[0][(size_t) ((xy[1] * 512 + xy[0]) * 4 + c)]);
            out ("tex.samples", 'f', v);
            out ("tex.mip1samples", 'f', floats (mips[1].data(), 8));
            const auto& d = w->detail();
            v = { (double) d.size(), (double) fnvValues (d.data(), d.size()) };
            for (int i = 0; i < 8; ++i) v.push_back (d[(size_t) i]);
            for (int i = 0; i < 4; ++i) v.push_back (d[(size_t) (4 * (64 * 33 + 17) + i)]);
            out ("tex.detail", 'i', v);
            v.clear();
            for (const auto& xz : { std::array<double, 2> { 2033, 2125 }, { 0.3, 8191.7 }, { 5000.25, 123.5 }, { -3, -7 }, { 1023.9, 1024.1 } })
                v.push_back (w->groundH (xz[0], xz[1]));
            out ("ground", 'd', v);
            // The mushrooms of a few cells, every field.
            const int cells[8][3] { { 0, 1016, 1062 }, { 0, -3, 4100 }, { 1, 254, 265 }, { 1, 255, 265 }, { 1, 1030, -2 }, { 2, 50, 53 }, { 2, 51, 53 }, { 2, -1, 204 } };
            for (const auto& c : cells)
            {
                WalkerWorld::Mushroom m;
                const std::string label = "mush." + std::to_string (c[0]) + "." + std::to_string (c[1]) + "." + std::to_string (c[2]);
                if (w->mushroomAt (c[0], c[1], c[2], m))
                    out (label, 'd', { 1, m.x, m.z, m.size, (double) m.variant, (double) m.hue, m.phase, m.rot, m.lean, m.glow, m.stalkR, m.clear, m.capR, m.capY });
                else
                    out (label, 'd', { 0 });
            }
            // The start window's instance lists, placed as the renderer places them.
            const auto& s = w->state();
            const double fx = WalkerWorld::Js::sin (s.heading), fz = -WalkerWorld::Js::cos (s.heading);
            for (int k = 0; k < WalkerWorld::numClasses; ++k)
            {
                const auto& C = WalkerWorld::classes[(size_t) k];
                const double shift = C.w * C.cell * 0.3;
                const int bx = (int) std::floor ((s.x + fx * shift) / C.cell) - C.w / 2, bz = (int) std::floor ((s.z + fz * shift) / C.cell) - C.w / 2;
                std::array<std::vector<float>, WalkerWorld::variants> per;
                w->instancesOf (k, bx, bz, per);
                out ("inst." + std::to_string (k) + ".base", 'i', { (double) bx, (double) bz });
                for (int vv = 0; vv < WalkerWorld::variants; ++vv)
                {
                    const auto& l = per[(size_t) vv];
                    const std::string label = "inst." + std::to_string (k) + "." + std::to_string (vv);
                    out (label, 'i', { (double) l.size(), (double) fnvWords (l.data(), l.size()) });
                    out (label + ".first", 'f', floats (l.data(), std::min<size_t> (8, l.size())));
                }
            }
            dumpState ("start", *w, true);
        }

        // The walks: gears 1 and 2, free and at 100 bpm; 1, 5, 20 and 120 s.
        for (int gear : { 1, 2 })
            for (int bpm : { 0, 100 })
            {
                auto w = make ({ gear, (double) bpm });
                int t = 0;
                for (int T : { 1, 5, 20, 120 })
                {
                    run (*w, (T - t) * 60); t = T;
                    dumpState ("walk.g" + std::to_string (gear) + ".b" + std::to_string (bpm) + ".t" + std::to_string (T), *w);
                }
            }
        // Gear 3: TURBO free, and the tempo lock with TURBO; gear 2 with TURBO.
        {
            const std::pair<const char*, Opts> walks[] { { "g3turbo", { 3, 0, true } }, { "g3b100turbo", { 3, 100, true } }, { "g2turbo", { 2, 0, true } } };
            for (const auto& wk : walks)
            {
                auto w = make (wk.second);
                int t = 0;
                for (int T : { 1, 5, 20 }) { run (*w, (T - t) * 60); t = T; dumpState (std::string ("walk.") + wk.first + ".t" + std::to_string (T), *w); }
            }
        }
        // A shift from 1 to 2 at 100 bpm, TURBO on, then gear 3 free.
        {
            auto w = make ({ 1, 100 });
            run (*w, 600); w->setGear (2); run (*w, 60); dumpState ("shift.t11", *w); run (*w, 240); dumpState ("shift.t15", *w);
            w->setTurbo (true); run (*w, 300); dumpState ("shift.t20", *w);
            w->setGear (3); w->setTurbo (false); w->setBpm (0); run (*w, 300); dumpState ("shift.t25", *w);
        }
        // The bass: a held low note, then a jump in the level (an onset), then attacks by hand.
        {
            auto w = make ({ 2 });
            run (*w, 180, [] (WalkerWorld& x) { x.setBass (0.85); }); dumpState ("bass.t3", *w);
            run (*w, 12, [] (WalkerWorld& x) { x.setBass (0); x.setLevel (0.9); });
            run (*w, 18, [] (WalkerWorld& x) { x.setLevel (0); }); dumpState ("bass.t3_5", *w, true);
            run (*w, 90); w->hit (0.8); dumpState ("bass.hit", *w, true);
            run (*w, 30); w->hit(); w->hit (0.3); run (*w, 60); dumpState ("bass.t7", *w, true);
        }
        // A drag across the glass: 5 s free, right at 0.8 for 6 s, left at full for 3 s, let go for 10 s.
        {
            auto w = make ({ 2 });
            run (*w, 300); dumpState ("steer.t5", *w);
            run (*w, 360, [] (WalkerWorld& x) { x.setSteer (0.8); }); dumpState ("steer.t11", *w);
            run (*w, 180, [] (WalkerWorld& x) { x.setSteer (-1); }); dumpState ("steer.t14", *w);
            run (*w, 600, [] (WalkerWorld& x) { x.setSteer (0); }); dumpState ("steer.t24", *w);
        }
        // Through dense clusters: a trace every half second.
        {
            const std::pair<const char*, Opts> dense[] { { "dense.a", { 2, 0, false, true, 2952, 6757 } },
                                                          { "dense.b", { 2, 0, false, true, 7243, 5583 } },
                                                          { "dense.c", { 1, 0, false, true, 2952, 6757 } } };
            for (const auto& dn : dense)
            {
                auto w = make (dn.second);
                for (int i = 1; i <= 120; ++i)
                {
                    run (*w, 30);
                    const auto& s = w->state();
                    out (std::string (dn.first) + ".trace" + std::to_string (i), 'd', { s.x, s.z, s.heading, s.brake, s.squeeze, s.pace });
                }
                dumpState (std::string (dn.first) + ".end", *w);
            }
        }
        // Across the wrap: x past worldPeriod and z below 0, so the legs, bursts, prints and dead ends shift with it.
        {
            auto w = make ({ 2, 0, false, true, 8186, 6 });
            run (*w, 240, [] (WalkerWorld& x) { x.setSteer (0.6); }); dumpState ("wrap.t4", *w, true);
            run (*w, 360, [] (WalkerWorld& x) { x.setSteer (0.6); }); dumpState ("wrap.t10", *w, true);
            run (*w, 600, [] (WalkerWorld& x) { x.setSteer (0); }); dumpState ("wrap.t20", *w, true);
        }
        // Before build: no ground, every grove dense.
        {
            WalkerWorld w;
            WalkerWorld::Mushroom m, q;
            std::vector<double> v { w.groundH (2033, 2125) };
            if (w.mushroomAt (1, 254, 265, m))
                v.insert (v.end(), { 1, m.x, m.z, m.size, (double) m.variant, (double) m.hue, m.phase, m.rot, m.lean, m.glow, m.stalkR, m.clear, m.capR, m.capY });
            else
                v.push_back (0);
            v.push_back (w.mushroomAt (0, 1016, 1062, q) ? 1 : 0);
            out ("prebuild", 'd', v);
        }
    }

    /** The scenario an entry belongs to, for the summary: walk.g2.b100, dense.a, mesh, ... */
    std::string groupOf (const std::string& label)
    {
        std::vector<std::string> parts;
        std::stringstream ss (label);
        for (std::string p; std::getline (ss, p, '.');) parts.push_back (p);
        std::string g = parts[0];
        if ((g == "walk" || g == "dense") && parts.size() > 1) g += "." + parts[1];
        if (parts[0] == "walk" && parts.size() > 2 && parts[2][0] == 'b') g += "." + parts[2];
        return g;
    }

    struct Group { double maxDiff = 0; int entries = 0, exactBad = 0, tolBad = 0; };
}

int main (int argc, char** argv)
{
    bool dump = false;
    const char* path = WALKER_FIXTURE;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--dump") == 0) dump = true;
        else path = argv[i];
    }
    makeAll();
    if (dump)
    {
        for (const auto& e : made)
        {
            std::printf ("%s %c %zu", e.label.c_str(), e.kind, e.v.size());
            for (double v : e.v) std::printf (" %.17g", v);
            std::printf ("\n");
        }
        return 0;
    }

    std::ifstream in (path);
    if (! in) { std::printf ("FAIL cannot read the fixture %s\n", path); return 1; }
    std::map<std::string, const Entry*> mine;
    for (const auto& e : made) mine[e.label] = &e;
    std::map<std::string, Group> groups;
    std::vector<std::string> order;
    int failures = 0, seen = 0, shown = 0;
    for (std::string line; std::getline (in, line);)
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls (line);
        std::string label, kind;
        size_t n = 0;
        ls >> label >> kind >> n;
        std::vector<double> want;
        for (std::string tok; want.size() < n && ls >> tok;) want.push_back (std::strtod (tok.c_str(), nullptr));
        ++seen;
        const std::string g = groupOf (label);
        if (groups.find (g) == groups.end()) order.push_back (g);
        Group& G = groups[g];
        ++G.entries;
        const auto it = mine.find (label);
        if (it == mine.end() || it->second->kind != kind[0] || it->second->v.size() != want.size())
        {
            ++failures; ++G.exactBad;
            std::printf ("FAIL %s: missing here, or another kind or count\n", label.c_str());
            continue;
        }
        const auto& got = it->second->v;
        for (size_t i = 0; i < want.size(); ++i)
        {
            const double a = got[i], b = want[i];
            const bool same = a == b || (a != a && b != b);
            if (kind[0] == 'd')
            {
                const double diff = same ? 0 : std::abs (a - b);
                if (diff > G.maxDiff || diff != diff) G.maxDiff = diff != diff ? INFINITY : diff;
                if (! (diff <= 1e-6)) { ++G.tolBad; ++failures; if (shown++ < 20) std::printf ("FAIL %s[%zu] got %.17g want %.17g\n", label.c_str(), i, a, b); }
            }
            else if (! same)
            {
                ++G.exactBad; ++failures;
                if (shown++ < 20) std::printf ("FAIL %s[%zu] (%c, exact) got %.17g want %.17g\n", label.c_str(), i, kind[0], a, b);
            }
        }
    }
    if (seen != (int) made.size()) { ++failures; std::printf ("FAIL the fixture has %d entries, this side makes %zu\n", seen, made.size()); }
    for (const auto& g : order)
    {
        const Group& G = groups[g];
        std::printf ("%s %-22s %4d entries, max |diff| %.3g, exact mismatches %d, over 1e-6 %d\n",
                     G.exactBad + G.tolBad ? "FAIL " : "  ok ", g.c_str(), G.entries, G.maxDiff, G.exactBad, G.tolBad);
    }
    std::printf ("%s\n", failures ? "WALKER WORLD: FAILED" : "WALKER WORLD: all match the prototype");
    return failures ? 1 : 0;
}
