// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/oversampling/PolyphaseOversampler.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace orbitamp::core
{

/** OrbitAmp's DRIVE: a modest pedal in front of a strong captured preamp (the owner, 21.11). Not a copy of any pedal —
    in front of a NAM amp what matters is what a pedal does BEFORE the amp's own clipping: the low end it takes away,
    the mids it pushes, how it clips, and the level it hands on. Four types, voiced from what is publicly known of the
    circuits they are named after; our own instrument.

    TS, PRECISION and SD-1 are the op-amp that clips in its own feedback. The clean signal x passes at unity; the leg's
    current, i = HP(x) / Rg, flows into the feedback — Rf (the GAIN pot: Rf = Rg k, k the leg's gain less one) beside
    the diodes — whose voltage solves

        V / Rf + Id (V) = i,       Id the pair 2 Is sinh (V / n Vt), Is 2.52 nA, n 1.752, Vt 25.85 mV,

    and the pedal's output is x + V. Rf in it is why the pedal cleans up when played softly: a small i is V = k HP(x),
    a line, and only a large one meets the diodes. ONE Newton step per oversampled sample, from the last sample's V,
    held inside [0, Rf i] where the solution must lie (Diode, below; a gate holds it to a converged solve).

        TS         leg HP 720 Hz, its gain 6 -> 40 dB evenly in dB over GAIN, its high cut the 51 pF across Rf; the
                   symmetric pair. After the sum the hardware's FIXED low-pass at 723 Hz (1k and 220n) — the clipping
                   stage alone is a rising shelf, and shelf times this low-pass is the famous hump near 720-800 Hz —
                   then TONE a 1.5 kHz shelf -6 -> +6 dB (0 at noon), then a fixed low-pass at 8 kHz.     GAIN, TONE
        PRECISION  the TS's, ATTACK moving the LEG's high-pass 340 -> 4800 Hz (noon about 1.28 kHz) — what goes into the
                   clip, never the clean leg — its gain 6 -> 36 dB; the fixed 723 Hz low-pass, then BRIGHT a 2 kHz
                   shelf 0 -> +9 dB.                                                            GAIN, ATTACK, BRIGHT
        33         linear: TIGHT a second-order Butterworth high-pass on the whole signal, 60 -> 400 Hz (noon about
                   155 Hz), GAIN 0 -> +15 dB evenly in dB; the output's guard is its rail. Nothing after.  GAIN, TIGHT
        SD-1       an input high-pass at 88 Hz, leg HP 720 Hz, its gain 6 -> 44 dB, its high cut the 100 pF across Rf
                   (1.54 kHz at full drive); the ASYMMETRIC string — two diodes one way, one the other; a fixed
                   low-pass at 884 Hz, TONE a 1.5 kHz shelf -6 -> +6 dB, a fixed low-pass at 6 kHz.        GAIN, TONE

    After every clip a first-order DC block at 12 Hz (the SD-1's asymmetry makes DC; the others' none, at no cost).

    THE SCALE. 1 V is 0.5 in the float (-6 dBFS), every type: a DI peaking at -12 dBFS is a humbucker's half volt.
    The owner's own TS captures were trained at a level of their own — a NAM has none, the reamp chain of the day said
    which float meant which volt — and fed 9.5 dB under the DI they clip, compress and colour as this TS does
    (tests/DriveMatch.cpp: the 3rd and 5th harmonics within 1 dB, the crest within 0.2, every 1/3 octave within 2).

    THE LEVEL HANDED ON. VOLUME (`Settings::volume`, -12..+12 dB) is the output, after the tone and before the guard;
    at noon the pedal hands on the LOUDNESS it was given — integrated, K-weighted (BS.1770) on the reference DI, within
    0.3 LU at every GAIN, every type (`Voicing::trimDb`, a curve smooth in GAIN): GAIN changes how it clips and
    sounds, not how hard it pushes the amp; VOLUME does that. Smoothed: VOLUME eases to the hand in about 10 ms, the
    trim follows GAIN's own glide.

    THE CEILING. The output never passes `ceilingDb`, -1 dBFS peak. THE GUARD makes it so: nothing below -2.5 dBFS,
    then a soft knee to a hard stop just under -1, inside the oversampled loop — the 33's rail — and at the session's
    rate one hard limit at exactly -1 for the half-band's ripple over a stop pressed hard (`guarded` counts it). No
    RMS normalising: the level moves with the playing, as a pedal's does.

    OVERSAMPLED 8x (4x past 50 kHz, 2x past 100 kHz) where it clips, through core's Kaiser polyphase oversampler at
    64 taps a phase: the clean leg and the clipped one go up together, so they meet in time, and the round trip is 63
    samples whatever the factor. The 33 runs at the session's rate, delayed by the same 63: the block's latency
    (`latencySamples`) is one number for every type. The leg's linear filters run at the session's rate before the
    loop; inside it the diode solve, the sum, the DC block, the tone network and the guard — the tone inside so that
    the guard is the last thing before the way down, and the ceiling holds.

    SMOOTHED on a 64-sample grid: GAIN glides linearly in dB, every knob at most its whole travel in 20 ms, the
    coefficients recomputed once a grid; a change of type fades out over 20 ms, swaps, starts from rest and fades in.

    JUCE-free and real-time safe after `prepare` (which allocates the oversampler): no allocation, no locks. Mono — one
    per channel. NaN and infinity in are zero. Knob values are the face's 0..10. */
class DrivePedal
{
public:
    enum class Type : int { ts, precision, thirtyThree, sd1, count };

    /** TEMPORARY (the owner, 21.11: to choose by ear; out before any release, like the portable set's IN/OUT knobs):
        the clip law of TS, PRECISION and SD-1. DIODE is the solved law, each type's own (the pair; SD-1's string); TANH
        and HARD take the leg's linear voltage, Rf i, to `knee` softly or by a clamp; ASYM is the 2:1 string on any type. */
    enum class Clip : int { diode, tanh, hard, asym, count };
    static constexpr double knee = 0.55;   // volts: where the pair's forward drop stands at a few hundred microamps

    struct Settings
    {
        Type  type   = Type::ts;
        float gain   = 5.0f;   // 0..10, every type
        float tone   = 5.0f;   // 0..10, TS and SD-1
        float attack = 5.0f;   // 0..10, PRECISION
        float bright = 5.0f;   // 0..10, PRECISION
        float tight  = 5.0f;   // 0..10, 33
        float volume = 0.0f;   // dB, -12..+12, every type: 0 hands on the loudness it was given
        Clip  clip   = Clip::diode;   // TEMPORARY
    };

    /** The pedal's output never passes this, dBFS peak; the guard begins at `guardFromDb`. */
    static constexpr float ceilingDb   = -1.0f;
    static constexpr float guardFromDb = -2.5f;
    static constexpr float ceiling() noexcept { return 0.891250938f; }   // 10^(ceilingDb / 20)
    static constexpr float volumeRangeDb = 12.0f;                         // VOLUME: -12..+12 dB

    /** THE VOICING'S NUMBERS that were matched against the owner's own TS (tests/DriveMatch.cpp: his trained captures
        TS-0001/0003/0005 on one DI, 1/3 octave by 1/3 octave) — here so that the tool can move them; the amp never does. */
    struct Voicing
    {
        double volt = 0.5;              // one volt in the float, every type: a DI at -12 dBFS is a humbucker's half volt
        double tsPostHz = 600.0;        // TS's fixed low-pass after the sum (723 by the book)
        double tsShelfNoonDb = 9.0;     // TONE's shelf at noon, TS; the knob is +-6 dB about it (0 by the book)
        double tsLegLoDb = 18.5, tsLegHiDb = 38.0;  // TS's leg gain at GAIN 0 and 10, even in dB between (6..40)
        double tsLpHz = 8500.0;         // TS's fixed low-pass at the top (8000)
        int    factor = 0;              // the oversampling, 0 = by the rate (`prepare`); a gate measures 4 against 8

        /** THE TRIM at VOLUME noon, dB, per type at GAIN 0, 1, .. 10 — a Catmull-Rom curve between: what makes the
            pedal's output as loud as its input on the reference DI (tests/DriveMatch.cpp `trims` fits it). */
        std::array<std::array<double, 11>, (size_t) Type::count> trimDb {{
            {{ -9.35, -10.08, -10.73, -11.29, -11.78, -12.20, -12.55, -12.85, -13.10, -13.31, -13.48 }},   // TS
            {{ -0.09, -1.21, -2.65, -4.21, -5.69, -7.04, -8.13, -9.06, -9.80, -10.41, -10.87 }},       // PRECISION
            {{ 0.49, -1.01, -2.51, -4.01, -5.51, -7.01, -8.51, -10.01, -11.51, -13.01, -14.51 }},      // 33: its push, undone
            {{ -0.86, -3.14, -5.48, -7.72, -9.75, -11.40, -12.70, -13.66, -14.35, -14.83, -15.18 }},   // SD-1
        }};
    };
    Voicing voicing;

    static constexpr int taps = 64;
    static constexpr int grid = 64;

    /** THE FEEDBACK'S DIODES, solved. Public for the gate that holds the one Newton step to a converged solve. */
    struct Diode
    {
        static constexpr double is = 2.52e-9, n = 1.752, vt = 0.02585;

        /** Id (V) and its slope: the pair, or the asymmetric string (two diodes for V > 0, one for V < 0). */
        static void current (double v, bool asymmetric, double& id, double& slope) noexcept
        {
            const double nvt = n * vt;
            if (! asymmetric)
            {
                const double e = std::exp (std::clamp (v / nvt, -80.0, 80.0)), r = 1.0 / e;
                id    = is * (e - r);                  // 2 Is sinh
                slope = is / nvt * (e + r);            // 2 Is cosh / n Vt
            }
            else
            {
                const double up = std::exp (std::clamp (v / (2.0 * nvt), -80.0, 80.0));
                const double dn = std::exp (std::clamp (-v / nvt, -80.0, 80.0));
                id    = is * (up - 1.0) - is * (dn - 1.0);
                slope = is / (2.0 * nvt) * up + is / nvt * dn;
            }
        }

        /** Where V would stand with the diodes alone taking all of i — past the solution, since Rf takes some: the
            pair's n Vt asinh (i / 2 Is), or the string's 2 n Vt ln (1 + i / Is) one way and n Vt ln (1 + |i| / Is)
            the other. */
        static double diodesAlone (double i, bool asymmetric) noexcept
        {
            const double nvt = n * vt;
            if (! asymmetric)
                return nvt * std::asinh (i / (2.0 * is));
            return i >= 0.0 ? 2.0 * nvt * std::log1p (i / is) : -nvt * std::log1p (-i / is);
        }

        /** Newton for V / rf + Id (V) = i from `v`, held inside the bracket the solution lies in: between 0 and the
            nearer of rf i (Rf alone) and `diodesAlone` (the diodes alone), the latter asked only where the diodes can
            conduct. ONE step, almost always: up to three more only where a step itself moved 1 mV or more — at
            high drive the solution crosses the whole line in one oversampled sample near every zero crossing, and a
            single step from the far side of the knee lands tens of millivolts off. */
        static double step (double i, double rf, double v, bool asymmetric) noexcept
        {
            for (int k = 0; k < 4; ++k)
            {
                const double next = stepOnce (i, rf, v, asymmetric);
                if (std::abs (next - v) < 1.0e-3)
                    return next;
                v = next;
            }
            return v;
        }

        /** One Newton step, bracketed: between 0 and the nearer of rf i (Rf alone) and `diodesAlone` (the diodes
            alone), the latter asked only where the diodes can conduct. A step that leaves the bracket lands on its edge
            and steps once more from there — from above, where the curve is convex, Newton never overshoots. */
        static double stepOnce (double i, double rf, double v, bool asymmetric) noexcept
        {
            const double lin = rf * i;
            double lo = std::min (0.0, lin), hi = std::max (0.0, lin);
            if (std::abs (lin) > 0.2)
            {
                const double alone = diodesAlone (i, asymmetric);
                if (i > 0.0) hi = std::min (hi, alone);
                else         lo = std::max (lo, alone);
            }
            v = std::clamp (v, lo, hi);
            const auto newton = [&] (double from)
            {
                double id, slope;
                current (from, asymmetric, id, slope);
                return from - (from / rf + id - i) / (1.0 / rf + slope);
            };
            const double next = newton (v);
            if (next >= lo && next <= hi)
                return next;
            return std::clamp (newton (next < lo ? lo : hi), lo, hi);
        }

        /** The solution, converged (bisection to the last bit) — the gate's reference. */
        static double solve (double i, double rf, bool asymmetric) noexcept
        {
            double lo = std::min (0.0, rf * i), hi = std::max (0.0, rf * i);
            for (int k = 0; k < 200; ++k)
            {
                const double mid = 0.5 * (lo + hi);
                double id, slope;
                current (mid, asymmetric, id, slope);
                (mid / rf + id - i > 0.0 ? hi : lo) = mid;
            }
            return 0.5 * (lo + hi);
        }
    };

    /** A fast tanh, odd, its slope 0 where it reaches ±1 at ±3 (a Padé form): the guard's knee. */
    static float fastTanh (float x) noexcept
    {
        const float c = std::clamp (x, -3.0f, 3.0f);
        const float c2 = c * c;
        return c * (27.0f + c2) / (27.0f + 9.0f * c2);
    }

    /** Allocates the oversampler: prepareToPlay only. */
    void prepare (double sampleRate)
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        factor = voicing.factor > 0 ? voicing.factor : rate <= 50000.0 ? 8 : (rate <= 100000.0 ? 4 : 2);
        ready = oversampler.prepare (factor, 2, taps);
        fadeStep = (float) (grid * 1000.0 / (fadeMs * rate));
        slewKnob = (float) (10.0 * grid * 1000.0 / (glideMs * rate));
        volumeEase = 1.0 - std::exp (-grid * 1000.0 / (volumeMs * rate));
        guardedCount = 0;
        reset();
    }

    /** From rest: every state cleared, the knobs taken where they stand at the next block. */
    void reset() noexcept
    {
        oversampler.reset();
        inCut = {}; legCut = {}; capCut = {}; post = {}; lowpass = {}; dcBlock = {}; shelf = {};
        tight = {};
        delay.fill (0.0f);
        delayAt = 0;
        v = 0.0;
        primed = primedGrid = false;
        phase = Phase::steady;
        fadeGain = fadeFrom = 1.0f;
    }

    /** What the block delays, samples at the session's rate: the oversampler's round trip, every type alike. */
    static constexpr int latencySamples() noexcept { return taps - 1; }
    int oversampling() const noexcept { return factor; }
    int roundTrip() const noexcept    { return oversampler.latencySamples(); }   // == latencySamples() (the gate)

    /** How many samples since `prepare` the hard limit at the session's rate had to touch. */
    long long guarded() const noexcept { return guardedCount; }

    /** The trim at VOLUME noon for `type` at `gain` (0..10), dB: `Voicing::trimDb`, Catmull-Rom between its steps. */
    double trimAt (Type type, float gain) const noexcept
    {
        const auto& p = voicing.trimDb[(size_t) std::clamp ((int) type, 0, (int) Type::count - 1)];
        const double x = unit (gain) * 10.0;
        const int i = std::min (9, (int) x);
        const double f = x - i;
        const double p0 = p[(size_t) std::max (i - 1, 0)], p1 = p[(size_t) i], p2 = p[(size_t) i + 1], p3 = p[(size_t) std::min (i + 2, 10)];
        return p1 + 0.5 * f * (p2 - p0 + f * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + f * (3.0 * (p1 - p2) + p3 - p0)));
    }

    /** In place, `n` samples of one channel. */
    void process (float* x, int n, const Settings& s) noexcept
    {
        if (! ready)
            return;

        target = s;
        if (! primed)
        {
            now = s;
            playing = s.type;
            primed = true;
            retune();
        }

        for (int done = 0; done < n; done += grid)
        {
            const int m = std::min (grid, n - done);
            stepGrid ((float) m / (float) grid);
            run (x + done, m);
        }

        flushTiny();
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double fadeMs = 20.0, glideMs = 20.0, volumeMs = 10.0;

    /** A TPT one-pole, its state in double: the 12 Hz DC block runs at eight times 48 kHz. */
    struct OnePole
    {
        double g = 0.0, s = 0.0;
        void set (double hz, double fs) noexcept
        {
            const double w = std::tan (pi * std::min (hz, 0.45 * fs) / fs);
            g = w / (1.0 + w);
        }
        double lowpass (double x) noexcept
        {
            const double u = (x - s) * g, lp = u + s;
            s = lp + u;
            return lp;
        }
        double highpass (double x) noexcept { return x - lowpass (x); }
        void flush() noexcept { if (std::abs (s) < 1.0e-25) s = 0.0; }
    };

    /** A second-order state-variable high-pass (Simper's), stable under a moving corner. */
    struct HighPass2
    {
        double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.41421356237;
        double ic1 = 0.0, ic2 = 0.0;
        void set (double hz, double q, double fs) noexcept
        {
            const double g = std::tan (pi * std::min (hz, 0.45 * fs) / fs);
            k  = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        double process (double x) noexcept
        {
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return x - k * v1 - v2;
        }
        void flush() noexcept
        {
            if (std::abs (ic1) < 1.0e-25) ic1 = 0.0;
            if (std::abs (ic2) < 1.0e-25) ic2 = 0.0;
        }
    };

    static double unit (float knob) noexcept { return (double) std::clamp (knob, 0.0f, 10.0f) / 10.0; }
    static double sweep (float knob, double lo, double hi) noexcept { return lo * std::pow (hi / lo, unit (knob)); }

    /** One grid's smoothing (`share` of a whole grid): a type change's fade, the knobs toward the hand, the coefficients. */
    void stepGrid (float share) noexcept
    {
        fadeFrom = fadeGain;
        rfFrom = rfTo;
        boostFrom = boostTo;
        outFrom = outTo;

        // A CHANGE OF TYPE: out, swap from rest, in.
        if (phase == Phase::steady && target.type != playing)
            phase = Phase::out;
        if (phase == Phase::out)
        {
            if (fadeGain <= 0.0f)
            {
                reset();   // from rest — `primed` too, the fade and the phase
                primed = true;
                playing = target.type;
                now = target;
                retune();
                phase = Phase::in;
                fadeGain = fadeFrom = 0.0f;
                return;
            }
            fadeGain = std::max (0.0f, fadeGain - fadeStep * share);
        }
        else if (phase == Phase::in)
        {
            fadeGain = std::min (1.0f, fadeGain + fadeStep * share);
            if (fadeGain >= 1.0f)
                phase = Phase::steady;
        }

        // Every knob at most its whole travel in 20 ms — GAIN's in dB, as the laws below are even in dB.
        bool moved = false;
        const float most = slewKnob * share;
        for (auto [knob, aim] : { std::pair<float*, float> { &now.gain, target.gain }, std::pair<float*, float> { &now.tone, target.tone },
                                  std::pair<float*, float> { &now.attack, target.attack }, std::pair<float*, float> { &now.bright, target.bright },
                                  std::pair<float*, float> { &now.tight, target.tight } })
        {
            const float d = aim - *knob;
            if (std::abs (d) < 1.0e-6f)
                continue;
            *knob += std::clamp (d, -most, most);
            moved = true;
        }
        if (moved)
            retune();

        // VOLUME eases to the hand in about 10 ms; the trim rides GAIN's glide. Both a line across the grid.
        const double aim = std::clamp ((double) target.volume, (double) -volumeRangeDb, (double) volumeRangeDb);
        volumeNow = std::abs (aim - volumeNow) < 1.0e-4 ? aim : volumeNow + (aim - volumeNow) * volumeEase * share;
        outTo = std::pow (10.0, (trimAt (playing, now.gain) + volumeNow) / 20.0);
    }

    /** The type's circuit for the knobs where they now stand. */
    void retune() noexcept
    {
        const double fs = rate, os = rate * factor, g = unit (now.gain);
        voltNow = voicing.volt;
        double legDb = 0.0, legHz = 720.0, rg = 4700.0, capF = 51.0e-12, postHz = 723.0, shelfHz = 1500.0, shelfDb = 0.0,
               lpHz = 0.0;
        linear = false; asymmetric = false;

        switch (playing)
        {
            case Type::ts:
                legDb   = voicing.tsLegLoDb + (voicing.tsLegHiDb - voicing.tsLegLoDb) * g;
                postHz  = voicing.tsPostHz;
                shelfDb = voicing.tsShelfNoonDb - 6.0 + 12.0 * unit (now.tone);   // TONE: +-6 dB over 1.5 kHz about noon's
                lpHz    = voicing.tsLpHz;
                break;
            case Type::precision:
                legDb   = 6.0 + 30.0 * g;
                legHz   = sweep (now.attack, 340.0, 4800.0);
                shelfHz = 2000.0;
                shelfDb = 9.0 * unit (now.bright);         // BRIGHT: 0 .. +9 dB over 2 kHz
                break;
            case Type::thirtyThree:
                linear = true;
                tight.set (sweep (now.tight, 60.0, 400.0), 0.70710678118654752, fs);
                boostTo = std::pow (10.0, 15.0 * g / 20.0);                         // 0 .. +15 dB
                break;
            case Type::sd1:
                legDb  = 6.0 + 38.0 * g;
                rg     = 6580.0;    // the 1M drive's 44 dB: its 100 pF meets 1.54 kHz at the top
                capF   = 100.0e-12;
                asymmetric = true;
                postHz  = 884.0;
                shelfDb = -6.0 + 12.0 * unit (now.tone);
                lpHz    = 6000.0;
                inCut.set (88.0, fs);
                break;
            case Type::count:
                break;
        }

        if (! linear)
        {
            const double k = std::pow (10.0, legDb / 20.0) - 1.0;
            rgNow = rg;
            rfTo  = rg * k;
            legCut.set (legHz, fs);
            capCut.set (1.0 / (2.0 * pi * rfTo * capF), fs);
            post.set (postHz, os);
            shelf.set (shelfHz, os);
            shelfGain = std::pow (10.0, shelfDb / 20.0) - 1.0;
            lpOn = lpHz > 0.0;
            if (lpOn)
                lowpass.set (lpHz, os);
            dcBlock.set (12.0, os);
        }
        if (! primedGrid)   // from rest: no glide from wherever it was
        {
            rfFrom = rfTo;
            boostFrom = boostTo;
            volumeNow = std::clamp ((double) now.volume, (double) -volumeRangeDb, (double) volumeRangeDb);
            outTo = outFrom = std::pow (10.0, (trimAt (playing, now.gain) + volumeNow) / 20.0);
            primedGrid = true;
        }
    }

    /** THE GUARD inside the loop: nothing below -2.5 dBFS, then a knee to a hard stop at 0.94 of the ceiling (-1.5 dBFS):
        the rest is room for the way down's own ripple. */
    static float guard (float y) noexcept
    {
        constexpr float from = 0.749894209f, stop = 0.94f * ceiling(), room = stop - from;   // 10^(guardFromDb / 20)
        const float a = std::abs (y);
        if (a <= from)
            return y;
        return std::copysign (from + room * fastTanh ((a - from) / room), y);
    }

    void run (float* x, int m) noexcept
    {
        // The leg's linear filters and the clean leg, at the session's rate, in volts.
        for (int i = 0; i < m; ++i)
        {
            const float raw = x[i];
            double in = std::isfinite (raw) ? (double) raw / voltNow : 0.0;
            if (linear)
            {
                const double t = (double) (i + 1) / (double) m;
                const double y = tight.process (in) * (boostFrom + (boostTo - boostFrom) * t);
                clean[(size_t) i] = delay[(size_t) delayAt];   // 63 samples ago: the clipping types' round trip
                delay[(size_t) delayAt] = (float) y;
                delayAt = (delayAt + 1) % (int) delay.size();
                continue;
            }
            if (asymmetric)
                in = inCut.highpass (in);
            clean[(size_t) i] = (float) in;
            leg[(size_t) i] = (float) capCut.lowpass (legCut.highpass (in));   // volts across Rg
        }

        if (linear)
        {
            for (int i = 0; i < m; ++i)
                x[i] = limit (guard ((float) (clean[(size_t) i] * voltNow * outAt ((double) (i + 1) / (double) m))) * fadeAt (i, m));
            return;
        }

        // Up, the clip, the sum, the DC block, the tone and the guard, down.
        const float* ins[2] = { clean.data(), leg.data() };
        float* ups[2] = { upClean.data(), upLeg.data() };
        oversampler.upsample (ins, 2, m, ups);
        const int total = m * factor;
        for (int j = 0; j < total; ++j)
        {
            const double t  = (double) (j + 1) / (double) total;
            const double rf = rfFrom + (rfTo - rfFrom) * t;
            const double i = (double) upLeg[(size_t) j] / rgNow;
            switch (target.clip)   // TEMPORARY: the law by ear; DIODE is the pedal
            {
                case Clip::tanh: v = knee * std::tanh (rf * i / knee); break;
                case Clip::hard: v = std::clamp (rf * i, -knee, knee); break;
                case Clip::asym: v = Diode::step (i, rf, v, true); break;
                case Clip::diode:
                case Clip::count:
                default:         v = Diode::step (i, rf, v, asymmetric); break;
            }
            double y = post.lowpass (dcBlock.highpass ((double) upClean[(size_t) j] + v));
            y += shelfGain * (y - shelf.lowpass (y));
            if (lpOn)
                y = lowpass.lowpass (y);
            upClean[(size_t) j] = guard ((float) (y * voltNow * outAt (t)));
        }
        const float* back[1] = { upClean.data() };
        float* outs[1] = { x };
        oversampler.downsample (back, 1, m, outs);
        for (int i = 0; i < m; ++i)
            x[i] = limit (x[i] * fadeAt (i, m));
    }

    /** The output's gain — the trim and VOLUME — `t` of the way across the grid. */
    double outAt (double t) const noexcept { return outFrom + (outTo - outFrom) * t; }

    float fadeAt (int i, int m) const noexcept
    {
        return fadeFrom + (fadeGain - fadeFrom) * ((float) (i + 1) / (float) m);
    }

    /** The promise at the session's rate: never past the ceiling. */
    float limit (float y) noexcept
    {
        constexpr float c = ceiling();
        if (std::abs (y) <= c)
            return y;
        ++guardedCount;
        return std::copysign (c, y);
    }

    void flushTiny() noexcept
    {
        for (OnePole* p : { &inCut, &legCut, &capCut, &post, &lowpass, &dcBlock, &shelf })
            p->flush();
        tight.flush();
        if (std::abs (v) < 1.0e-25)
            v = 0.0;
    }

    enum class Phase { steady, out, in };

    double rate = 48000.0;
    int factor = 8;
    bool ready = false, primed = false, primedGrid = false;
    felitronics::oversampling::PolyphaseOversampler oversampler;
    std::array<float, grid> clean {}, leg {};
    std::array<float, grid * 8> upClean {}, upLeg {};
    std::array<float, taps - 1> delay {};
    int delayAt = 0;

    Settings now, target;
    Type playing = Type::ts;
    Phase phase = Phase::steady;
    float fadeGain = 1.0f, fadeFrom = 1.0f, fadeStep = 0.05f, slewKnob = 0.3f;

    OnePole inCut, legCut, capCut, post, lowpass, dcBlock, shelf;
    HighPass2 tight;
    bool linear = false, asymmetric = false, lpOn = true;
    double voltNow = 0.5, shelfGain = 0.0, rgNow = 4700.0, rfFrom = 4700.0, rfTo = 4700.0, boostFrom = 1.0, boostTo = 1.0, v = 0.0;
    double volumeNow = 0.0, volumeEase = 0.1, outFrom = 1.0, outTo = 1.0;
    long long guardedCount = 0;
};

} // namespace orbitamp::core
