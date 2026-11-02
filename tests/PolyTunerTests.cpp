// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless acceptance gate for core::PolyTuner — the polyphonic tuner's analysis, checked against
// strums with a KNOWN answer. Every string is additive synthesis of a stiff string: partial h at
// h f0 sqrt(1 + B h^2), a pluck-position spectrum, higher partials dying faster, a pitch that starts
// sharp and settles, the strings struck one after another as a strum does. The truth is each
// string's first partial. Seeded: the gate never rolls dice.
//
// And what the faces draw from it (core::TunerPoly): the tunings each product offers, a reading's lanes,
// and the pump's tick — the analysis run only while it is asked for, nothing allocated per tick.
//
// POLY_STRUMS=<n> runs n random strums per tuning and rate instead of the gate's few (heavy runs).
// JUCE-free, like the code under test: links nothing at all.

#include "core/PolyTuner.h"
#include "core/TunerPoly.h"
#include "core/TunerTap.h"

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// Every allocation is counted while `countingAllocations` is set: the tuner's timer tick must not
// allocate at all — everything it needs is sized in prepare().
namespace { bool countingAllocations = false; long allocationsCounted = 0; }
void* operator new (std::size_t n)
{
    if (countingAllocations)
        ++allocationsCounted;
    if (void* p = std::malloc (n > 0 ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

using orbitamp::core::PolyTuner;
using Heard = PolyTuner::Heard;

namespace
{
    int failures = 0;
    constexpr double pi = 3.14159265358979323846;

    struct Rng
    {
        std::uint64_t s;
        double uniform()   // [0, 1)
        {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            return (double) (s >> 11) / 9007199254740992.0;
        }
        double range (double a, double b) { return a + (b - a) * uniform(); }
    };

    struct StringSpec
    {
        double f1      = 0.0;    // the truth: the first partial
        double B       = 0.0;
        double levelDb = 0.0;
        double pluck   = 0.15;   // pluck position, a fraction of the length
        double tau1    = 3.0;    // the first partial's decay, s
        bool   on      = true;
    };

    struct Scene
    {
        std::vector<StringSpec> strings;
        double fs        = 48000.0;
        double strumAt   = 0.6;     // s: hum and hiss alone before it
        double spreadMs  = 12.0;    // between strings
        double glideCents = 8.0;    // sharp at the pluck, settling with glideTau
        double glideTau  = 0.06;
        double humHz     = 0.0;     // 0: none
        double humDb     = -40.0;   // the hum's fundamental, dBFS
        double noiseDb   = -90.0;
        double seconds   = 2.75;
        std::uint64_t seed = 1;
    };

    std::vector<float> render (const Scene& sc)
    {
        const int n = (int) (sc.seconds * sc.fs);
        std::vector<double> acc ((size_t) n, 0.0);
        Rng rng { sc.seed * 7919u + 17u };

        const double glide = std::pow (2.0, sc.glideCents / 1200.0) - 1.0, tauG = sc.glideTau;
        for (size_t i = 0; i < sc.strings.size(); ++i)
        {
            const auto& st = sc.strings[i];
            if (! st.on)
                continue;
            const int n0 = (int) ((sc.strumAt + 1.0e-3 * sc.spreadMs * (double) i) * sc.fs);
            const double f0 = st.f1 / std::sqrt (1.0 + st.B);
            const double gain = 0.1 * std::pow (10.0, st.levelDb / 20.0) / std::sin (pi * st.pluck);

            for (int h = 1; h <= 40; ++h)
            {
                const double fh = h * f0 * std::sqrt (1.0 + st.B * h * h);
                if (fh > std::min (0.45 * sc.fs, 2500.0))
                    break;
                const double a   = gain * std::abs (std::sin (pi * h * st.pluck)) / std::pow ((double) h, 0.9);
                const double tau = st.tau1 / (1.0 + fh / 600.0);
                const double ph0 = rng.range (0.0, 2.0 * pi);
                const double dec = std::exp (-1.0 / (tau * sc.fs));

                for (int b = n0; b < n; b += 64)
                {
                    const double t     = (b - n0) / sc.fs;
                    const double phase = 2.0 * pi * fh * (t + glide * tauG * (1.0 - std::exp (-t / tauG))) + ph0;
                    const double fNow  = fh * (1.0 + glide * std::exp (-(t + 32.0 / sc.fs) / tauG));
                    std::complex<double> z = std::polar (a * std::exp (-t / tau), phase);
                    const std::complex<double> step = std::polar (dec, 2.0 * pi * fNow / sc.fs);
                    const int e = std::min (n, b + 64);
                    for (int k = b; k < e; ++k)
                    {
                        const double tk = (k - n0) / sc.fs;
                        acc[(size_t) k] += z.imag() * (tk < 0.002 ? tk / 0.002 : 1.0);
                        z *= step;
                    }
                }
            }
        }

        if (sc.humHz > 0.0)
        {
            const double a1 = std::pow (10.0, sc.humDb / 20.0);
            const double rel[4] = { 1.0, 0.5, 0.35, 0.25 };
            for (int k = 1; k <= 4; ++k)
            {
                const double ph = rng.range (0.0, 2.0 * pi);
                for (int i = 0; i < n; ++i)
                    acc[(size_t) i] += a1 * rel[k - 1] * std::sin (2.0 * pi * k * sc.humHz * i / sc.fs + ph);
            }
        }

        std::vector<float> out ((size_t) n);
        const double na = std::pow (10.0, sc.noiseDb / 20.0) * std::sqrt (3.0);
        std::uint32_t ns = (std::uint32_t) sc.seed * 2654435761u + 1u;
        for (int i = 0; i < n; ++i)
        {
            ns = ns * 1664525u + 1013904223u;
            const double u = (double) (ns >> 8) / 8388608.0 - 1.0;
            out[(size_t) i] = (float) (acc[(size_t) i] + na * u);
        }
        return out;
    }

    double targetOf (const PolyTuner::Tuning& t, int s) { return 440.0 * std::pow (2.0, (t.midi[(size_t) s] - 69) / 12.0); }
    bool isBass (const PolyTuner::Tuning& t) { return t.id[0] == 'b'; }

    /** A random strum of the tuning: every string detuned within +-40 cents, levels within 20 dB. */
    Scene randomScene (const PolyTuner::Tuning& t, double fs, std::uint64_t seed)
    {
        Scene sc;
        sc.fs = fs;
        sc.seed = seed;
        Rng rng { seed };
        const bool bass = isBass (t);
        for (int s = 0; s < t.strings; ++s)
        {
            StringSpec st;
            st.f1      = targetOf (t, s) * std::pow (2.0, rng.range (-40.0, 40.0) / 1200.0);
            st.B       = bass ? rng.range (5.0e-5, 4.0e-4) : rng.range (1.0e-5, 1.2e-4);
            st.levelDb = rng.range (-20.0, 0.0);
            st.pluck   = rng.range (0.08, 0.2);
            st.tau1    = bass ? rng.range (3.0, 6.0) : rng.range (2.0, 4.0);
            sc.strings.push_back (st);
        }
        sc.spreadMs = rng.range (8.0, 20.0);
        // POLY_SPAN_S=<s>: stop that long after the settle — how good a reading is while it is young.
        if (const char* span = std::getenv ("POLY_SPAN_S"))
            sc.seconds = sc.strumAt + 1.0e-3 * sc.spreadMs * t.strings + 0.25 + std::atof (span);
        return sc;
    }

    PolyTuner::Reading listen (const PolyTuner::Tuning& t, const Scene& sc, double* seconds = nullptr)
    {
        static PolyTuner pt;   // big: keep it off the stack
        pt.prepare (sc.fs);
        pt.setTuning (t);
        const auto x = render (sc);
        pt.push (x.data(), (int) x.size());
        const auto t0 = std::chrono::steady_clock::now();
        pt.analyse();
        if (seconds != nullptr)
            *seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        return pt.reading();
    }


    bool partialNear (const StringSpec& o, double hz, double cents)
    {
        const double f0 = o.f1 / std::sqrt (1.0 + o.B);
        for (int h = 1; h <= 12; ++h)
            if (std::abs (1200.0 * std::log2 (h * f0 * std::sqrt (1.0 + o.B * h * h) / hz)) < cents)
                return true;
        return false;
    }

    const char* heardName (Heard h)
    {
        switch (h)
        {
            case Heard::no:     return "no";
            case Heard::masked: return "masked";
            case Heard::near:   return "near";
            case Heard::far:    return "far";
        }
        return "?";
    }

    void dump (const PolyTuner::Tuning& t, const Scene& sc, const PolyTuner::Reading& r)
    {
        for (int s = 0; s < t.strings; ++s)
        {
            const auto& o = r.string[(size_t) s];
            const auto& st = sc.strings[(size_t) s];
            std::printf ("      string %d  true %8.3f Hz (%+6.1f c, %5.1f dB, B %.1e)  read %-6s %8.3f Hz  err %+7.3f c  sd %.3f  lvl %.1f\n",
                         s + 1, st.f1, 1200.0 * std::log2 (st.f1 / targetOf (t, s)), st.levelDb, st.B, heardName (o.heard),
                         o.hz, o.hz > 0.0f ? 1200.0 * std::log2 (o.hz / st.f1) : 0.0, o.sigmaCents, o.levelDb);
        }
    }

    void check (bool ok, const char* what, const PolyTuner::Tuning& t, const Scene& sc, const PolyTuner::Reading& r)
    {
        if (ok)
            return;
        ++failures;
        std::printf ("  FAIL %s — %s @ %.0f Hz, seed %llu\n", what, t.name, sc.fs, (unsigned long long) sc.seed);
        dump (t, sc, r);
    }

    double errCents (const PolyTuner::String& o, const StringSpec& st)
    {
        return o.hz > 0.0f ? 1200.0 * std::log2 (o.hz / st.f1) : 999.0;
    }

    // The rule the analysis keeps (PolyTuner.h, Decisions): a published number within a cent of the truth,
    // or none — "pluck alone" or "-". Withholding is counted, and the count is held to a ceiling at the end:
    // a tuner that withheld everything would pass the rest.
    int withheld = 0, sounding = 0;
    constexpr double withheldCeiling = 10.0;
    bool rightOrWithheld (const PolyTuner::String& o, const StringSpec& st)
    {
        ++sounding;
        if (o.heard == Heard::near)
            return std::abs (errCents (o, st)) <= 1.0;
        if (o.heard == Heard::far)
            return false;   // every string here is within 40 c of its note
        ++withheld;
        return true;
    }
}

int main()
{
    const int strums = std::getenv ("POLY_STRUMS") != nullptr ? std::max (1, std::atoi (std::getenv ("POLY_STRUMS"))) : 3;

    // ---- every tuning, random strums, three rates ------------------------------------------------
    // The claim: within +-1 cent for every string inside +-50 cents of its note, none read as
    // another. A string within half a bin of the span of another string's partial may read MASKED —
    // it cannot be told from that harmonic — and is counted, not failed.
    std::printf ("Accuracy: %d random strums per tuning and rate, detune +-40 c, levels 0..-20 dB\n", strums);
    std::printf ("%-24s %s\n", "tuning", "worst |error| per string, cents (readings withheld)");
    double worstAll = 0.0, analysisSeconds = 0.0, audioSeconds = 0.0;
    int analyses = 0;
    for (const auto& t : PolyTuner::tunings)
    {
        double worst[PolyTuner::maxStrings] {};
        int masked[PolyTuner::maxStrings] {};
        for (const double fs : { 44100.0, 48000.0, 96000.0 })
            for (int i = 0; i < strums; ++i)
            {
                const auto sc = randomScene (t, fs, 1000u + (std::uint64_t) i * 31u + (std::uint64_t) fs);
                double sec = 0.0;
                const auto r = listen (t, sc, &sec);
                analysisSeconds += sec;
                audioSeconds += 0.2;   // one analysis per 200 ms of audio is the cadence
                ++analyses;
                bool ok = true;
                for (int s = 0; s < t.strings; ++s)
                {
                    const auto& o = r.string[(size_t) s];
                    if (o.heard != Heard::near && o.heard != Heard::far)
                        ++masked[s];
                    else
                        worst[s] = std::max (worst[s], std::abs (errCents (o, sc.strings[(size_t) s])));
                    ok = rightOrWithheld (o, sc.strings[(size_t) s]) && ok;
                }
                check (ok, "strum", t, sc, r);
            }
        std::printf ("%-24s", t.name);
        for (int s = 0; s < t.strings; ++s)
        {
            std::printf ("  %6.3f", worst[s]);
            if (masked[s] > 0)
                std::printf (" (%d)", masked[s]);
            worstAll = std::max (worstAll, worst[s]);
        }
        std::printf ("\n");
    }
    std::printf ("worst over all: %.3f cents\n\n", worstAll);

    // ---- a muted string ----------------------------------------------------------------------------
    // Not heard — or masked, where its window holds another string's partial. The rest still right.
    for (const auto& t : PolyTuner::tunings)
        for (int m = 0; m < t.strings; ++m)
        {
            auto sc = randomScene (t, 48000.0, 5000u + (std::uint64_t) m);
            sc.strings[(size_t) m].on = false;
            const auto r = listen (t, sc);
            bool ok = r.string[(size_t) m].heard == Heard::no || r.string[(size_t) m].heard == Heard::masked;
            for (int s = 0; s < t.strings; ++s)
                if (s != m)
                    ok = rightOrWithheld (r.string[(size_t) s], sc.strings[(size_t) s]) && ok;
            check (ok, "muted string", t, sc, r);
        }
    std::printf ("muted string: checked every string of every tuning\n");

    // ---- one string far off: +150 cents, then -150 -----------------------------------------------
    for (const auto& t : PolyTuner::tunings)
        for (int f = 0; f < t.strings; ++f)
        {
            auto sc = randomScene (t, 48000.0, 7000u + (std::uint64_t) f);
            const double off = (f & 1) ? -150.0 : 150.0;
            sc.strings[(size_t) f].f1 = targetOf (t, f) * std::pow (2.0, off / 1200.0);
            const auto r = listen (t, sc);
            const auto& o = r.string[(size_t) f];
            bool ok = o.heard == Heard::far && std::abs (o.cents - off) <= 2.0;
            for (int s = 0; s < t.strings; ++s)
                if (s != f)
                    ok = rightOrWithheld (r.string[(size_t) s], sc.strings[(size_t) s]) && ok;
            check (ok, "one string 150 c off", t, sc, r);
        }
    std::printf ("far string: +-150 c on every string of every tuning\n");

    // ---- mains hum under the strum: 50 and 60 Hz with harmonics, -40 dBFS --------------------------
    double humShift = 0.0;
    for (const auto& t : PolyTuner::tunings)
        for (const double hz : { 50.02, 59.97 })
        {
            auto sc = randomScene (t, 48000.0, 9000u + (std::uint64_t) hz);
            const auto clean = listen (t, sc);
            sc.humHz = hz;
            const auto r = listen (t, sc);
            bool ok = true;
            for (int s = 0; s < t.strings; ++s)
            {
                const auto& o = r.string[(size_t) s];
                ok = rightOrWithheld (o, sc.strings[(size_t) s]) && ok;
                if (clean.string[(size_t) s].heard == Heard::near && o.heard == Heard::near)
                    humShift = std::max (humShift, (double) std::abs (o.cents - clean.string[(size_t) s].cents));
            }
            check (ok, hz < 55.0 ? "hum 50 Hz" : "hum 60 Hz", t, sc, r);
        }
    if (humShift > 0.2)
        ++failures;
    std::printf ("hum: 50 and 60 Hz under every tuning, largest shift %.3f c (want <= 0.2)  %s\n", humShift, humShift <= 0.2 ? "ok" : "FAIL");

    // ---- a single string: mono ------------------------------------------------------------------------
    for (const auto& t : PolyTuner::tunings)
        for (int one = 0; one < t.strings; ++one)
        {
            auto sc = randomScene (t, 48000.0, 11000u + (std::uint64_t) one);
            for (int s = 0; s < t.strings; ++s)
                sc.strings[(size_t) s].on = s == one;
            const auto r = listen (t, sc);
            bool ok = r.mono && r.string[(size_t) one].heard == Heard::near
                   && std::abs (errCents (r.string[(size_t) one], sc.strings[(size_t) one])) <= 1.0;
            // The rest: not heard — or masked where the plucked string's own partial stands within a
            // semitone of their note, as in a muted string's case: a string in unison with that
            // partial would look exactly the same.
            for (int s = 0; s < t.strings; ++s)
                if (s != one)
                    ok = ok && (r.string[(size_t) s].heard == Heard::no
                                || (r.string[(size_t) s].heard == Heard::masked && partialNear (sc.strings[(size_t) one], targetOf (t, s), 100.0)));
            check (ok, "single string", t, sc, r);
        }
    std::printf ("single string: every string of every tuning alone\n");

    // ---- the first reading after a hard pluck ---------------------------------------------------------
    // A string struck hard starts sharp — here 20 cents, settling with 100 ms — and the first reading
    // the timer gets must not carry that: it is why the first 250 ms after the attack are skipped.
    for (const int ti : { 0, 5 })
    {
        const auto& t = PolyTuner::tunings[(size_t) ti];
        for (int i = 0; i < 4; ++i)
        {
            auto sc = randomScene (t, 48000.0, 17000u + (std::uint64_t) i);
            sc.glideCents = 20.0;
            sc.glideTau   = 0.1;
            PolyTuner pt;
            pt.prepare (sc.fs);
            pt.setTuning (t);
            const auto x = render (sc);
            bool got = false;
            for (size_t k = 0; k < x.size() && ! got; k += 1600)   // ~30 Hz ticks
            {
                pt.push (x.data() + k, (int) std::min<size_t> (1600, x.size() - k));
                got = pt.maybeAnalyse() && pt.reading().strum > 0 && pt.reading().spanMs > 0.0f
                      && (double) k / sc.fs > sc.strumAt;
            }
                bool ok = got;
            for (int s = 0; s < t.strings; ++s)
            {
                const auto& o = pt.reading().string[(size_t) s];
                ok = rightOrWithheld (o, sc.strings[(size_t) s]) && ok;
            }
            check (ok, "first reading after a hard pluck", t, sc, pt.reading());
        }
    }
    std::printf ("first reading after a hard pluck: +20 c settling over 100 ms, guitar and 5-string bass\n");

    // ---- nothing played ------------------------------------------------------------------------------
    for (const auto& t : PolyTuner::tunings)
    {
        auto sc = randomScene (t, 48000.0, 13000u);
        for (auto& st : sc.strings)
            st.on = false;
        sc.noiseDb = -70.0;
        sc.humHz = 50.0;
        PolyTuner pt;
        pt.prepare (sc.fs);
        pt.setTuning (t);
        const auto x = render (sc);
        for (size_t i = 0; i < x.size(); i += 1600)   // the timer's ticks
        {
            pt.push (x.data() + i, (int) std::min<size_t> (1600, x.size() - i));
            pt.maybeAnalyse();
        }
        pt.analyse();
        bool ok = true;
        for (int s = 0; s < t.strings; ++s)
            ok = ok && pt.reading().string[(size_t) s].heard == Heard::no;
        check (ok, "hum and hiss alone", t, sc, pt.reading());
    }
    std::printf ("nothing played: hum and hiss read no string\n");

    // ---- through the tap, on a timer: the same reading, and what it costs ------------------------
    {
        const auto& t = PolyTuner::tunings[0];
        auto sc = randomScene (t, 48000.0, 15000u);
        const auto x = render (sc);
        const auto direct = listen (t, sc);

        static orbitamp::core::TunerTap tap;
        PolyTuner pt;
        pt.prepare (sc.fs);
        pt.setTuning (t);
        double busy = 0.0;
        int fresh = 0;
        allocationsCounted = 0;
        countingAllocations = true;
        for (size_t i = 0; i < x.size(); i += 512)
        {
            tap.write (x.data() + i, (int) std::min<size_t> (512, x.size() - i));
            if ((i / 512) % 3 == 0)   // a ~30 Hz timer
            {
                const auto t0 = std::chrono::steady_clock::now();
                fresh += pt.update (tap) ? 1 : 0;
                busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            }
        }
        pt.update (tap);
        pt.analyse();
        countingAllocations = false;
        bool ok = fresh > 3 && allocationsCounted == 0;
        for (int s = 0; s < t.strings; ++s)
            ok = ok && pt.reading().string[(size_t) s].heard == direct.string[(size_t) s].heard
                    && std::abs (pt.reading().string[(size_t) s].cents - direct.string[(size_t) s].cents) <= 0.01f;
        check (ok, "through the tap", t, sc, pt.reading());
        std::printf ("through the tap: %d readings, %.1f ms busy for %.2f s of audio (%.1f%% of a core), %ld allocations\n",
                     fresh, busy * 1000.0, sc.seconds, 100.0 * busy / sc.seconds, allocationsCounted);
    }

    // ---- what the faces draw: the tunings, the lanes, the pump --------------------------------------
    {
        using namespace orbitamp::core;
        using Mark = PolyLane::Mark;
        const auto expect = [] (bool ok, const char* what)
        {
            if (! ok)
                ++failures;
            std::printf ("%-58s %s\n", what, ok ? "ok" : "FAIL");
        };

        // A guitar amp offers the guitar's four, a bass amp the bass's five; a name it does not offer is its standard.
        const auto ids = [] (bool bass)
        {
            std::string all;
            for (const auto& t : polyTunings (bass))
                all += std::string (t.id) + " ";
            return all;
        };
        expect (ids (false) == "guitar guitar-drop-d guitar-half-down guitar-whole-down ", "tunings: a guitar amp's");
        expect (ids (true) == "bass-4 bass-5 bass-6 bass-drop-d bass-half-down ", "tunings: a bass amp's");
        expect (std::strcmp (polyTuning ("guitar-drop-d", false).id, "guitar-drop-d") == 0
                    && std::strcmp (polyTuning ("bass-5", true).id, "bass-5") == 0, "tunings: by name");
        expect (std::strcmp (polyTuning ("bass-5", false).id, "guitar") == 0 && std::strcmp (polyTuning ("", true).id, "bass-4") == 0
                    && std::strcmp (polyTuning (nullptr, false).id, "guitar") == 0, "tunings: one not offered is the standard");

        // A reading's lanes: every mark, the band, the clamp, and nothing at all while the tuner is off.
        const auto& t = PolyTuner::tunings[0];
        PolyTuner::Reading r;
        r.strings = 6;
        const PolyTuner::Heard heard[6] = { PolyTuner::Heard::near, PolyTuner::Heard::near, PolyTuner::Heard::masked,
                                            PolyTuner::Heard::no, PolyTuner::Heard::far, PolyTuner::Heard::near };
        const float cents[6] = { 12.0f, -30.0f, 0.0f, 0.0f, -150.0f, 2.4f };
        for (int s = 0; s < 6; ++s)
            r.string[(size_t) s] = { heard[s], cents[s], 0.0f, 0.0f, 0.0f, -20.0f };
        const auto on = polyLanes (r, t, true);
        bool names = on.count == 6;
        for (int s = 0; s < 6; ++s)
            names = names && on.lane[(size_t) s].midi == t.midi[(size_t) s];
        expect (names, "lanes: one per string, lowest first, each its target");
        expect (on.lane[0].mark == Mark::near && on.lane[0].cents == 12.0f && ! on.lane[0].green && on.lane[0].sharp
                    && on.lane[1].mark == Mark::near && on.lane[1].cents == -30.0f && ! on.lane[1].sharp,
                "lanes: near, its cents and its side");
        expect (on.lane[2].mark == Mark::masked && on.lane[3].mark == Mark::none
                    && on.lane[4].mark == Mark::far && ! on.lane[4].sharp, "lanes: masked, not heard, far");
        expect (on.lane[5].green && on.lane[5].cents == 2.4f, "lanes: green within the needle's band");
        r.string[5].cents = 2.6f;
        r.string[0].cents = 80.0f;
        const auto out = polyLanes (r, t, true);
        expect (! out.lane[5].green && out.lane[0].cents == PolyLane::showCents, "lanes: past the band, past the scale");
        const auto off = polyLanes (r, t, false);
        bool none = off.count == 6;
        for (int s = 0; s < 6; ++s)
            none = none && off.lane[(size_t) s].mark == Mark::none;
        expect (none, "lanes: the tuner off hears nothing");

        // The pump, through the tap: the strum the faces were drawn with — A +12 c, G -30 c, B muted.
        Scene sc;
        sc.fs = 48000.0;
        sc.seconds = 2.6;
        sc.strumAt = 0.5;
        sc.spreadMs = 14.0;
        const double detune[6] = { 1.0, 12.0, -1.5, -30.0, 0.0, 0.8 };
        for (int s = 0; s < 6; ++s)
        {
            StringSpec st;
            st.f1 = targetOf (t, s) * std::pow (2.0, detune[s] / 1200.0);
            st.B = 6.0e-5;
            st.levelDb = -3.0 * (s % 2);
            st.pluck = 0.13;
            st.on = s != 4;
            sc.strings.push_back (st);
        }
        const auto x = render (sc);
        static TunerTap tap;
        static TunerPoly poly;
        expect (! poly.on() && ! poly.running(), "pump: nothing until asked");
        poly.tune (t, 440.0f);
        long perTick = 0;
        int ticks = 0;
        bool kept = true;
        for (size_t i = 0; i < x.size(); i += 512)
        {
            tap.write (x.data() + i, (int) std::min<size_t> (512, x.size() - i));
            if ((i / 512) % 3 != 2)
                continue;
            const auto before = poly.lanes (true);
            allocationsCounted = 0;
            countingAllocations = ticks > 0;   // the first tick prepares
            poly.tune (t, 440.0f);             // every tick, as the processor's pump does
            const auto after = poly.lanes (true);
            for (int s = 0; s < after.count; ++s)
                kept = kept && after.lane[(size_t) s].mark == before.lane[(size_t) s].mark
                            && after.lane[(size_t) s].cents == before.lane[(size_t) s].cents;
            poly.update (tap, sc.fs, -60.0);
            countingAllocations = false;
            perTick += allocationsCounted;
            ++ticks;
        }
        expect (poly.on() && poly.running() && perTick == 0, "pump: runs, and no tick after the first allocates");
        expect (kept && poly.reading().spanMs > 900.0f, "pump: the same tuning again keeps the reading");
        const auto lanes = poly.lanes (true);
        expect (lanes.lane[1].mark == Mark::near && std::abs (lanes.lane[1].cents - 12.0f) < 0.5f && ! lanes.lane[1].green
                    && lanes.lane[3].mark == Mark::near && std::abs (lanes.lane[3].cents + 30.0f) < 0.5f
                    && lanes.lane[4].mark != Mark::near && lanes.lane[4].mark != Mark::far
                    && lanes.lane[5].mark == Mark::masked
                    && lanes.lane[0].green && lanes.lane[2].green,
                "pump: the strum's lanes - +12, -30, B muted (no number), E on the low E's 4th partial (pluck alone), the rest home");

        poly.tune (t, 442.0f);
        const auto retuned = poly.lanes (true);
        bool cleared = poly.referenceHz() == 442.0f;
        for (int s = 0; s < retuned.count; ++s)
            cleared = cleared && retuned.lane[(size_t) s].mark == Mark::none;
        expect (cleared, "pump: another A retunes, and reads afresh");
        poly.update (tap, sc.fs, -60.0);
        poly.stop (true);
        expect (poly.on() && ! poly.running() && poly.reading().spanMs == 0.0f, "pump: stopped, shown, nothing kept");
        poly.stop();
        expect (! poly.on(), "pump: another display, not shown");
    }

    // Withholding is allowed, not free: on the gate's own clean strums it stays under a ceiling.
    {
        const double share = 100.0 * withheld / std::max (1, sounding);
        if (share > withheldCeiling)
            ++failures;
        std::printf ("withheld: %.1f%% of %d sounding-string readings (want <= %.0f%%)  %s\n", share, sounding, withheldCeiling,
                     share <= withheldCeiling ? "ok" : "FAIL");
    }

    std::printf ("cost: %.2f ms per analysis on average (%d analyses), %.1f%% of a core at one per 200 ms\n",
                 1000.0 * analysisSeconds / std::max (1, analyses), analyses, 100.0 * analysisSeconds / std::max (1e-9, audioSeconds));

    std::printf ("\n%s\n", failures == 0 ? "ALL OK" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
