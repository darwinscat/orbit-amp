// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The tuner on a BASS — a characterisation, not a verdict. It plays every string a bassist tunes
// through the tuner's own path and PRINTS what the needle shows; it fails only if the harness
// itself stops hearing (a guitar's A2, which the tuner has always read). Whatever the bass rows
// say, the run passes: the tables are the product, to be read and compared run to run.
//
// The path is the processor's, step for step (src/PluginProcessor.h, pumpTuner): the raw input
// written into a TunerTap in host blocks, a TunerEar ticked at 30 Hz with the default level floor
// (-60 dBFS) and reference (A4 = 440) set before every tick. 44.1, 48 and 96 kHz.
//
// The string: additive partials of a stiff string (B = 2e-4), each decaying faster than the one
// below it, the fundamental at ~9 dB/s, so a note that starts at -15 dBFS RMS sinks under the
// floor four to five seconds after the pluck. A finger pluck's partials fall at 1/k after the
// 2nd; a slap's at 1/sqrt(k), brighter and dying faster, under a hard crack. The fundamental's
// level against the 2nd harmonic is a parameter — a bass through a DI, a low B above all, often
// has the 2nd louder than the fundamental, which is where bass tuners read an octave wrong.
//
// Tables:
//   1. the range — 4-string E1 A1 D2 G2, 5-string B0, drop-D D1, drop-A A0, 6-string C3, each
//      at 0, +-5, +-15, +-40 cents (a full-range pluck, the fundamental 6 dB over the 2nd);
//   2. octave errors — the same notes in tune, the fundamental 6 and 12 dB UNDER the 2nd,
//      finger and slap;
//   3. mains hum — G1 under 50 Hz hum, A#1 and B1 under 60 Hz, the hum 30 and 40 dB under the
//      note; and the hum alone against each level floor the player can pick;
//   4. the cost of one ear tick, by rate.
// ORBITAMP_BASS_TUNER_RUNS=1 adds one line per run behind tables 1 and 2.
//
// JUCE-free, like the code under test: links nothing at all.

#include "core/PitchTracker.h"
#include "core/TunerEar.h"
#include "core/TunerTap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using orbitamp::core::PitchTracker;
using orbitamp::core::TunerEar;
using orbitamp::core::TunerTap;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    // What the processor hands the ear (src/PluginProcessor.h pumpTuner, src/ui/Prefs.h defaults,
    // startTimerHz (30) in src/PluginProcessor.cpp).
    constexpr double floorDb   = -60.0;
    constexpr float  reference = 440.0f;
    constexpr double tickHz    = 30.0;
    constexpr int    hostBlock = 512;

    constexpr double preRoll = 0.5;      // seconds before the pluck: silence, or the hum alone
    constexpr double ringFor = 6.0;      // seconds listened to after it
    constexpr double noteDb  = -15.0;    // the note's RMS over its first 300 ms, dBFS
    constexpr double stiff   = 2.0e-4;   // B, a bass string's inharmonicity
    constexpr double rates[] = { 44100.0, 48000.0, 96000.0 };

    constexpr double lockCents = 3.0;    // a needle within this of the truth ...
    constexpr int    lockTicks = 10;     // ... for this many ticks in a row is a lock

    bool perRun = false;

    /** Deterministic noise — the run must not roll dice. */
    float lcgNoise (std::uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((double) (s >> 8) / 8388608.0 - 1.0);
    }

    struct Voice
    {
        double      fundDb;   // the fundamental against the 2nd harmonic
        bool        slap;
        const char* name;
    };

    struct Hum
    {
        double hz      = 0.0;   // 0: none
        double underDb = 0.0;   // under the note's RMS
    };

    std::string noteText (int midi)
    {
        return std::string (PitchTracker::noteName (midi)) + std::to_string (PitchTracker::noteOctave (midi));
    }

    /** One plucked note: `preRoll` of nothing but hiss (and hum), then the string. */
    std::vector<float> pluck (double f0, double sr, Voice v, Hum hum, std::uint32_t seed)
    {
        const int total = (int) ((preRoll + ringFor) * sr);
        const int start = (int) (preRoll * sr);
        std::vector<double> y ((size_t) total, 0.0);
        std::uint32_t rng = seed * 7919u + 17u;

        const double a2 = v.slap ? 1.0 / std::sqrt (2.0) : 0.5;
        for (int k = 1; k <= (v.slap ? 40 : 24); ++k)
        {
            const double fk = k * f0 * std::sqrt (1.0 + stiff * k * k);
            if (fk > std::min (2500.0, 0.45 * sr))   // past the tracker's 1.5 kHz anti-alias anyway
                break;

            const double amp    = k == 1 ? a2 * std::pow (10.0, v.fundDb / 20.0)
                                         : (v.slap ? 1.0 / std::sqrt ((double) k) : 1.0 / k);
            const double dbPerS = 9.0 * (1.0 + (v.slap ? 0.6 : 0.25) * (k - 1));
            const double r      = std::pow (10.0, -dbPerS / 20.0 / sr);
            const double w      = 2.0 * pi * fk / sr;
            const double cw = r * std::cos (w), sw = r * std::sin (w);
            const double ph = pi * (1.0 + lcgNoise (rng));
            double re = amp * std::cos (ph), im = amp * std::sin (ph);

            for (int n = start; n < total; ++n)
            {
                y[(size_t) n] += im;
                const double nr = re * cw - im * sw;
                im = re * sw + im * cw;
                re = nr;
            }
        }

        // The attack: a finger's soft burst, or a slap's crack.
        const double burst = v.slap ? 1.2 : 0.25, tau = v.slap ? 0.002 : 0.001;
        for (int n = start; n < std::min (total, start + (int) (0.02 * sr)); ++n)
            y[(size_t) n] += burst * lcgNoise (rng) * std::exp (-(n - start) / sr / tau);

        const int len = (int) (0.3 * sr);
        double e = 0.0;
        for (int n = start; n < start + len; ++n)
            e += y[(size_t) n] * y[(size_t) n];

        const double level  = std::pow (10.0, noteDb / 20.0);
        const double g      = level / std::sqrt (e / len);
        const double humAmp = hum.hz > 0.0 ? std::sqrt (2.0) * level * std::pow (10.0, -hum.underDb / 20.0) : 0.0;
        const double humPh  = pi * (1.0 + lcgNoise (rng));

        std::vector<float> out ((size_t) total);
        for (int n = 0; n < total; ++n)
            out[(size_t) n] = (float) (g * y[(size_t) n] + humAmp * std::sin (2.0 * pi * hum.hz * n / sr + humPh)
                                       + 1.0e-4 * lcgNoise (rng));
        return out;
    }

    struct Tick
    {
        double t;        // seconds after the pluck
        bool   live;
        int    midi;
        float  needle;
        float  hz;
        double rmsDb;    // the RMS of the input behind the tracker's window
    };

    struct Cost
    {
        double totalUs = 0.0, worstUs = 0.0;
        long   calls   = 0;
    };

    std::map<double, Cost> costByRate;

    /** The processor's path: host blocks into the tap, the ear ticked at 30 Hz with the floor and
        the reference set before every tick. */
    std::vector<Tick> listen (const std::vector<float>& y, double sr)
    {
        auto tap = std::make_unique<TunerTap>();
        auto ear = std::make_unique<TunerEar>();
        ear->prepare (sr);

        // The tracker analyses the newest 3072 decimated samples (PitchTracker::windowCap).
        const int decim = std::max (1, (int) std::lround (sr / 12000.0));
        const int win   = 3072 * decim;

        std::vector<double> cum (y.size() + 1, 0.0);
        for (size_t i = 0; i < y.size(); ++i)
            cum[i + 1] = cum[i] + (double) y[i] * y[i];

        auto& cost = costByRate[sr];
        std::vector<Tick> ticks;
        double nextTick = 0.0;
        const int total = (int) y.size();

        for (int pos = 0; pos + hostBlock <= total; pos += hostBlock)
        {
            tap->write (y.data() + pos, hostBlock);
            const int end = pos + hostBlock;

            for (; nextTick <= end / sr; nextTick += 1.0 / tickHz)
            {
                ear->setLevelFloorDb (floorDb);
                ear->setReferenceHz (reference);

                const auto a = std::chrono::steady_clock::now();
                ear->update (*tap, (unsigned) std::lround (nextTick * 1000.0));
                const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - a).count();
                cost.totalUs += us;
                cost.worstUs  = std::max (cost.worstUs, us);
                ++cost.calls;

                const int    from = std::max (0, end - win);
                const double rms  = std::sqrt ((cum[(size_t) end] - cum[(size_t) from]) / std::max (1, end - from));
                ticks.push_back ({ nextTick - preRoll, ear->live(), ear->nearestNote().midi, ear->needle(), ear->hz(),
                                   20.0 * std::log10 (std::max (rms, 1.0e-12)) });
            }
        }
        return ticks;
    }

    struct Verdict
    {
        int    shown      = -1;     // the note shown most before the floor (-1: never live)
        double livePct    = 0.0;    // ticks before the floor that show anything
        double octavePct  = 0.0;    // live ticks before the floor that show the right OCTAVE
        double firstMs    = -1.0;   // pluck to the first tick on the right letter
        double lockMs     = -1.0;   // pluck to a needle on the right letter, within lockCents
        double errMed     = 0.0;    // needle minus truth, cents: median and worst, lock to floor
                                    // (the worst only from lockTicks after the lock: the lock tick
                                    // itself is lockCents off by definition)
        double errWorst   = 0.0;
        bool   anyErr     = false;
        double heldTo     = -1.0;   // the last tick on the right letter, s after the pluck
        double floorAt    = -1.0;   // when the window's RMS sinks under the floor (-1: never)
        int    octUp      = 0;      // entries into an octave up / down / another note
        int    octDown    = 0;
        int    other      = 0;
        double firstOctAt = -1.0;
    };

    /** What the needle did, against a note whose truth is `want` (the nearest note and its cents).
        `stopAt` ends the note's span where the window never sinks under the floor (a hum keeps it
        up): the error is measured only while the note is still the note. */
    Verdict judge (const std::vector<Tick>& ticks, PitchTracker::Note want, double stopAt = 1.0e9)
    {
        Verdict v;
        const auto letter = [&want] (const Tick& k) { return k.live && ((k.midi - want.midi) % 12 + 12) % 12 == 0; };

        for (const auto& k : ticks)
            if (k.t >= 0.3 && k.rmsDb < floorDb)
            {
                v.floorAt = k.t;
                break;
            }
        const double until = v.floorAt >= 0.0 ? v.floorAt : stopAt;

        std::map<int, int> shownCount;
        int before = 0, live = 0, octaveRight = 0;
        for (const auto& k : ticks)
            if (k.t >= 0.0 && k.t < until)
            {
                ++before;
                if (k.live)
                {
                    ++live;
                    ++shownCount[k.midi];
                    octaveRight += k.midi == want.midi ? 1 : 0;
                }
            }
        v.livePct   = before > 0 ? 100.0 * live / before : 0.0;
        v.octavePct = live > 0 ? 100.0 * octaveRight / live : 0.0;
        int most = 0;
        for (const auto& [m, c] : shownCount)
            if (c > most)
            {
                most    = c;
                v.shown = m;
            }

        size_t lock = ticks.size();
        for (size_t i = 0; i + lockTicks <= ticks.size(); ++i)
        {
            if (ticks[i].t < 0.0)
                continue;
            bool ok = true;
            for (size_t j = i; j < i + lockTicks && ok; ++j)
                ok = letter (ticks[j]) && std::fabs (ticks[j].needle - want.cents) <= lockCents;
            if (ok)
            {
                lock     = i;
                v.lockMs = 1000.0 * ticks[i].t;
                break;
            }
        }

        std::vector<double> errs;
        for (size_t i = 0; i < ticks.size(); ++i)
        {
            const auto& k = ticks[i];
            if (k.t < 0.0)
                continue;
            if (letter (k))
            {
                v.heldTo = k.t;
                if (v.firstMs < 0.0)
                    v.firstMs = 1000.0 * k.t;
            }
            const size_t from = lock < ticks.size() ? lock : ticks.size();
            if ((lock < ticks.size() ? i >= from : k.t >= 0.3) && k.t < until && letter (k))
            {
                errs.push_back (k.needle - want.cents);
                if (i >= from + lockTicks || lock >= ticks.size())
                    v.errWorst = std::max (v.errWorst, (double) std::fabs (k.needle - want.cents));
            }
        }
        if (! errs.empty())
        {
            v.anyErr = true;
            std::sort (errs.begin(), errs.end());
            v.errMed = errs[errs.size() / 2];
        }

        int state = 0;   // 0 idle, 1 right, 2 octave up, 3 octave down, 4 another note
        for (const auto& k : ticks)
        {
            if (k.t < 0.0)
                continue;
            const int d  = k.midi - want.midi;
            const int st = ! k.live ? 0 : d == 0 ? 1 : (d == 12 || d == 24) ? 2 : (d == -12 || d == -24) ? 3 : 4;
            if (st != state)
            {
                if (st == 2) ++v.octUp;
                if (st == 3) ++v.octDown;
                if (st == 4) ++v.other;
                if ((st == 2 || st == 3) && v.firstOctAt < 0.0)
                    v.firstOctAt = k.t;
                state = st;
            }
        }
        return v;
    }

    std::string fmtMs (double ms) { char b[16]; ms < 0.0 ? std::snprintf (b, sizeof (b), "never") : std::snprintf (b, sizeof (b), "%.0f", ms); return b; }

    struct Played
    {
        PitchTracker::Note want;
        Verdict            v;
    };

    Played play (double hz, double cents, double sr, Voice voice, Hum hum, std::uint32_t seed)
    {
        const double f0 = hz * std::pow (2.0, cents / 1200.0);
        // The pitch a tuner means by this string: its fundamental, the stiff string's first partial.
        const auto want = PitchTracker::nearestNote ((float) (f0 * std::sqrt (1.0 + stiff)), reference);
        return { want, judge (listen (pluck (f0, sr, voice, hum, seed), sr), want) };
    }

    void printRun (const char* note, double cents, double sr, const char* voice, const Played& p)
    {
        const auto& v = p.v;
        std::printf ("  run %-3s %+4.0fc %5.1fk %-10s shown %-4s live %3.0f%% oct-right %3.0f%% first %5s lock %5s ms"
                     "  err %+6.1f worst %5.1f  held %4.2f / floor %4.2f s  oct up %d down %d other %d first %.2f s\n",
                     note, cents, sr / 1000.0, voice, v.shown < 0 ? "-" : noteText (v.shown).c_str(), v.livePct,
                     v.octavePct, fmtMs (v.firstMs).c_str(), fmtMs (v.lockMs).c_str(), v.errMed, v.errWorst, v.heldTo, v.floorAt, v.octUp,
                     v.octDown, v.other, v.firstOctAt);
    }

    /** One row of a table: the same note and condition at the three rates, folded — the shown
        notes as a set, the worst lock, the median of the medians and the worst error, the
        tightest hold, and the octave entries summed. */
    void printRow (const char* label, const std::vector<Played>& runs)
    {
        std::string shown;
        double firstWorst = 0.0, lockWorst = 0.0, errWorst = 0.0, heldMargin = 1.0e9, octPctMin = 100.0, livePctMin = 100.0;
        bool   neverLock = false, anyErr = false;
        int    up = 0, down = 0, other = 0;
        double firstOct = -1.0;
        std::vector<double> meds;

        for (const auto& p : runs)
        {
            const auto& v = p.v;
            const std::string s = v.shown < 0 ? "-" : noteText (v.shown);
            if (shown.find (s) == std::string::npos)
                shown += (shown.empty() ? "" : "/") + s;
            if (v.lockMs < 0.0)
                neverLock = true;
            lockWorst  = std::max (lockWorst, v.lockMs);
            firstWorst = v.firstMs < 0.0 || firstWorst < 0.0 ? -1.0 : std::max (firstWorst, v.firstMs);
            if (v.anyErr)
            {
                anyErr = true;
                meds.push_back (v.errMed);
                errWorst = std::max (errWorst, v.errWorst);
            }
            if (v.floorAt >= 0.0 && v.heldTo >= 0.0)
                heldMargin = std::min (heldMargin, v.heldTo - v.floorAt);
            octPctMin  = std::min (octPctMin, v.octavePct);
            livePctMin = std::min (livePctMin, v.livePct);
            up += v.octUp;
            down += v.octDown;
            other += v.other;
            if (v.firstOctAt >= 0.0 && (firstOct < 0.0 || v.firstOctAt < firstOct))
                firstOct = v.firstOctAt;
        }
        std::sort (meds.begin(), meds.end());

        char err[32] = "     -        -";
        if (anyErr)
            std::snprintf (err, sizeof (err), "%+6.1f   %5.1f", meds[meds.size() / 2], errWorst);
        char held[16] = "    -";
        if (heldMargin < 1.0e8)
            std::snprintf (held, sizeof (held), "%+5.2f", heldMargin);
        char first[16] = "   -";
        if (firstOct >= 0.0)
            std::snprintf (first, sizeof (first), "%4.2f", firstOct);

        std::printf ("%-24s %-14s %4.0f%% %4.0f%%  %s  %6s %6s  %s   %2d %2d %2d   %s\n", label, shown.c_str(), livePctMin,
                     octPctMin, err, fmtMs (firstWorst).c_str(), neverLock ? "never" : fmtMs (lockWorst).c_str(), held,
                     up, down, other, first);
    }

    void printHeader()
    {
        std::printf ("%-24s %-14s %5s %5s  %6s   %5s  %6s %6s  %5s   up dn ot   %s\n", "", "shown", "live", "oct", "err",
                     "worst", "first", "lock", "held", "1st-oct");
    }

    struct Bass
    {
        const char* name;
        double      hz;
    };

    constexpr Bass basses[] = {
        { "A0", 27.500 },  { "B0", 30.868 },  { "D1", 36.708 },  { "E1", 41.203 },
        { "A1", 55.000 },  { "D2", 73.416 },  { "G2", 97.999 },  { "C3", 130.813 },
    };
}

int main()
{
    if (const char* e = std::getenv ("ORBITAMP_BASS_TUNER_RUNS"); e != nullptr && *e == '1')
        perRun = true;

    std::uint32_t seed = 1;

    // ---- the harness hears: a guitar's A2, which the tuner has always read --------------------
    bool harnessHears = true;
    for (const double sr : rates)
    {
        const auto p = play (110.0, 0.0, sr, { 6.0, false, "finger" }, {}, seed++);
        harnessHears = harnessHears && p.v.shown == p.want.midi && p.v.lockMs >= 0.0 && p.v.errWorst <= lockCents;
    }
    std::printf ("harness: a guitar A2 reads A2 and locks at every rate: %s\n\n", harnessHears ? "ok" : "FAIL");

    // ---- 1. the range ---------------------------------------------------------------------------
    std::printf ("== 1. RANGE: a full-range finger pluck (fundamental +6 dB over the 2nd), folded over 44.1/48/96 kHz\n"
                 "   live/oct: least share of ticks before the floor showing anything / the right octave;\n"
                 "   err: median of the needle minus truth, cents (median over rates), worst |err| lock..floor;\n"
                 "   first/lock: slowest pluck to the first right letter / to a lock (within %.0f c for %d ticks), ms;\n"
                 "   held: last right tick minus the floor crossing, s (worst); up/dn/ot: entries into an octave up/down/another note, summed.\n",
                 lockCents, lockTicks);
    printHeader();
    const double offsets[] = { -40.0, -15.0, -5.0, 0.0, 5.0, 15.0, 40.0 };
    for (const auto& b : basses)
    {
        for (const double c : offsets)
        {
            std::vector<Played> runs;
            for (const double sr : rates)
            {
                runs.push_back (play (b.hz, c, sr, { 6.0, false, "finger" }, {}, seed++));
                if (perRun)
                    printRun (b.name, c, sr, "finger", runs.back());
            }
            char label[40];
            std::snprintf (label, sizeof (label), "%-3s %7.3f Hz %+4.0fc", b.name, b.hz * std::pow (2.0, c / 1200.0), c);
            printRow (label, runs);
        }
    }

    // ---- 2. octave errors -------------------------------------------------------------------------
    std::printf ("\n== 2. OCTAVE ERRORS: in tune, a weak fundamental, finger and slap, folded over 44.1/48/96 kHz\n"
                 "   1st-oct: the earliest tick (s after the pluck) on a wrong octave.\n");
    printHeader();
    const Voice voices[] = { { -6.0, false, "fund-6 finger" }, { -12.0, false, "fund-12 finger" },
                             { -6.0, true, "fund-6 slap" },    { -12.0, true, "fund-12 slap" } };
    for (const auto& b : basses)
        for (const auto& voice : voices)
        {
            std::vector<Played> runs;
            for (const double sr : rates)
            {
                runs.push_back (play (b.hz, 0.0, sr, voice, {}, seed++));
                if (perRun)
                    printRun (b.name, 0.0, sr, voice.name, runs.back());
            }
            char label[40];
            std::snprintf (label, sizeof (label), "%-3s %s", b.name, voice.name);
            printRow (label, runs);
        }

    // ---- 3. mains hum -----------------------------------------------------------------------------
    std::printf ("\n== 3. MAINS HUM under the note (full-range finger pluck, in tune), one line per rate\n"
                 "   alone: what the needle shows in the pre-roll, the hum by itself; lock as above, err/worst from\n"
                 "   the lock to hum-at: the first tick after the attack that reads the hum (within 10 c of it);\n"
                 "   drift: the first tick, after the lock has settled, with the needle more than 5 c off the note;\n"
                 "   last: the note and needle shown on the final tick, %.0f s after the pluck.\n", ringFor);
    struct HumCase { const char* name; double hz; double humHz; };
    const HumCase humCases[] = { { "G1", 48.999, 50.0 }, { "A#1", 58.270, 60.0 }, { "B1", 61.735, 60.0 } };
    for (const auto& hc : humCases)
        for (const double under : { 30.0, 40.0 })
            for (const double sr : rates)
            {
                const double f0   = hc.hz;
                const auto   want = PitchTracker::nearestNote ((float) (f0 * std::sqrt (1.0 + stiff)), reference);
                const auto   ticks = listen (pluck (f0, sr, { 6.0, false, "finger" }, { hc.humHz, under }, seed++), sr);

                // The hum takes over: the first tick after the attack that reads the hum itself.
                double humAt = -1.0;
                for (const auto& k : ticks)
                    if (k.t >= 0.3 && k.live && std::fabs (1200.0 * std::log2 (k.hz / hc.humHz)) < 10.0)
                    {
                        humAt = k.t;
                        break;
                    }
                const auto v = judge (ticks, want, humAt >= 0.0 ? humAt : 1.0e9);

                std::string alone = "-";
                for (const auto& k : ticks)
                    if (k.t < 0.0 && k.t > -0.1)
                    {
                        char b[32];
                        if (k.live)
                            std::snprintf (b, sizeof (b), "%s %+.0fc", noteText (k.midi).c_str(), k.needle);
                        alone = k.live ? b : "-";
                    }


                // The needle starts to leave the note: more than 5 c off, once the lock has settled.
                double driftAt = -1.0;
                if (v.lockMs >= 0.0)
                    for (const auto& k : ticks)
                        if (k.t * 1000.0 >= v.lockMs + 1000.0 * lockTicks / tickHz
                            && (! k.live || k.midi != want.midi || std::fabs (k.needle - want.cents) > 5.0))
                        {
                            driftAt = k.t;
                            break;
                        }

                const auto& last = ticks.back();
                char lastText[32] = "-";
                if (last.live)
                    std::snprintf (lastText, sizeof (lastText), "%s %+.0fc", noteText (last.midi).c_str(), last.needle);

                std::printf ("%-3s %6.3f Hz, %2.0f Hz hum -%2.0f dB, %5.1fk: alone %-10s lock %5s ms  err %+5.1f worst %5.1f"
                             "  drift %5.2f s  hum-at %5.2f s  last %s\n",
                             hc.name, f0, hc.humHz, under, sr / 1000.0, alone.c_str(), fmtMs (v.lockMs).c_str(), v.errMed,
                             v.errWorst, driftAt, humAt, lastText);
            }

    // The hum alone against each floor the player can pick (src/ui/Prefs.h tunerFloors), 48 kHz:
    // the levels a -15 dBFS note puts 30 and 40 dB under it.
    std::printf ("\n   the hum alone, one window, 48 kHz, against each level floor:\n");
    for (const double humHz : { 50.0, 60.0 })
        for (const double humDb : { noteDb - 30.0, noteDb - 40.0 })
        {
            std::printf ("   %2.0f Hz at %3.0f dBFS:", humHz, humDb);
            for (const double fl : { -80.0, -70.0, -60.0, -50.0 })
            {
                std::vector<float> w ((size_t) TunerTap::size);
                std::uint32_t rng = 99;
                for (int n = 0; n < TunerTap::size; ++n)
                    w[(size_t) n] = (float) (std::sqrt (2.0) * std::pow (10.0, humDb / 20.0) * std::sin (2.0 * pi * humHz * n / 48000.0)
                                             + 1.0e-4 * lcgNoise (rng));
                PitchTracker t;
                t.prepare (48000.0);
                t.setLevelFloorDb (fl);
                const auto r = t.analyse (w.data(), (int) w.size());
                char cell[32] = "nothing";
                if (r.hz > 0.0f)
                {
                    const auto n = PitchTracker::nearestNote (r.hz, reference);
                    std::snprintf (cell, sizeof (cell), "%s %+.0fc", noteText (n.midi).c_str(), n.cents);
                }
                std::printf ("  floor %3.0f: %-9s", fl, cell);
            }
            std::printf ("\n");
        }

    // ---- 4. cost ---------------------------------------------------------------------------------
    std::printf ("\n== 4. COST of one ear tick (tap copy + decimation + MPM), everything above, by rate\n");
    for (const auto& [sr, c] : costByRate)
        std::printf ("   %5.1f kHz: mean %6.1f us, worst %7.1f us over %ld ticks\n", sr / 1000.0,
                     c.calls > 0 ? c.totalUs / (double) c.calls : 0.0, c.worstUs, c.calls);

    return harnessHears ? 0 : 1;
}
