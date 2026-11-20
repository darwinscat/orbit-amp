// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for metal::MetalWorld — the metal world (the E530's) against its prototype, metal-world.js and the CPU's
// part of metal-gl.js, number for number. The fixture (tests/fixtures/metal/metal-world.txt) is the prototype's own
// state, dumped by node (tests/fixtures/metal/dump.mjs): the hashes and the half floats, the four textures and the road
// map, the loop and its table, the height mirror, every thing's place (the crags, the props, the banners, the targets,
// the perches, the landmarks, the groves and their guitars, the burning letters, the rare finds lap by lap, the jump
// list), the meshes, the drive in every gear, PARK in and out, the storm, an attack (the flash over the second after it,
// the fires and the ribbons the renderer uploads), the dragons over two minutes (roaming, perched on the citadel and
// taking off, a swoop with its smoke ring, a clang on lap 1, perched ahead in PARK), the ball lightning, the Tesla
// towers' discharge, and the readers of the finds' files. This makes the same entries the same way and compares them: a
// double within 1e-9 of its size, an f32 value, a hash, a count or a string exactly.
//
// `metal_world_test [fixture] [prototype dir]` compares. The prototype's folder (holding finds/, and ../rogue for the
// dragon) is where the readers' entries come from: without it those entries are skipped, said so, and the worlds are
// furnished with what the fixture says the finds bring. Returns non-zero on any failure. JUCE-free, like the code under
// test.

#include "metal/view/MetalFound.h"
#include "metal/view/MetalWorld.h"
#include "MetalShipped.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef METAL_FIXTURE
 #define METAL_FIXTURE "tests/fixtures/metal/metal-world.txt"
#endif
#ifndef METAL_PROTOTYPE
 #define METAL_PROTOTYPE ""
#endif

using orbitamp::metal::MetalWorld;
namespace fs = std::filesystem;

namespace
{
    using W = MetalWorld;
    using V3 = W::V3;
    struct Entry { char kind = 'd'; std::vector<double> v; };
    std::map<std::string, Entry> mine, fixture;
    int failures = 0;

    void E (const std::string& label, char kind, std::vector<double> v) { mine[label] = { kind, std::move (v) }; }
    void check (bool ok, const char* what) { if (! ok) { ++failures; std::printf ("FAIL %s\n", what); } }

    uint32_t fnvBytes (const uint8_t* b, size_t n) { uint32_t h = 0x811c9dc5u; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 0x01000193u; } return h; }
    template <typename T>
    uint32_t fnv (const std::vector<T>& a) { return a.empty() ? 0x811c9dc5u : fnvBytes (reinterpret_cast<const uint8_t*> (a.data()), a.size() * sizeof (T)); }
    double hstr (const std::string& s) { return fnvBytes (reinterpret_cast<const uint8_t*> (s.data()), s.size()); }
    std::vector<double> chars (const std::string& s) { std::vector<double> o; for (const char c : s) o.push_back ((unsigned char) c); return o; }
    std::string unchars (const std::vector<double>& v) { std::string s; for (const double c : v) s.push_back ((char) (int) c); return s; }
    template <typename T>
    std::vector<double> sample (const std::vector<T>& a, int n)
    {
        std::vector<double> o;
        for (int k = 0; k < n; ++k) o.push_back (a.empty() ? 0 : (double) a[(size_t) std::floor ((double) k * (double) a.size() / n)]);
        return o;
    }
    void add (std::vector<double>& o, std::initializer_list<double> v) { o.insert (o.end(), v); }
    template <typename A> void addAll (std::vector<double>& o, const A& a) { for (const auto x : a) o.push_back ((double) x); }

    std::vector<uint8_t> bytesOf (const fs::path& p)
    {
        std::ifstream in (p, std::ios::binary);
        return std::vector<uint8_t> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    }
    std::string textOf (const fs::path& p) { const auto b = bytesOf (p); return std::string (b.begin(), b.end()); }
    std::vector<std::string> listing (const fs::path& d)
    {
        std::vector<std::string> o;
        for (const auto& e : fs::directory_iterator (d)) o.push_back (e.path().filename().string());
        std::sort (o.begin(), o.end());
        return o;
    }
    bool endsWith (const std::string& t, const std::string& e) { return t.size() >= e.size() && t.compare (t.size() - e.size(), e.size(), e) == 0; }

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
    /** An environment variable's value, or `otherwise` when it is not set (MSVC's _dupenv_s: its getenv warns, C4996). */
    std::string environment (const char* name, const char* otherwise)
    {
       #if defined (_MSC_VER)
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s (&value, &length, name) != 0 || value == nullptr)
            return otherwise;
        std::string out (value);
        std::free (value);
        return out;
       #else
        const char* value = std::getenv (name);
        return value != nullptr ? std::string (value) : std::string (otherwise);
       #endif
    }

    const std::vector<double>& want (const std::string& label)
    {
        static const std::vector<double> none;
        const auto it = fixture.find (label);
        return it == fixture.end() ? none : it->second.v;
    }

    //==========================================================================
    /** THE FINDS: read here from the prototype's folder (the readers' entries made as dump.mjs makes them), or, without
        it, taken from what the fixture says they bring. */
    W::Finds finds;
    bool findsRead = false;

    void readFinds (const fs::path& proto)
    {
        const fs::path fd = proto / "finds", dragonBase = proto / ".." / "rogue" / "finds" / "dragon3d" / "dragon";
        std::vector<std::string> dirs;
        for (const auto& n : listing (fd)) if (endsWith (n, "3d") && fs::is_directory (fd / n)) dirs.push_back (n);
        std::string gd;
        for (const auto& d : dirs) if (W::roleOf (d) == "guitars") { gd = d; break; }
        std::vector<double> v;
        for (const auto& d : dirs) v.push_back (hstr (W::roleOf (d)));
        E ("parse.roles", 'i', v);
        const auto meshE = [] (const std::string& label, const std::vector<uint8_t>& b)
        {
            const auto m = W::readMesh (b.data(), b.size());
            E (label, 'd', m.ok ? std::vector<double> { 1, (double) m.nv, (double) m.ni, m.zmin, m.zmax } : std::vector<double> { 0 });
            return m;
        };
        for (const auto& d : dirs)
            for (const auto& f : listing (fd / d))
                if (endsWith (f, ".mesh")) meshE ("parse.mesh." + d + "/" + f, bytesOf (fd / d / f));
        const auto dragonMeshBytes = bytesOf (dragonBase.string() + ".mesh");
        const auto dragonMesh = meshE ("parse.mesh.dragon", dragonMeshBytes);
        const auto firstMesh = [&] (const std::string& d) { for (const auto& f : listing (fd / d)) if (endsWith (f, ".mesh")) return f; return std::string(); };
        const auto creditE = [] (const std::string& label, const fs::path& txt)
        {
            const auto c = fs::exists (txt) ? W::creditOf (textOf (txt)) : std::nullopt;
            E (label, 'i', c ? chars (*c) : std::vector<double> { -1 });
        };
        for (const auto& d : dirs) { const auto m = firstMesh (d); creditE ("parse.credit." + d, fd / d / (m.substr (0, m.size() - 5) + ".txt")); }
        creditE ("parse.credit.dragon", dragonBase.string() + ".txt");
        const std::string guitarsTxt = textOf (fd / gd / "guitars.txt");
        const auto heads = [] (const std::vector<W::Headstock>& hs)
        {
            std::vector<double> o;
            for (const auto& h : hs) { o.push_back (hstr (h.name)); addAll (o, h.c); addAll (o, h.s); }
            return o;
        };
        E ("parse.headstocks", 'd', heads (W::readHeadstocks (guitarsTxt)));
        E ("parse.headstocks.pair", 'd', heads (W::readHeadstocks (textOf (fd / gd / "guitars_pair.txt"))));
        const auto set = W::readGuitars (guitarsTxt);
        v.clear();
        for (const auto& g : set)
        {
            add (v, { hstr (g.name), hstr (g.file), g.box ? 1.0 : 0.0 });
            if (g.box) { addAll (v, g.box->c); addAll (v, g.box->s); } else add (v, { 0, 0, 0, 0, 0, 0 });
            v.push_back (g.paint ? 1 : 0);
            if (g.paint) addAll (v, *g.paint); else add (v, { 0, 0, 0 });
        }
        E ("parse.guitars", 'd', v);
        const auto deal = W::dealGroves (set);
        v.clear(); for (const int c : deal.chosen) v.push_back (c);
        E ("parse.deal", 'i', v);
        v.clear(); for (const auto& q : deal.items) add (v, { (double) q.grove, q.star ? 1.0 : 0.0 });
        E ("parse.deal.items", 'i', v);
        {
            const auto atlas = bytesOf (fd / "banners" / "atlas.png");
            const auto ad = W::pngSize (atlas.data(), atlas.size());
            E ("parse.png.banners", 'i', { (double) ad[0], (double) ad[1] });
            v.clear(); for (const auto& c : W::readBanners (textOf (fd / "banners" / "banners.txt"), ad[0], ad[1])) addAll (v, c);
            E ("parse.banners", 'd', v);
        }
        std::vector<W::Png> pngs;
        v.clear();
        for (const auto& f : listing (fd / "letters"))
            if (endsWith (f, ".png")) { const auto b = bytesOf (fd / "letters" / f); const auto d = W::pngSize (b.data(), b.size()); pngs.push_back ({ f, d[0], d[1] }); add (v, { (double) d[0], (double) d[1] }); }
        E ("parse.png.letters", 'i', v);
        const auto atlas = W::packLetters (textOf (fd / "letters" / "letters.txt"), pngs);
        v = { (double) atlas.width, (double) atlas.height };
        if (atlas.censored) addAll (v, *atlas.censored); else add (v, { -1, -1, -1, -1 });
        E ("parse.letters.atlas", 'd', v);
        v.clear(); for (const auto& p : atlas.pieces) { add (v, { hstr (p.name), hstr (p.file) }); addAll (v, p.cell); }
        E ("parse.letters.pieces", 'd', v);
        const auto anim = bytesOf (dragonBase.string() + ".anim");
        finds.dragonHead = W::readDragonHead (anim.data(), anim.size(), dragonMesh.nv);
        finds.letters = atlas.table;
        finds.guitars = deal.items;
        finds.guitarNames = deal.names;
        for (const auto& d : dirs)
        {
            if (d == gd) continue;
            const auto b = bytesOf (fd / d / firstMesh (d));
            const std::string role = W::roleOf (d);
            if (const auto top = W::landmarkTop (role, W::readMesh (b.data(), b.size()))) finds.landmarkTops.push_back ({ role, *top });
        }
        findsRead = true;
    }
    /** The finds as the fixture says they come, for a run without the prototype's folder. */
    void findsFromFixture()
    {
        const int nl = (int) want ("finds.letters.n").at (0);
        for (int k = 0; k < nl; ++k)
        {
            const auto& t = want ("finds.letters." + std::to_string (k));
            W::LetterText q;
            q.name = unchars (want ("finds.letters." + std::to_string (k) + ".name"));
            q.cell = { t[0], t[1], t[2], t[3] }; q.size = { t[4], t[5] }; q.up = t[6]; q.once = t[7] != 0;
            if (t[8] != 0) q.colour = std::array<double, 4> { t[9], t[10], t[11], t[12] };
            finds.letters.push_back (q);
        }
        const auto& g = want ("finds.guitars");
        for (size_t i = 0; i + 1 < g.size(); i += 2)
        {
            finds.guitars.push_back ({ (int) g[i], g[i + 1] != 0 });
            finds.guitarNames.push_back (unchars (want ("finds.guitars.name." + std::to_string (i / 2))));
        }
        const int nm = (int) want ("finds.landmarks.n").at (0);
        for (int k = 0; k < nm; ++k)
            finds.landmarkTops.push_back ({ unchars (want ("finds.landmarks." + std::to_string (k) + ".role")), want ("finds.landmarks." + std::to_string (k)).at (0) });
        const std::vector<double> d = want ("finds.dragon");      // a copy: a reference into a temporary label's lookup warns (gcc -Wdangling-reference)
        const size_t nf = (size_t) d.at (0);
        W::DragonHead h;
        for (size_t f = 0; f < nf; ++f) h.mouth.push_back ({ d[1 + f * 3], d[2 + f * 3], d[3 + f * 3] });
        for (size_t f = 0; f < nf; ++f) h.mid.push_back ({ d[1 + nf * 3 + f * 3], d[2 + nf * 3 + f * 3], d[3 + nf * 3 + f * 3] });
        finds.dragonHead = h;
    }
    /** What the world is furnished with, as entries (the strings as their bytes). */
    void findsEntries()
    {
        E ("finds.letters.n", 'i', { (double) finds.letters.size() });
        for (size_t k = 0; k < finds.letters.size(); ++k)
        {
            const auto& t = finds.letters[k];
            E ("finds.letters." + std::to_string (k) + ".name", 'i', chars (t.name));
            std::vector<double> v;
            addAll (v, t.cell); addAll (v, t.size); add (v, { t.up, t.once ? 1.0 : 0.0, t.colour ? 1.0 : 0.0 });
            if (t.colour) addAll (v, *t.colour); else add (v, { 0, 0, 0, 0 });
            E ("finds.letters." + std::to_string (k), 'd', v);
        }
        std::vector<double> v;
        for (const auto& q : finds.guitars) add (v, { (double) q.grove, q.star ? 1.0 : 0.0 });
        E ("finds.guitars", 'i', v);
        for (size_t i = 0; i < finds.guitarNames.size(); ++i) E ("finds.guitars.name." + std::to_string (i), 'i', chars (finds.guitarNames[i]));
        E ("finds.landmarks.n", 'i', { (double) finds.landmarkTops.size() });
        for (size_t k = 0; k < finds.landmarkTops.size(); ++k)
        {
            E ("finds.landmarks." + std::to_string (k) + ".role", 'i', chars (finds.landmarkTops[k].first));
            E ("finds.landmarks." + std::to_string (k), 'd', { finds.landmarkTops[k].second });
        }
        v = { finds.dragonHead ? (double) finds.dragonHead->mouth.size() : 0.0 };
        if (finds.dragonHead) { for (const auto& p : finds.dragonHead->mouth) addAll (v, p); for (const auto& p : finds.dragonHead->mid) addAll (v, p); }
        E ("finds.dragon", 'd', v);
    }

    //==========================================================================
    std::unique_ptr<W> fresh (int gear = 1, bool parked = false)
    {
        auto w = std::make_unique<W>();
        w->build();
        w->furnish (finds);
        w->setGear (gear); w->setParked (parked); w->settle();
        w->setPitchOffset (W::pitchOffsetDefault);
        return w;
    }
    void run (W& w, double seconds) { for (int i = 0, n = (int) std::round (seconds * 60); i < n; ++i) w.step (1.0 / 60); }
    void st (const std::string& label, const W& w)
    {
        const auto& s = w.state();
        E (label + ".state", 'd', { s.sr, (double) s.lap, s.lat, s.latV, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.aurora, s.speed, s.storm, s.calm, s.park,
                                    s.hitT, s.hitS, (double) s.hits, s.flash, s.findFade, s.beat, s.stageHitT, (double) s.gear, s.parked ? 1.0 : 0.0 });
    }
    void dr (const std::string& label, const W& w)
    {
        std::vector<double> v;
        for (const auto& u : w.dragons())
        {
            add (v, { (double) (int) u.mode, u.x, u.y, u.z, u.vx, u.vy, u.vz, u.mt, u.as, u.lat, u.up, u.front ? 1.0 : 0.0, (double) (int) u.kind, u.follow, u.loopR, u.loopW,
                      u.anchorT, u.beat, u.rate });
            addAll (v, u.U); addAll (v, u.F); addAll (v, u.L); addAll (v, u.mouth); addAll (v, u.headDir);
            add (v, { u.flameT, u.hicT, u.nextHic, u.ringT, u.ring ? 1.0 : 0.0 });
            if (u.ring) { addAll (v, u.ring->at); addAll (v, u.ring->dir); } else add (v, { 0, 0, 0, 0, 0, 0 });
            add (v, { u.perchT, u.tumble, u.shake, u.nextGest, u.g.hy, u.g.hp, u.g.wing, u.g.pitch, u.g.yaw, u.g.side });
            if (u.gest) add (v, { (double) (int) u.gest->kind, u.gest->t, u.gest->dur, u.gest->a, u.gest->b }); else add (v, { -1, 0, 0, 0, 0 });
            if (u.perch) add (v, { 1, u.perch->x, u.perch->y, u.perch->z, u.perch->yaw }); else add (v, { 0, 0, 0, 0, 0 });
        }
        E (label + ".dragons", 'd', v);
    }
    void orbs (const std::string& label, const W& w)
    {
        std::vector<double> v;
        for (const auto& o : w.orbs()) add (v, { o.x, o.y, o.z, o.age, o.life, o.n, o.as, o.lat, o.up, o.ph });
        E (label + ".orbs", 'd', v);
    }
    void lightning (const std::string& label, const W& w)
    {
        std::vector<double> v;
        for (const auto& b : w.bolts())
        {
            add (v, { b.t, b.k, (double) b.n }); addAll (v, b.at); v.push_back ((double) b.lines.size());
            for (const auto& L : b.lines) { add (v, { L.w, (double) L.pts.size() }); for (const auto& p : L.pts) addAll (v, p); }
        }
        E (label + ".bolts", 'd', v);
        v.clear();
        for (const auto& q : w.sparks()) { v.push_back (q.t); addAll (v, q.at); v.push_back (q.k); }
        E (label + ".sparks", 'd', v);
        const auto ts = w.teslaState();
        v = { (double) ts.arc.i, ts.arc.t, ts.arc.dur, ts.chain };
        addAll (v, w.shakes());
        E (label + ".tesla", 'd', v);
    }
    void fires (const std::string& label, const W& w)
    {
        const auto f = w.fires();
        E (label + ".fires", 'i', { (double) f.n });
        E (label + ".fires.em", 'f', std::vector<double> (f.em.begin(), f.em.begin() + f.n * 12));
        E (label + ".fires.pl", 'f', std::vector<double> (f.lights.begin(), f.lights.end()));
    }
    void ribbons (const std::string& label, const W& w)
    {
        const auto r = w.ribbons();
        const std::vector<float> rb (r.rb.begin(), r.rb.begin() + r.n * 16);
        E (label + ".rib", 'i', { (double) r.n, (double) fnv (rb) });
        E (label + ".rib.sample", 'f', sample (rb, 96));
    }
    void meshE (const std::string& label, const W::Mesh& m)
    {
        if (m.wide())
            E (label, 'i', { (double) m.v.size(), (double) fnv (m.v), (double) m.idx.size(), (double) fnv (m.idx), 4 });
        else
        {
            std::vector<uint16_t> i16 (m.idx.begin(), m.idx.end());
            E (label, 'i', { (double) m.v.size(), (double) fnv (m.v), (double) m.idx.size(), (double) fnv (i16), 2 });
        }
    }

    /** The prototype's scenarios, as dump.mjs runs them. */
    void makeEntries()
    {
        const double L = W::loopLength();
        std::vector<double> v;
        for (const uint32_t x : { 0u, 1u, 12345u, 0x7fffffffu, 0x80000000u, 4294967295u }) v.push_back (W::ih (x));
        E ("hash.ih", 'i', v); v.clear();
        for (const auto& [a, b, c] : std::vector<std::array<double, 3>> { { 0, 0, 0 }, { 1, 2, 3 }, { -5, 7, 101 }, { 255, 255, 707 }, { -1, -1, 1010 }, { 999, 3, 1 } })
            v.push_back (W::hcell (a, b, c));
        E ("hash.hcell", 'f', v); v.clear();
        for (const auto& [a, b] : std::vector<std::array<double, 2>> { { 1, 1 }, { 7, 92 }, { 120, 16 }, { 0, 0 }, { 241, 77 }, { 15011234567, 51 }, { -3, 840 }, { 2.5e9, 71 }, { -7e10, 49 } })
            v.push_back (W::hs (a, b));
        E ("hash.hs", 'f', v); v.clear();
        for (const double x : { 0.0, 1e-6, -3e-5, 0.1, 0.333, 1.0, 1.0009765625, 2047.7, -1599.3, 70000.0, -1e9, 2.9e-8, 0.49999 }) v.push_back (W::toHalf (x));
        E ("half", 'd', v);
        {
            const auto base = W::makeBase();
            const auto detail = W::makeDetail(), noise = W::makeNoise(), cells = W::makeCells();
            E ("tex.base", 'i', { (double) base.size(), (double) fnv (base) });
            E ("tex.base.sample", 'f', sample (base, 64));
            const auto bytes = [] (const std::vector<uint8_t>& t) { std::vector<double> o { (double) t.size(), (double) fnv (t) }; addAll (o, sample (t, 32)); return o; };
            E ("tex.detail", 'i', bytes (detail)); E ("tex.noise", 'i', bytes (noise)); E ("tex.cells", 'i', bytes (cells));
        }
        E ("loop.len", 'd', { L, (double) W::studs() });
        E ("loop.table", 'd', sample (W::loopTable(), 64));
        v.clear(); for (const double th : { 0.0, 1.0, 2.5, 4.0, 6.2 }) addAll (v, W::loopR (th));
        E ("loop.R", 'd', v);
        v.clear(); for (const double s : { 0.0, 100.0, 1234.5, 5000.0, -20.0, 5147.1, 9999.0 }) v.push_back (W::thetaAt (s));
        E ("loop.theta", 'd', v);
        v.clear();
        for (const double s : { 0.0, 300.0, 1777.7, 4000.0 }) { addAll (v, W::roadPointAt (s, 0)); addAll (v, W::roadPointAt (s, -12.5)); addAll (v, W::tangentAt (W::thetaAt (s))); v.push_back (W::headingAt (s)); }
        E ("loop.point", 'd', v);
        v.clear(); for (const auto& q : std::vector<std::array<double, 4>> { { 24000, 24000, 600, 30 }, { 25100, 24100, 2000, -20 }, { 24520, 25500, 3500, 0 } }) addAll (v, W::frontAt (q[0], q[1], q[2], q[3]));
        E ("loop.front", 'd', v);
        v.clear(); for (const double x : { -1.0, 0.0, 5147.139620441408, 12000.5, -77777.0 }) v.push_back (W::modL (x));
        E ("loop.modL", 'd', v);
        v.clear();
        for (const auto& q : std::vector<std::array<double, 2>> { { 0, 0 }, { 1.3, -0.087 }, { -2.9, 0.04 }, { 7.5, -0.12 } })
        { const auto b = W::cameraBasis (q[0], q[1]); addAll (v, b.r); addAll (v, b.u); addAll (v, b.f); }
        E ("camera", 'd', v);

        // THE WORLD: its places, furnished
        {
            auto wp = fresh();
            W& w = *wp;
            std::vector<W::V2> pts { { 24520, 24680 }, { 23900, 24100 }, { 25280.5, 24680.25 }, { 24000, 25500 }, { 26000, 23000 }, { 10, 47990 }, { 24300.7, 23950.1 }, { 24130, 24600 }, { 25000, 25300 } };
            for (const double s : { 0.0, 230.0, 617.0, 1290.0, 2316.0, 3047.0, 3800.0, 4790.0 }) for (const double d : { 0.0, 6.0, -30.0, 120.0 }) pts.push_back (W::roadPointAt (s, d));
            std::vector<double> fh, th, pr, rt;
            for (const auto& p : pts)
            {
                fh.push_back (w.floorHeight (p[0], p[1])); th.push_back (w.terrainH (p[0], p[1]));
                const auto q = w.probe (p[0], p[1]); add (pr, { q.hot, q.mtn }); addAll (rt, w.roadTap (p[0], p[1]));
            }
            E ("height.floor", 'd', fh); E ("height.terrain", 'd', th); E ("height.probe", 'd', pr); E ("height.road", 'd', rt);
            E ("tex.road", 'i', { (double) w.roadMap().size(), (double) fnv (w.roadMap()) });
            E ("tex.road.sample", 'f', sample (w.roadMap(), 64));
            v.clear(); for (const auto& c : w.crags()) { addAll (v, c.a); addAll (v, c.b); add (v, { c.r, c.h, hstr (c.name) }); }
            E ("crags", 'd', v);
            v.clear(); for (const auto& q : w.props()) add (v, { hstr (q.mesh), q.x, q.y, q.z, q.yaw, q.s, q.p, (double) q.idx, hstr (q.name) });
            E ("props", 'd', v);
            v.clear(); for (const auto& b : w.banners()) add (v, { b.x, b.y, b.z, b.yaw, b.w, b.h, b.seed, (double) b.cell });
            E ("banners", 'd', v);
            v.clear(); for (const auto& q : w.targets()) add (v, { q.x, q.y, q.z, q.w, q.hit ? 1.0 : 0.0, (double) q.tesla });
            E ("targets", 'd', v);
            v.clear(); for (const auto& q : w.perches()) add (v, { q.x, q.y, q.z, q.yaw, hstr (q.name), (double) q.tower });
            E ("perches", 'd', v);
            v.clear(); for (const auto& q : w.spires()) add (v, { q.x, q.y, q.z, (double) q.prop });
            E ("spires", 'd', v);
            E ("gate", 'd', { w.gate().at, w.gate().x, w.gate().z, w.gate().y, (double) w.gate().prop });
            v = { w.citadel().at, w.citadel().x, w.citadel().z, w.citadel().y };
            for (const auto& q : w.citadel().towers) add (v, { q.x, q.y, q.z, q.h, q.tip, (double) q.prop });
            E ("citadel", 'd', v);
            E ("warrior", 'd', { w.warrior().at, w.warrior().x, w.warrior().y, w.warrior().z, w.warrior().yaw });
            v.clear(); for (const auto& q : w.landmarks()) add (v, { hstr (q.role), q.at, q.x, q.y, q.z, q.yaw, q.came ? 1.0 : 0.0 });
            E ("landmarks", 'd', v);
            v.clear(); for (const auto& q : w.tesla()) add (v, { q.x, q.y, q.z, q.top, (double) q.prop });
            E ("tesla", 'd', v);
            E ("amps", 'd', { w.amps().at, w.amps().x, w.amps().z, w.amps().y, w.amps().fx, w.amps().fy, w.amps().fz });
            E ("henge", 'd', { w.henge().at, w.henge().x, w.henge().z, w.henge().y });
            E ("anvil", 'd', { w.anvil().at, w.anvil().x, w.anvil().z, w.anvil().y });
            E ("cat", 'd', { w.cat().at, w.cat().x, w.cat().z, w.cat().y, w.cat().nx, w.cat().nz });
            E ("stage", 'd', { w.stage().at, w.stage().x, w.stage().z, w.stage().y, w.stage().nx, w.stage().nz, w.stage().yaw });
            v.clear(); for (const auto& q : w.pyro()) add (v, { q.x, q.y, q.z, q.at, (double) q.k, (double) q.i });
            E ("pyro", 'd', v);
            v.clear(); for (const auto& q : w.fountains()) add (v, { q.x, q.y, q.z, q.at, q.next, q.t, (double) q.n, (double) q.i });
            E ("fountains", 'd', v);
            E ("tunnel", 'd', { w.tunnel().from, w.tunnel().to, w.tunnel().hw });
            E ("taken", 'd', w.taken());
            v.clear(); for (const auto& g : w.groves()) add (v, { (double) g.g, g.at, g.both ? 1.0 : 0.0, (double) g.starSide, (double) g.members.size() });
            E ("groves", 'd', v);
            v.clear();
            for (const auto& q : w.guitars())
                add (v, { (double) q.i, q.at, q.x, q.y, q.z, q.yaw, q.k, q.lean[0], q.lean[1], (double) q.grove, q.star ? 1.0 : 0.0, (double) q.side, q.off, q.along, hstr (q.name) });
            E ("guitars", 'd', v);
            v.clear();
            for (const auto& q : w.letters())
            {
                add (v, { hstr (q.name), q.at, q.x, q.z, q.y, q.yaw, q.nx, q.nz, q.hw, q.hh }); addAll (v, q.cell); v.push_back (q.colour ? 1 : 0);
                if (q.colour) addAll (v, *q.colour); else add (v, { 0, 0, 0, 0 });
                add (v, { q.seed, q.big ? 1.0 : 0.0 });
            }
            E ("letters", 'd', v);
            const std::vector<std::string> groups { "finds", "big", "road", "signs", "sky" };
            const auto pl = w.places();
            v.clear();
            for (const auto& q : pl)
                add (v, { (double) (std::find (groups.begin(), groups.end(), q.group) - groups.begin()), hstr (q.name), q.at, q.seen, (double) q.lap, (double) q.kind });
            E ("places", 'd', v);
            meshE ("mesh.hill", w.hillMesh());
            for (const auto& [name, m] : W::propMeshes()) meshE ("mesh." + name, m);
            st ("start", w); dr ("start", w); orbs ("start", w);
            E ("start.nextFind", 'd', { w.nextFindAt() });
            w.step (1.0 / 60); st ("start1", w);
            for (int lap = 0; lap < 5; ++lap)
            {
                w.startAt (w.state().sr, lap);
                v.clear(); for (const auto& f : w.findsNow()) add (v, { (double) f.kind, f.x, f.z, f.y, f.yaw, f.at, f.d });
                E ("finds.lap" + std::to_string (lap), 'd', v);
            }
            for (const size_t k : { (size_t) 0, (size_t) 7, (size_t) 15, (size_t) 22, pl.size() - 1 })
            {
                const auto j = w.jumpTo (pl[k]);
                const std::string lb = "jump." + std::to_string (k);
                E (lb, 'd', { j.at, (double) j.lap }); st (lb, w);
                run (w, 2); st (lb + ".2", w);
            }
        }
        // THE DRIVE in every gear, PARK in and out, the storm
        for (int g = 1; g <= 4; ++g)
        {
            auto w = fresh (g);
            const std::string lb = "g" + std::to_string (g);
            run (*w, 3); st (lb + ".3", *w);
            run (*w, 7); st (lb + ".10", *w); lightning (lb + ".10", *w);
        }
        {
            auto w = fresh (2);
            run (*w, 2); w->setParked (true);
            run (*w, 3); st ("park.in3", *w);
            run (*w, 9); st ("park.in12", *w); dr ("park.in12", *w);
            w->setParked (false);
            run (*w, 2); st ("park.out2", *w);
            run (*w, 6); st ("park.out8", *w);
        }
        {
            auto w = fresh (1);
            run (*w, 2); w->setGear (4);
            run (*w, 1); st ("storm.1", *w);
            std::vector<double> flash;
            for (int i = 0; i < 360; ++i) { w->step (1.0 / 60); flash.push_back (w->state().flash); }
            E ("storm.flash", 'd', flash);
            st ("storm.7", *w); lightning ("storm.7", *w); fires ("storm.7", *w); ribbons ("storm.7", *w);
            w->setGear (3);
            run (*w, 3); st ("storm.off3", *w);
        }
        // AN ATTACK on the pyro stretch
        {
            auto w = fresh (1);
            w->startAt (200);
            run (*w, 1); w->hit (0.8);
            std::vector<double> flash;
            for (int i = 0; i < 60; ++i)
            {
                w->step (1.0 / 60); flash.push_back (w->state().flash);
                if (i == 11) { st ("hit.0.2", *w); lightning ("hit.0.2", *w); dr ("hit.0.2", *w); fires ("hit.0.2", *w); ribbons ("hit.0.2", *w); }
            }
            E ("hit.flash", 'd', flash);
            st ("hit.1", *w); fires ("hit.1", *w);
        }
        {
            auto w = fresh (2);
            w->startAt (0.77 * L - 600);
            run (*w, 2); w->hit (1); run (*w, 0.5);
            st ("stage", *w); fires ("stage", *w); ribbons ("stage", *w);
        }
        {
            auto w = fresh (4);
            w->startAt (330);
            run (*w, 3); w->hit (1); run (*w, 0.1);
            st ("tesla.0.1", *w); lightning ("tesla.0.1", *w); ribbons ("tesla.0.1", *w);
            run (*w, 5);
            st ("tesla.5", *w); lightning ("tesla.5", *w); ribbons ("tesla.5", *w); fires ("tesla.5", *w);
        }
        // THE DRAGONS
        {
            auto w = fresh (2);
            for (int k = 1; k <= 26; ++k) { run (*w, 5); dr ("dragons." + std::to_string (5 * k), *w); }
            st ("dragons.130", *w); orbs ("dragons.130", *w);
        }
        {
            auto w = fresh (3);
            w->startAt (0.70 * L - 900);
            for (int k = 1; k <= 40; ++k) { run (*w, 2); dr ("swoop." + std::to_string (2 * k), *w); fires ("swoop." + std::to_string (2 * k), *w); }
        }
        {
            auto w = fresh (2);
            w->startAt (L - 280, 1);
            for (int k = 1; k <= 16; ++k) { run (*w, 5); dr ("lap2." + std::to_string (5 * k), *w); }
            lightning ("lap2.80", *w);
        }
        {   // lap 1 by the citadel: one flies into a spire, tumbles, the spire shakes
            auto w = fresh (2);
            w->startAt (w->citadel().at - 500, 1);
            for (int k = 1; k <= 30; ++k) { run (*w, 1); dr ("clang." + std::to_string (k), *w); }
            lightning ("clang.30", *w);
        }
        {
            auto w = fresh (1, true);
            for (int k = 1; k <= 8; ++k) { run (*w, 5); dr ("parkd." + std::to_string (5 * k), *w); }
            fires ("parkd.40", *w);
        }
        // THE BALL LIGHTNING
        {
            auto w = fresh (3);
            for (int k = 1; k <= 6; ++k) { run (*w, 10); orbs ("orbs." + std::to_string (10 * k), *w); lightning ("orbs." + std::to_string (10 * k), *w); }
        }
        {
            auto w = fresh (1);
            w->startAt (w->letters().at (1).at - 150);
            run (*w, 1); fires ("letter", *w);
        }
        // THE CABIN'S PLATE
        {
            const int wd = 64, ht = 40;
            std::vector<uint8_t> rgba ((size_t) (wd * ht * 4), 255);
            for (int y = 9; y < 27; ++y) for (int x = 5; x < 50; ++x) rgba[(size_t) ((y * wd + x) * 4 + 3)] = (uint8_t) ((x * 7 + y) % 255);
            const auto a = W::glassBounds (rgba.data(), wd, ht, 3), b = W::glassBounds (rgba.data(), wd, ht, 0);
            const std::vector<uint8_t> opaque (16, 255);
            v.clear(); addAll (v, *a); addAll (v, *b); v.push_back (W::glassBounds (opaque.data(), 2, 2, 1) ? 0 : 1);
            E ("glass", 'd', v);
        }
    }

    void compare (bool skipParse)
    {
        int entries = 0, skipped = 0;
        double worst = 0;
        std::string worstLabel;
        for (const auto& [label, e] : fixture)
        {
            const auto it = mine.find (label);
            if (it == mine.end())
            {
                if (skipParse && label.rfind ("parse.", 0) == 0) { ++skipped; continue; }
                std::printf ("FAIL %s: not made here\n", label.c_str()); ++failures; continue;
            }
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
        check (entries == (int) mine.size(), "every entry made here is in the fixture");
        if (skipped > 0)
            std::printf ("SKIP %d entries of the finds' readers: no prototype folder given (metal_world_test <fixture> <prototype dir>);"
                         " the worlds were furnished with the finds as the fixture has them\n", skipped);
        std::printf ("%d entries compared; the largest double's deviation %.3g (%s)\n", entries, worst, worstLabel.c_str());
    }

    /** What the port adds to the prototype: the camera settled before the first step, an attack called off, the glass. */
    void ownChecks()
    {
        auto a = fresh (2, true), b = fresh (2, true);
        check (a->state().camY == 0, "unstepped, the camera is at 0");
        a->settleCamera();
        check (a->state().camY > 0 && std::abs (a->state().camY - a->floorHeight (a->state().x, a->state().z) - W::eye) < 0.05, "settled, the eye stands over the road");
        run (*b, 3);
        check (std::abs (a->state().camY - b->state().camY) < 0.01 && std::abs (a->state().pitch - b->state().pitch) < 1e-3, "settled, the camera is where the steps take it, parked");
        auto c = fresh (1);
        c->startAt (200);
        run (*c, 1); c->hit (1); run (*c, 0.2);
        const auto lit = c->fires();
        c->quiet();
        const auto calm = c->fires();
        int litPyro = 0, calmPyro = 0;
        for (int i = 0; i < lit.n; ++i) litPyro += lit.em[(size_t) (i * 12 + 3)] == 0 && lit.em[(size_t) (i * 12 + 8)] < 4;
        for (int i = 0; i < calm.n; ++i) calmPyro += calm.em[(size_t) (i * 12 + 3)] == 0 && calm.em[(size_t) (i * 12 + 8)] < 4;
        check (litPyro > 0 && calmPyro == 0 && c->state().hitS == 0, "quiet: an attack's pyro is called off");
        std::vector<uint8_t> rgba (8 * 8 * 4, 255);
        rgba[(size_t) ((2 * 8 + 6) * 4 + 3)] = 0;
        const auto g = W::Glass::of (rgba.data(), 8, 8, 2);
        check (g.width == 4 && g.height == 4 && g.open (6.5f / 8, 2.5f / 8) && ! g.open (0.1f, 0.9f), "the glass: open where the plate is clear");
    }
}

namespace
{
    /** THE ART AS IT SHIPS (resources/metal, its world/ in WebP): metal::find over it must bring the world the very
        finds the prototype's folder (or its dump) brings it — the letters' table and atlas cells (the WebP of each PNG
        the table names, the same size), the groves' deal and names, the landmarks' tops in their order, the dragon's
        head in every pose — and find every file it reads (MetalShipped.h says which; the E530's face gate asks the
        same of the factory folder, as the build lays it out). */
    void shippedChecks (const std::string& dir)
    {
        namespace M = orbitamp::metal;
        if (! fs::is_directory (fs::path (dir) / "world")) { std::printf ("FAIL shipped: no art at %s\n", dir.c_str()); ++failures; return; }
        M::Art art;
        art.bytes = [dir] (const std::string& path) { return bytesOf (fs::path (dir) / path); };
        art.list = [dir] (const std::string& folder)
        {
            std::error_code ec;
            return fs::is_directory (fs::path (dir) / folder, ec) ? listing (fs::path (dir) / folder) : std::vector<std::string> {};
        };
        art.pictureSize = [dir] (const std::string& path) { const auto b = bytesOf (fs::path (dir) / path); return M::webpSize (b.data(), b.size()); };
        for (const auto& what : M::shipped::missing (art))
        {
            ++failures;
            std::printf ("FAIL shipped: %s\n", what.c_str());
        }
        const auto found = M::find (art);
        const auto& got = found.finds;
        const auto near = [] (double a, double b) { return std::abs (a - b) <= 1e-9 * std::max (1.0, std::abs (b)); };
        bool lettersOk = got.letters.size() == finds.letters.size() && ! got.letters.empty();
        for (size_t k = 0; lettersOk && k < got.letters.size(); ++k)
        {
            const auto& a = got.letters[k]; const auto& b = finds.letters[k];
            lettersOk = a.name == b.name && a.cell == b.cell && near (a.size[0], b.size[0]) && near (a.size[1], b.size[1]) && near (a.up, b.up)
                     && a.once == b.once && a.colour.has_value() == b.colour.has_value() && (! a.colour || *a.colour == *b.colour);
        }
        check (lettersOk, "shipped: the letters' table and cells from the WebP letters are the prototype's");
        bool guitarsOk = got.guitars.size() == finds.guitars.size() && ! got.guitars.empty() && got.guitarNames == finds.guitarNames;
        for (size_t i = 0; guitarsOk && i < got.guitars.size(); ++i)
            guitarsOk = got.guitars[i].grove == finds.guitars[i].grove && got.guitars[i].star == finds.guitars[i].star;
        check (guitarsOk, "shipped: the groves' deal and names are the prototype's");
        bool topsOk = got.landmarkTops.size() == finds.landmarkTops.size() && ! got.landmarkTops.empty();
        for (size_t k = 0; topsOk && k < got.landmarkTops.size(); ++k)
            topsOk = got.landmarkTops[k].first == finds.landmarkTops[k].first && near (got.landmarkTops[k].second, finds.landmarkTops[k].second);
        check (topsOk, "shipped: the landmarks' tops, in order, are the prototype's");
        bool dragonOk = got.dragonHead.has_value() && finds.dragonHead.has_value() && got.dragonHead->mouth.size() == finds.dragonHead->mouth.size();
        for (size_t f = 0; dragonOk && f < got.dragonHead->mouth.size(); ++f)
            for (size_t c = 0; c < 3; ++c)
                dragonOk = dragonOk && near (got.dragonHead->mouth[f][c], finds.dragonHead->mouth[f][c]) && near (got.dragonHead->mid[f][c], finds.dragonHead->mid[f][c]);
        check (dragonOk, "shipped: the dragon's head in every pose is the prototype's");
        size_t meshesThere = 0;
        for (const auto& [role, path] : found.meshOf) if (fs::exists (fs::path (dir) / path)) ++meshesThere;
        check (found.meshOf.size() == 16 && meshesThere == 16, "shipped: the eight meshes and the groves' eight guitars are found and there");
        check (found.letters.censored.has_value() && ! found.bannersTxt.empty() && found.credits.size() == 8 && found.credits[0].find ("LasquetiSpice") != std::string::npos,
               "shipped: the CENSORED cell, the banners' table, eight credits (the lava crawler's file has none), the dragon's first");
        // What a late batch of files is judged by (MetalDrive::takeArt, MetalGl::takeArt): the shipped finds bring the
        // world something, no files bring it nothing, and a batch with the plates alone brings no finds.
        M::Art plates;
        plates.bytes = [dir] (const std::string& path) { return path.rfind ("dashboard", 0) == 0 ? bytesOf (fs::path (dir) / path) : std::vector<std::uint8_t> {}; };
        plates.list = [] (const std::string&) { return std::vector<std::string> {}; };
        check (found.brings() && ! M::find (M::Art {}).brings() && ! M::find (plates).brings(),
               "shipped: the finds bring the world something; no files, or the plates alone, bring it nothing");
    }
}

int main (int argc, char** argv)
{
    readFixture (argc > 1 ? argv[1] : METAL_FIXTURE);
    const std::string proto = argc > 2 ? argv[2] : environment ("METAL_PROTOTYPE", METAL_PROTOTYPE);
    if (! proto.empty() && fs::is_directory (fs::path (proto) / "finds")) readFinds (proto);
    else findsFromFixture();
    findsEntries();
    makeEntries();
    compare (! findsRead);
    ownChecks();
    shippedChecks (argc > 3 ? argv[3] : METAL_ART);
    if (failures == 0)
        std::printf ("metal_world_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
