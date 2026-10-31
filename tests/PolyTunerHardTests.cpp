// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The polyphonic tuner against strings that are NOT its model. PolyTunerTests renders exactly what the
// analysis assumes (partials on the stiff-string formula, one exponential each, a clean start); real strings
// do none of that, and a gate in the analysis's own image passes whatever the analysis gets wrong outside it.
// This renderer is written apart from the analysis, and breaks its assumptions on purpose:
//
//   - partials stray off the stiff-string formula by 1.5 cents (sd), as wound strings and their ends make them;
//   - two planes of vibration: half the strings carry a second, weaker, shorter-lived copy of every partial a
//     fraction of a hertz above — they beat;
//   - each partial dies away in two stages, a loud prompt sound and a long aftersound, not one exponential;
//   - the pick: a burst of noise at every string's onset, and the pickup's comb over the partials;
//   - the pitch glides down after the pluck, by up to 20 cents and as slowly as 0.4 s;
//   - mains hum to the 12th harmonic, learned in the quiet before the strum, or arriving WITH the strum, or
//     already learned when a second strum comes over the first one's tail; strings standing on mains lines.
//
// Every reading the tuner's timer produces is judged — not the last one: a MUTED string is never NEAR or FAR,
// and a published number is within a cent of the truth (the string's first partial at the moment of the
// reading; between a doublet's planes) — or there is none. Withholding is allowed and counted, under a ceiling.
//
// POLY_HARD_SEEDS=<n> runs n seeds per scene and tuning instead of the gate's two. JUCE-free, links nothing.

#include "core/PolyTuner.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using orbitamp::core::PolyTuner;
using Heard = PolyTuner::Heard;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double fs = 48000.0;

    struct Dice
    {
        std::uint64_t s;
        double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (double) (s >> 11) / 9007199254740992.0; }
        double in (double a, double b) { return a + (b - a) * next(); }
        double normal() { const double u = std::max (1.0e-12, next()), v = next(); return std::sqrt (-2.0 * std::log (u)) * std::cos (2.0 * pi * v); }
    };

    struct Str
    {
        double f1 = 0.0;          // the truth: the first partial of the main plane, at rest
        bool   on = true;
        double level = 0.1;       // the main plane's first partial, linear
        double stiffness = 1e-4;  // nominal; every partial then strays off it
        double split = 0.0;       // Hz: a second plane above the first (0: one plane)
        double pluck = 0.15, pickup = 0.2;
        double ring = 3.0;        // the aftersound's decay of the first partial, s
    };

    struct Scene
    {
        std::vector<Str> strings;
        double strumAt = 0.8, spread = 0.012, restrumAt = -1.0;
        double glide = 10.0, glideTau = 0.15;   // cents at the pluck, s
        double humHz = 0.0, humDb = -42.0, humFrom = 0.0;
        double stray = 1.5;                     // cents, sd, every partial above the first off the formula
        double seconds = 3.0;
        std::uint64_t seed = 1;
    };

    double ratioOf (double cents) { return std::pow (2.0, cents / 1200.0); }

    /** One pluck of one string, added into `y`, struck at `at`. */
    void pluckInto (std::vector<double>& y, const Scene& sc, const Str& st, double at, Dice& dice)
    {
        const int n0 = (int) (at * fs), n = (int) y.size();
        const double g = ratioOf (sc.glide) - 1.0;
        for (int h = 1; h <= 30; ++h)
        {
            double fh = h * st.f1 * std::sqrt ((1.0 + st.stiffness * h * h) / (1.0 + st.stiffness));
            if (h > 1)
                fh *= ratioOf (sc.stray * dice.normal());
            if (fh > 1500.0)
                break;
            const double comb = std::abs (std::sin (pi * h * st.pluck)) * std::abs (std::sin (pi * h * st.pickup))
                              / (std::abs (std::sin (pi * st.pluck)) * std::abs (std::sin (pi * st.pickup)) + 1e-9);
            const double amp  = st.level * comb / h;
            const double slow = st.ring / (1.0 + fh / 500.0), fast = 0.12;
            for (int plane = 0; plane < (st.split > 0.0 ? 2 : 1); ++plane)
            {
                const double fp = fh + plane * st.split;
                const double ap = plane == 0 ? amp : amp * 0.7;
                const double ts = plane == 0 ? slow : slow * 0.6;
                const double ph = dice.in (0.0, 2.0 * pi);
                std::complex<double> zs, zf;
                for (int b = n0; b < n; b += 32)
                {
                    const double t = (b - n0) / fs;
                    const double phase = 2.0 * pi * fp * (t + g * sc.glideTau * (1.0 - std::exp (-t / sc.glideTau))) + ph;
                    const double fNow  = fp * (1.0 + g * std::exp (-(t + 16.0 / fs) / sc.glideTau));
                    zs = std::polar (0.6 * ap * std::exp (-t / ts), phase);   // the aftersound
                    zf = std::polar (0.4 * ap * std::exp (-t / fast), phase); // the prompt sound
                    const std::complex<double> rs = std::polar (std::exp (-1.0 / (ts * fs)), 2.0 * pi * fNow / fs);
                    const std::complex<double> rf = std::polar (std::exp (-1.0 / (fast * fs)), 2.0 * pi * fNow / fs);
                    for (int k = b; k < std::min (n, b + 32); ++k)
                    {
                        y[(size_t) k] += (zs + zf).imag();
                        zs *= rs;
                        zf *= rf;
                    }
                }
            }
        }
        // The pick: 4 ms of noise, a click every string makes.
        for (int k = n0; k < std::min (n, n0 + (int) (0.004 * fs)); ++k)
            y[(size_t) k] += st.level * 0.5 * dice.in (-1.0, 1.0) * (1.0 - (k - n0) / (0.004 * fs));
    }

    std::vector<float> render (const Scene& sc)
    {
        std::vector<double> y ((size_t) (sc.seconds * fs), 0.0);
        Dice dice { sc.seed * 0x9E3779B97F4A7C15ull + 1u };
        for (size_t i = 0; i < sc.strings.size(); ++i)
            if (sc.strings[i].on)
            {
                pluckInto (y, sc, sc.strings[i], sc.strumAt + sc.spread * (double) i, dice);
                if (sc.restrumAt > 0.0)
                    pluckInto (y, sc, sc.strings[i], sc.restrumAt + sc.spread * (double) i, dice);
            }
        if (sc.humHz > 0.0)
            for (int k = 1; k <= 12; ++k)
            {
                const double a = std::pow (10.0, sc.humDb / 20.0) / k, ph = dice.in (0.0, 2.0 * pi);
                for (size_t i = (size_t) (sc.humFrom * fs); i < y.size(); ++i)
                    y[i] += a * std::sin (2.0 * pi * k * sc.humHz * (double) i / fs + ph);
            }
        std::vector<float> out (y.size());
        for (size_t i = 0; i < y.size(); ++i)
            out[i] = (float) (y[i] + 3.0e-5 * dice.in (-1.0, 1.0));   // hiss, -90 dBFS
        return out;
    }

    double noteHz (const PolyTuner::Tuning& t, int s) { return 440.0 * std::pow (2.0, (t.midi[(size_t) s] - 69) / 12.0); }

    Scene strum (const PolyTuner::Tuning& t, std::uint64_t seed)
    {
        Scene sc;
        sc.seed = seed;
        Dice dice { seed * 7919u + 3u };
        const bool bass = t.id[0] == 'b';
        for (int s = 0; s < t.strings; ++s)
        {
            Str st;
            st.f1        = noteHz (t, s) * ratioOf (dice.in (-40.0, 40.0));
            st.level     = 0.1 * std::pow (10.0, dice.in (-18.0, 0.0) / 20.0);
            st.stiffness = bass ? dice.in (5e-5, 4e-4) : dice.in (1e-5, 1.2e-4);
            st.split     = dice.next() < 0.5 ? dice.in (0.05, 0.4) : 0.0;
            st.pluck     = dice.in (0.08, 0.25);
            st.pickup    = dice.in (0.05, 0.25);
            st.ring      = bass ? dice.in (3.0, 6.0) : dice.in (2.0, 4.0);
            sc.strings.push_back (st);
        }
        sc.spread   = dice.in (0.008, 0.02);
        sc.glide    = dice.in (0.0, 10.0);     // a strum's own; the slow 20 c settle is a scene of its own
        sc.glideTau = dice.in (0.05, 0.15);
        return sc;
    }

    struct Tally { int readings = 0, wrong = 0, withheld = 0, sounding = 0, mutedShown = 0; };

    /** Every reading of the tuner's timer (30 Hz ticks) over the scene, judged. */
    void judge (const char* what, const PolyTuner::Tuning& t, const Scene& sc, Tally& ty, int& printed)
    {
        static PolyTuner tuner;
        tuner.prepare (fs);
        tuner.setTuning (t);
        const auto x = render (sc);
        const double g = ratioOf (sc.glide) - 1.0;
        for (size_t k = 0; k < x.size(); k += 1600)
        {
            const int n = (int) std::min<size_t> (1600, x.size() - k);
            tuner.push (x.data() + k, n);
            if (! tuner.maybeAnalyse())
                continue;
            const double now = (double) (k + (size_t) n) / fs;
            if (now < sc.strumAt + 0.2)
                continue;
            ++ty.readings;
            for (int s = 0; s < t.strings; ++s)
            {
                const auto& o  = tuner.reading().string[(size_t) s];
                const auto& st = sc.strings[(size_t) s];
                const bool shown = o.heard == Heard::near || o.heard == Heard::far;
                // The pluck the reading is OF: a new strum is read no sooner than 1.25 s after it (the settle and
                // a second of ringing), so until then the span is the last strum's.
                const double at  = (sc.restrumAt > 0.0 && now > sc.restrumAt + 1.0 ? sc.restrumAt : sc.strumAt) + sc.spread * s;
                const double f   = st.f1 * (1.0 + g * std::exp (-(now - at) / sc.glideTau));
                double err = 0.0;
                if (shown)
                {
                    // Two planes sound as ONE partial whose pitch wanders with their beat: at its fade the sum
                    // runs below the stronger plane by 0.7/0.3 of their split (the weaker's share over the
                    // difference). That range is what the string sounds like — a strobe reads it too.
                    const double lo = 1200.0 * std::log2 (o.hz / (f - st.split * 0.7 / 0.3)), hi = 1200.0 * std::log2 (o.hz / (f + st.split));
                    err = lo >= 0.0 && hi <= 0.0 ? 0.0 : (std::abs (lo) < std::abs (hi) ? lo : hi);
                }
                bool bad = false;
                if (! st.on)
                {
                    bad = shown;
                    ty.mutedShown += shown ? 1 : 0;
                }
                else
                {
                    ++ty.sounding;
                    ty.withheld += shown ? 0 : 1;
                    bad = shown && std::abs (err) > 1.0;
                }
                if (bad)
                {
                    ++ty.wrong;
                    if (printed++ < 12)
                        std::printf ("  WRONG %s — %s seed %llu at %.2f s: string %d %s, true %+.2f c, read %+.2f c (err %+.2f, sd %.3f)\n",
                                     what, t.name, (unsigned long long) sc.seed, now, s + 1, st.on ? "sounding" : "MUTED",
                                     1200.0 * std::log2 (f / noteHz (t, s)), (double) o.cents, err, (double) o.sigmaCents);
                }
            }
        }
    }
}

int main()
{
    const int seeds = std::getenv ("POLY_HARD_SEEDS") != nullptr ? std::max (1, std::atoi (std::getenv ("POLY_HARD_SEEDS"))) : 2;
    int failures = 0, printed = 0;
    Tally all;

    // Withholding is allowed, not free: each scene has its ceiling — a plain strum's lowest (8 % of its
    // readings without the doublets; half the strings here carry a second plane, and those wait), a pitch
    // still settling or strings on the mains lines high, where waiting IS the right answer.
    auto scene = [&] (const char* what, double ceiling, auto make, int moreSeeds = 1)
    {
        Tally ty;
        for (const auto& t : PolyTuner::tunings)
            for (int i = 0; i < seeds * moreSeeds; ++i)
            {
                Scene sc = strum (t, 1000u + 97u * (std::uint64_t) i + (std::uint64_t) t.midi[0]);
                Dice dice { sc.seed + 5u };
                make (t, sc, dice);
                judge (what, t, sc, ty, printed);
            }
        const double share = 100.0 * ty.withheld / std::max (1, ty.sounding);
        std::printf ("%-52s %5d readings  %3d wrong  %5.1f%% withheld (want <= %.0f%%)\n", what, ty.readings, ty.wrong, share, ceiling);
        if (share > ceiling)
            ++failures;
        all.readings += ty.readings; all.wrong += ty.wrong; all.withheld += ty.withheld; all.sounding += ty.sounding;
    };

    const auto muteOne = [] (Scene& sc, Dice& dice) { sc.strings[(size_t) (dice.next() * (double) sc.strings.size())].on = false; };

    scene ("a strum, every string", 40.0, [&] (const PolyTuner::Tuning&, Scene&, Dice&) {});
    scene ("a strum, one string muted", 40.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) { muteOne (sc, d); });
    scene ("partials 2.5 c off, a top string muted (on harmonics)", 45.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
        // The top strings stand on the low strings' harmonics (a guitar's B on the low E's 3rd, its high E on
        // the 4th): muted, all that is left there is another string's partial, off its formula — the reading
        // a muted string must never borrow.
        sc.stray = 2.5;
        sc.strings[(size_t) (t.strings - 1 - (d.next() < 0.5 ? 0 : 1))].on = false; }, 4);   // the rare far stray is the case
    scene ("hum to the 12th, learned before: one muted", 50.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; muteOne (sc, d); });
    scene ("hum to the 12th, arriving with the strum: one muted", 60.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; sc.humFrom = sc.strumAt; muteOne (sc, d); });
    scene ("hum learned, then a strum over the last one's tail", 50.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; sc.restrumAt = sc.strumAt + 2.2; sc.seconds = 5.0; muteOne (sc, d); });
    scene ("the pitch settling slowly: 20 c, 0.4 s", 85.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.glide = 20.0; sc.glideTau = 0.4; sc.seconds = 4.0; muteOne (sc, d); });
    scene ("strings on mains lines, with and without the mains", 65.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
        // Each string within 40 c of its note that a 50 Hz harmonic can stand on, put there.
        for (int s = 0; s < t.strings; ++s)
        {
            const double k = std::round (noteHz (t, s) / 50.0);
            if (std::abs (1200.0 * std::log2 (k * 50.0 / noteHz (t, s))) < 40.0)
                sc.strings[(size_t) s].f1 = k * 50.0;
        }
        if (d.next() < 0.5)
            sc.humHz = 50.0;
    });

    const double share = 100.0 * all.withheld / std::max (1, all.sounding);
    std::printf ("\nall: %d readings, %d wrong (want 0), %.1f%% of sounding strings' readings withheld\n", all.readings, all.wrong, share);
    if (all.wrong > 0)
        ++failures;
    std::printf ("%s\n", failures == 0 ? "ALL OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
