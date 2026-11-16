// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for underwater::UnderwaterWorld — the sea floor's world against its prototype, sea-gl.js, number for
// number. The fixture (tests/fixtures/underwater/underwater-world.txt) is the prototype's own state, dumped by node
// (tests/fixtures/underwater/dump.mjs): the hashes, the three textures, the meshes, the floor's height, the jellyfish
// as made, the drive as cabin.html?t=14 leaves it, ten seconds of frames, the top gear steered and let go, PARK (the
// curious jellyfish, the view turned in place, driving on), two passing bathyscaphes, an attack, the prototype's
// TURBO, and the sprites' table. This makes the same entries the same way and compares them: a double within 1e-9
// of its size, an f32 value, a hash or a count exactly. Then what the drive reads besides: the cabin's window.
//
// `underwater_world_test [fixture]` compares. Returns non-zero on any failure. JUCE-free, like the code under test.

#include "underwater/view/UnderwaterWorld.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef UNDERWATER_FIXTURE
 #define UNDERWATER_FIXTURE "tests/fixtures/underwater/underwater-world.txt"
#endif
#ifndef UNDERWATER_SPRITES
 #define UNDERWATER_SPRITES "resources/underwater/sprites.txt"
#endif

using orbitamp::underwater::UnderwaterWorld;

namespace
{
    struct Entry { char kind = 'd'; std::vector<double> v; };
    std::map<std::string, Entry> mine;
    int failures = 0;

    void E (const std::string& label, char kind, std::vector<double> v) { mine[label] = { kind, std::move (v) }; }

    template <typename T>
    uint32_t fnv (const std::vector<T>& a)
    {
        uint32_t h = 0x811c9dc5u;
        std::vector<uint8_t> b (a.size() * sizeof (T));
        if (! a.empty())
            std::memcpy (b.data(), a.data(), b.size());
        for (const uint8_t x : b) { h ^= x; h *= 0x01000193u; }
        return h;
    }
    template <typename T>
    std::vector<double> sample (const std::vector<T>& a, int n)
    {
        std::vector<double> o;
        for (int k = 0; k < n; ++k)
            o.push_back ((double) a[(size_t) std::floor ((double) k * (double) a.size() / n)]);
        return o;
    }

    void check (bool ok, const char* what)
    {
        if (! ok) { ++failures; std::printf ("FAIL %s\n", what); }
    }

    /** The prototype's scenarios, as dump.mjs runs them. */
    void makeEntries()
    {
        using W = UnderwaterWorld;
        std::vector<double> v;
        for (const uint32_t x : { 0u, 1u, 12345u, 0x7fffffffu, 0x80000000u, 4294967295u }) v.push_back (W::ih (x));
        E ("hash.ih", 'i', v); v.clear();
        for (const auto& [a, b, c] : std::vector<std::array<int, 3>> { { 0, 0, 0 }, { 1, 2, 3 }, { -5, 7, 101 }, { 255, 255, 707 }, { -1, -1, 1010 } })
            v.push_back (W::hcell (a, b, c));
        E ("hash.hcell", 'f', v); v.clear();
        for (const auto& [a, b] : std::vector<std::array<int, 2>> { { 1, 1 }, { 7, 92 }, { 120, 16 }, { 0, 0 }, { 241, 77 } })
            v.push_back (W::hs (a, b));
        E ("hash.hs", 'f', v); v.clear();

        const auto height = W::makeHeight();
        const auto detail = W::makeDetail();
        const auto caustic = W::makeCaustic();
        E ("tex.height", 'i', { (double) height.size(), (double) fnv (height) });
        E ("tex.height.sample", 'f', sample (height, 64));
        v = { (double) detail.size(), (double) fnv (detail) }; for (const double x : sample (detail, 32)) v.push_back (x);
        E ("tex.detail", 'i', v);
        v = { (double) caustic.px.size(), (double) fnv (caustic.px) }; for (const double x : sample (caustic.px, 32)) v.push_back (x);
        E ("tex.caustic", 'i', v);
        E ("tex.gain", 'd', { caustic.gain });

        for (const int seed : { 77, 78 }) { const auto m = W::icosphere (seed); E ("mesh.ico" + std::to_string (seed), 'i', { (double) m.size(), (double) fnv (m) }); }
        const auto mesh = [] (const W::Mesh& m) { return std::vector<double> { (double) m.v.size(), (double) fnv (m.v), (double) m.idx.size(), (double) fnv (m.idx) }; };
        E ("mesh.tube", 'i', mesh (W::tubeMesh (6, 9)));
        const auto flora = W::floraMeshes();
        for (size_t k = 0; k < flora.size(); ++k) E ("mesh.flora" + std::to_string (k), 'i', mesh (flora[k]));
        E ("mesh.bell", 'i', mesh (W::bellMesh (28, 12)));
        E ("mesh.tendril", 'i', mesh (W::tendrilMesh (10, 14)));

        const double pitchOffset = (-11.0 + 3.5) * W::pi / 180;
        const auto fresh = [pitchOffset] (int gear, bool parked, bool turbo)
        {
            auto w = std::make_unique<W>();
            w->build();
            w->setGear (gear); w->setParked (parked); w->setTurbo (turbo); w->settle();
            w->setPitchOffset (pitchOffset);
            return w;
        };
        const auto run = [] (W& w, double seconds, bool frames)
        {
            for (int i = 0, n = (int) std::round (seconds * 60); i < n; ++i) { w.step (1.0 / 60); if (frames) w.updateJellies (1.0 / 60); }
        };
        const auto st = [] (const std::string& label, const W& w)
        {
            const auto& s = w.state();
            std::vector<double> o { s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.silt, s.flinch, s.curious, s.thrust };
            for (const auto* f : { &s.snowFlow, &s.siltFlow, &s.bubbleFlow }) for (const double x : *f) o.push_back (x);
            o.push_back (s.lookHeading); o.push_back (s.hitT); o.push_back (s.hitS);
            E (label + ".state", 'd', o);
        };
        const auto sub = [] (const std::string& label, const W& w)
        {
            const auto& b = w.state().sub;
            E (label + ".sub", 'd', { b.on ? 1.0 : 0.0, (double) b.n, b.x, b.y, b.z, b.fx, b.fz, b.v, b.v0, b.h, b.t, b.d, b.vis });
        };
        const auto jel = [] (const std::string& label, const W& w)
        {
            std::vector<double> o;
            for (const auto& j : w.jellies())
                for (const double x : { j.rel[0], j.rel[1], j.rel[2], j.dist, j.outAxis[0], j.outAxis[1], j.outAxis[2], j.phase })
                    o.push_back (x);
            E (label + ".jelly", 'd', o);
        };
        {
            const auto w = fresh (1, false, false);
            v.clear();
            for (const auto& [x, z] : std::vector<std::array<double, 2>> { { 1200, 800 }, { 0, 0 }, { 30239.5, 17.25 }, { -3.5, 812.75 }, { 5000.125, 29999.9 }, { 1234.5678, 765.4321 } })
                v.push_back (w->floorHeight (x, z));
            E ("floor.h", 'd', v); v.clear();
            for (const auto& j : w->jellies())
                for (const double x : { j.ax, j.az, j.alt, j.scale, j.omega, j.phase, j.axis[0], j.axis[1], j.axis[2], j.tend, j.curiousW, j.giant ? 1.0 : 0.0,
                                        j.flinchK, j.near, j.side, j.lift, j.vx, j.vz, j.col[0], j.col[1], j.col[2], j.glow[0], j.glow[1], j.glow[2] })
                    v.push_back (x);
            E ("jelly.init", 'd', v);
        }
        { auto w = fresh (1, false, false); run (*w, 14, false); w->step (0); w->updateJellies (0); st ("cabin14", *w); sub ("cabin14", *w); jel ("cabin14", *w); }
        { auto w = fresh (1, false, false); run (*w, 10, true); st ("frames10", *w); jel ("frames10", *w); }
        {
            auto w = fresh (3, false, false);
            w->setSteer (0.6); run (*w, 5, true); st ("gear3.steered", *w);
            w->setSteer (0); run (*w, 10, true); st ("gear3.free", *w); jel ("gear3.free", *w);
        }
        {
            auto w = fresh (1, true, false);
            run (*w, 14, true); st ("park14", *w); jel ("park14", *w);
            w->setSteer (1); run (*w, 3, true); st ("park.turned", *w); jel ("park.turned", *w);
            w->setSteer (0); w->setParked (false); run (*w, 5, true); st ("park.off", *w);
        }
        {
            auto w = fresh (1, false, false);
            w->setPassFirst (w->state().time + 0);
            for (const auto& [t, label] : std::vector<std::pair<double, const char*>> { { 0.5, "a" }, { 9.5, "b" }, { 15, "c" }, { 20, "d" } })
            {
                run (*w, t, false);
                sub (std::string ("sub.") + label, *w);
            }
            st ("sub", *w);
        }
        { auto w = fresh (2, false, false); w->setSteer (-0.5); w->setPassFirst (w->state().time + 0); run (*w, 6, false); sub ("sub.left", *w); }
        { auto w = fresh (1, false, false); run (*w, 14, false); w->hit (0.8); run (*w, 0.2, true); st ("hit", *w); }
        {
            auto w = fresh (2, false, false);
            w->setTurbo (true); run (*w, 3, true); st ("turbo.on", *w); jel ("turbo.on", *w);
            w->setTurbo (false); run (*w, 3, true); st ("turbo.off", *w);
        }

        std::ifstream in (UNDERWATER_SPRITES, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const auto sp = W::parseSprites (text.str());
        E ("sprites.head", 'd', { sp.cell[0], sp.cell[1], sp.scale, (double) sp.grid[0], (double) sp.grid[1], (double) sp.cells.size() });
        v.clear();
        for (const auto& c : sp.cells)
        {
            v.push_back ((double) c.size());
            for (const auto& L : c) for (const double x : { L.x, L.y, L.dx, L.dy, L.toward }) v.push_back (x);
        }
        E ("sprites.lamps", 'd', v);
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

    void glassChecks()
    {
        // A dashboard of 16 x 8: the left half open window (alpha 0), the right half metal (255), a ramp in between.
        std::vector<uint8_t> px (16 * 8 * 4, 0);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                px[(size_t) ((y * 16 + x) * 4 + 3)] = (uint8_t) (x < 8 ? 0 : 255);
        const auto g = UnderwaterWorld::Glass::of (px.data(), 16, 8, 4);
        check (g.width == 4 && g.height == 2, "the window's mask is every fourth pixel");
        check (g.open (0.1f, 0.5f), "a press on the open window is the glass's");
        check (! g.open (0.9f, 0.5f), "a press on the panel's metal is not");
        check (UnderwaterWorld::Glass().open (0.9f, 0.5f), "with no dashboard, everywhere is glass");
    }
}

int main (int argc, char** argv)
{
    makeEntries();
    compareWithFixture (argc > 1 ? argv[1] : UNDERWATER_FIXTURE);
    glassChecks();
    if (failures == 0)
        std::printf ("underwater_world_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
