// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for the DRIVE's pedal itself (src/core/DrivePedal.h), JUCE-free:
//
//   - the feedback's diodes: one Newton step a sample stays within 1 mV of the converged solve;
//   - each type's small-signal shape: TS's a rising shelf (2 kHz 12 dB and more over 100 Hz from GAIN 3 up) whose leg
//     is 3 dB down at 720 Hz; PRECISION's leg corner moving with ATTACK and the clean leg left alone; 33's low cut
//     moving with TIGHT, and the 33 growing 1 dB per dB of GAIN under its knee;
//   - the clip on a sine: TS and PRECISION symmetric (odd harmonics only), SD-1 asymmetric (even harmonics too);
//     TS and PRECISION within 1.5 dB from GAIN 3 to 10 — the clamp;
//   - every type delays by the same 63 samples;
//   - THE LEVEL HANDED ON: VOLUME is that many dB, eased in about 10 ms, no click; at noon a guitar's phrases come out
//     about as loud as they went in (BS.1770) at every GAIN, every type — the trims, fitted on the reference DI at
//     -18 dBFS peak by tests/DriveMatch.cpp to 0.3 LU, here on phrases of our own at that peak;
//   - at full GAIN and VOLUME, a -6 dBFS 2 kHz sine and a 100 Hz + 2 kHz pair, a guitar's phrase up to 0 dBFS: never
//     past -1 dBFS; and +60 dBFS, NaN, infinity and DC in: finite, under the ceiling, no denormal;
//   - silence in, silence out — exactly zero, and back to exactly zero after playing;
//   - a change of type and a jump of GAIN are no clicks;
//   - what it costs, said.
//
// main() returns 0 when every check held.

#include "core/DrivePedal.h"
#include "Loudness.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

using orbitamp::core::DrivePedal;
using Type = DrivePedal::Type;

namespace
{
    int failed = 0;

    void check (bool ok, const char* what, double value = std::numeric_limits<double>::quiet_NaN())
    {
        if (std::isnan (value))
            std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        else
            std::printf ("%s  %s  (%.3f)\n", ok ? "ok  " : "FAIL", what, value);
        if (! ok)
            ++failed;
    }

    constexpr double rate = 48000.0;
    constexpr double pi = 3.14159265358979323846;
    const char* const names[] = { "TS", "PRECISION", "33", "SD-1" };

    DrivePedal::Settings at (Type t, float gain, float eq = 5.0f)
    {
        DrivePedal::Settings s;
        s.type = t;
        s.gain = gain;
        s.tone = s.attack = s.tight = s.bright = eq;
        return s;
    }

    /** What `run` plays: the shipping voicing, unless a check says otherwise (and puts it back). */
    DrivePedal::Voicing voicing;

    /** `n` samples of `in` through a fresh pedal at `s`, in blocks of 256. */
    std::vector<float> run (const DrivePedal::Settings& s, const std::function<float (int)>& in, int n, long long* guarded = nullptr)
    {
        DrivePedal p;
        p.voicing = voicing;
        p.prepare (rate);
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = in (i);
        for (int i = 0; i < n; i += 256)
            p.process (v.data() + i, std::min (256, n - i), s);
        if (guarded != nullptr)
            *guarded += p.guarded();
        return v;
    }

    std::complex<double> binAt (const std::vector<float>& v, double hz, size_t from, size_t len)
    {
        std::complex<double> acc;
        for (size_t i = from; i < from + len && i < v.size(); ++i)
            acc += (double) v[i] * std::polar (1.0, -2.0 * pi * hz * (double) i / rate);
        return acc * (2.0 / (double) len);
    }

    double db (double g) { return 20.0 * std::log10 (std::abs (g) + 1.0e-30); }

    double levelAt (const std::vector<float>& v, double hz, size_t from, size_t len) { return db (std::abs (binAt (v, hz, from, len))); }

    /** The transfer at `hz` at -100 dBFS, where every clip is still a line. */
    std::complex<double> transfer (const DrivePedal::Settings& s, double hz)
    {
        const double a = 1.0e-5;
        const auto out = run (s, [&] (int i) { return (float) (a * std::sin (2.0 * pi * hz * i / rate)); }, 48000);
        const size_t len = (size_t) (std::floor (24000.0 * hz / rate) * rate / hz);   // whole periods
        return binAt (out, hz, 24000, std::max<size_t> (len, 1000)) / a;
    }

    double smallSignal (const DrivePedal::Settings& s, double hz) { return db (std::abs (transfer (s, hz))); }

    float peakOf (const std::vector<float>& v, size_t from = 0)
    {
        float p = 0.0f;
        for (size_t i = from; i < v.size(); ++i)
            p = std::max (p, std::abs (v[i]));
        return p;
    }

    double rmsDb (const std::vector<float>& v, size_t from)
    {
        double e = 0.0;
        for (size_t i = from; i < v.size(); ++i)
            e += (double) v[i] * v[i];
        return 10.0 * std::log10 (e / (double) (v.size() - from) + 1.0e-30);
    }

    /** A guitar's phrase peaking at `peak`: strums of an open E, a palm-muted low power chord, a lead line. */
    std::vector<float> phrase (int kind, double peak, int n)
    {
        std::vector<float> v ((size_t) n);
        static constexpr double chords[3][4] = { { 82.41, 123.47, 164.81, 207.65 }, { 82.4, 123.5, 164.8, 0 },
                                                 { 329.6, 392.0, 440.0, 493.9 } };
        double top = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / rate, every = kind == 1 ? 0.125 : 0.25, since = std::fmod (t, every);
            const int step = (int) (t / every);
            double s = 0.0;
            for (int k = 0; k < 4; ++k)
            {
                const double f = kind == 2 ? chords[2][step % 4] : chords[kind][k];
                if (f <= 0.0 || (kind == 2 && k > 0))
                    continue;
                for (int h = 1; h <= 8; ++h)
                    s += std::exp (-since * (kind == 1 ? 18.0 : 3.0 + 2.0 * h)) / h * std::sin (2.0 * pi * f * h * since + k);
            }
            v[(size_t) i] = (float) s;
            top = std::max (top, std::abs (s));
        }
        for (auto& x : v)
            x = (float) (x * peak / top);
        return v;
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::printf ("The DRIVE's pedal\n");

    // THE DIODES: one Newton step a sample, from the last, against the converged solve — a full-drive leg's current at
    // eight times 48 kHz: a 2 kHz sine at the clip's hardest, and a pluck.
    {
        using D = DrivePedal::Diode;
        double worst = 0.0;
        for (const bool asym : { false, true })
            for (const double rf : { 4700.0 * 99.0, 6580.0 * 157.5, 4700.0 * 5.0 })
                for (int kind = 0; kind < 2; ++kind)
                {
                    double v = 0.0;
                    for (int j = 0; j < 384000 / 10; ++j)
                    {
                        const double t = j / 384000.0;
                        const double volts = kind == 0 ? 1.0 * std::sin (2.0 * pi * 2000.0 * t)
                                                       : 2.0 * std::exp (-t * 30.0) * std::sin (2.0 * pi * 330.0 * t) * (j > 100 ? 1.0 : 0.0);
                        const double i = volts / 4700.0;
                        v = D::step (i, rf, v, asym);
                        worst = std::max (worst, std::abs (v - D::solve (i, rf, asym)));
                    }
                }
        check (worst < 1.0e-3, "the diodes: Newton from the last sample (a step, more only where it moved 1 mV) within 1 mV of the converged solve, mV", worst * 1000.0);
    }

    // SMALL-SIGNAL SHAPES — the circuit's, before the trim at VOLUME noon (a gain that moves with GAIN) takes its level.
    {
        voicing.trimDb = {};
        // THE CLIPPING STAGE ALONE, out of the whole pedal: two GAINs low enough that the 51 pF stays out of the band —
        // their difference is the leg through the tone network after the sum, and that network alone (the clean leg's
        // path) is the first less k0 times it. The stage is then the transfer over the network.
        const DrivePedal::Voicing voice;
        const double k0 = std::pow (10.0, voice.tsLegLoDb / 20.0) - 1.0,
                     k1 = std::pow (10.0, (voice.tsLegLoDb + (voice.tsLegHiDb - voice.tsLegLoDb) * 0.2) / 20.0) - 1.0;
        const auto network = [&] (double hz, std::complex<double>& legThere)
        {
            const auto h0 = transfer (at (Type::ts, 0.0f), hz), h1 = transfer (at (Type::ts, 2.0f), hz);
            const auto d = h1 - h0;
            const auto net = h0 - k0 * d / (k1 - k0);
            legThere = d / ((k1 - k0) * net);
            return net;
        };

        // TS's stage is a rising shelf — 2 kHz over 100 Hz, 12 dB and more from GAIN 5 up.
        {
            std::complex<double> legLow, legHigh;
            const auto netLow = network (100.0, legLow), netHigh = network (2000.0, legHigh);
            bool shelf = true;
            for (const float g : { 0.0f, 3.0f, 5.0f, 10.0f })
            {
                const double rise = db (std::abs (transfer (at (Type::ts, g), 2000.0) / netHigh))
                                  - db (std::abs (transfer (at (Type::ts, g), 100.0) / netLow));
                std::printf ("     TS's clipping stage, GAIN %.0f: 2 kHz over 100 Hz %+.1f dB\n", (double) g, rise);
                shelf = shelf && rise >= (g >= 5.0f ? 12.0 : g >= 3.0f ? 10.0 : 3.0);
            }
            check (shelf, "TS's clipping stage is a rising shelf: 2 kHz over 100 Hz 12 dB and more from GAIN 5 up, 10 at 3, 3 at 0");
        }

        // ...its leg 3 dB down at 720 Hz.
        {
            std::complex<double> at720, at2880;
            (void) network (720.0, at720);
            (void) network (2880.0, at2880);
            const double under = db (std::abs (at2880)) - db (std::abs (at720));
            check (std::abs (under - 3.0) <= 0.5, "...its leg 3 dB (+-0.5) under its plateau at 720 Hz, dB", under);
        }

        // ...and at a DI's level (-18 and -12 dBFS), TONE noon, GAIN 0 and noon: where its peak stands and the skirts
        // against it — said, for the owner's measured TS (its Tone noon: the peak 714-734 Hz, 100 Hz -4 to -6, 300 Hz
        // -1.4, 3 kHz -5 to -6, 6 kHz -10 to -11).
        for (const float g : { 0.0f, 5.0f })
            for (const double lvl : { 0.0316, 0.125, 0.25 })
            {
                const auto shape = [&] (double hz)
                {
                    const auto v = run (at (Type::ts, g), [&] (int i) { return (float) (lvl * std::sin (2.0 * pi * hz * i / rate)); }, 24000);
                    const size_t len = (size_t) (std::floor (12000.0 * hz / rate) * rate / hz);
                    return levelAt (v, hz, 12000, std::max<size_t> (len, 1000));
                };
                double best = -200.0, where = 0.0;
                for (double hz = 200.0; hz <= 3000.0; hz *= std::pow (2.0, 1.0 / 12.0))
                    if (const double l = shape (hz); l > best) { best = l; where = hz; }
                std::printf ("     TS GAIN %.0f TONE noon at %.0f dBFS: the peak at %.0f Hz; 100 Hz %+.1f, 300 Hz %+.1f, 3 kHz %+.1f, 6 kHz %+.1f dB\n",
                             (double) g, db (lvl), where, shape (100.0) - best, shape (300.0) - best, shape (3000.0) - best, shape (6000.0) - best);
            }

        // THE WHOLE PEDAL has the hump. TS at the level his sweeps' -24 dB rung reached his pedal (-30 dBFS), GAIN and
        // TONE noon, as he measured it: its maximum between 600 Hz and 1.25 kHz, 4 dB and more over 100 Hz (his: -3.9 at
        // Overdrive 0, -6.0 at noon) and 6 dB and more over 5 kHz. His pedal moved this window twice: from the small
        // signal to a DI's level, where it clips already, and its top from 1 kHz to 1.25 — his peak stands at 714 Hz but
        // flat, 1.2 kHz only 1 dB under it (-3 dB from 217 Hz to 1.9 kHz), and the match to his captures lands ours there. SD-1 at the small signal and a low GAIN, its maximum between 600 Hz and
        // 1.1 kHz, 6 dB over both.
        for (const auto t : { Type::ts, Type::sd1 })
        {
            const auto s = at (t, t == Type::ts ? 5.0f : 3.0f);
            const double a = t == Type::ts ? 0.0316 : 1.0e-5;
            const auto gainAt = [&] (double hz)
            {
                const auto v = run (s, [&] (int i) { return (float) (a * std::sin (2.0 * pi * hz * i / rate)); }, 24000);
                const size_t len = (size_t) (std::floor (12000.0 * hz / rate) * rate / hz);
                return levelAt (v, hz, 12000, std::max<size_t> (len, 1000)) - db (a);
            };
            double best = -200.0, where = 0.0;
            for (double hz = 300.0; hz <= 3000.0; hz *= std::pow (2.0, 1.0 / 12.0))
                if (const double g = gainAt (hz); g > best) { best = g; where = hz; }
            const double low = gainAt (100.0), high = gainAt (5000.0);
            std::printf ("     %s, TONE noon: 100 Hz %+.1f dB, the hump %+.1f dB at %.0f Hz, 5 kHz %+.1f dB\n", names[(int) t],
                         low, best, where, high);
            const double top = t == Type::ts ? 1250.0 : 1100.0;
            check (where >= 600.0 && where <= top && best - low >= (t == Type::ts ? 4.0 : 6.0) && best - high >= 6.0,
                   t == Type::ts ? "TS has the hump at -30 dBFS: its maximum between 600 Hz and 1.25 kHz, 4 dB over 100 Hz, 6 over 5 kHz, Hz"
                                 : "SD-1 has the hump: its maximum between 600 Hz and 1.1 kHz, 6 dB over 100 Hz and 5 kHz, Hz", where);
        }

        // PRECISION: ATTACK moves the LEG's corner — the leg at 200 Hz falls step by step from ATTACK 0 to 10 — and the
        // clean leg is left alone: at GAIN 0 the 60 Hz transfer does not move with it.
        {
            const auto branch = [] (float attack, double hz)
            {
                return db (std::abs (transfer (at (Type::precision, 10.0f, attack), hz) - transfer (at (Type::precision, 0.0f, attack), hz)));
            };
            double last = 1.0e9, first = 0.0, now = 0.0;
            bool falls = true;
            for (const float a : { 0.0f, 2.5f, 5.0f, 7.5f, 10.0f })
            {
                now = branch (a, 200.0);
                if (a <= 0.0f) first = now;
                falls = falls && now < last - 1.0;
                last = now;
                std::printf ("     PRECISION ATTACK %.1f: the leg into the clip at 200 Hz %+.1f dB\n", (double) a, now);
            }
            check (falls && first - now >= 15.0, "PRECISION: ATTACK moves the leg's low cut up, step by step (dB in all)", first - now);
            const double clean = std::abs (smallSignal (at (Type::precision, 0.0f, 0.0f), 60.0) - smallSignal (at (Type::precision, 0.0f, 10.0f), 60.0));
            check (clean < 0.6, "...and never the clean leg: 60 Hz at GAIN 0 the same at ATTACK 0 and 10, dB apart", clean);
        }

        // 33: TIGHT moves its low cut; at noon its -3 dB near 155 Hz; and under its knee it grows 1 dB per dB of GAIN.
        {
            double last = 1.0e9, first = 0.0, now = 0.0;
            bool falls = true;
            for (const float tight : { 0.0f, 5.0f, 10.0f })
            {
                const auto s = at (Type::thirtyThree, 5.0f, tight);
                now = smallSignal (s, 100.0) - smallSignal (s, 2000.0);
                if (tight <= 0.0f) first = now;
                falls = falls && now < last - 1.0;
                last = now;
            }
            check (falls && first - now >= 10.0, "33: TIGHT moves its low cut up, step by step (dB in all)", first - now);
            const auto s = at (Type::thirtyThree, 5.0f, 5.0f);
            const double ref = smallSignal (s, 2000.0);
            double corner = 0.0;
            for (double hz = 60.0; hz < 1000.0; hz *= std::pow (2.0, 1.0 / 24.0))
                if (smallSignal (s, hz) - ref >= -3.0) { corner = hz; break; }
            check (corner >= 135.0 && corner <= 180.0, "33: at TIGHT noon the low cut's -3 dB stands near 155 Hz, Hz", corner);
            const double grows = smallSignal (at (Type::thirtyThree, 8.0f), 1000.0) - smallSignal (at (Type::thirtyThree, 2.0f), 1000.0);
            voicing = {};
            check (std::abs (grows - 9.0) < 0.05, "33: GAIN 2 to 8 is 9 dB more out, 1 dB per dB, before the trim, dB", grows);
        }
    }

    // THE CLIP ON A SINE: 300 Hz at -12 dBFS, full GAIN; and the clamp, GAIN 3 against 10 on a strum.
    {
        const double f = 300.0;
        const auto harmonics = [&] (Type t, double* out)
        {
            const auto v = run (at (t, 10.0f), [&] (int i) { return (float) (0.25 * std::sin (2.0 * pi * f * i / rate)); }, 48000);
            const double fund = levelAt (v, f, 24000, 24000);
            for (int h = 2; h <= 7; ++h)
                out[h] = levelAt (v, f * h, 24000, 24000) - fund;
        };
        double ts[8] {}, pr[8] {}, sd[8] {};
        harmonics (Type::ts, ts);
        harmonics (Type::precision, pr);
        harmonics (Type::sd1, sd);
        std::printf ("     harmonics re the fundamental, dB:  TS 2nd %.0f 3rd %.0f 4th %.0f 5th %.0f;  SD-1 2nd %.0f 3rd %.0f 4th %.0f\n",
                     ts[2], ts[3], ts[4], ts[5], sd[2], sd[3], sd[4]);
        check (ts[3] > -40.0, "TS clips: its 3rd harmonic above -40 dB", ts[3]);
        check (std::max ({ ts[2], ts[4], ts[6] }) < -100.0, "TS's clip is symmetric: no even harmonic above -100 dB", std::max ({ ts[2], ts[4], ts[6] }));
        check (std::max (pr[2], pr[4]) < -100.0, "PRECISION's too", std::max (pr[2], pr[4]));
        check (sd[2] > -40.0, "SD-1's clip is asymmetric: its 2nd harmonic above -40 dB", sd[2]);

        voicing.trimDb = {};   // the clip's own clamp, before the trim
        for (const auto t : { Type::ts, Type::precision })
        {
            const auto note = [] (int i) { return (float) (0.125 * std::sin (2.0 * pi * 1000.0 * i / rate)); };   // -18 dBFS, held
            const double lo = rmsDb (run (at (t, 3.0f), note, 48000), 4800);
            const double hi = rmsDb (run (at (t, 10.0f), note, 48000), 4800);
            std::printf ("     %s on a held note at -18 dBFS: GAIN 3 %.1f dB RMS, GAIN 10 %.1f dB RMS\n", names[(int) t], lo, hi);
            check (std::abs (hi - lo) < 1.5, t == Type::ts ? "TS clamps: GAIN 3 to 10 within 1.5 dB, before the trim, dB"
                                                           : "PRECISION clamps: GAIN 3 to 10 within 1.5 dB, before the trim, dB",
                   hi - lo);
        }
        voicing = {};
    }

    // EVERY TYPE DELAYS BY THE SAME 63 SAMPLES: 63 is the oversampler's own round trip, the 33's line is as long — a
    // click at 1000 comes out of it loudest at 1063 — and the clipping types' clicks never come early, only leaned a
    // few samples on by the low-passes after their sum.
    {
        DrivePedal probe;
        probe.prepare (rate);
        bool same = probe.roundTrip() == DrivePedal::latencySamples();
        for (int t = 0; t < (int) Type::count; ++t)
        {
            const auto v = run (at ((Type) t, 0.0f, 10.0f), [] (int i) { return i == 1000 ? 0.01f : 0.0f; }, 4096);
            size_t where = 0;
            for (size_t i = 0; i < v.size(); ++i)
                if (std::abs (v[i]) > std::abs (v[where]))
                    where = i;
            std::printf ("     %s: the click out at %d\n", names[t], (int) where);
            const int late = (int) where - 1000 - DrivePedal::latencySamples();
            same = same && (t == 2 ? late == 0 : late >= 0 && late <= 3);
        }
        check (same && DrivePedal::latencySamples() == 63, "every type delays by the same 63 samples");
    }

    // THE LEVEL HANDED ON. VOLUME is that many dB (a quiet phrase, under the guard, past the ease); at noon a guitar's
    // phrases come out about as loud as they went in, every type at GAIN 0, 5 and 10.
    {
        bool exact = true;
        double worst = 0.0;
        for (int t = 0; t < (int) Type::count; ++t)
        {
            const auto quiet = phrase (0, 0.03, 48000);
            const auto with = [&] (float volume)
            {
                auto s = at ((Type) t, 5.0f);
                s.volume = volume;
                return rmsDb (run (s, [&] (int i) { return quiet[(size_t) i]; }, 48000), 4800);
            };
            const double noon = with (0.0f);
            for (const float volume : { -12.0f, -6.0f, 6.0f, 12.0f })
            {
                const double d = with (volume) - noon - volume;
                worst = std::max (worst, std::abs (d));
                exact = exact && std::abs (d) < 0.05;
            }
        }
        check (exact, "VOLUME -12, -6, +6, +12 is that many dB over noon, every type (worst dB off)", worst);

        // The trims are fitted on the reference DI at -18 dBFS peak to 0.3 LU (tests/DriveMatch.cpp `trims`); our
        // phrases at that peak are sustained sines, denser at a high GAIN than a guitar is, so here they land under it —
        // never over it by more than 1 LU, never 8 under: no block hands the amp more than it was given.
        double most = -100.0, least = 100.0;
        for (int t = 0; t < (int) Type::count; ++t)
            for (const float gain : { 0.0f, 5.0f, 10.0f })
                for (int kind = 0; kind < 3; ++kind)
                {
                    const auto v = phrase (kind, 0.125893, 96000);   // -18 dBFS peak
                    const auto out = run (at ((Type) t, gain), [&] (int i) { return v[(size_t) i]; }, 96000);
                    const double d = orbitamp::loudness::integrated (out, rate) - orbitamp::loudness::integrated (v, rate);
                    most = std::max (most, d);
                    least = std::min (least, d);
                }
        std::printf ("     at VOLUME noon, our own phrases at -18 dBFS peak (the reference's) out less in: %+.2f to %+.2f LU\n", least, most);
        check (most < 1.0 && least > -8.0, "...and at noon a guitar's phrases never come out louder than in, every type at GAIN 0, 5, 10 (most LU over)", most);

        // VOLUME thrown +12 to -12 mid-note on the 33 at noon (a line, so the level is VOLUME's alone): a third to nine
        // tenths of the way there 10 ms on, there within half a dB by 60 ms, no step past the steady ones'.
        DrivePedal p;
        p.prepare (rate);
        std::vector<float> v (48000 * 2);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (0.03 * std::sin (2.0 * pi * 1000.0 * (double) i / rate));
        auto s = at (Type::thirtyThree, 5.0f);
        s.volume = 12.0f;
        constexpr size_t thrown = 48128;
        for (size_t i = 0; i < v.size(); i += 256)
        {
            if (i == thrown) s.volume = -12.0f;
            p.process (v.data() + i, 256, s);
        }
        const auto levelAround = [&] (size_t at) { return db (peakOf (std::vector<float> (v.begin() + (long) at - 48, v.begin() + (long) at + 48))); };
        const double from = levelAround (thrown - 960), to = levelAround (thrown + 24000);
        const double share10 = (from - levelAround (thrown + 480)) / (from - to), gap60 = std::abs (levelAround (thrown + 2880) - to);
        float steady = 0.0f, during = 0.0f;
        for (size_t i = 24001; i < thrown; ++i) steady = std::max (steady, std::abs (v[i] - v[i - 1]));
        for (size_t i = thrown; i < thrown + 4800; ++i) during = std::max (during, std::abs (v[i] - v[i - 1]));
        std::printf ("     VOLUME thrown +12 to -12: %.0f %% of the way 10 ms on, %.2f dB short of it 60 ms on\n", share10 * 100.0, gap60);
        check (share10 > 0.33 && share10 < 0.9 && gap60 < 0.5 && during <= steady * 1.01f,
               "VOLUME thrown +12 to -12 mid-note eases in about 10 ms, there by 60 ms, no click (share 10 ms on)", share10);
    }

    // THE CEILING: -1 dBFS, every type at full GAIN and full VOLUME.
    {
        long long touched = 0;
        float worst = 0.0f;
        const auto loudest = [] (Type t) { auto s = at (t, 10.0f); s.volume = DrivePedal::volumeRangeDb; return s; };
        for (int t = 0; t < (int) Type::count; ++t)
        {
            worst = std::max (worst, peakOf (run (loudest ((Type) t), [] (int i) { return (float) (0.5 * std::sin (2.0 * pi * 2000.0 * i / rate)); }, 48000, &touched)));
            worst = std::max (worst, peakOf (run (loudest ((Type) t), [] (int i)
                                                  { return (float) (0.25 * std::sin (2.0 * pi * 100.0 * i / rate) + 0.25 * std::sin (2.0 * pi * 2000.0 * i / rate)); },
                                                  48000, &touched)));
        }
        check (db (worst) <= -1.0 + 0.01, "a -6 dBFS 2 kHz sine and a 100 Hz + 2 kHz pair at full GAIN and VOLUME: never past -1 dBFS, dBFS", db (worst));
        check (db (worst) > -2.0, "...and up to it: the guard lets them past -2 dBFS (it was -3), dBFS", db (worst));
        std::printf ("     ...the hard limit at the session's rate touched %lld samples of them\n", touched);

        long long onPhrases = 0;
        float loud = 0.0f;
        for (int t = 0; t < (int) Type::count; ++t)
            for (int kind = 0; kind < 3; ++kind)
            {
                const auto v = phrase (kind, 1.0, 48000);
                loud = std::max (loud, peakOf (run (loudest ((Type) t), [&] (int i) { return v[(size_t) i]; }, 48000, &onPhrases)));
            }
        check (db (loud) <= -1.0 + 0.01, "a guitar's phrases up to 0 dBFS at full GAIN and VOLUME: never past -1 dBFS, dBFS", db (loud));
        std::printf ("     ...the hard limit touched %lld samples of them\n", onPhrases);

        // A hard strum (-6 dBFS peak) at noon, what each hands the AMP.
        for (int t = 0; t < (int) Type::count; ++t)
        {
            float top = 0.0f;
            double rms = -200.0;
            for (int kind = 0; kind < 3; ++kind)
            {
                const auto v = phrase (kind, 0.5, 48000);
                const auto out = run (at ((Type) t, 5.0f), [&] (int i) { return v[(size_t) i]; }, 48000);
                top = std::max (top, peakOf (out, 4800));
                rms = std::max (rms, rmsDb (out, 4800));
            }
            std::printf ("     %-9s at noon, a hard strum (-6 dBFS peak) out at %.1f dBFS peak, %.1f dBFS RMS\n", names[t], db (top), rms);
        }
    }

    // SILENCE, AND WHAT IS NOT A SIGNAL.
    {
        bool silent = true;
        for (int t = 0; t < (int) Type::count; ++t)
            for (const float x : run (at ((Type) t, 10.0f), [] (int) { return 0.0f; }, 9600))
                silent = silent && std::abs (x) <= 0.0f;
        check (silent, "silence in, silence out: every type, every sample exactly zero");

        bool back = true, normal = true;
        for (int t = 0; t < (int) Type::count; ++t)
        {
            const auto v = run (at ((Type) t, 10.0f),
                                [] (int i) { return i < 24000 ? (float) (0.5 * std::sin (2.0 * pi * 110.0 * i / rate)) : 0.0f; }, 24000 + 48000 * 20);
            for (size_t i = 24000; i < v.size(); ++i)
                normal = normal && (std::abs (v[i]) <= 0.0f || std::abs (v[i]) >= std::numeric_limits<float>::min());
            back = back && std::abs (v.back()) <= 0.0f;
        }
        check (normal, "after a note, the tail has no denormal in it");
        check (back, "...and within twenty seconds it is exactly zero again");

        bool finite = true;
        float worst = 0.0f;
        const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
        for (int t = 0; t < (int) Type::count; ++t)
            for (const float x : run ([&] { auto s = at ((Type) t, 10.0f); s.volume = DrivePedal::volumeRangeDb; return s; }(), [&] (int i)
                                      {
                                          switch ((i / 4800) % 5)
                                          {
                                              case 0:  return (float) (1000.0 * std::sin (2.0 * pi * 200.0 * i / rate));
                                              case 1:  return i % 7 == 0 ? nan : 0.3f;
                                              case 2:  return i % 11 == 0 ? inf : -inf;
                                              case 3:  return 1.0f;
                                              default: return (float) ((i * 7919 % 2001) - 1000);
                                          }
                                      }, 48000))
            {
                finite = finite && std::isfinite (x) && (std::abs (x) <= 0.0f || std::abs (x) >= std::numeric_limits<float>::min());
                worst = std::max (worst, std::abs (x));
            }
        check (finite, "+60 dBFS, NaN, infinity, DC and loud noise in: every sample out finite, no denormal");
        check (db (worst) <= -1.0 + 0.01, "...and never past -1 dBFS, at full VOLUME too, dBFS", db (worst));
    }

    // NO CLICKS: a change of type, then GAIN thrown 5 to 10, mid-note.
    {
        DrivePedal p;
        p.prepare (rate);
        std::vector<float> v (48000 * 2);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = (float) (0.2 * std::sin (2.0 * pi * 196.0 * (double) i / rate));
        auto s = at (Type::ts, 5.0f);
        for (size_t i = 0; i < v.size(); i += 256)
        {
            if (i == 48128) s.type = Type::sd1;
            if (i == 72192) s.gain = 10.0f;
            p.process (v.data() + i, 256, s);
        }
        const auto steps = [&] (size_t from, size_t to)
        {
            float m = 0.0f;
            for (size_t i = from; i < to; ++i)
                m = std::max (m, std::abs (v[i] - v[i - 1]));
            return m;
        };
        const float steady = std::max ({ steps (24001, 48128), steps (64000, 72192), steps (84000, 96000) });
        const float during = std::max (steps (48128, 52928), steps (72192, 76992));
        check (during < 1.25f * steady, "a change of type, and GAIN thrown 5 to 10, mid-note: no step past the steady ones' (ratio)",
               (double) (during / steady));
    }

    // WHAT THE OVERSAMPLING BUYS: a 1997 Hz sine at -12 dBFS, full GAIN — a second of whole cycles, so every harmonic and
    // every alias of one sits on its own bin — the energy that is no harmonic, against the fundamental; and the cost, at
    // 4x and at 8x, under the DIODE law and under HARD (no solver: what the Newton steps cost is the difference).
    {
        const auto measure = [] (Type t, DrivePedal::Clip clip, int factor, double& dbc, double& ns)
        {
            DrivePedal p;
            p.voicing.factor = factor;
            p.prepare (rate);
            auto s = at (t, 10.0f);
            s.clip = clip;
            std::vector<float> v (48000 * 3);
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = (float) (0.25 * std::sin (2.0 * pi * 1997.0 * (double) i / rate));
            const auto a = std::chrono::steady_clock::now();
            for (size_t i = 0; i < v.size(); i += 512)
                p.process (v.data() + i, 512, s);
            ns = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count() * 1.0e9 / (double) v.size();
            const size_t from = 96000, n = 48000;
            double total = 0.0, harmonic = 0.0, mean = 0.0;
            for (size_t i = from; i < from + n; ++i)
                mean += v[i];
            mean /= (double) n;
            for (size_t i = from; i < from + n; ++i)
                total += (v[i] - mean) * (v[i] - mean);
            double fund = 0.0;
            for (int h = 1; 1997.0 * h < rate / 2.0; ++h)
            {
                const double e = std::norm (binAt (v, 1997.0 * h, from, n)) * (double) n / 2.0;
                harmonic += e;
                if (h == 1)
                    fund = e;
            }
            dbc = 10.0 * std::log10 (std::max (total - harmonic, 1.0e-30) / fund);
        };
        bool clean = true;
        for (const auto t : { Type::ts, Type::precision, Type::sd1 })
            for (const auto clip : { DrivePedal::Clip::diode, DrivePedal::Clip::hard })
            {
                double d4, d8, ns4, ns8;
                measure (t, clip, 4, d4, ns4);
                measure (t, clip, 8, d8, ns8);
                std::printf ("     %-9s %-5s  4x: %.1f dBc, %.0f ns a sample   8x: %.1f dBc, %.0f ns a sample\n", names[(int) t],
                             clip == DrivePedal::Clip::diode ? "DIODE" : "HARD", d4, ns4, d8, ns8);
                if (clip == DrivePedal::Clip::diode)
                    clean = clean && d8 < d4 - 6.0;
            }
        check (clean, "8x keeps what is no harmonic 6 dB and more under 4x for the DIODE law, every clipping type: it stays 8x");
    }

    // WHAT IT COSTS.
    for (int t = 0; t < (int) Type::count; ++t)
    {
        auto v = phrase (0, 0.25, 480000);
        DrivePedal p;
        p.prepare (rate);
        const auto s = at ((Type) t, 5.0f);
        const auto a = std::chrono::steady_clock::now();
        for (size_t i = 0; i < v.size(); i += 512)
            p.process (v.data() + i, 512, s);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
        std::printf ("     %-9s %dx: %.0f ns a sample, %.2f %% of one core in real time (one channel, 48 kHz)\n",
                     names[t], t == 2 ? 1 : p.oversampling(), sec * 1.0e9 / (double) v.size(), 100.0 * sec / 10.0);
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
