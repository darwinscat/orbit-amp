// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for california::CaliforniaWorld — the California world against its prototype, california-gl.js, number
// for number. The fixture (tests/fixtures/california/california-world.txt) is the prototype's own state, dumped by node
// (tests/fixtures/california/dump.mjs): the hashes and half floats, the textures and the road and land maps, the loop
// and its arclength table, the meshes, the floor and the ridges, everything on the loop (the sprites' places, the
// dishes, the pumps, the fences and flags, the signs' boxes, the chalk, the finds lap by lap, the jump list and three
// jumps), the oaks, the poles, the shadow map as made and driven through, the drive in both gears, a gear change, PARK
// and the pull-away, two attacks, the pendant over 10 s, two minutes of the saucers, the swooping stretch, the yellow
// submarine — and the files the world reads (tests/fixtures/california/files: copies of the prototype's, their sizes
// and hashes in the fixture). This makes the same entries the same way and compares them: a double within 1e-9 of its
// size, an f32 value, a hash or a count exactly. Then what the prototype has no dump of: settleCamera, quiet.
//
// `california_world_test [fixture [files dir]]` compares. Returns non-zero on any failure. JUCE-free, like the code under test.

#include "california/view/CaliforniaWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef CALIFORNIA_FIXTURE
 #define CALIFORNIA_FIXTURE "tests/fixtures/california/california-world.txt"
#endif
#ifndef CALIFORNIA_ART
 #define CALIFORNIA_ART "resources/california"
#endif
#ifndef CALIFORNIA_FILES
 #define CALIFORNIA_FILES "tests/fixtures/california/files"
#endif

using orbitamp::california::CaliforniaWorld;

namespace
{
    using W = CaliforniaWorld;
    struct Entry { char kind = 'd'; std::vector<double> v; };
    std::map<std::string, Entry> mine;
    int failures = 0;
    std::string filesDir = CALIFORNIA_FILES;
    std::string artDir = CALIFORNIA_ART;

    void E (const std::string& label, char kind, std::vector<double> v) { mine[label] = { kind, std::move (v) }; }
    void check (bool ok, const char* what) { if (! ok) { ++failures; std::printf ("FAIL %s\n", what); } }

    uint32_t fnvBytes (const uint8_t* b, size_t n)
    {
        uint32_t h = 0x811c9dc5u;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x01000193u; }
        return h;
    }
    template <typename T>
    double fnv (const std::vector<T>& a) { return fnvBytes (reinterpret_cast<const uint8_t*> (a.data()), a.size() * sizeof (T)); }
    double fnvS (const std::string& s) { return fnvBytes (reinterpret_cast<const uint8_t*> (s.data()), s.size()); }
    template <typename T>
    std::vector<double> sample (const std::vector<T>& a, int n)
    {
        std::vector<double> o;
        for (int k = 0; k < n; ++k) o.push_back ((double) a[(size_t) std::floor ((double) k * (double) a.size() / n)]);
        return o;
    }
    void add (std::vector<double>& v, std::initializer_list<double> xs) { v.insert (v.end(), xs); }
    template <size_t N> void add (std::vector<double>& v, const std::array<double, N>& xs) { v.insert (v.end(), xs.begin(), xs.end()); }
    std::string bytesOf (const std::string& rel)
    {
        std::ifstream in (filesDir + "/" + rel, std::ios::binary);
        if (! in) { std::printf ("FAIL no file %s/%s\n", filesDir.c_str(), rel.c_str()); ++failures; return {}; }
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    // The atlases' sizes as the view reads them from the pictures (the fixture's atlas.sizes says these are they).
    constexpr int signAtlasW = 2048, signAtlasH = 1024, chalkAtlasW = 2048, chalkAtlasH = 1024, flagAtlasW = 576, flagAtlasH = 256;
    constexpr double pitchOffset = -3.0 * W::pi / 180;        // cabin.html's

    std::unique_ptr<W> fresh (int gear = 1, bool parked = false, double at = -1, int lap = -1)
    {
        auto w = std::make_unique<W>();
        w->build();
        w->setGear (gear); w->setParked (parked); w->settle();
        w->setPitchOffset (pitchOffset);
        if (at >= 0) w->setAt (at, lap);
        return w;
    }
    void run (W& w, double seconds) { for (int i = 0, n = (int) std::round (seconds * 60); i < n; ++i) w.step (1.0 / 60); }
    void st (const std::string& label, const W& w)
    {
        const auto& s = w.state();
        std::vector<double> o { s.sr, (double) s.lap, s.lat, s.latV, s.latA, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.aFwd, s.aLeft,
                                s.dt, s.pitchOffset, s.park, s.parkT, s.hitT, s.hitS, (double) s.hits, s.startleT, s.startleS,
                                s.pend.th, s.pend.thV, s.pend.ph, s.pend.phV };
        add (o, s.pollenFlow); add (o, s.fluffFlow);
        add (o, { s.findFade, w.speedOf() });
        E (label + ".state", 'd', o);
    }
    void more (const std::string& label, const W& w)
    {
        const auto& s = w.state();
        std::vector<double> v;
        for (const auto& b : s.bursts) add (v, { b.t, b.x, b.z, b.fx, b.fz, b.level, (double) b.seed });
        E (label + ".bursts", 'd', v);
        const auto& f = s.flock;
        E (label + ".flock", 'd', { f.on ? 1.0 : 0.0, (double) f.n, f.x, f.y, f.z, f.fx, f.fz, f.t, f.vis });
        v.clear();
        for (const auto& g : s.glass) add (v, { (double) g.st, g.t, g.x, g.y, g.k, g.open, g.a, g.rot, g.fx, g.fy, g.fk });
        E (label + ".glass", 'd', v);
    }
    void bfl (const std::string& label, const W& w)
    {
        std::vector<double> v;
        for (const auto& b : w.butterflies()) { add (v, b.rel); add (v, { b.yaw, b.flap, b.pitch, b.vis, b.dist, b.phase }); }
        E (label + ".bfly", 'd', v);
    }
    void sau (const std::string& label, const W& w)
    {
        std::vector<double> v { w.saucerSeen() };
        for (const auto& u : w.saucers())
            add (v, { u.x, u.y, u.z, u.vx, u.vy, u.vz, u.up, u.front ? 1.0 : 0.0, u.ph, u.loopR, u.loopW, (double) (int) u.mode, u.mt, u.lean[0], u.lean[1],
                      u.yaw, u.beam, u.beamOn ? 1.0 : 0.0, u.beamT, u.anchorT, u.as, u.lat, (double) (int) u.kind, u.follow });
        E (label + ".saucers", 'd', v);
    }
    void sub (const std::string& label, const W& w)
    {
        const auto& b = w.sub();
        E (label + ".sub", 'd', { b.on ? 1.0 : 0.0, b.t, b.as, (double) b.from, b.up, b.yb, b.x, b.y, b.z, b.yaw, b.lean[0], b.lean[1], b.fade, (double) b.pass, (double) b.k, b.lift });
    }
    void all (const std::string& label, const W& w) { st (label, w); more (label, w); bfl (label, w); sau (label, w); sub (label, w); }
    void box (std::vector<double>& v, const W::Box& b) { add (v, { b.x, b.y, b.z, b.yaw, b.hx, b.hy, b.hz, (double) b.part }); add (v, b.rect); add (v, { b.vis, b.flutter }); }
    std::vector<double> boxes (const std::vector<W::Box>& bs, bool count)
    {
        std::vector<double> v;
        if (count) v.push_back ((double) bs.size());
        for (const auto& b : bs) box (v, b);
        return v;
    }
    std::vector<double> finds (W& w)
    {
        std::vector<double> v;
        for (const auto& f : w.findsNow())
        {
            if (f.kind == 0) { add (v, { (double) f.kind, f.x, f.z, f.y }); add (v, f.u); add (v, f.v); add (v, { f.R, f.k }); }
            else add (v, { (double) f.kind, f.x, f.z, f.y, f.nx, f.nz });
        }
        return v;
    }

    /** The prototype's scenarios, as dump.mjs runs them. */
    void makeEntries()
    {
        std::vector<double> v;
        for (const uint32_t x : { 0u, 1u, 12345u, 0x7fffffffu, 0x80000000u, 4294967295u }) v.push_back (W::ih (x));
        E ("hash.ih", 'i', v); v.clear();
        for (const auto& [a, b, c] : std::vector<std::array<int, 3>> { { 0, 0, 0 }, { 1, 2, 3 }, { -5, 7, 101 }, { 255, 255, 707 }, { -1, -1, 1010 }, { 3299, 17, 51 } })
            v.push_back (W::hcell (a, b, c));
        E ("hash.hcell", 'f', v); v.clear();
        for (const auto& [a, b] : std::vector<std::array<int, 2>> { { 1, 1 }, { 7, 92 }, { 120, 16 }, { 0, 0 }, { 241, 77 }, { 45, 801 } }) v.push_back (W::hs (a, b));
        E ("hash.hs", 'f', v); v.clear();
        for (const double x : { 0.0, 1e-9, -3e-6, 6.1e-5, 6.2e-5, 0.1, 0.33333, -0.7, 1.0, 1.0004, 2047.6, 70000.0, -1e6, 2.9802322387695312e-08, -0.49999 })
            v.push_back (W::toHalf (x));
        E ("half", 'd', v); v.clear();

        const auto height = W::makeHeight();
        const auto detail = W::makeDetail();
        const auto noise = W::makeNoise();
        E ("tex.height", 'i', { (double) height.size(), fnv (height) });
        E ("tex.height.sample", 'f', sample (height, 64));
        v = { (double) detail.size(), fnv (detail) }; for (const double x : sample (detail, 32)) v.push_back (x);
        E ("tex.detail", 'i', v);
        v = { (double) noise.size(), fnv (noise) }; for (const double x : sample (noise, 32)) v.push_back (x);
        E ("tex.noise", 'i', v);

        const double L = W::loopLength();
        E ("loop.len", 'd', { L, W::poleDs() });
        E ("loop.n", 'i', { (double) W::loopTable().size(), (double) W::polesN(), (double) W::dashesN() });
        v.clear();
        for (const int i : { 0, 1, 2, 100, 1000, 2048, 4096, 6000, 8191, 8192 }) v.push_back (W::loopTable()[(size_t) i]);
        E ("loop.table", 'd', v);
        const std::vector<double> ths { 0, 0.5, 1, 2, 3.14, 4, 5.5, 6.2, -1, 10 };
        v.clear(); for (const double th : ths) { const auto r = W::loopR (th); add (v, { r.R, r.dR }); }
        E ("loop.R", 'd', v);
        v.clear(); for (const double th : ths) add (v, W::loopPoint (th));
        E ("loop.point", 'd', v);
        v.clear(); for (const double th : ths) { add (v, W::tangentAt (th)); add (v, W::rightAt (th)); }
        E ("loop.tangent", 'd', v);
        v.clear(); for (const double th : ths) v.push_back (W::sOfTheta (th));
        E ("loop.s", 'd', v);
        const std::vector<double> ss { 0, 1, 10.5, 1000, 2000, 2560, 4300, 5000, 5146, -20, 6000, 12345.6 };
        v.clear(); for (const double s : ss) v.push_back (W::thetaAt (s));
        E ("loop.theta", 'd', v);
        v.clear();
        const std::array<double, 6> ds { 0, 1.65, -8.6, 4.3, 200, -343 };
        for (size_t k = 0; k < ss.size(); ++k) add (v, W::roadPointAt (ss[k], ds[k % 6]));
        E ("loop.road", 'd', v);
        v.clear(); add (v, W::sunDir()); add (v, W::wind());
        E ("sun", 'd', v);
        v.clear();
        for (const auto& [h, p] : std::vector<std::array<double, 2>> { { 0.3, -0.05 }, { -2.1, 0.2 } }) { const auto b = W::cameraBasis (h, p); add (v, b.r); add (v, b.u); add (v, b.f); }
        E ("camera", 'd', v);
        v.clear();
        for (const auto& [l, k] : std::vector<std::pair<W::V2, double>> { { { 0, 0 }, 1 }, { { 0.1, -0.2 }, 1 }, { { 0.3, 0.05 }, 4 }, { { 0, 0 }, 10 } })
            for (const float x : W::tiltOf (l, k)) v.push_back (x);
        E ("tilt", 'f', v);

        {
            const auto ico = W::icosphere (2);
            std::vector<double> V; for (const auto& p : ico.V) add (V, p);
            std::vector<uint32_t> F; for (const auto& f : ico.F) for (const int i : f) F.push_back ((uint32_t) i);
            E ("mesh.ico2", 'i', { (double) ico.V.size(), fnv (V), (double) ico.F.size(), fnv (F) });
            const auto mesh = [] (const W::Mesh& m) { return std::vector<double> { (double) m.v.size(), fnv (m.v), (double) m.idx.size(), fnv (m.idx) }; };
            for (int d = 0; d < 3; ++d) E ("mesh.oak" + std::to_string (d), 'i', mesh (W::oakMesh (d)));
            E ("mesh.pole", 'i', mesh (W::poleMesh()));
            E ("mesh.pumpTower", 'i', mesh (W::pumpTowerMesh()));
            E ("mesh.pumpFan", 'i', mesh (W::pumpFanMesh()));
            const auto flora = W::floraMeshes();
            for (size_t k = 0; k < flora.size(); ++k) E ("mesh.flora" + std::to_string (k), 'i', mesh (flora[k]));
            E ("mesh.butterfly", 'i', mesh (W::butterflyMesh()));
        }
        {
            const int w = 40, h = 30;
            std::vector<uint8_t> px ((size_t) (w * h * 4), 255);
            for (int y = 3; y < 12; ++y) for (int x = 5; x < 20; ++x) px[(size_t) ((y * w + x) * 4 + 3)] = 0;
            px[(size_t) ((25 * w + 33) * 4 + 3)] = 254;
            v.clear(); add (v, *W::glassBounds (px.data(), w, h, 2));
            const std::vector<uint8_t> opaque (16, 255);
            const auto none = W::glassBounds (opaque.data(), 2, 2, 2);
            if (none) add (v, *none); else v.push_back (-1);
            E ("glassBounds", 'd', v);
        }

        // The files.
        const std::vector<std::string> files { "finds/dish/sprites.txt", "finds/tower/sprites.txt", "finds/signs/signs.txt", "finds/chalk/chalk.txt", "finds/flags/flags.txt",
                                               "cabin-van/pendant_a.txt", "cabin-van/pendant_b.txt", "cabin-van/pendant_c.txt",
                                               "finds/van3d/van.txt", "finds/guitars3d/guitars.txt", "finds/barn3d/barn.txt", "finds/barn3d/barn.mesh" };
        for (const auto& rel : files) { const auto b = bytesOf (rel); E ("file." + rel, 'i', { (double) b.size(), (double) fnvBytes (reinterpret_cast<const uint8_t*> (b.data()), b.size()) }); }
        E ("atlas.sizes", 'i', { signAtlasW, signAtlasH, chalkAtlasW, chalkAtlasH, flagAtlasW, flagAtlasH });
        const auto table = [] (const W::SpriteTable& t) { return std::vector<double> { t.cell[0], t.cell[1], t.scale, (double) t.grid[0], (double) t.grid[1], t.anchor[0], t.anchor[1], (double) t.count }; };
        for (const char* what : { "dish", "tower" }) E (std::string ("table.") + what, 'd', table (W::parseSpriteTable (bytesOf (std::string ("finds/") + what + "/sprites.txt"))));
        E ("table.none", 'd', table (W::parseSpriteTable ("")));
        for (const char* c : { "a", "b", "c" })
        {
            const auto b = W::parsePendantBox (bytesOf (std::string ("cabin-van/pendant_") + c + ".txt"));
            v.clear(); if (b) { add (v, b->box); add (v, b->pivot); }
            E (std::string ("pendant.") + c, 'd', v);
        }
        E ("pendant.none", 'i', { W::parsePendantBox ("box 1 2 3 4\n") ? 1.0 : 0.0 });
        const auto signTable = W::parseSigns (bytesOf ("finds/signs/signs.txt"));
        v.clear(); for (const auto& sg : signTable) { add (v, sg.cell); add (v, sg.size); v.push_back (sg.bottom); }
        E ("signs.table", 'd', v);
        v.clear(); for (const auto& sg : signTable) add (v, { fnvS (sg.name), fnvS (sg.shape), fnvS (sg.post) });
        E ("signs.names", 'i', v);
        v.clear(); for (const auto& c : W::parseFlagCells (bytesOf ("finds/flags/flags.txt"), flagAtlasW, flagAtlasH)) add (v, c);
        E ("flags.cells", 'd', v);
        const auto chalkTable = W::parseChalk (bytesOf ("finds/chalk/chalk.txt"), chalkAtlasW, chalkAtlasH);
        v.clear(); for (const auto& c : chalkTable) { v.push_back (c.optional ? 1 : 0); add (v, c.cell); add (v, c.size); }
        E ("chalk.table", 'd', v);
        v.clear(); for (const auto& c : chalkTable) v.push_back (fnvS (c.name));
        E ("chalk.names", 'i', v);
        for (const char* rel : { "finds/van3d/van.txt", "finds/guitars3d/guitars.txt", "finds/barn3d/barn.txt" })
        {
            const auto m = W::parseMeshInfo (bytesOf (rel));
            std::string key = rel;
            key = key.substr (key.rfind ('/') + 1); key = key.substr (0, key.size() - 4);
            v = { (double) m.textures.size() }; for (const auto& t : m.textures) v.push_back (fnvS (t));
            E ("meshinfo." + key + ".textures", 'i', v);
            v = { (double) m.censor.size() }; for (const auto& c : m.censor) { v.push_back (fnvS (c.name)); add (v, c.c); add (v, c.s); }
            E ("meshinfo." + key + ".censor", 'd', v);
        }
        {
            const auto raw = bytesOf ("finds/barn3d/barn.mesh");
            const auto* data = reinterpret_cast<const uint8_t*> (raw.data());
            const auto m = W::parseCbMesh (data, raw.size());
            const auto mp = W::parseCbMesh (data, raw.size(), { { -8.0, 0.0 }, { 0.0, 8.0 }, { 20.0, 30.0 } });
            if (m && mp)
            {
                E ("cbmesh.barn", 'i', { (double) m->vertices, (double) m->indices, fnv (m->vertexBytes), (double) m->idx.size(), fnv (m->idx) });
                v = { (double) mp->idx.size(), fnv (mp->idx) }; for (const auto& p : mp->parts) add (v, { (double) p.first, (double) p.count });
                E ("cbmesh.barn.parts", 'i', v);
                v.clear(); for (const auto& p : mp->parts) v.push_back (p.cx);
                E ("cbmesh.barn.cx", 'd', v);
            }
            std::vector<uint8_t> bad (data, data + std::min<size_t> (4096, raw.size()));
            const auto badOk = ! bad.empty() && (bad[0] = 0x58, W::parseCbMesh (bad.data(), bad.size()).has_value());
            E ("cbmesh.refused", 'i', { badOk ? 1.0 : 0.0, W::parseCbMesh (data, std::min<size_t> (4096, raw.size())) ? 1.0 : 0.0,
                                         W::parseCbMesh (data, std::min<size_t> (12, raw.size())) ? 1.0 : 0.0 });
        }

        // The world as made.
        {
            const auto w = fresh();
            const std::vector<std::array<double, 2>> pts { { 46960, 46200 }, { 45000, 46000 }, { 46200, 46200 }, { 44700, 47700 }, { 47500.125, 45555.9 },
                                                           { 0, 0 }, { 92399.5, 17.25 }, { -3.5, 46812.75 }, { 100000, -5000 } };
            v.clear(); for (const auto& [x, z] : pts) v.push_back (w->floorHeight (x, z));
            E ("floor.h", 'd', v);
            v.clear(); for (const auto& [x, z] : pts) v.push_back (w->sunAt (x, z));
            E ("floor.sun", 'd', v);
            v.clear();
            for (const auto& [a, k, h] : std::vector<std::array<double, 3>> { { 0, 0, 2 }, { 0.13, 1, 40 }, { 0.5, 2, -10 }, { 0.77, 0, 20 }, { 1.3, 1, 0 }, { -0.2, 2, 60 } })
                v.push_back (w->ridgeTan (a, (int) k, h));
            E ("ridge", 'd', v);
            E ("tex.road", 'i', { (double) w->roadMap().size(), fnv (w->roadMap()) });
            E ("tex.road.sample", 'f', sample (w->roadMap(), 64));
            E ("tex.land", 'i', { (double) w->landMap().size(), fnv (w->landMap()) });
            E ("tex.land.sample", 'f', sample (w->landMap(), 64));
            E ("tex.height.world", 'i', { fnv (w->height()), fnv (w->detail()), fnv (w->noise()) });
            st ("start", *w); more ("start", *w); sau ("start", *w); sub ("start", *w);
            v.clear();
            for (const auto& b : w->butterflies()) add (v, { b.ax, b.az, b.alt, b.scale, b.A, b.w1, b.w2, b.p1, b.p2, b.p3, b.p4, b.rate, b.glideW, b.phase });
            E ("start.bfly.init", 'd', v);
            v.clear();
            std::vector<double> names;
            for (const auto& p : w->spritePlaces())
            {
                add (v, { p.at, p.x, p.z, p.y, p.nx, p.nz, p.vis, p.scale, p.pull, p.seen, p.hill ? 1.0 : 0.0, p.paint ? 1.0 : 0.0, p.noBox ? 1.0 : 0.0, p.noJump ? 1.0 : 0.0 });
                add (v, p.hasPart ? p.part : W::V2 { 0, 0 });
                add (names, { fnvS (p.name), fnvS (p.label), fnvS (p.mesh), fnvS (p.group) });
            }
            E ("places.sprites", 'd', v);
            E ("places.sprites.names", 'i', names);
            v.clear(); for (const auto& d : w->dishes()) add (v, { d.x, d.z, d.y, d.nx, d.nz });
            E ("places.dishes", 'd', v);
            v.clear(); for (const auto& p : w->pumps()) add (v, { p.x, p.z, p.y, p.vis, p.phase });
            E ("places.pumps", 'd', v);
            E ("places.flags", 'd', boxes (w->flags(), false));
            E ("places.boxes", 'd', boxes (w->signBoxes(), true));
            v.clear(); for (const auto& b : w->signBoxes()) v.push_back (fnvS (b.ph));
            E ("places.boxes.ph", 'i', v);
            E ("finds.lap0", 'd', finds (*w));
            E ("finds.next", 'd', { w->nextFindAt() });
            const auto& oaks = w->listOaks();
            E ("oaks.count", 'i', { (double) oaks.size() });
            v.clear(); for (size_t i = 0; i < std::min<size_t> (80, oaks.size()); ++i) { const auto& o = oaks[i]; add (v, { o.x, o.z, o.y, o.R, o.s, o.yaw, o.tint, o.vis }); }
            E ("oaks.first", 'd', v);
            std::array<double, 8> sums {};
            for (const auto& o : oaks) { const std::array<double, 8> f { o.x, o.z, o.y, o.R, o.s, o.yaw, o.tint, o.vis }; for (size_t k = 0; k < 8; ++k) sums[k] += f[k]; }
            v.clear(); add (v, sums);
            E ("oaks.sums", 'd', v);
            v.clear(); for (const auto& p : w->listPoles()) { add (v, { (double) p.k, p.x, p.z, p.y, p.h, p.yaw, p.lean }); add (v, p.side); }
            E ("poles", 'd', v);
            v.clear(); for (const int k : { -3, 0, 122, 500 }) { const auto p = w->poleAt (k); add (v, { (double) p.k, p.x, p.z, p.y, p.h }); }
            E ("poles.far", 'd', v);
            const auto c = w->updateShadow();
            v = { fnv (w->shadowPixels()), (double) w->shadowUpdates().size() };
            for (const auto& u : w->shadowUpdates()) for (const int x : u) v.push_back (x);
            v.push_back ((*w->shadowCorner())[0]); v.push_back ((*w->shadowCorner())[1]);
            E ("shadow.start", 'i', v);
            E ("shadow.start.at", 'd', { c[0], c[1], w->lightAtVan() });
            w->clearShadowUpdates();
            std::vector<double> lights;
            for (int i = 0; i < 1200; ++i) { w->step (1.0 / 60); w->updateShadow(); if (i % 120 == 0) lights.push_back (w->lightAtVan()); }
            std::vector<int32_t> ups; for (const auto& u : w->shadowUpdates()) for (const int x : u) ups.push_back (x);
            E ("shadow.drive", 'i', { fnv (w->shadowPixels()), (double) w->shadowUpdates().size(), fnv (ups), (double) (*w->shadowCorner())[0], (double) (*w->shadowCorner())[1] });
            E ("shadow.drive.light", 'd', lights);
            E ("oaks.drive", 'i', { (double) w->listOaks().size() });
            w->buildSigns (signTable, signAtlasW, signAtlasH);
            E ("signs.boxes", 'd', boxes (w->signBoxes(), true));
            w->updateShadow();
            E ("shadow.signs", 'i', { fnv (w->shadowPixels()), (double) (*w->shadowCorner())[0], (double) (*w->shadowCorner())[1] });
            w->placeChalk (chalkTable, true);
            v.clear(); names.clear();
            for (const auto& ch : w->chalks()) { add (v, { ch.x, ch.z, ch.tx, ch.tz, ch.hw, ch.hl }); add (v, ch.cell); add (v, { ch.at, ch.d }); names.push_back (fnvS (ch.name)); }
            E ("chalk.places", 'd', v);
            E ("chalk.places.names", 'i', names);
            const auto pl = w->places();
            v.clear(); names.clear();
            for (const auto& q : pl) { add (v, { q.at, q.seen, (double) q.lap, (double) q.kind }); add (names, { fnvS (q.group), fnvS (q.name) }); }
            E ("places.list", 'd', v);
            E ("places.list.names", 'i', names);
            w->spriteCame ("tower"); w->spriteCame ("barn");
            const auto& B = w->signBoxes();
            v = { (double) B.size() };
            for (size_t i = B.size() >= 6 ? B.size() - 6 : 0; i < B.size(); ++i) box (v, B[i]);
            E ("places.came", 'd', v);
            v.clear(); for (const auto& b : B) v.push_back (fnvS (b.ph));
            E ("places.came.ph", 'i', v);
            w->placeChalk (chalkTable, false);
            v.clear(); for (const auto& ch : w->chalks()) add (v, { ch.x, ch.z, ch.at, ch.d });
            E ("chalk.nowink", 'd', v);
            for (const size_t k : { (size_t) 0, (size_t) 5, pl.size() - 1 })
            {
                const auto j = w->jumpTo (pl[k]);
                const auto label = "jump" + std::to_string (k);
                E (label, 'd', { j.s, (double) j.lap });
                st (label, *w); sau (label, *w);
            }
        }
        {
            auto w = fresh (1, false, 0, 1); E ("finds.lap1", 'd', finds (*w));
            w->setAt (0, 2); E ("finds.lap2", 'd', finds (*w));
            w->setAt (L - 40, 0); w->setGear (2); run (*w, 5); st ("lapwrap", *w); E ("finds.lapwrap", 'd', finds (*w)); E ("finds.lapwrap.next", 'd', { w->nextFindAt() });
        }

        // The drive.
        { auto w = fresh(); run (*w, 14); w->step (0); all ("cabin14", *w); }
        for (const int gear : { 1, 2 })
        {
            auto w = fresh (gear);
            for (const int t : { 5, 30 })
            {
                run (*w, t - w->state().time);
                const auto label = "gear" + std::to_string (gear) + ".t" + std::to_string (t);
                st (label, *w); bfl (label, *w);
            }
        }
        { auto w = fresh(); run (*w, 3); w->setGear (2); run (*w, 3); st ("shift", *w); w->setGear (1); run (*w, 1); st ("shift.down", *w); }
        {
            auto w = fresh (1, true);
            run (*w, 14); all ("park14", *w);
            w->setParked (false); run (*w, 2); st ("pull2", *w); more ("pull2", *w);
            run (*w, 6); st ("pull8", *w); more ("pull8", *w);
        }
        {
            auto w = fresh (2);
            run (*w, 4); w->setParked (true); run (*w, 6); st ("stop6", *w); more ("stop6", *w);
            w->setParked (false); run (*w, 1); w->setGear (1); run (*w, 1); st ("pullshift", *w);
        }
        {
            auto w = fresh();
            run (*w, 14); w->hit (0.8); run (*w, 0.2); st ("hit", *w); more ("hit", *w); bfl ("hit", *w);
            w->hit (1.5); run (*w, 0.1); st ("hit2", *w); more ("hit2", *w);
            run (*w, 6); st ("hit.after", *w);
        }
        {
            auto w = fresh (2, false, 1500);
            v.clear();
            for (int k = 0; k < 20; ++k) { run (*w, 0.5); const auto& p = w->state().pend; add (v, { p.th, p.thV, p.ph, p.phV }); }
            E ("pendant.swing", 'd', v);
        }
        {
            auto w = fresh();
            for (int k = 1; k <= 13; ++k) { run (*w, 10); sau ("long" + std::to_string (k * 10), *w); }
            st ("long", *w); more ("long", *w);
        }
        {
            auto w = fresh (2, false, 1020.0 - 500);
            for (int k = 1; k <= 15; ++k) { run (*w, 2); sau ("swoop" + std::to_string (k), *w); }
        }
        { auto w = fresh (1, true); run (*w, 40); sau ("park40", *w); }
        {
            auto w = fresh (1, false, 2560.0 - 10);
            for (const int t : { 3, 12, 20, 31 }) { run (*w, t - w->state().time); sub ("sub" + std::to_string (t), *w); }
            st ("sub", *w);
        }
    }

    void compareWithFixture (const char* path)
    {
        std::ifstream in (path);
        if (! in) { std::printf ("FAIL no fixture at %s\n", path); ++failures; return; }
        int entries = 0;
        double worst = 0;
        std::string worstLabel;
        for (std::string line; std::getline (in, line);)
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream words (line);
            std::string label, kind;
            size_t n = 0;
            words >> label >> kind >> n;
            std::vector<double> want (n);
            for (auto& x : want) { std::string w; words >> w; x = std::strtod (w.c_str(), nullptr); }
            ++entries;
            const auto it = mine.find (label);
            if (it == mine.end()) { std::printf ("FAIL %s: not made here\n", label.c_str()); ++failures; continue; }
            const auto& got = it->second.v;
            if (got.size() != want.size()) { std::printf ("FAIL %s: %zu values, the prototype's %zu\n", label.c_str(), got.size(), want.size()); ++failures; continue; }
            int bad = 0;
            for (size_t i = 0; i < n; ++i)
            {
                const double a = got[i], b = want[i];
                bool ok;
                if (kind == "i")      ok = a == b;
                else if (kind == "f") ok = (float) a == (float) b;
                else
                {
                    const double dev = std::abs (a - b) / std::max (1.0, std::abs (b));
                    if (dev > worst) { worst = dev; worstLabel = label + "[" + std::to_string (i) + "]"; }
                    ok = dev <= 1e-9 || (a != a && b != b);
                }
                if (! ok && bad++ < 3)
                    std::printf ("FAIL %s[%zu]: %.17g, the prototype's %.17g\n", label.c_str(), i, a, b);
            }
            if (bad > 0) { ++failures; std::printf ("     %s: %d of %zu differ\n", label.c_str(), bad, n); }
        }
        check (entries == (int) mine.size(), "every entry made here is in the fixture");
        std::printf ("%d entries compared; the largest double's deviation %.3g (%s)\n", entries, worst, worstLabel.c_str());
    }

    /** What the prototype has no dump of: the camera settled at once, an attack called off. */
    void ownChecks()
    {
        // settleCamera: the camera where the first steps leave it settled, here at once — the van parked, standing
        // still, the camera eased over ten seconds to where settleCamera puts it.
        auto settled = fresh (1, true, 3000);
        settled->settleCamera();
        auto eased = fresh (1, true, 3000);
        run (*eased, 10);
        check (std::abs (settled->state().camY - eased->state().camY) < 1e-9 && std::abs (settled->state().pitch - eased->state().pitch) < 1e-9,
               "settleCamera puts the camera where standing still eases it");
        check (settled->state().camY > settled->floorHeight (settled->state().x, settled->state().z) + 1.8, "a settled camera is at the eye's height over the road");
        // startAt: the same as building and then putting the van there.
        auto a = std::make_unique<W>(); a->startAt (4321, 2); a->build();
        auto b = std::make_unique<W>(); b->build(); b->setAt (4321, 2);
        check (a->state().x == b->state().x && a->state().lap == 2 && a->saucers()[1].as == b->saucers()[1].as, "startAt is setAt at the build");
        // quiet: an attack called off leaves the silent world's light, and the drive on as it was.
        auto loud = fresh(), hush = fresh();
        run (*loud, 2); run (*hush, 2);
        loud->hit (1); loud->quiet();
        check (loud->state().hitS == 0 && loud->state().startleS == 0 && loud->state().hitT >= 1e4 && loud->state().bursts[0].level == 0, "quiet calls an attack off");
        check (loud->state().x == hush->state().x && loud->state().speed == hush->state().speed, "quiet leaves the drive alone");
        // the shadow map's toroidal pieces: a strip across the texture's edge is two pieces, the whole one one
        const auto p = W::shadowPieces ({ 250, 10, 258, 266 });
        check (p.size() == 2 && p[0] == std::array<int, 4> { 250, 0, 6, 256 } && p[1] == std::array<int, 4> { 0, 0, 2, 256 }, "a strip over the edge is two pieces");
        check (W::shadowPieces ({ -8, -300, 248, -44 }).size() == 1, "the whole texture is one piece");
        // the parsers refuse what is not theirs, and the setSteer of a self-driving van does nothing
        check (W::parseSigns ("sign a board 1 2 3 4 5 6 7\n").empty() && W::parseChalk ("# chalk 1 2 3 4 5 6\n", 2, 2).empty(), "short and commented lines are no entries");
        auto steer = fresh(), straight = fresh();
        steer->setSteer (1); run (*steer, 2); run (*straight, 2);
        check (steer->state().heading == straight->state().heading, "setSteer is a no-op");
    }
}

namespace
{
    /** THE FILES THE GPU'S HALF READS AT RUN TIME (CaliforniaGl::makeArt, as the prototype's view loads them), all in
        resources/california as the installer lays them out (CaliforniaArt.h): the cabin's, every sprite's atlas and
        table, the signs', the flags', the chalk's, and every mesh with its table and each texture its table names —
        the prototype's .png read as .webp — each table one its parser takes. */
    void shippedFiles()
    {
        namespace fs = std::filesystem;
        const auto has = [] (const std::string& rel)
        {
            std::error_code e;
            const auto f = fs::path (artDir) / rel;
            return fs::is_regular_file (f, e) && fs::file_size (f, e) > 16;
        };
        const auto read = [] (const std::string& rel)
        {
            std::ifstream in (fs::path (artDir) / rel, std::ios::binary);
            return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        };
        const auto webp = [] (std::string name) { return name.size() > 4 && name.compare (name.size() - 4, 4, ".png") == 0 ? name.substr (0, name.size() - 4) + ".webp" : name; };
        const auto need = [&] (bool ok, const std::string& what) { if (! ok) { ++failures; std::printf ("FAIL shipped: %s\n", what.c_str()); } };
        for (const char* f : { "dashboard.webp", "dashboard_pendant.webp", "label.webp" })
            need (has (f), f);
        need (W::parsePendantBox (read ("dashboard_pendant.txt")).has_value(), "dashboard_pendant.txt: a box and a pivot");
        W w;
        w.build();
        std::vector<std::string> sprites { "dish" }, meshes { "van3d/van", "saucer3d/saucer", "yellowsub3d/yellowsub" };
        for (const auto& sp : w.spritePlaces())
            if (sp.mesh.empty()) sprites.push_back (sp.name);
            else if (std::find (meshes.begin(), meshes.end(), sp.mesh) == meshes.end()) meshes.push_back (sp.mesh);
        for (const auto& s : sprites)
            need (has ("finds/" + s + "/atlas.webp") && W::parseSpriteTable (read ("finds/" + s + "/sprites.txt")).scale > 0, "the sprite " + s);
        need (has ("finds/signs/atlas.webp") && ! W::parseSigns (read ("finds/signs/signs.txt")).empty(), "the signs");
        need (has ("finds/flags/atlas.webp") && ! W::parseFlagCells (read ("finds/flags/flags.txt"), 576, 256).empty(), "the flags");
        need (has ("finds/chalk/atlas.webp") && ! W::parseChalk (read ("finds/chalk/chalk.txt"), 2048, 1024).empty(), "the chalk");
        for (const auto& m : meshes)
        {
            const auto bytes = read ("finds/" + m + ".mesh");
            need (W::parseCbMesh (reinterpret_cast<const uint8_t*> (bytes.data()), bytes.size()).has_value(), "the mesh " + m);
            const auto dir = m.substr (0, m.find_last_of ('/') + 1);
            for (const auto& tex : W::parseMeshInfo (read ("finds/" + m + ".txt")).textures)
                if (! tex.empty())
                    need (has ("finds/" + dir + webp (tex)), "the texture " + dir + tex);
        }
    }
}

int main (int argc, char** argv)
{
    if (argc > 3) artDir = argv[3];
    shippedFiles();
    if (argc > 2) filesDir = argv[2];
    makeEntries();
    compareWithFixture (argc > 1 ? argv[1] : CALIFORNIA_FIXTURE);
    ownChecks();
    if (failures == 0)
        std::printf ("california_world_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
