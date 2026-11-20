// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for io::IoWorld — Io's world (the SansAmp RBI bass's) against its prototype, io-world.js and the CPU's
// part of io-gl.js, number for number. The fixture (tests/fixtures/io/io-world.txt) is the prototype's own state, dumped
// by node (tests/fixtures/io/dump.mjs): the hashes and the half floats, the textures and the road map, the loop and its
// table, the Sun from the two knobs and the old path, the surface's light, the height mirror, the paterae, the plumes,
// the fountains, the flows, the sites, the stations, the hardware and the rocks, the jump list, the meshes, the drive
// in both gears, PARK in and out, the knobs swept and eased, an attack, the auto-hits and the activity's history, the
// dosimeter and its needle, the readers on the finds' files, and nine frames drawn by the prototype's own view (every
// uniform the world feeds it, the hardware's and the rocks' instances, the plumes' bodies, the emitters). This makes
// the same entries the same way and compares them: a double within 1e-9 of its size, an f32 value, a hash or a count
// exactly. Then what the port adds: the camera settled before the first step, an attack called off.
//
// `io_world_test [fixture] [files dir]`; the files dir holds the copies of the finds' small files the readers are run on
// (tests/fixtures/io/files). Returns non-zero on any failure. JUCE-free, like the code under test.

#include "io/view/IoWorld.h"
#ifdef IO_ART
 #include <webp/decode.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef IO_FIXTURE
 #define IO_FIXTURE "tests/fixtures/io/io-world.txt"
#endif
#ifndef IO_FILES
 #define IO_FILES "tests/fixtures/io/files"
#endif

using orbitamp::io::IoWorld;
namespace fs = std::filesystem;

namespace
{
    using W = IoWorld;
    using V3 = W::V3;
    struct Entry { char kind = 'd'; std::vector<double> v; };
    std::map<std::string, Entry> mine, fixture;
    int failures = 0;

    void E (const std::string& label, char kind, std::vector<double> v) { mine[label] = { kind, std::move (v) }; }
    void check (bool ok, const char* what) { if (! ok) { ++failures; std::printf ("FAIL %s\n", what); } }

    uint32_t fnvBytes (const uint8_t* b, size_t n) { uint32_t h = 0x811c9dc5u; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x01000193u; } return h; }
    template <typename T>
    double fnv (const std::vector<T>& a) { return a.empty() ? 0x811c9dc5u : fnvBytes (reinterpret_cast<const uint8_t*> (a.data()), a.size() * sizeof (T)); }
    double fnvS (const std::string& s) { return fnvBytes (reinterpret_cast<const uint8_t*> (s.data()), s.size()); }
    template <typename T>
    std::vector<double> sample (const std::vector<T>& a, int n)
    {
        std::vector<double> o;
        for (int k = 0; k < n; ++k) o.push_back ((double) a[(size_t) std::floor ((double) k * (double) a.size() / n)]);
        return o;
    }
    std::vector<float> asF32 (const std::vector<double>& a) { std::vector<float> o; for (const double v : a) o.push_back ((float) v); return o; }
    template <typename A> void addAll (std::vector<double>& o, const A& a) { for (const auto x : a) o.push_back ((double) x); }
    std::vector<uint8_t> bytesOf (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }

    void readFixture (const char* path)
    {
        std::ifstream in (path);
        if (! in) { std::printf ("FAIL no fixture at %s\n", path); ++failures; return; }
        for (std::string line; std::getline (in, line);)
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream words (line);
            std::string label, kind;
            size_t n = 0;
            words >> label >> kind >> n;
            Entry e;
            e.kind = kind.empty() ? 'd' : kind[0];
            e.v.resize (n);
            for (auto& x : e.v) { std::string w; words >> w; x = std::strtod (w.c_str(), nullptr); }
            fixture[label] = e;
        }
    }

    std::unique_ptr<W> fresh (int gear = 1, bool parked = false, std::vector<std::pair<W::Knob, double>> knobs = {})
    {
        auto w = std::make_unique<W>();
        w->build();
        w->setGear (gear); w->setParked (parked);
        for (const auto& [k, v] : knobs) w->setKnob (k, v);
        w->settle();
        return w;
    }
    void run (W& w, double seconds) { const int n = (int) std::lround (seconds * 60); for (int i = 0; i < n; ++i) w.step (1.0 / 60); }
    std::vector<double> sunV (const W::Sun& S)
    {
        std::vector<double> o { S.sep, S.vis };
        addAll (o, S.hue);
        for (const double v : { S.light, S.flat, S.graze, S.ring, S.depth, S.u, S.eclAir, S.eclExpo, S.eclDark }) o.push_back (v);
        addAll (o, S.dir);
        for (const double v : { S.el, S.az, S.eA, S.gate, S.night, S.dark, S.airglow, S.expo, S.cabin }) o.push_back (v);
        return o;
    }
    void st (const std::string& label, const W& w)
    {
        const auto& s = w.state();
        E (label + ".state", 'd', { s.sr, (double) s.lap, s.lat, s.latV, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.park, (double) s.gear,
                                    (double) s.parked, s.drive, s.blend, s.presence, s.sunK, s.dark, s.jupLit, s.cabinDay, s.act, s.actE, s.actHold, (double) s.autoHits,
                                    s.autoNext, (double) s.autoN, s.hitT, s.hitS, (double) s.hits, s.dose, s.doseV, s.doseE, s.doseJ, (double) s.doseN });
        E (label + ".sun", 'd', sunV (s.sun));
        std::vector<double> p;
        for (const auto& q : s.puffs) for (const double v : { q.x, q.y, q.z, q.t, q.k, q.R, (double) q.seed }) p.push_back (v);
        E (label + ".puffs", 'd', p);
        std::vector<double> h { fnv (w.history()) };
        addAll (h, sample (w.history(), 16));
        E (label + ".hist", 'i', h);
    }
    void structV (std::vector<double>& o, const W::Structure& q)
    {
        for (const double v : { (double) q.variant, q.x, q.z, q.y, q.yaw, q.k, q.lean[0], q.lean[1], (double) q.onFlow, (double) q.site, q.ci, q.cj, q.seed }) o.push_back (v);
    }
    std::string num (double v) { char b[32]; std::snprintf (b, sizeof b, "%.15g", v); return b; }     // as JS prints the labels' short numbers
    std::string roundLabel (double v) { return std::to_string ((long long) std::floor (v + 0.5)); }

    //==========================================================================
    void worldEntries (const W& w0)
    {
        E ("hash.ih", 'i', { (double) W::ih (0), (double) W::ih (1), (double) W::ih (12345), (double) W::ih (0x7fffffffu), (double) W::ih (0x80000000u), (double) W::ih (4294967295u) });
        E ("hash.hcell", 'f', { W::hcell (0, 0, 0), W::hcell (1, 2, 3), W::hcell (-5, 7, 101), W::hcell (255, 255, 707), W::hcell (-1, -1, 1010), W::hcell (350.5, 352, 602) });
        E ("hash.hs", 'f', { W::hs (1, 1), W::hs (7, 92), W::hs (120, 16), W::hs (0, 0), W::hs (241, 77), W::hs (-1003, 625) });
        {
            std::vector<double> h;
            for (const double v : { 0.0, 1e-6, -3e-5, 6.2e-5, 0.1, 0.3333, 1.0, 2.5, -17.77, 1000.3, 65519.0, 70000.0, -1e6 }) h.push_back (W::toHalf (v));
            E ("half", 'd', h);
        }
        E ("tex.base", 'i', { (double) w0.base().size(), fnv (w0.base()) }); E ("tex.base.sample", 'f', sample (w0.base(), 64));
        const auto tex = [] (const std::string& label, const std::vector<uint8_t>& a) { std::vector<double> o { (double) a.size(), fnv (a) }; addAll (o, sample (a, 32)); E (label, 'i', o); };
        tex ("tex.detail", w0.detail()); tex ("tex.noise", w0.noise()); tex ("tex.cells", w0.cells()); tex ("tex.feat", w0.featureMap());
        E ("tex.road", 'i', { (double) w0.roadMap().size(), fnv (w0.roadMap()) }); E ("tex.road.sample", 'f', sample (w0.roadMap(), 64));
        { const auto a = W::makeDetail(); E ("tex.detail.made", 'i', { (double) a.size(), fnv (a) }); }
        { const auto a = W::makeNoise(); E ("tex.noise.made", 'i', { (double) a.size(), fnv (a) }); }
        { const auto a = W::makeCells(); E ("tex.cells.made", 'i', { (double) a.size(), fnv (a) }); }

        E ("loop.len", 'd', { W::loopLength() });
        // V8 14'S SIN AND COS, A STATED EXCEPTION: node 26's V8 (14.6) computes Math.sin and Math.cos with its glibc-derived
        // code, no longer fdlibm; the family's Js (WalkerWorld.cpp) is fdlibm. They part in the last bit of ~0.5 % of
        // arguments (3 of the loop's 8193 points, a flow's width), which every double here takes within 4e-16 of its
        // size, but which a hash of the raw doubles sees. So the two tables of doubles (the loop's arclengths, the flows'
        // points) are hashed rounded to f32 — a tolerance of half an f32's ulp, ~6e-8 of the value — and sampled densely
        // as doubles at 1e-9 (512 of the arclengths, every fifth point of every flow).
        E ("loop.table", 'd', sample (W::loopTable(), 512));
        E ("loop.table.hash", 'i', { (double) W::loopTable().size(), fnv (asF32 (W::loopTable())) });
        {
            std::vector<double> th, pt, tg;
            for (const double s : { 0.0, 1.0, 160.0, 777.7, 1500.0, 2600.25, 4100.0, 5100.0, -30.0, 6000.0 })
            {
                th.push_back (W::thetaAt (s));
                addAll (pt, W::roadPointAt (s, 0)); addAll (pt, W::roadPointAt (s, 7.5)); addAll (pt, W::roadPointAt (s, -40));
                addAll (tg, W::tangentAt (W::thetaAt (s))); addAll (tg, W::rightAt (W::thetaAt (s))); tg.push_back (W::headingAt (s));
            }
            E ("loop.theta", 'd', th); E ("loop.point", 'd', pt); E ("loop.tangent", 'd', tg);
            std::vector<double> nr;
            for (const auto& [x, z] : std::vector<std::pair<double, double>> { { 24520, 24680 }, { 25300, 24680 }, { 23950, 25110 }, { 24000, 24000 } })
            { const auto n = W::nearestOnLoop (x, z); nr.push_back (n.at); nr.push_back (n.d); }
            E ("loop.near", 'd', nr);
            std::vector<double> bs;
            for (const auto& [h, p] : std::vector<std::pair<double, double>> { { 0, 0 }, { 1.2, -0.05 }, { -2.9, 0.1 } })
            { const auto b = W::cameraBasis (h, p); addAll (bs, b.r); addAll (bs, b.u); addAll (bs, b.f); }
            E ("loop.basis", 'd', bs);
        }
        {
            const auto& F = W::jupFrame();
            std::vector<double> j { W::jupRho(), W::jupEl(), W::jupAz() };
            addAll (j, F.J); addAll (j, F.R); addAll (j, F.U); addAll (j, F.axis); j.push_back (W::sunRho);
            E ("sky.jupiter", 'd', j);
            std::vector<double> m;
            for (const auto& q : W::moons()) { addAll (m, q.dir); m.push_back (q.rho); addAll (m, q.alb); }
            E ("sky.moons", 'd', m);
        }
        const std::vector<double> sweep { 0, 0.02, 0.058, 0.06, 0.1, 0.2, 0.3, 0.35, 0.42, 0.45, 0.5, 0.55, 0.6, 0.8, 1 };
        { std::vector<double> a, b; for (const double x : sweep) { a.push_back (W::sunHeightAt (x)); b.push_back (W::sunSepAt (x)); } E ("sky.height", 'd', a); E ("sky.sep", 'd', b); }
        for (const double d : { 0.0, 0.3, 0.45, 0.5, 0.8, 1.0 })
            for (const double b : { 0.0, 0.03, 0.058, 0.1, 0.2, 0.5, 1.0 })
            {
                const auto S = W::sunSolve (d, b);
                E ("sky.solve." + num (d) + "." + num (b), 'd', sunV (S));
                const auto R = W::ringLevel (S);
                const auto L = W::surfaceLight (S);
                std::vector<double> o { R.level, R.t };
                addAll (o, W::jupShineOf (S)); addAll (o, L.dir); addAll (o, L.key); o.push_back (L.k); addAll (o, L.adir); addAll (o, L.amb);
                E ("light." + num (d) + "." + num (b), 'd', o);
            }
        {
            std::vector<double> o;
            for (const double x : { 0.0, 0.2, 0.4, 0.41, 0.6, 0.9, 0.95, 1.0 }) { o.push_back (W::pathSepAt (x)); addAll (o, sunV (W::pathSun (x))); }
            E ("sky.path", 'd', o);
            std::vector<double> l;
            for (const auto& [s, la] : std::vector<std::pair<double, double>> { { W::jupRho() + 0.1, 0.6 }, { W::jupRho() + 0.02, 0.6 }, { W::jupRho() - 0.01, 0.8 } })
            { const auto v = sunV (W::sunLook (s, la)); l.insert (l.end(), v.begin(), v.begin() + 15); }
            E ("sky.look", 'd', l);
        }

        {
            std::vector<std::pair<double, double>> pts { { 24520, 24680 }, { 25280, 24700 }, { 23800, 24300 }, { 24110.5, 25402.25 }, { 26010, 23020 }, { 22900, 26500 }, { 24800, 23900 }, { 25000, 25000 } };
            for (const auto& c : w0.calderas()) { pts.push_back ({ c.x + 3, c.z - 2 }); pts.push_back ({ c.x + c.R + c.W * 0.5, c.z }); }
            for (const double k : { 0.0, 500.0, 1200.0, 2050.0, 3333.0, 4800.0 }) { const auto p = W::roadPointAt (k, 0); pts.push_back ({ p[0], p[1] }); pts.push_back ({ p[0] + 9, p[1] - 4 }); }
            std::vector<double> f, t, r, ft;
            for (const auto& [x, z] : pts) { f.push_back (w0.floorHeight (x, z)); t.push_back (w0.terrainH (x, z, 50)); addAll (r, w0.roadTap (x, z)); addAll (ft, w0.featTap (x, z)); }
            E ("height.floor", 'd', f); E ("height.terrain", 'd', t); E ("height.road", 'd', r); E ("height.feat", 'd', ft);
        }
        {
            std::vector<double> c;
            for (const auto& q : w0.calderas())
                for (const double v : { (double) q.i, q.at, q.d, q.R, q.W, q.D, q.rim, q.lake, q.th, (double) q.big, (double) q.near, (double) q.pit, q.x, q.z, q.baseY, q.out, q.lakeR }) c.push_back (v);
            E ("calderas", 'd', c);
            std::vector<double> p;
            for (const auto& q : w0.plumes())
                for (const double v : { (double) q.i, q.x, q.z, q.y, (double) q.row, q.ring, q.near.at, q.near.d, q.H, q.th, (double) q.pele, q.base, q.view ? *q.view : -1 }) p.push_back (v);
            E ("plumes", 'd', p);
            std::vector<double> f;
            for (const auto& q : w0.fountains()) for (const double v : { (double) q.i, (double) q.cald, q.x, q.z, q.y, (double) q.row, q.H, q.th, q.toward }) f.push_back (v);
            E ("fountains", 'd', f);
            std::vector<double> fl { (double) w0.flows().size() }, fr, fp, fs5;
            for (const auto& q : w0.flows())
            {
                std::vector<double> flat;
                for (const auto& pt : q.pts) addAll (flat, pt);
                fl.push_back ((double) q.pts.size()); fl.push_back (fnv (asF32 (flat))); fl.push_back (q.fissure ? 1 : 0);    // as f32: see "V8 14's sin and cos"
                for (size_t i = 2; i < q.pts.size(); i += 5) addAll (fs5, q.pts[i]);
                fr.push_back (q.fresh); addAll (fp, q.pts.front()); addAll (fp, q.pts.back());
            }
            E ("flows", 'i', fl); E ("flows.sample", 'd', fs5); E ("flows.fresh", 'd', fr); E ("flows.pts", 'd', fp);
            std::vector<double> si, sn, so, son;
            for (const auto& g : w0.sites()) { si.push_back (g.at); si.push_back ((double) g.items.size()); for (const auto& q : g.items) structV (si, q); sn.push_back (fnvS (g.name)); }
            for (const auto& g : w0.stations()) { for (const double v : { g.at, g.seen, g.r, (double) g.items.size() }) so.push_back (v); for (const auto& q : g.items) structV (so, q); son.push_back (fnvS (g.name)); }
            E ("sites", 'd', si); E ("sites.names", 'i', sn); E ("stations", 'd', so); E ("stations.names", 'i', son);
            const auto P = w0.places();
            std::vector<double> at, seen, names, groups;
            for (const auto& q : P) { at.push_back (q.at); seen.push_back (q.seen); names.push_back (fnvS (q.name)); groups.push_back (fnvS (q.group)); }
            E ("places.at", 'd', at); E ("places.seen", 'd', seen); E ("places.names", 'i', names); E ("places.groups", 'i', groups);
        }
        {
            const auto rp = W::roadPointAt (1300, 0);
            for (const auto& [x, z] : std::vector<std::pair<double, double>> { { 24520 + 760, 24680 }, { rp[0], rp[1] }, { 23700, 25200 } })
            {
                const auto list = w0.structuresNear (x, z);
                std::vector<double> o { (double) list.size() };
                for (const auto& q : list) structV (o, q);
                E ("structs.near." + roundLabel (x) + "." + roundLabel (z), 'd', o);
            }
            std::vector<double> o;
            for (const auto& [i, j] : std::vector<std::pair<double, double>> { { 349, 352 }, { 356, 340 }, { -3, 7 }, { 1000, 352 }, { 341, 361 } })
            { const auto q = w0.structureAt (i, j); if (q) { o.push_back (1); structV (o, *q); } else o.push_back (0); }
            E ("structs.at", 'd', o);
            for (const auto& [s, fx, fz] : std::vector<std::array<double, 3>> { { 0, 0, -1 }, { 870, 0.6, 0.8 }, { 3100, -1, 0 } })
            {
                const auto p = W::roadPointAt (s, 1.9);
                const auto list = w0.rocksNear (p[0], p[1], fx, fz, 85);
                std::vector<double> r { (double) list.size() };
                for (const auto& q : list) for (const double v : { q.x, q.z, q.y, q.yaw, q.sx, q.sy, q.sz, q.tint }) r.push_back (v);
                E ("rocks." + num (s), 'd', r);
            }
            std::vector<double> a;
            for (const double b : { 0.0, 0.1, 0.18, 0.3, 0.55, 0.7, 0.9, 1.0 })
            {
                for (const auto& p : w0.plumes()) a.push_back (W::plumeAct (p, b));
                for (const auto& f : w0.fountains()) a.push_back (W::fountainAct (f, b));
                for (const auto& c : w0.calderas()) a.push_back (W::lakeAct (c, b));
            }
            E ("act.curves", 'd', a);
        }
        {
            std::vector<double> m;
            for (const auto& v : W::structureMeshes()) { m.push_back ((double) v.size()); m.push_back (fnv (v)); }
            E ("mesh.struct", 'i', m);
            std::vector<double> r;
            for (const bool f : { true, false }) { const auto v = W::rockMesh (f); r.push_back ((double) v.size()); r.push_back (fnv (v)); }
            E ("mesh.rock", 'i', r);
            const int gw = 16, gh = 8;
            std::vector<uint8_t> rgba ((size_t) gw * gh * 4, 255);
            for (const auto& [x, y] : std::vector<std::pair<int, int>> { { 5, 2 }, { 9, 3 }, { 7, 6 } }) rgba[(size_t) ((y * gw + x) * 4 + 3)] = 100;
            const auto g = W::glassBounds (rgba.data(), gw, gh, 2);
            const std::vector<uint8_t> opaque (16, 255);
            std::vector<double> o;
            if (g) addAll (o, *g);
            o.push_back (W::glassBounds (opaque.data(), 2, 2, 1) ? 0 : 1);
            E ("glass", 'd', o);
        }
    }

    void driveEntries()
    {
        using K = W::Knob;
        { auto w = fresh(); st ("drive1.t0", *w); run (*w, 12); st ("drive1.t12", *w); run (*w, 30); st ("drive1.t42", *w); }
        { auto w = fresh (2); run (*w, 20); st ("drive2.t20", *w); }
        {
            auto w = fresh (2, true);
            run (*w, 8); st ("park.in", *w);
            w->setParked (false); run (*w, 2); st ("park.pull2", *w); run (*w, 5); st ("park.out", *w);
            w->setParked (true); run (*w, 3); st ("park.again", *w);
            w->setParked (false); run (*w, 1); w->setGear (1); run (*w, 3); st ("park.regear", *w);
        }
        {
            auto w = fresh();
            w->setKnob (K::drive, 0.5); run (*w, 0.5); st ("knobs.drive05", *w); run (*w, 3); st ("knobs.drive35", *w);
            w->setKnob (K::blend, 0); run (*w, 1); st ("knobs.blend1", *w); run (*w, 5); st ("knobs.eclipse", *w);
            w->setKnob (K::presence, 1); run (*w, 2); st ("knobs.presence", *w);
            w->setKnob (K::drive, 1); w->setKnob (K::blend, 0.2); run (*w, 6); st ("knobs.night", *w);
        }
        { auto w = fresh (1, false, { { K::drive, 0.8 }, { K::blend, 0.058 }, { K::presence, 0.6 } }); st ("knobs.settled", *w); run (*w, 1); st ("knobs.settled1", *w); }
        { auto w = fresh(); run (*w, 2); w->hit (0.8); run (*w, 0.3); st ("hit.03", *w); run (*w, 2); st ("hit.23", *w); }
        { auto w = fresh(); w->setAuto (true); run (*w, 15); st ("auto.15", *w); w->setAuto (false); run (*w, 12); st ("auto.off12", *w); }
        { auto w = fresh(); w->setActivity (0.7); run (*w, 4); st ("activity.held", *w); w->setActivity (0.3, false); run (*w, 6); st ("activity.free", *w); }
        {
            auto w = fresh();
            const auto P = w->places();
            for (const size_t k : { (size_t) 0, (size_t) 5, (size_t) 13, (size_t) 21, P.size() - 1 })
            {
                const auto j = w->jumpTo (P[k]);
                E ("jump." + std::to_string (k), 'd', { j.s, (double) j.lap });
                run (*w, 1); st ("jump." + std::to_string (k), *w);
            }
        }
        std::vector<double> n;
        for (const double d : { -0.2, 0.0, 0.13, 0.5, 0.9, 1.0, 1.3 }) addAll (n, W::needle (d));
        E ("needle", 'd', n);
    }

    //==========================================================================
    /** A frame as the prototype's view hands it to the GPU: the uniforms the world feeds and the uploads. */
    void frameEntries (const std::string& label, W& w)
    {
        const auto F = w.frame();
        const auto& G = F.ground;
        const std::string p = "frame." + label + ".";
        const auto d = [] (std::initializer_list<double> v) { return std::vector<double> (v); };
        const auto v3 = [] (const V3& a) { return std::vector<double> { a[0], a[1], a[2] }; };
        const auto fl = [] (const auto& a) { std::vector<double> o; addAll (o, a); return o; };
        const auto& J = W::jupFrame();
        E (p + "ground.uSun", 'd', v3 (G.dir)); E (p + "ground.uSunCol", 'd', v3 (G.key)); E (p + "ground.uSunK", 'd', d ({ G.k }));
        E (p + "ground.uJup", 'd', v3 (G.adir)); E (p + "ground.uJupShine", 'd', v3 (G.amb));
        E (p + "ground.uDark", 'd', d ({ F.dark })); E (p + "ground.uTerm", 'd', fl (F.term)); E (p + "ground.uAirglow", 'd', v3 (F.airglow));
        E (p + "ground.uJupSunCol", 'd', v3 (F.jupSunCol)); E (p + "ground.uFlat", 'd', d ({ F.flat })); E (p + "ground.uAir", 'd', d ({ F.air }));
        E (p + "ground.uPL[0]", 'f', fl (F.pl)); E (p + "ground.uPLC[0]", 'f', fl (F.plc)); E (p + "ground.uPLN", 'i', d ({ (double) F.plN }));
        E (p + "ground.uSL[0]", 'f', fl (F.sl)); E (p + "ground.uSLN", 'i', d ({ (double) F.slN })); E (p + "ground.uSLC", 'd', v3 (F.slc)); E (p + "ground.uSLBox", 'd', fl (F.slBox));
        E (p + "ground.uHitT", 'd', d ({ F.hitAge, w.state().hitS }));
        E (p + "ground.uCaldA[0]", 'f', fl (F.caldA)); E (p + "ground.uCaldB[0]", 'f', fl (F.caldB)); E (p + "ground.uCaldC[0]", 'f', fl (F.caldC));
        E (p + "ground.uCaldN", 'i', d ({ (double) F.caldN }));
        E (p + "ground.uCamCell", 'd', d ({ F.ccx, F.ccz })); E (p + "ground.uCamFrac", 'd', fl (F.camFrac)); E (p + "ground.uCamRel", 'd', fl (F.camRel));
        E (p + "ground.uOff0", 'd', fl (F.off0)); E (p + "ground.uOff1", 'd', fl (F.off1)); E (p + "ground.uOff2", 'd', fl (F.off2));
        E (p + "ground.uTime", 'd', d ({ F.time })); E (p + "ground.uLamps", 'd', d ({ F.camFrac[0], F.camFrac[1], F.fwd[0], F.fwd[1] }));
        E (p + "ground.uLoopC", 'd', d ({ W::loopC[0] - F.ccx, W::loopC[1] - F.ccz }));
        E (p + "ground.uRoadOrg", 'd', d ({ W::roadOrg[0] - F.ccx, W::roadOrg[1] - F.ccz })); E (p + "ground.uFeatOrg", 'd', d ({ W::featOrg[0] - F.ccx, W::featOrg[1] - F.ccz }));
        E (p + "sky.uSun", 'd', v3 (F.sun)); E (p + "sky.uSunCol", 'd', v3 (F.sunCol)); E (p + "sky.uSunK", 'd', d ({ F.sunK })); E (p + "sky.uJup", 'd', v3 (J.J));
        E (p + "sky.uJupShine", 'd', v3 (F.jupShine)); E (p + "sky.uGKey", 'd', v3 (G.dir));
        E (p + "sky.uGKeyCol", 'd', d ({ G.key[0] * G.k, G.key[1] * G.k, G.key[2] * G.k })); E (p + "sky.uGAmb", 'd', v3 (G.amb));
        E (p + "sky.uSunUp", 'd', v3 (F.sunUp)); E (p + "sky.uJupK", 'd', d ({ F.jupK })); E (p + "sky.uAurora", 'd', d ({ F.aurora })); E (p + "sky.uFlux", 'd', d ({ F.flux }));
        E (p + "sky.uFar", 'd', v3 (F.far)); E (p + "sky.uSep", 'd', d ({ F.sep })); E (p + "sky.uJupT", 'd', d ({ F.jupT }));
        E (p + "sky.uRho", 'd', d ({ W::jupRho() })); E (p + "sky.uJupSin", 'd', d ({ std::sin (W::jupRho()) })); E (p + "sky.uJupCos", 'd', d ({ std::cos (W::jupRho()) }));
        E (p + "sky.uMoon[0]", 'f', fl (F.moon)); E (p + "sky.uMoonAlb[0]", 'f', fl (F.moonAlb));
        E (p + "sky.uJupR", 'd', v3 (J.R)); E (p + "sky.uJupU", 'd', v3 (J.U)); E (p + "sky.uJupAxis", 'd', v3 (J.axis));
        E (p + "snow.uExpo", 'd', d ({ F.expo })); E (p + "snow.uTravel", 'd', d ({ F.travel })); E (p + "snow.uSpeed", 'd', d ({ F.speed }));
        E (p + "snow.uSnow", 'd', d ({ F.snow })); E (p + "snow.uSunV", 'd', v3 (F.sunV)); E (p + "snow.uLavaEnv", 'd', v3 (F.lavaEnv));
        std::vector<double> sv;
        for (const auto& b : F.hardware) for (const double v : { (double) b.n, (double) b.nSh, (double) b.data.size(), fnv (b.data) }) sv.push_back (v);
        E (p + "structs", 'i', sv);
        std::vector<double> rv;
        for (const auto& r : F.rocks) { rv.push_back ((double) r.size()); rv.push_back (fnv (r)); }
        E (p + "rocks", 'i', rv);
        std::vector<double> vv { (double) F.volumes.size(), (double) F.volumesInside };
        for (const auto& e : F.volumes) addAll (vv, e);
        E (p + "vols", 'f', vv);
        for (size_t k = 0; k < 3; ++k)
        {
            std::vector<double> ev { (double) F.emitters[k].size() };
            for (const auto& e : F.emitters[k]) addAll (ev, e);
            E (p + "emit" + std::to_string (k), 'f', ev);
        }
    }
    void frames()
    {
        using K = W::Knob;
        {   // by day, Jupiter dead ahead; an attack
            auto w = fresh();
            w->startAt (87, 0); run (*w, 1);
            frameEntries ("day1", *w); frameEntries ("day2", *w);
            w->hit (1); run (*w, 0.3); frameEntries ("hit", *w);
            st ("frame.hit", *w);
        }
        {   // at night, the volcanoes awake, by the lava lake nearest the road
            auto w = fresh (1, false, { { K::drive, 0.8 }, { K::blend, 1 }, { K::presence, 0.7 } });
            w->setActivity (1);
            double at = 0;
            for (const auto& q : w->places()) if (q.name == "the road's closest pass to a lava lake") at = q.at;
            w->startAt (at - 120, 0); run (*w, 2);
            frameEntries ("night1", *w); frameEntries ("night2", *w);
            w->hit (0.9); run (*w, 0.2); frameEntries ("night3", *w);
            st ("frame.night", *w);
        }
        {   // a low Sun over the skyline, then the eclipse
            auto w = fresh (2, false, { { K::drive, 0.45 }, { K::blend, 0.3 } });
            w->startAt (87, 0); run (*w, 1);
            frameEntries ("dusk1", *w); frameEntries ("dusk2", *w);
            w->setKnob (K::drive, 0.47); w->setKnob (K::blend, 0.0); w->settle(); run (*w, 0.5);
            frameEntries ("eclipse", *w);
            st ("frame.eclipse", *w);
        }
    }

    //==========================================================================
    /** THE FINDS' FILES: the readers on the copies in the files dir (their sizes and hashes the prototype's). */
    void findsEntries (const fs::path& files)
    {
        const fs::path finds = files / "finds";
        std::vector<std::string> folders;
        if (fs::is_directory (finds)) for (const auto& e : fs::directory_iterator (finds)) if (e.is_directory()) folders.push_back (e.path().filename().string());
        std::sort (folders.begin(), folders.end());
        std::vector<double> roles;
        for (const auto& d : folders) roles.push_back (W::roleOf (d));
        for (const char* d : { "Mast3d", "bus3d", "stationary" }) roles.push_back (W::roleOf (d));
        E ("finds.roles", 'i', roles);
        for (const auto& d : folders)
        {
            const std::string name = d.substr (0, d.size() - 2);
            const auto b = bytesOf (finds / d / (name + ".txt"));
            const std::string t (b.begin(), b.end());
            const auto c = W::creditOf (t);
            const auto pr = W::roleOf (d) == 7 ? W::pitRadiusOf (t) : std::optional<double> {};
            E ("finds." + d + ".txt", 'i', { (double) b.size(), fnv (b), c ? 1.0 : 0.0, c ? fnvS (*c) : 0.0, pr ? 1.0 : 0.0 });
            if (pr) E ("finds." + d + ".pit", 'd', { *pr });
        }
        {
            const auto b = bytesOf (finds / "dish3d" / "dish.mesh");
            std::vector<double> o { (double) b.size(), fnv (b) };
            for (const size_t n : { b.size(), b.size() - 1, (size_t) 12 })
            { const auto m = W::readMesh (b.data(), std::min (n, b.size())); for (const double v : { (double) m.ok, (double) m.nv, (double) m.ni }) o.push_back (v); }
            E ("finds.dish3d.mesh", 'i', o);
        }
        std::vector<double> cr, pi;
        for (const char* t : { "credit: none: our own model", "credit:  Jane Doe, CC-BY 4.0 \nmore", "credit:None of ours", "nothing here", "Credit: nonesuch",
                               "pit radius 13.0", "a pit at 5 m, radius: 7.25", "radius=.5", "pit 1.2.3" })
        {
            const auto c = W::creditOf (t);
            cr.push_back (c ? fnvS (*c) : 0);
            const auto p = W::pitRadiusOf (t);
            pi.push_back (! p ? -1 : *p != *p ? -2 : *p);
        }
        E ("finds.credit", 'i', cr); E ("finds.pitOf", 'd', pi);
    }

    void compare()
    {
        int entries = 0;
        double worst = 0;
        std::string worstLabel;
        for (const auto& [label, e] : fixture)
        {
            const auto it = mine.find (label);
            if (it == mine.end()) { std::printf ("FAIL %s: not made here\n", label.c_str()); ++failures; continue; }
            ++entries;
            const auto& got = it->second.v;
            const auto& wantV = e.v;
            if (got.size() != wantV.size()) { std::printf ("FAIL %s: %zu values, the prototype's %zu\n", label.c_str(), got.size(), wantV.size()); ++failures; continue; }
            int bad = 0;
            for (size_t i = 0; i < wantV.size(); ++i)
            {
                const double a = got[i], b = wantV[i];
                bool ok;
                if (e.kind == 'i')      ok = a == b;
                else if (e.kind == 'f') ok = (float) a == (float) b || (a != a && b != b);
                else
                {
                    const double dev = std::abs (a - b) / std::max (1.0, std::abs (b));
                    if (dev > worst) { worst = dev; worstLabel = label + "[" + std::to_string (i) + "]"; }
                    ok = dev <= 1e-9 || (a != a && b != b) || a == b;
                }
                if (! ok && bad++ < 3)
                    std::printf ("FAIL %s[%zu]: %.17g, the prototype's %.17g\n", label.c_str(), i, a, b);
            }
            if (bad > 0) { ++failures; std::printf ("     %s: %d of %zu differ\n", label.c_str(), bad, wantV.size()); }
        }
        for (const auto& [label, e] : mine) if (fixture.find (label) == fixture.end()) { std::printf ("FAIL %s: not in the fixture\n", label.c_str()); ++failures; }
        std::printf ("%d entries compared; the largest double's deviation %.3g (%s)\n", entries, worst, worstLabel.c_str());
    }

    /** What the port adds to the prototype: the camera settled before the first step, an attack called off. */
    void ownChecks()
    {
        auto a = fresh (2, true), b = fresh (2, true);
        check (a->state().camY == 0, "unstepped, the camera is at 0");
        a->settleCamera();
        check (a->state().camY > 0 && std::abs (a->state().camY - a->floorHeight (a->state().x, a->state().z) - W::eye) < 0.05, "settled, the eye stands over the road");
        run (*b, 3);
        check (std::abs (a->state().camY - b->state().camY) < 0.01 && std::abs (a->state().pitch - b->state().pitch) < 1e-3, "settled, the camera is where the steps take it, parked");
        auto c = fresh();
        c->startAt (87, 0); run (*c, 1);
        c->hit (1); run (*c, 0.3);
        const auto lit = c->frame();
        const double act = c->state().actE;
        c->quiet();
        const auto calm = c->frame();
        check (lit.aurora > 0 && ! lit.emitters[2].empty() && calm.aurora == 0 && calm.emitters[2].empty() && c->state().doseE == 0 && c->state().actE == act,
               "quiet: the aurora, the puffs and the dosimeter's kick are called off, the activity it added stays");
        const double kicked = c->state().dose;
        run (*c, 1.5);
        check (kicked > 0.5 && c->state().dose < 0.3, "quiet: the needle falls back to its rest");
    }
}

// THE ART THE WORLD READS AT RUN TIME (resources/io, as the factory folder lays it out): each find's mesh whole and in a
// variant of its own, the skylight's pit from its table, and the two plates — their window the clear glass the README
// measured (x 122.5-1549.5, y 73-601.5 stage px: 245-3099, 146-1203 on the plates at 2x, widened by 4 stage px, the union of
// both), their alpha exact, and the dosimeter's hub ink as the plates paint it (sRGB about 0.046, 0.035, 0.02 in both).
#ifdef IO_ART
std::string textOf (const fs::path& p) { const auto b = bytesOf (p); return std::string (b.begin(), b.end()); }
void shippedArt()
{
    const fs::path art = IO_ART;
    std::vector<int> roles;
    for (const char* dir : { "mast3d", "dish3d", "module3d", "lander3d", "station3d", "rover3d", "base3d", "tubebase3d", "observatory3d", "plant3d" })
    {
        const std::string name = std::string (dir).substr (0, std::string (dir).size() - 2);
        const auto bytes = bytesOf (art / "finds" / dir / (name + ".mesh"));
        const auto mesh = W::readMesh (bytes.data(), bytes.size());
        check (mesh.ok && mesh.nv > 0 && mesh.ni > 0 && bytes.size() >= 16 + (size_t) mesh.nv * 40 + (size_t) mesh.ni * 4, (std::string ("art: ") + dir + "'s mesh").c_str());
        roles.push_back (W::roleOf (dir));
        check (W::creditOf (textOf (art / "finds" / dir / (name + ".txt"))) == std::nullopt, (std::string ("art: ") + dir + " is our own").c_str());
    }
    for (int v = 0; v < W::numVariants; ++v)
        check (std::count (roles.begin(), roles.end(), v) == 1, "art: every variant has its one mesh");
    const auto pit = W::pitRadiusOf (textOf (art / "finds" / "tubebase3d" / "tubebase.txt"));
    check (pit && std::abs (*pit - 13.0) < 1e-9, "art: the skylight's pit radius is 13 m");

    std::optional<std::array<double, 4>> box;
    for (const char* plate : { "dashboard.webp", "dashboard_dark.webp" })
    {
        const auto bytes = bytesOf (art / plate);
        int w = 0, h = 0;
        uint8_t* px = bytes.empty() ? nullptr : WebPDecodeRGBA (bytes.data(), bytes.size(), &w, &h);
        check (px != nullptr && w == 3344 && h == 1882, (std::string ("art: ") + plate + " is the stage at 2x").c_str());
        if (px == nullptr) continue;
        const auto b = W::glassBounds (px, w, h, (int) std::ceil (4.0 * w / 1672));
        if (b) box = box ? std::array<double, 4> { std::min ((*box)[0], (*b)[0]), std::min ((*box)[1], (*b)[1]), std::max ((*box)[2], (*b)[2]), std::max ((*box)[3], (*b)[3]) } : *b;
        const double k = w / 1672.0;
        const auto ink = W::inkAt (px, w, h, 119.2 * k, 564.9 * k, std::max (1.0, 0.8 * k));
        check (ink && std::abs ((*ink)[0] - 0.046) < 0.012 && std::abs ((*ink)[1] - 0.035) < 0.012 && std::abs ((*ink)[2] - 0.02) < 0.012,
               (std::string ("art: the dosimeter's hub ink on ") + plate).c_str());
        WebPFree (px);
    }
    check (box && std::abs ((*box)[0] * 3344 - 237) < 0.5 && std::abs ((*box)[1] * 1882 - 138) < 0.5 && std::abs ((*box)[2] * 3344 - 3107) < 0.5
           && std::abs ((*box)[3] * 1882 - 1211) < 0.5, "art: the window's box is the clear glass, widened by 4 stage px");
}
#endif

int main (int argc, char** argv)
{
    readFixture (argc > 1 ? argv[1] : IO_FIXTURE);
    const fs::path files = argc > 2 ? argv[2] : IO_FILES;
    auto w0 = fresh();
    worldEntries (*w0);
    driveEntries();
    frames();
    findsEntries (files);
    compare();
    ownChecks();
   #ifdef IO_ART
    shippedArt();
   #endif
    if (failures == 0)
        std::printf ("io_world_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
