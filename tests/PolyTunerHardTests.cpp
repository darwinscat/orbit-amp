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
//     already learned when a second strum comes over the first one's tail; strings standing on mains lines;
//     and a mains OFF its nominal: the grid wanders 49.9-50.1 Hz every day, further on events.
//
// Every reading the tuner's timer produces is judged — not the last one: a MUTED string is never NEAR or FAR,
// and a published number is within a cent of the truth or there is none. In a chord, within the needle's green band,
// 2.5 c: a string's partials crowded by the others' are read off its clear ones' model, the stray of a real partial off
// it the price — and a chord's strings past that band are counted and held to a share of the readings (chordOff). The truth is pointwise: the pitch the
// string sounds at the moment the reading's span ends — its partials' instantaneous frequencies under the band it is
// read in, together as the needle hears them (PolyTuner::heardAs; heardHz), as rendered:
// the glide where it is, and for two planes the phase of their SUM, which swings with their beat around the main
// plane's pitch. (Not the whole range of that swing: a number from the far side of the beat is a wrong number.)
// How far the shown numbers of doublet strings stand from the main plane alone is counted and printed: planes a
// tenth of a hertz apart beat over ten seconds, and no span of 1.7 s tells them apart (its bins are 0.6 Hz).
// Withholding is allowed and counted, under a ceiling.
//
// POLY_HARD_SEEDS=<n> runs n seeds per scene and tuning instead of the gate's two. JUCE-free, links nothing.

#include "core/PolyTuner.h"
#include "core/TunerEar.h"

#include <array>
#include <chrono>
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
        double ph1[2][2] {};      // as rendered: the first partial's phase, per pluck and plane
        // As rendered, per pluck: every partial's frequency (its stray in), its amplitude, and its phase per plane
        // — the truth is all of them (heardHz).
        double fh[2][31] {}, amp[2][31] {}, ph[2][2][31] {};
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

    /** One pluck of one string, added into `y`, struck at `at`; what was rendered into `st`, pluck `p`. */
    void pluckInto (std::vector<double>& y, const Scene& sc, Str& st, int p, double at, Dice& dice)
    {
        double* ph1 = st.ph1[p];
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
            st.fh[p][h]  = fh;
            st.amp[p][h] = amp;
            for (int plane = 0; plane < (st.split > 0.0 ? 2 : 1); ++plane)
            {
                const double fp = fh + plane * st.split;
                const double ap = plane == 0 ? amp : amp * 0.7;
                const double ts = plane == 0 ? slow : slow * 0.6;
                const double ph = dice.in (0.0, 2.0 * pi);
                if (h == 1)
                    ph1[plane] = ph;
                st.ph[p][plane][h] = ph;
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

    std::vector<float> render (Scene& sc)
    {
        std::vector<double> y ((size_t) (sc.seconds * fs), 0.0);
        Dice dice { sc.seed * 0x9E3779B97F4A7C15ull + 1u };
        for (size_t i = 0; i < sc.strings.size(); ++i)
            if (sc.strings[i].on)
            {
                pluckInto (y, sc, sc.strings[i], 0, sc.strumAt + sc.spread * (double) i, dice);
                if (sc.restrumAt > 0.0)
                    pluckInto (y, sc, sc.strings[i], 1, sc.restrumAt + sc.spread * (double) i, dice);
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

    /** The pitch string s sounds at `now`, s into the scene: its first partial's instantaneous frequency — every
        pluck of it, both planes, the prompt and the aftersound summed as rendered (pluckInto, h = 1). */
    double soundingHz (const Scene& sc, const Str& st, int s, double now)
    {
        const double g = ratioOf (sc.glide) - 1.0;
        std::complex<double> z, dz;
        for (int p = 0; p < (sc.restrumAt > 0.0 ? 2 : 1); ++p)
        {
            const double t = now - ((p == 0 ? sc.strumAt : sc.restrumAt) + sc.spread * s);
            if (t < 0.0)
                continue;
            for (int plane = 0; plane < (st.split > 0.0 ? 2 : 1); ++plane)
            {
                const double fp = st.f1 + plane * st.split, ap = plane == 0 ? st.level : st.level * 0.7;
                const double slow = st.ring / (1.0 + st.f1 / 500.0), ts = plane == 0 ? slow : slow * 0.6;
                const double theta = 2.0 * pi * fp * (t + g * sc.glideTau * (1.0 - std::exp (-t / sc.glideTau))) + st.ph1[p][plane];
                const double w = 2.0 * pi * fp * (1.0 + g * std::exp (-t / sc.glideTau));
                for (const auto& [a, tau] : { std::pair { 0.6 * ap, ts }, std::pair { 0.4 * ap, 0.12 } })
                {
                    const auto zz = std::polar (a * std::exp (-t / tau), theta);
                    z  += zz;
                    dz += zz * std::complex<double> (-1.0 / tau, w);
                }
            }
        }
        return (dz / z).imag() / (2.0 * pi);
    }

    /** The pitch string s sounds at `now` as the needle hears it (PolyTuner::heardAs): the mean of its partials' h-ths
        under `band`, each weighted by h^2 x its energy over the reading's span [from, now] under the analysis's Hann
        window (what its fitted
        amplitude stands for: two planes beat through it) — each partial's instantaneous frequency as soundingHz
        takes the first's: every pluck, both planes (`planes`), both stages — the string as the needle hears it alone. */
    double heardHz (const Scene& sc, const Str& st, int s, double now, double from, double band, int planes = 2)
    {
        constexpr int looks = 48;   // points across the span the energy is summed at
        const double g = ratioOf (sc.glide) - 1.0;
        double wsum = 0.0, lsum = 0.0;
        for (int h = 1; h <= 30; ++h)
        {
            if (st.fh[0][h] <= 0.0 || st.fh[0][h] > band)
                break;
            std::complex<double> z, dz;
            std::array<std::complex<double>, looks> zm {};
            for (int p = 0; p < (sc.restrumAt > 0.0 ? 2 : 1); ++p)
            {
                const double at = (p == 0 ? sc.strumAt : sc.restrumAt) + sc.spread * s;
                for (int plane = 0; plane < std::min (planes, st.split > 0.0 ? 2 : 1); ++plane)
                {
                    const double fp = st.fh[p][h] + plane * st.split, ap = plane == 0 ? st.amp[p][h] : st.amp[p][h] * 0.7;
                    const double slow = st.ring / (1.0 + st.fh[p][h] / 500.0), ts = plane == 0 ? slow : slow * 0.6;
                    for (int into = -1; into < looks; ++into)
                    {
                        const double t = (into < 0 ? now : from + (now - from) * (into + 0.5) / looks) - at;
                        if (t < 0.0)
                            continue;
                        const double theta = 2.0 * pi * fp * (t + g * sc.glideTau * (1.0 - std::exp (-t / sc.glideTau))) + st.ph[p][plane][h];
                        const double w = 2.0 * pi * fp * (1.0 + g * std::exp (-t / sc.glideTau));
                        for (const auto& [a, tau] : { std::pair { 0.6 * ap, ts }, std::pair { 0.4 * ap, 0.12 } })
                        {
                            const auto zz = std::polar (a * std::exp (-t / tau), theta);
                            if (into < 0)
                            {
                                z  += zz;
                                dz += zz * std::complex<double> (-1.0 / tau, w);
                            }
                            else
                                zm[(size_t) into] += zz;
                        }
                    }
                }
            }
            if (std::abs (z) <= 0.0)
                continue;
            double energy = 0.0;
            for (int j = 0; j < looks; ++j)
            {
                const double hann = 0.5 - 0.5 * std::cos (2.0 * pi * (j + 0.5) / looks);
                energy += hann * hann * std::norm (zm[(size_t) j]);
            }
            const double wt = energy * h * h;
            wsum += wt;
            lsum += wt * std::log ((dz / z).imag() / (2.0 * pi) / h);
        }
        return wsum > 0.0 ? std::exp (lsum / wsum) : soundingHz (sc, st, s, now);
    }

    constexpr double chordCents = orbitamp::core::TunerEar::inTuneCents;   // a chord's promise: the needle's green band

    struct Tally { int readings = 0, wrong = 0, chordOff = 0, withheld = 0, sounding = 0, mutedShown = 0, doubletShown = 0, doubletOff = 0; };

    // A chord's strings past the green band, counted, not failed one by one: a partial crowded by another string's
    // is read off its string's model, and a partial strayed 10-13 c (the scenes' rare far stray) where it cannot be
    // measured moves the string with it. Held to this share of the readings (measured 1.4 % over the gate's seeds).
    constexpr double chordOffCeiling = 2.0;

    double worstTickMs = 0.0;   // the longest maybeAnalyse() of any tick — the message thread's stall

    /** Every reading of the tuner's timer (30 Hz ticks) over the scene, judged. */
    void judge (const char* what, const PolyTuner::Tuning& t, Scene sc, Tally& ty, int& printed)
    {
        static PolyTuner tuner;
        tuner.prepare (fs);
        tuner.setTuning (t);
        const auto x = render (sc);
        int sounding = 0;
        for (const auto& st : sc.strings)
            sounding += st.on ? 1 : 0;
        const bool chord = sounding > 1;
        for (size_t k = 0; k < x.size(); k += 1600)
        {
            const int n = (int) std::min<size_t> (1600, x.size() - k);
            tuner.push (x.data() + k, n);
            const auto t0 = std::chrono::steady_clock::now();
            const bool fresh = tuner.maybeAnalyse();
            worstTickMs = std::max (worstTickMs, 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
            if (! fresh)
                continue;
            // The truth at the moment the reading's span ENDS: on the timer the analysis runs a step at a time,
            // and the reading comes a few ticks later than the audio it rests on.
            const double now = tuner.reading().endSec;
            if (now < sc.strumAt + 0.2)
                continue;
            ++ty.readings;
            for (int s = 0; s < t.strings; ++s)
            {
                const auto& o  = tuner.reading().string[(size_t) s];
                const auto& st = sc.strings[(size_t) s];
                const bool shown = o.heard == Heard::near || o.heard == Heard::far;
                // The main plane alone, for the doublets' count.
                const double from = now - 1.0e-3 * tuner.reading().spanMs, band = tuner.readBandHz();
                const double f   = heardHz (sc, st, s, now, from, band, 1);
                const double truth = heardHz (sc, st, s, now, from, band);
                // Pointwise, against what the string sounds at the span's end (two planes: their sum's pitch).
                const double err = shown && st.on ? 1200.0 * std::log2 (o.hz / truth) : 0.0;
                if (shown && st.on && st.split > 0.0)
                {
                    ++ty.doubletShown;
                    ty.doubletOff += std::abs (1200.0 * std::log2 (o.hz / f)) > 1.0 ? 1 : 0;   // against the main plane alone
                }
                bool bad = false, off = false;
                if (! st.on)
                {
                    bad = shown;
                    ty.mutedShown += shown ? 1 : 0;
                }
                else
                {
                    ++ty.sounding;
                    ty.withheld += shown ? 0 : 1;
                    off = chord && shown && std::abs (err) > chordCents;
                    bad = ! chord && shown && std::abs (err) > 1.0;
                }
                ty.chordOff += off ? 1 : 0;
                if (bad || off)
                {
                    ty.wrong += bad ? 1 : 0;
                    if (printed++ < 12)
                        std::printf ("  %s %s — %s seed %llu at %.2f s: string %d %s, true %+.2f c, read %+.2f c (err %+.2f, sd %.3f)\n",
                                     bad ? "WRONG" : "OFF", what, t.name, (unsigned long long) sc.seed, now, s + 1, st.on ? "sounding" : "MUTED",
                                     1200.0 * std::log2 ((st.on ? truth : f) / noteHz (t, s)), (double) o.cents, err,
                                     (double) o.sigmaCents);
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
    // still settling or strings on the mains lines high, where waiting IS the right answer. A number is published
    // only where the span's halves can see a cent and agree with the whole to half of one, with their
    // disagreement in its sigma: the bass's halves (a few periods of a 31-55 Hz string each) often cannot.
    // Availability is not the promise — correctness is ("0 wrong" below is the gate's assertion). Each ceiling is
    // the share measured over 24 seeds (POLY_HARD_SEEDS=24) plus 5 points, so that any seed count stays under it.
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
        std::printf ("%-52s %5d readings  %3d wrong  %3d off  %5.1f%% withheld (want <= %.0f%%)  doublets shown %d, >1 c off the main plane %d\n",
                     what, ty.readings, ty.wrong, ty.chordOff, share, ceiling, ty.doubletShown, ty.doubletOff);
        if (share > ceiling)
            ++failures;
        all.readings += ty.readings; all.wrong += ty.wrong; all.chordOff += ty.chordOff; all.withheld += ty.withheld; all.sounding += ty.sounding;
        all.doubletShown += ty.doubletShown; all.doubletOff += ty.doubletOff;
    };

    const auto muteOne = [] (Scene& sc, Dice& dice) { sc.strings[(size_t) (dice.next() * (double) sc.strings.size())].on = false; };

    scene ("a strum, every string", 55.0, [&] (const PolyTuner::Tuning&, Scene&, Dice&) {});
    scene ("a strum, one string muted", 50.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) { muteOne (sc, d); });
    scene ("partials 2.5 c off, a top string muted (on harmonics)", 57.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
        // The top strings stand on the low strings' harmonics (a guitar's B on the low E's 3rd, its high E on
        // the 4th): muted, all that is left there is another string's partial, off its formula — the reading
        // a muted string must never borrow.
        sc.stray = 2.5;
        sc.strings[(size_t) (t.strings - 1 - (d.next() < 0.5 ? 0 : 1))].on = false; }, 4);   // the rare far stray is the case
    scene ("hum to the 12th, learned before: one muted", 58.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; muteOne (sc, d); });
    scene ("hum to the 12th, arriving with the strum: one muted", 71.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; sc.humFrom = sc.strumAt; muteOne (sc, d); });
    scene ("hum learned, then a strum over the last one's tail", 59.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.humHz = d.next() < 0.5 ? 50.0 : 60.0; sc.restrumAt = sc.strumAt + 2.2; sc.seconds = 5.0; muteOne (sc, d); });
    scene ("the pitch settling slowly: 20 c, 0.4 s", 95.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.glide = 20.0; sc.glideTau = 0.4; sc.seconds = 4.0; muteOne (sc, d); });
    scene ("the pitch settling: 20 c, 0.2 s", 75.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        sc.glide = 20.0; sc.glideTau = 0.2; sc.seconds = 4.0; muteOne (sc, d); });
    scene ("strings on mains lines, with and without the mains", 69.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
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

    // The mains off its nominal, to the 12th: every line of it off its nominal multiple by k times the drift,
    // learned before the strum or arriving with it. The string muted is the one a mains line stands nearest
    // (within reach of its note, not on it): what is left there is the hum's line.
    const auto offMains = [] (const PolyTuner::Tuning& t, Scene& sc, Dice& d)
    {
        static constexpr double grid[] = { 49.8, 49.85, 50.1, 50.2, 60.1 };
        sc.humHz = grid[(size_t) (d.next() * 5.0)];
        sc.stray = 1.5;
        int muted = 0;
        double best = 1.0e9;
        for (int s = 0; s < t.strings; ++s)
            for (int k = 1; k <= 12; ++k)
            {
                const double c = std::abs (1200.0 * std::log2 (k * sc.humHz / noteHz (t, s)));
                if (c > 5.0 && c < best)
                {
                    best = c;
                    muted = s;
                }
            }
        sc.strings[(size_t) muted].on = false;
    };
    scene ("mains off nominal (49.8-60.1 Hz), learned before", 58.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
        offMains (t, sc, d); }, 2);
    scene ("loud hum (-30 dBFS, 50.0/50.1 Hz) arriving with it", 75.0, [&] (const PolyTuner::Tuning&, Scene& sc, Dice& d) {
        // Hands off the strings on a noisy single coil: a mains line louder than a string's own first partial, never
        // learned — it came with the strum.
        sc.humHz = d.next() < 0.5 ? 50.0 : 50.1; sc.humDb = -30.0; sc.humFrom = sc.strumAt; muteOne (sc, d); }, 2);
    scene ("mains off nominal (49.8-60.1 Hz), arriving with it", 66.0, [&] (const PolyTuner::Tuning& t, Scene& sc, Dice& d) {
        offMains (t, sc, d); sc.humFrom = sc.strumAt; }, 2);

    // The tuner opened onto a string still ringing on a mains line — a bass G on 100 Hz, the 50 Hz mains's 2nd —
    // alone or in its chord, dying away slowly (7.6 s, 15 s): no strum to measure the quiet against, and the line
    // steady to a fraction of a dB over the stretch. It is not the mains, and must not be learned as it.
    {
        const PolyTuner::Tuning* bass4 = nullptr;
        for (const auto& t : PolyTuner::tunings)
            if (std::string (t.name) == "Bass 4 strings")
                bass4 = &t;
        static PolyTuner tuner;
        bool ok = bass4 != nullptr;
        for (const double tau : { 7.6, 15.0 })
            for (const bool chord : { false, true })
            {
                if (bass4 == nullptr)
                    break;
                Scene sc = strum (*bass4, 4242u);
                for (auto& st : sc.strings)
                {
                    st.on = chord;
                    st.split = 0.0;
                }
                auto& g = sc.strings[3];
                g.on = true;
                g.f1 = 100.0;
                g.ring = tau * (1.0 + 100.0 / 500.0);   // the first partial's aftersound dies with tau
                sc.strumAt = 0.05;
                sc.glide = 0.0;
                const auto x = render (sc);
                tuner.prepare (fs);
                tuner.setTuning (*bass4);
                for (size_t k = (size_t) (0.35 * fs); k < x.size(); k += 1600)   // opened 0.3 s after the strum
                {
                    tuner.push (x.data() + k, (int) std::min<size_t> (1600, x.size() - k));
                    tuner.maybeAnalyse();
                    ok = ok && ! ((tuner.mainsMask() >> 2) & 1u);
                }
            }
        std::printf ("%-52s %s\n", "opened onto a string ringing on 100 Hz: not the mains", ok ? "ok" : "FAIL");
        if (! ok)
            ++failures;
    }

    // A plain string, nearly without stiffness, its partials on the harmonic series to a tenth of a cent, sustaining
    // long (its first three partials die slower than 8 s) and alone — a B off any mains line. It is a string, not a
    // hum: read, not refused as an exact series that never dies.
    {
        const auto& t = PolyTuner::tunings[0];
        Scene sc = strum (t, 4343u);
        for (auto& st : sc.strings)
            st.on = false;
        auto& b = sc.strings[4];
        b.on = true;
        b.f1 = noteHz (t, 4) * ratioOf (-10.0);
        b.stiffness = 1.0e-5;
        b.split = 0.0;
        b.ring = 22.0;
        b.level = 0.05;
        sc.stray = 0.0;
        sc.glide = 0.0;
        sc.seconds = 4.0;
        Tally ty;
        int printed0 = 0;
        judge ("a plain string sustaining long, alone", t, sc, ty, printed0);
        const bool ok = ty.wrong == 0 && ty.withheld * 2 < ty.sounding;
        std::printf ("%-52s %s (%d of %d readings withheld)\n", "a plain string sustaining long, alone: read", ok ? "ok" : "FAIL",
                     ty.withheld, ty.sounding);
        if (! ok)
            ++failures;
    }

    // The timer stalls (a window dragged, the message thread busy) while an analysis is in flight, and a new chord
    // is struck under the stall, 20 c sharper: the tap is lapped, what came before the gap is not what comes after
    // it. The span in flight is the old chord's — no reading may come of it; the next one rests on audio after the gap.
    {
        const auto& t = PolyTuner::tunings[0];
        Scene a = strum (t, 1040u), b = a;
        for (auto& st : a.strings) st.split = 0.0;
        a.glide = 0.0;
        a.seconds = 7.0;
        b = a;
        b.strumAt = 2.2;
        for (auto& st : b.strings) st.f1 *= ratioOf (20.0);
        auto x = render (a);
        const auto y = render (b);
        for (size_t i = (size_t) (2.2 * fs); i < x.size(); ++i)
            x[i] = y[i];   // the new chord over silence where the old one was: what the player hears after the stall
        static orbitamp::core::TunerTap tap;
        static PolyTuner tuner;
        tuner.prepare (fs);
        tuner.setTuning (t);
        size_t k = 0;
        bool armed = false;
        for (; k + 1600 <= x.size() && ! armed; k += 1600)
        {
            tap.write (x.data() + k, 1600);
            tuner.update (tap);
            armed = tuner.reading().strum > 0 && tuner.pending();   // a reading came, and the next one is in flight
        }
        const double gapAt = (double) k / fs;
        tap.write (x.data() + k, 48000);   // a second's stall: the tap laps the history
        k += 48000;
        bool ok = armed;
        for (; k + 1600 <= x.size(); k += 1600)
        {
            const bool fresh = tuner.update (tap);
            tap.write (x.data() + k, 1600);
            ok = ok && ! (fresh && tuner.reading().endSec < gapAt + 0.5);
        }
        std::printf ("%-52s %s\n", "a stall over a new chord: no reading of the old span", ok ? "ok" : "FAIL");
        if (! ok)
            ++failures;
    }

    const double share = 100.0 * all.withheld / std::max (1, all.sounding);
    const double offShare = 100.0 * all.chordOff / std::max (1, all.readings);
    std::printf ("\nall: %d readings, %d wrong (want 0), %d chord strings past the green band (%.1f%% of readings, want <= %.0f%%), "
                 "%.1f%% of sounding strings' readings withheld; worst tick %.1f ms\n",
                 all.readings, all.wrong, all.chordOff, offShare, chordOffCeiling, share, worstTickMs);
    if (all.wrong > 0 || offShare > chordOffCeiling)
        ++failures;
    std::printf ("%s\n", failures == 0 ? "ALL OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
