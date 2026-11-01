// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "TunerTap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

namespace orbitamp::core
{

/** The polyphonic tuner's ear: strum the open strings, read every string's deviation at once.

    WHAT IT HEARS. The raw input (what TunerTap holds), anti-aliased and decimated to ~2.4 kHz into
    a history of its own. A strum is an ATTACK — the envelope rising 3 dB within 40 ms. The first
    250 ms after the last attack are skipped (a plucked string starts sharp and settles); the first
    reading comes once a second of ringing is in, and from there the span grows with the ringing, up
    to ~1.7 s, re-analysed every 200 ms.

    HOW IT MEASURES. Every partial of every string below the highest second harmonic in use is a
    COMPONENT: a damped complex exponential with its own frequency, decay and amplitude. The span is
    Hann-windowed and transformed once; a component's spectrum is the window's kernel, known in
    closed form for any frequency and decay, so the spectrum is fitted, not read. Components whose
    main lobes overlap form a CLUSTER, fitted jointly by Levenberg-Marquardt against the spectrum
    near them — every other component's leakage subtracted (backfitting). The frequency comes out of
    the fit with the precision of the phase across the whole span, not of a bin: that is the
    vocoder's phase-difference estimate taken to its limit, and it is what reads a low B (30.9 Hz)
    to a fraction of a cent.

    SHARED HARMONICS. A tuning puts strings on each other's harmonics — a guitar's high E is the low
    E's 4th and the A's 3rd partial; in drop D the D string IS the low D's octave. Each string is a
    STIFF-STRING MODEL (f0, inharmonicity B; partial h at h f0 sqrt(1 + B h^2)) fitted to its own
    partials, weighted by how well each was measured. Inside a cluster, every component is held to
    where its string's OTHER partials put it (a prior, leave-one-out, so a partial never vouches for
    itself): a low E's 4th partial is pinned by its 1st, 2nd, 5th, 7th..., and the high E is what is
    left beside it. Measurements carry only the data's information (plus the OTHER strings' priors),
    so a partial buried under another string's weighs nothing in its own string's model.

    MASKED. A string whose every peak lies on another string's partial is tested for whether it adds
    anything at all: the cluster is refitted without it, and if the residual barely moves, the string
    is MASKED — muted, or in exact unison with a harmonic it cannot be told from. A face shows that
    as "damp the others, pluck it alone", never as a reading. In a full strum that is a guitar's B and
    high E every time: an in-tune B stands 2.7-4.4 c from the low E's 3rd partial, inside one bin of a
    1.7 s span (4 c at 247 Hz) — no rule tells them apart there, and plucked over the chord still
    ringing the B stays covered. A strum of the top four reads all four.

    MAINS HUM. 50 or 60 Hz lines are learned before a strum (the quiet stretch before the attack) and
    then fitted as components of their own, held to their line and to no decay — a bass A string
    5 Hz from the hum, a B string's octave 1.7 Hz from 60, are read beside it, not through it. The
    mains is TRACKED, never taken for exact: the grid wanders 49.9-50.1 Hz every day, and its k-th
    harmonic is k times that off k x 50 — at 50.2 Hz the 4th stands 0.8 Hz off 200, on a G string.

    WITHHOLD RATHER THAN LIE. A confident wrong number is the defect. Where the evidence does not hold
    — a string on another's partial or on a mains line, halves of the span that disagree, a model that
    does not fit — the lane says "pluck alone" or waits, never a number. What is published is within
    a cent of the string's own sustained pitch (the main plane of a string that beats), or withheld.

    Off the audio thread, JUCE-free and headless like PitchTracker: push() or update() on the tuner's
    own timer, read reading(). Everything is allocated in prepare(); nothing per tick. */
class PolyTuner
{
public:
    static constexpr int maxStrings = 6;

    /** A tuning is data: the open strings' notes, lowest first. */
    struct Tuning
    {
        const char* id;
        const char* name;
        int         strings;
        std::array<int, maxStrings> midi;   // MIDI note numbers, 69 = A4
    };

    static constexpr std::array<Tuning, 9> tunings { {
        { "guitar",            "Guitar standard",          6, { 40, 45, 50, 55, 59, 64 } },
        { "guitar-drop-d",     "Guitar drop D",            6, { 38, 45, 50, 55, 59, 64 } },
        { "guitar-half-down",  "Guitar half step down",    6, { 39, 44, 49, 54, 58, 63 } },
        { "guitar-whole-down", "Guitar whole step down",   6, { 38, 43, 48, 53, 57, 62 } },
        { "bass-4",            "Bass 4 strings",           4, { 28, 33, 38, 43 } },
        { "bass-5",            "Bass 5 strings",           5, { 23, 28, 33, 38, 43 } },
        { "bass-6",            "Bass 6 strings",           6, { 23, 28, 33, 38, 43, 48 } },
        { "bass-drop-d",       "Bass drop D",              4, { 26, 33, 38, 43 } },
        { "bass-half-down",    "Bass half step down",      4, { 27, 32, 37, 42 } },
    } };

    enum class Heard : std::uint8_t
    {
        no,       // nothing of this string's in its window
        masked,   // its window holds only other strings' partials — muted, or in unison with one
        near,     // read, within farCents of its note
        far       // read, further than farCents — a big detune, or the wrong string
    };

    struct String
    {
        Heard heard      = Heard::no;
        float cents      = 0.0f;     // deviation from the target (near and far)
        float hz         = 0.0f;     // the string's first partial
        float targetHz   = 0.0f;
        float sigmaCents = 0.0f;     // one standard deviation of `cents`, from the fit
        float levelDb    = -200.0f;  // the string's strongest of partials 1-2, dBFS
    };

    struct Reading
    {
        int      strings = 0;
        std::array<String, maxStrings> string {};
        float    spanMs  = 0.0f;   // how much ringing the reading rests on
        double   endSec  = 0.0;    // where that span ends: seconds of input since prepare() / reset()
        unsigned strum   = 0;      // counts strums: a new one starts a new reading
        bool     mono    = false;  // exactly one string heard — a single pluck
    };

    static constexpr float farCents   = 100.0f;   // beyond a semitone: far
    static constexpr float reachCents = 220.0f;   // how far from its note a string is looked for

    //==========================================================================
    PolyTuner() { setTuning (tunings[0]); }

    void prepare (double sampleRate)
    {
        fs    = std::max (8000.0, sampleRate);
        decim = std::max (1, (int) std::lround (fs / decimRate));
        fsD   = fs / decim;
        designAntiAlias();

        settleN  = (std::uint64_t) std::lround (settleSec * fsD);
        minN     = (int) std::lround (minSpanSec * fsD);
        cadenceN = (std::uint64_t) std::lround (cadenceSec * fsD);
        blockLen = std::max (4, (int) std::lround (0.010 * fsD));
        envA     = 1.0 - std::exp (-1.0 / (0.008 * fsD));
        levelA   = 1.0 - std::exp (-1.0 / (0.05 * fsD));

        hist.assign ((size_t) histCap, 0.0f);
        chunk.assign ((size_t) (TunerTap::size - TunerTap::guard), 0.0f);
        spec.assign ((size_t) maxL, cd {});
        humSpec.assign ((size_t) maxL, cd {});
        humMag.assign ((size_t) maxL / 2 + 1, 0.0);
        mag.assign ((size_t) maxL / 2 + 1, 0.0);
        scratch.assign ((size_t) maxL / 2 + 1, 0.0);
        twiddle.resize ((size_t) maxL / 2);
        for (int i = 0; i < maxL / 2; ++i)
            twiddle[(size_t) i] = std::polar (1.0, -2.0 * pi * i / maxL);

        ybuf.assign ((size_t) maxProbes, cd {});
        res.assign ((size_t) 2 * maxProbes, 0.0);
        resTry.assign ((size_t) 2 * maxProbes, 0.0);
        jac.assign ((size_t) 2 * maxProbes * maxParams, 0.0);
        jacTry.assign ((size_t) 2 * maxProbes * maxParams, 0.0);
        kern.assign ((size_t) 4 * maxClusterComps * maxProbes, cd {});

        reset();
        retarget();
    }

    void reset()
    {
        histCount = 0;
        for (auto& z : aaZ)
            z[0] = z[1] = 0.0;
        hpZ[0] = hpZ[1] = hpSum = 0.0;
        phaseCount = 0;
        env1 = env2 = 0.0;
        blockFill = 0;
        blocksSeen = 0;
        ring.fill (0.0);
        anyAttack = false;
        lastAttack = 0;
        lastAnalysed = 0;
        analysedStrum = ~0u;
        strumCount = 0;
        humHz = spanHumHz = 0.0;
        humMask = spanHumMask = 0;
        job.busy = false;
        bbLevel = strumPeak = 0.0;
        strummed = false;
        lastHumAt = 0;
        tapValid = false;
        out = {};
        out.strings = tuning.strings;
        for (int s = 0; s < tuning.strings; ++s)
            out.string[(size_t) s].targetHz = (float) target[(size_t) s];
    }

    void setTuning (const Tuning& t)
    {
        tuning = t;
        tuning.strings = std::clamp (t.strings, 1, maxStrings);
        retarget();
    }

    const Tuning& currentTuning() const noexcept { return tuning; }

    /** The pitch A4 is tuned to — every target is counted from it. */
    void  setReferenceHz (float hz) { a4 = hz > 0.0f ? hz : 440.0f; retarget(); }
    float referenceHz() const noexcept { return (float) a4; }

    /** The quietest span that is analysed at all, RMS in dBFS. */
    void setLevelFloorDb (double db) noexcept { rmsFloor = std::pow (10.0, db / 20.0); }

    /** Feeds raw input, any block size. Cheap — the analysis runs in analyse() / update(). */
    void push (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            double v = x[i];
            for (int s = 0; s < 4; ++s)
            {
                const auto& c = aa[s];
                const double yv = c.b0 * v + aaZ[s][0];
                aaZ[s][0] = c.b1 * v - c.a1 * yv + aaZ[s][1];
                aaZ[s][1] = c.b2 * v - c.a2 * yv;
                v = yv;
            }
            // The attack band: above 1 kHz a pick is loud and a ringing strum's partials are long
            // gone — and below it strings ringing together BEAT, swinging the envelope from near
            // nothing to double several times a second, every swing a false pluck.
            const double xin = x[i];
            const double hp = hpf.b0 * xin + hpZ[0];
            hpZ[0] = hpf.b1 * xin - hpf.a1 * hp + hpZ[1];
            hpZ[1] = hpf.b2 * xin - hpf.a2 * hp;
            hpSum += hp * hp;

            if (++phaseCount >= decim)
            {
                phaseCount = 0;
                pushDecimated ((float) v, hpSum / decim);
                hpSum = 0.0;
            }
        }
    }

    /** One tick of the tuner's timer: take what the tap got since the last tick, and analyse when
        a new span is due. Returns true when reading() changed.

        The samples come through TunerTap::readSince, the strobe's path: never the newest stretch the
        audio thread may be writing into, and never a sample the writer lapped under the copy. The
        first tick takes as much of the ring's past as that leaves — a strum may already be ringing. */
    bool update (const TunerTap& tap)
    {
        if (chunk.empty())
            return false;   // not prepared

        const bool first = ! tapValid;
        if (first)
        {
            const unsigned t = tap.writtenCount();
            tapCursor = t - std::min (t, (unsigned) chunk.size());
            tapValid  = true;
        }

        unsigned skipped = 0;
        const int n = tap.readSince (tapCursor, chunk.data(), (int) chunk.size(), skipped);
        if (skipped > 0 && ! first)
        {
            // The timer stalled past what the tap keeps, or the writer lapped the copy: what came before
            // the gap is not what comes after it. Start over from the oldest sample handed over, as
            // after a pluck.
            anyAttack  = true;
            lastAttack = histCount;
        }
        push (chunk.data(), n);

        return maybeAnalyse();
    }

    /** Analyses when a span is ready and either a new strum began or `cadence` of new audio came in. */
    bool maybeAnalyse()
    {
        // The mains is learned in the stretch before a strum, and — with nobody strumming — from what
        // is there, once a second. Both only in QUIET (learnHum asks): over a strum still ringing, a
        // string on 200 Hz is "mains", and a real 240 Hz line hidden by a partial is lost.
        // The tick's budget is counted from here: learning the mains is part of what the tick costs.
        const double w0 = work;
        if (histCount - lastHumAt >= (std::uint64_t) fsD)
            learnHum();

        if (job.busy && job.strum != strumCount)
            job.busy = false;   // a new strum: the span in flight is the last one's
        if (! job.busy)
        {
            if (histCount < spanStart() + (std::uint64_t) minN)
                return false;
            if (analysedStrum == strumCount && histCount - lastAnalysed < cadenceN)
                return false;
            if (! startJob())
                return false;
        }
        return runJob (w0, tickWork);
    }

    /** Analyses the current span now, all of it in this call. Returns false (and keeps the last reading)
        when there is no span yet or it is below the level floor.

        The span is analysed whole and in its two halves, and a string's number is published only where the
        three agree: a pitch still gliding after the attack, two planes of vibration beating, read as one
        steady partial with a confident sigma, are what they disagree on. Their disagreement beyond their own
        noise is part of the published sigma, and a sigma past publishSigmaCents is no reading — the lane
        waits for it instead of showing a wrong number. On the timer (maybeAnalyse) the same three analyses
        run a step at a time, tickWork of them per tick, and the reading comes when the third is done. */
    bool analyse()
    {
        job.busy = false;
        if (! startJob())
            return false;
        return runJob (work, std::numeric_limits<double>::infinity());
    }

    const Reading& reading() const noexcept { return out; }

    /** What the analysis decimates to, Hz — for tests and cost accounting. */
    double analysisRate() const noexcept { return fsD; }

    /** The mains as learned: its fundamental, Hz (0: none), and which harmonics stand (bit k: the k-th). */
    double   mainsHz()   const noexcept { return humHz; }
    unsigned mainsMask() const noexcept { return humMask; }

private:
    //==========================================================================
    // The three analyses of a span — whole, first half, second half — as steps: a span's spectrum, a sweep's
    // clusters, each cluster's fit, each string's verdict. The message thread runs as many per tick as
    // tickWork allows, never fewer than one. Counted in work, not read off a clock: what is analysed when
    // is the same on every machine — a slow one only waits longer for the reading.

    struct Job
    {
        bool busy = false;
        unsigned strum = 0;
        std::uint64_t start = 0;
        int N = 0;
        int part = 0;        // 0 the whole span, 1 its first half, 2 its second
        int step = 0;        // 0 the spectrum, 1 a sweep's clusters, 2 their fits, 3 the strings, 4 the part done
        int sweep = 0, ci = 0, s = 0;
        bool ok[3] {};
        std::array<double, maxStrings> decay[2] {};
    };

    bool startJob()
    {
        const std::uint64_t start = std::max (spanStart(), histCount - std::min<std::uint64_t> (histCount, (std::uint64_t) maxN));
        const int N = (int) (histCount - start);
        if (N < minN)
            return false;

        lastAnalysed  = histCount;
        analysedStrum = strumCount;
        job = {};
        job.busy  = true;
        job.strum = strumCount;
        job.start = start;
        job.N     = N;
        // What the three analyses hold the span to is what was known when it began: the mains learned since is
        // the next span's.
        spanHumHz   = humHz;
        spanHumMask = humMask;
        geometrySec = N / fsD;
        return true;
    }

    /** Steps until the trio is done or the tick's `budget` of work (counted from `w0`) is spent. True when a
        reading came. */
    bool runJob (double w0, double budget)
    {
        for (;;)
        {
            const int r = stepJob();
            if (r != 0)
                return r > 0;
            if (work - w0 >= budget)
                return false;
        }
    }

    /** One step. 0: more to come; 1: the trio is done and published; -1: it ended with nothing to publish. */
    int stepJob()
    {
        auto& j = job;
        Reading& r = j.part == 0 ? whole : j.part == 1 ? firstHalf : secondHalf;
        switch (j.step)
        {
            case 0:
            {
                const int half = j.N / 2;
                const std::uint64_t st = j.part == 2 ? j.start + (std::uint64_t) (j.N - half) : j.start;
                j.ok[j.part] = spanBegin (st, j.part == 0 ? j.N : half);
                if (! j.ok[j.part] && j.part == 0)
                {
                    j.busy = false;
                    return -1;
                }
                j.sweep = 0;
                j.step  = j.ok[j.part] ? 1 : 4;
                return 0;
            }
            case 1:
                if (j.sweep > 0)
                {
                    rehomeCoincident();
                    relocateStrays();
                }
                buildClusters();
                j.ci = 0;
                j.step = 2;
                return 0;
            case 2:
                if (j.ci < nClusters)
                {
                    fitClusterWithPriors (j.ci++, j.sweep);
                    return 0;
                }
                if (++j.sweep < sweeps)
                {
                    j.step = 1;
                    return 0;
                }
                decideBegin();
                j.s = 0;
                j.step = 3;
                return 0;
            case 3:
                if (j.s < tuning.strings)
                {
                    // The halves are a third as long in sigma terms (T^-1.5): their gate is wider by that.
                    decideString (j.s++, r, j.part == 0 ? publishSigmaCents : publishSigmaCents * 2.83);
                    return 0;
                }
                j.step = 4;
                return 0;
            default:
                if (j.part > 0)
                    j.decay[j.part - 1] = decayOf;
                if (++j.part < 3)
                {
                    j.step = 0;
                    return 0;
                }
                j.busy = false;
                finishTrio();
                return 1;
        }
    }

    /** The whole span's verdicts, held to its halves: a number only where the three agree, and their
        disagreement in its sigma. */
    void finishTrio()
    {
        const bool okA = job.ok[1], okB = job.ok[2];
        int heard = 0;
        for (int s = 0; s < tuning.strings; ++s)
        {
            auto& w = whole.string[(size_t) s];
            if (w.heard != Heard::near && w.heard != Heard::far)
                continue;
            const auto& a = firstHalf.string[(size_t) s];
            const auto& b = secondHalf.string[(size_t) s];
            const auto said = [] (const String& x) { return x.heard == Heard::near || x.heard == Heard::far; };
            // ... and one steady partial dies away at one rate: two planes of vibration a fraction of a hertz
            // apart beat, their sum fading and returning — read as one partial, its pitch is off by more
            // than their split, and the halves' decay rates are what give it away. Nor does its decay ever
            // QUICKEN: one partial's prompt sound and aftersound are a sum of exponentials at one pitch, whose
            // log is convex — it dies away fast, then slower. Dying faster in the second half is a sum fading
            // into its beat: a second plane, the pitch swinging toward the far side of it.
            const double dA = job.decay[0][(size_t) s], dB = job.decay[1][(size_t) s];
            const bool steady = std::abs (dA - dB) <= halvesDecayAgree && dB - dA <= halvesDecayRise;
            bool keep = okA && okB && said (a) && said (b) && std::abs (a.cents - b.cents) <= halvesAgreeCents
                     && std::abs (w.cents - 0.5f * (a.cents + b.cents)) <= halvesAgreeCents && steady;
            if (keep)
            {
                // The sigma a reading is published with is the WHOLE error budget: the fit's own, and what the
                // halves and the whole disagree on — a pitch that moves under the span, two planes read as one,
                // a model that does not fit, all show there and nowhere else. Counted whole, their own noise
                // with it: the halves are the only check there is on the model, and a check too noisy to see a
                // cent (their difference known no better than halvesResolveCents) vouches for nothing. Past
                // publishSigmaCents the promise — within a cent, or nothing — does not hold: the lane waits.
                const double sw = w.sigmaCents, sa = a.sigmaCents, sb = b.sigmaCents;
                const double dh = (double) a.cents - b.cents, dw = (double) w.cents - 0.5 * ((double) a.cents + b.cents);
                const double total = std::sqrt (sw * sw + dh * dh + dw * dw);
                keep = total <= publishSigmaCents && std::sqrt (sa * sa + sb * sb) <= halvesResolveCents;
                w.sigmaCents = (float) total;
            }
            if (! keep)
            {
                const float lvl = w.levelDb, tgt = w.targetHz;
                w = {};
                w.heard = Heard::no;   // the lane waits
                w.levelDb = lvl;
                w.targetHz = tgt;
                continue;
            }
            ++heard;
        }
        whole.mono    = heard == 1;
        whole.strings = tuning.strings;
        whole.spanMs  = (float) (1000.0 * job.N / fsD);
        whole.endSec  = (double) (job.start + (std::uint64_t) job.N) / fsD;
        whole.strum   = job.strum;
        out = whole;
    }

    /** A span's spectrum, peaks and components: the first step of its analysis. False below the level floor. */
    bool spanBegin (std::uint64_t start, int N)
    {
        // Hann over the span, zero-padded at least twice.
        spanN = N;
        L = 1;
        while (L < 2 * N)
            L <<= 1;
        double energy = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const double v = hist[(size_t) ((start + (std::uint64_t) n) & (histCap - 1))];
            energy += v * v;
            spec[(size_t) n] = cd (v * (0.5 - 0.5 * std::cos (2.0 * pi * n / N)), 0.0);
        }
        if (std::sqrt (energy / N) < rmsFloor)
            return false;
        for (int n = N; n < L; ++n)
            spec[(size_t) n] = cd {};
        fft (spec.data(), L);
        for (int p = 0; p <= L / 2; ++p)
            mag[(size_t) p] = std::abs (spec[(size_t) p]);

        binW = 2.0 * pi / N;
        rot  = std::polar (1.0, 2.0 * pi / N);

        estimateNoise();
        findPeaks();
        coarse();
        buildComponents();
        return true;
    }

    using cd = std::complex<double>;
    static constexpr double pi = 3.14159265358979323846;
    static constexpr double centsPerLog = 1200.0 / 0.69314718055994530942;   // cents per natural-log unit

    // Timing and sizes.
    static constexpr double decimRate  = 2400.0;
    static constexpr int    histCap    = 16384;   // power of two, ~6.8 s
    static constexpr int    maxN       = 4096;    // the longest span, ~1.7 s
    static constexpr int    maxL       = 8192;
    static constexpr double settleSec  = 0.25;
    // The first reading waits for a second of ringing past the settle: measured over 324 random
    // strums, every string of every tuning is within a cent from there (worst 0.26 c), while at
    // 0.6-0.8 s a few strums still read a string wrong — a bass B a cent and a half off, a high E
    // taken for the low E's harmonic. A reading that is wrong fast is worse than one a moment later.
    static constexpr double minSpanSec = 1.00;
    static constexpr double cadenceSec = 0.20;
    static constexpr double strumGapSec = 0.15;   // attacks closer than this are one strum

    // The fit.
    static constexpr int    maxComps        = 128;
    static constexpr int    maxClusterComps = 8;
    static constexpr int    maxParams       = 4 * maxClusterComps;
    static constexpr int    maxProbes       = 512;
    static constexpr int    maxPeaks        = 1024;
    static constexpr int    sweeps          = 3;
    static constexpr double mergeBins       = 3.5;   // main lobes closer than this are one cluster
    static constexpr double marginBins      = 2.5;   // spectrum fitted around a cluster
    static constexpr double leakBins        = 16.0;  // neighbours whose leakage is subtracted

    // The string model.
    static constexpr double Bnom = 1.0e-4, Bsd = 2.0e-4, Bmax = 3.0e-3;
    static constexpr double priorFloorCents = 2.0;    // how far a real string's partial strays from the model: 1-3 c
    static constexpr double measFloorCents  = 0.005;
    static constexpr double modelFloor      = 1.0e-7;   // -70 dB of the loudest bin
    static constexpr double outlierFloorCents = 3.0;  // further than a real partial strays: another string's

    // Decisions — and the rule over all of them: A CONFIDENT WRONG NUMBER IS THE DEFECT. When the evidence
    // does not hold, the lane says "pluck alone" (masked) or "-" (no), never a number. A tuner that sometimes
    // withholds is fine; one that confidently lies is not.
    static constexpr double unisonBins        = 1.0;   // a string this close to another's partial cannot be told from it
    static constexpr double publishSigmaCents = 0.5;   // a reading known no better is not one
    static constexpr double halvesAgreeCents  = 1.0;   // the span's halves must read the same pitch
    static constexpr double halvesDecayAgree  = 0.3;   // ... and see the first partial die away alike, 1/s
    static constexpr double halvesDecayRise   = 0.1;   // ... never faster in the second, 1/s (their noise allowed for)
    static constexpr double halvesResolveCents = 0.7;  // ... and know their difference to this: a check that can see a cent
    static constexpr double vouchCents        = 5.0;   // the first partial against the string's other partials
    static constexpr double mainsDecaySec     = 8.0;   // on a mains line, a string must be seen to die away
    static constexpr int    maxHumK           = 20;    // mains harmonics modelled, as far as they stand
    static constexpr double quietDropDb       = 35.0;  // the mains is learned only this far under the last strum
    static constexpr double humFlatDb         = 0.1;   // a mains line is as loud in both halves of its stretch (a partial
                                                       // dying with tau 26 s drops this much in 0.3 s)
    static constexpr double humSteadyDb       = 0.25;  // ... and so is the whole stretch: a background, not a ringing chord
    static constexpr double mainsWanderHz     = 0.3;   // how far the mains strays off 50 / 60 Hz — k times that at the k-th
    static constexpr double mainsDriftHz      = 0.06;  // how far it moves within a span once learned, sd
    static constexpr double tickWork          = 2.0e6; // the analysis a timer tick carries, at most (and one step more,
                                                       // 0.6 ms at most): 2.7 ms on an i9-13900H, 1.9 ms on an M-series Mac
    static constexpr double heardRelDb   = -40.0;   // a string this far under the loudest is not heard
    static constexpr double maskedSigmaCents = 0.25;  // a coincident string known no better is masked
    static constexpr double maskedShare  = 0.01;    // a masked string leaves less than this of its own energy unexplained

    struct Biquad { double b0, b1, b2, a1, a2; };

    struct Comp
    {
        int    string = -1;     // -1: a mains line
        int    harm   = 1;
        bool   coincident = false;
        bool   shield = false;     // above the band in use: fitted so its leakage is, never read
        bool   active = true;
        double w = 0.0, d = 0.0;   // rad per decimated sample; decay per sample
        cd     c {};               // x[n] = c e^{sn} + conj, s = -d + jw, n from the span's start
        double pw = 0.0, psw = 0.0, pd = 0.0, psd = 0.0;   // priors (sd <= 0: none)
        double wLo = 0.0, wHi = pi;
        double varW = std::numeric_limits<double>::infinity();
    };

    struct Cluster { int first = 0, count = 0, p0 = 0, p1 = 0; double energy = 0.0, resid = 0.0, cost = 0.0; };

    struct StringState
    {
        int    status = 0;        // 0 absent, 1 clean, 2 coincident
        bool   underMains = false; // coincident with a mains line only — nothing to mask a string
        double lo = 0.0, hi = 0.0;
        int    harmonics = 0;
    };

    struct Peak { double f = 0.0, m = 0.0; bool ok = true; };

    struct ModelFit { bool ok = false; double L = 0.0, B = 0.0, cLL = 0.0, cLB = 0.0, cBB = 0.0; };

    //==========================================================================
    std::uint64_t spanStart() const noexcept { return anyAttack ? lastAttack + settleN : 0; }

    void retarget()
    {
        job.busy = false;   // a span in flight was analysed for the strings it had
        for (int s = 0; s < maxStrings; ++s)
            target[(size_t) s] = a4 * std::pow (2.0, (tuning.midi[(size_t) s] - 69) / 12.0);

        for (int s = 0; s < tuning.strings; ++s)
            order[(size_t) s] = s;
        std::sort (order.begin(), order.begin() + tuning.strings,
                   [this] (int a, int b) { return target[(size_t) a] < target[(size_t) b]; });

        double top = 0.0;
        for (int s = 0; s < tuning.strings; ++s)
            top = std::max (top, target[(size_t) s]);
        fUse   = 2.0 * top * std::pow (2.0, reachCents / 1200.0);
        // Partials are modelled a margin above the band any string is read in: the ones just past
        // the last modelled partial leak into it, and the top of the band must not be its edge.
        fModel = std::min (fUse * 1.15 + 20.0, 0.36 * fsD);

        out.strings = tuning.strings;
        for (int s = 0; s < maxStrings; ++s)
            out.string[(size_t) s] = {};
        for (int s = 0; s < tuning.strings; ++s)
            out.string[(size_t) s].targetHz = (float) target[(size_t) s];
    }

    void designAntiAlias()
    {
        // 8th-order Butterworth at 0.36 of the decimated rate: what folds onto the analysed band
        // (below ~0.36 fsD) starts at 0.64 fsD, 80 dB down.
        static constexpr double q[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
        const double w0 = 2.0 * pi * 0.36 * fsD / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        for (int s = 0; s < 4; ++s)
        {
            const double alpha = sw / (2.0 * q[s]);
            const double a0    = 1.0 + alpha;
            aa[s] = { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
        }

        const double wh = 2.0 * pi * std::min (1000.0, 0.2 * fs) / fs;
        const double ch = std::cos (wh), ah = std::sin (wh) / (2.0 * 0.70710678), h0 = 1.0 + ah;
        hpf = { (1.0 + ch) * 0.5 / h0, -(1.0 + ch) / h0, (1.0 + ch) * 0.5 / h0, -2.0 * ch / h0, (1.0 - ah) / h0 };
    }

    //==========================================================================
    // Input, envelope, attacks.

    void pushDecimated (float v, double p)
    {
        hist[(size_t) (histCount & (histCap - 1))] = v;
        ++histCount;

        env1 += envA * (p - env1);
        env2 += envA * (env1 - env2);
        bbLevel += levelA * ((double) v * v - bbLevel);
        strumPeak = std::max (strumPeak, bbLevel);

        if (++blockFill >= blockLen)
        {
            blockFill = 0;
            onBlock();
        }
    }

    void onBlock()
    {
        // An attack is energy in the attack band beyond anything of its last 150 ms: a pluck's click
        // climbs out of whatever rang before; what still rings only falls.
        const double e  = env2;
        double hi = 0.0;
        for (const double v : ring)
            hi = std::max (hi, v);
        for (size_t i = ring.size() - 1; i > 0; --i)
            ring[i] = ring[i - 1];
        ring[0] = e;
        ++blocksSeen;

        const double floorPow = 0.5 * std::pow (0.03 * rmsFloor, 2.0);
        if (e > riseRatio * hi && e > floorPow)
        {
            const bool newStrum = ! anyAttack || histCount - lastAttack > (std::uint64_t) (strumGapSec * fsD);
            if (newStrum)
            {
                ++strumCount;
                learnHum();   // the stretch before it — if that was quiet
                // The very first rise, out of an empty history, is the tuner opening, not a strum: what
                // it hears (hum, hiss) is still the background.
                if (histCount > (std::uint64_t) (0.15 * fsD))
                {
                    strummed  = true;
                    strumPeak = bbLevel;
                }
            }
            anyAttack  = true;
            lastAttack = histCount;
        }
    }

    /** The mains in the stretch before an attack: 50 or 60 Hz and its harmonics, TRACKED. The grid is never
        exactly on its nominal (49.9-50.1 Hz any day, more on events), and its k-th harmonic stands k times as
        far off k x 50: so each harmonic is the strongest peak within mainsWanderHz k of its nominal line,
        standing well above its surroundings and as loud at the stretch's end as at its start; the mains is
        fitted from the series of those (a line off the series dropped), and a harmonic the search missed — a
        louder peak beside it — is probed again where the fit puts it. Kept until the next quiet stretch says
        otherwise. */
    void learnHum()
    {
        lastHumAt = histCount;
        const std::uint64_t end   = histCount - std::min (histCount, (std::uint64_t) std::lround (0.05 * fsD));
        std::uint64_t       start = end - std::min (end, (std::uint64_t) std::lround (0.6 * fsD));
        start = std::max (start, histCount - std::min<std::uint64_t> (histCount, (std::uint64_t) histCap - 1));
        if (anyAttack)   // past the last attack's own transient — hum has no pluck to settle from
            start = std::max (start, lastAttack + (std::uint64_t) std::lround (0.1 * fsD));
        if (end <= start || (int) (end - start) < (int) std::lround (0.35 * fsD))
            return;

        const int n = (int) (end - start), hn = n / 2;
        const double T = n / fsD;

        // Quiet: nothing strummed yet, or the last strum died away quietDropDb under its loudest. Otherwise
        // the mask learned in the last quiet stands.
        double pwA = 0.0, pwB = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double v = hist[(size_t) ((start + (std::uint64_t) i) & (histCap - 1))];
            (i < hn ? pwA : pwB) += v * v;
        }
        const double pw0 = pwA + pwB;
        if (strummed && pw0 / n > strumPeak * std::pow (10.0, -quietDropDb / 10.0))
            return;
        if (pw0 <= 0.0)
        {
            humHz   = 0.0;   // nothing at all: no mains either
            humMask = 0;
            return;
        }
        // ... and steady: as loud in its second half as in its first — a background. A chord still ringing
        // falls (the tuner opened onto one: nothing strummed yet, no strum to measure the quiet against).
        const double steady = 10.0 * std::log10 ((pwB / (n - hn) + 1.0e-30) / (pwA / hn + 1.0e-30));
        if (std::abs (steady) > humSteadyDb)
            return;

        // The stretch's spectrum, a quarter of its bin apart at most.
        int Lh = 1;
        while (Lh < 4 * n && Lh < maxL)
            Lh <<= 1;
        for (int i = 0; i < n; ++i)
            humSpec[(size_t) i] = cd (hist[(size_t) ((start + (std::uint64_t) i) & (histCap - 1))] * (0.5 - 0.5 * std::cos (2.0 * pi * i / n)), 0.0);
        for (int i = n; i < Lh; ++i)
            humSpec[(size_t) i] = cd {};
        fft (humSpec.data(), Lh);
        for (int p = 0; p <= Lh / 2; ++p)
            humMag[(size_t) p] = std::abs (humSpec[(size_t) p]);
        const double binHz = fsD / Lh;
        const auto magAt = [&] (double f) { return humMag[(size_t) std::clamp ((int) std::lround (f / binHz), 0, Lh / 2)]; };
        const auto floorAt = [&] (double f)
        {
            double fl[4];
            for (int i = 0; i < 4; ++i)
                fl[i] = magAt (f + (i < 2 ? -1.0 : 1.0) * (4.0 + 2.0 * (i & 1)) / T);
            std::sort (fl, fl + 4);
            return 0.5 * (fl[0] + fl[1]) + 1.0e-12;
        };
        // A line stands well above its surroundings, and is as loud in the stretch's second half as in its
        // first — a string dies away, the mains does not (the halves' own noise allowed for).
        const auto stands = [&] (double f, double a)
        {
            const double floorMag = floorAt (f);
            if (! (a > 3.16 * floorMag && a > 0.25 * rmsFloor * n * 0.25))
                return false;
            const double q1 = std::abs (dftAt (start, hn, f)), q2 = std::abs (dftAt (start + (std::uint64_t) hn, hn, f));
            return q1 > 0.0 && q2 > 0.0 && std::abs (20.0 * std::log10 (q2 / q1)) <= humFlatDb + 10.0 * floorMag / a;
        };

        double   bestHz = 0.0, bestPow = 0.0;
        int      bestCount = 0;
        unsigned bestMask = 0;
        for (const int m : { 50, 60 })
        {
            std::array<int, maxHumK + 1> kk {};
            std::array<double, maxHumK + 1> fk {}, ak {};
            int nf = 0;
            for (int k = 1; k <= maxHumK && k * m <= fModel; ++k)
            {
                const int p0 = std::max (1, (int) std::ceil ((k * m - mainsWanderHz * k) / binHz));
                const int p1 = std::min (Lh / 2 - 1, (int) std::floor ((k * m + mainsWanderHz * k) / binHz));
                int best = -1;
                for (int p = p0; p <= p1; ++p)
                    if (humMag[(size_t) p] >= humMag[(size_t) p - 1] && humMag[(size_t) p] >= humMag[(size_t) p + 1]
                        && (best < 0 || humMag[(size_t) p] > humMag[(size_t) best]))
                        best = p;
                if (best < 0)
                    continue;
                const double a = std::log (humMag[(size_t) best - 1] + 1e-300), b = std::log (humMag[(size_t) best] + 1e-300),
                             c = std::log (humMag[(size_t) best + 1] + 1e-300);
                const double den = a - 2.0 * b + c;
                const double dlt = den < 0.0 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
                const double f = (best + dlt) * binHz, mg = std::exp (b - 0.25 * (a - c) * dlt);
                if (stands (f, mg))
                {
                    kk[(size_t) nf] = k;
                    fk[(size_t) nf] = f;
                    ak[(size_t) nf++] = mg;
                }
            }

            // The mains from the series: f_k = k m, each line weighted as well as it is measured; the line
            // furthest off it dropped while one is further than a quarter of the stretch's bin.
            double mf = 0.0;
            while (nf > 0)
            {
                double num = 0.0, den = 0.0;
                for (int i = 0; i < nf; ++i)
                {
                    const double w = ak[(size_t) i] * ak[(size_t) i];
                    num += w * kk[(size_t) i] * fk[(size_t) i];
                    den += w * kk[(size_t) i] * kk[(size_t) i];
                }
                mf = num / den;
                int worst = 0;
                for (int i = 1; i < nf; ++i)
                    if (std::abs (fk[(size_t) i] - kk[(size_t) i] * mf) > std::abs (fk[(size_t) worst] - kk[(size_t) worst] * mf))
                        worst = i;
                if (std::abs (fk[(size_t) worst] - kk[(size_t) worst] * mf) <= 0.25 / T)
                    break;
                for (int i = worst; i < nf - 1; ++i)
                {
                    kk[(size_t) i] = kk[(size_t) i + 1];
                    fk[(size_t) i] = fk[(size_t) i + 1];
                    ak[(size_t) i] = ak[(size_t) i + 1];
                }
                --nf;
            }

            unsigned mask = 0;
            double pw = 0.0;
            for (int i = 0; i < nf; ++i)
            {
                mask |= 1u << kk[(size_t) i];
                pw += ak[(size_t) i] * ak[(size_t) i];
            }
            int count = nf;
            // A harmonic the search missed, where the series puts it: a peak there, standing.
            for (int k = 1; nf > 0 && k <= maxHumK && k * mf <= fModel; ++k)
            {
                if ((mask >> k) & 1u)
                    continue;
                const double f = k * mf, a0 = std::abs (dftAt (start, n, f));
                if (a0 >= magAt (f - 0.35 / T) && a0 >= magAt (f + 0.35 / T) && stands (f, a0))
                {
                    mask |= 1u << k;
                    ++count;
                    pw += a0 * a0;
                }
            }
            if (count > bestCount || (count == bestCount && pw > bestPow))
            {
                bestHz    = count > 0 ? mf : 0.0;
                bestCount = count;
                bestMask  = mask;
                bestPow   = pw;
            }
        }

        humHz   = bestHz;
        humMask = bestMask;
    }

    cd dftAt (std::uint64_t start, int n, double f) const
    {
        work += 8.0 * n;
        const cd step = std::polar (1.0, -2.0 * pi * f / fsD);
        cd ph (1.0, 0.0), acc {};
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / n);
            acc += (double) hist[(size_t) ((start + (std::uint64_t) i) & (histCap - 1))] * w * ph;
            ph *= step;
        }
        return acc;
    }

    //==========================================================================
    // The spectrum: FFT, noise, peaks.

    void fft (cd* a, int n) const
    {
        work += 2.5 * n * std::log2 ((double) n);
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int step = maxL / len;
            const int half = len >> 1;
            for (int i = 0; i < n; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const cd u = a[i + k];
                    const cd v = a[i + k + half] * twiddle[(size_t) (k * step)];
                    a[i + k]        = u + v;
                    a[i + k + half] = u - v;
                }
        }
    }

    int binOf (double hz) const noexcept { return (int) std::lround (hz * L / fsD); }

    /** The noise per bin: a low percentile of |X|^2 over the analysed band, where most bins hold
        nothing but leakage and hiss. It only sets how much the data outweighs the priors. */
    void estimateNoise()
    {
        const int p0 = std::max (1, binOf (18.0)), p1 = std::min (L / 2 - 1, binOf (fModel + 20.0));
        int n = 0;
        double peak = 0.0;
        for (int p = p0; p <= p1; ++p)
        {
            const double v = mag[(size_t) p] * mag[(size_t) p];
            scratch[(size_t) n++] = v;
            peak = std::max (peak, v);
        }
        const int k = n * 3 / 10;
        std::nth_element (scratch.begin(), scratch.begin() + k, scratch.begin() + n);
        noisePow = std::max (scratch[(size_t) k] / 0.357, peak * 1.0e-14 + 1.0e-30);
        noiseMag = std::sqrt (noisePow);
        // Each complex residual's real and imaginary parts carry half the bin's noise, and the
        // zero-padded bins are not independent: L/N of them share one bin's worth of information.
        // And no string fits its model to better than modelFloor of the loudest bin: below that the
        // "noise" is the model's own mismatch, and the priors must still count against it — on a
        // clean signal they otherwise never would, and two partials a tenth of a hertz apart settle
        // whichever way the fit happened to start.
        s2 = 0.5 * std::max (noisePow, peak * modelFloor) * (double) L / spanN;
    }

    void findPeaks()
    {
        nPeaks = 0;
        strongest = 0.0;
        const int p0 = std::max (2, binOf (18.0)), p1 = std::min (L / 2 - 2, binOf (fModel + 20.0));
        for (int p = p0; p <= p1 && nPeaks < maxPeaks; ++p)
        {
            const double m0 = mag[(size_t) p];
            if (! (m0 > mag[(size_t) p - 1] && m0 >= mag[(size_t) p + 1] && m0 > 2.0 * noiseMag))
                continue;
            const double a = std::log (mag[(size_t) p - 1] + 1e-300), b = std::log (m0), c = std::log (mag[(size_t) p + 1] + 1e-300);
            const double den = a - 2.0 * b + c;
            const double dlt = den < 0.0 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            peaks[(size_t) nPeaks++] = { (p + dlt) * fsD / L, std::exp (b - 0.25 * (a - c) * dlt), true };
            strongest = std::max (strongest, peaks[(size_t) nPeaks - 1].m);
        }

        // A Hann sidelobe is a local maximum too. Below this envelope of a stronger neighbour's
        // sidelobes (-31 dB at 2.5 bins, 18 dB an octave after), with 16 dB to spare, it is one.
        const double T = spanN / fsD;
        for (int i = 0; i < nPeaks; ++i)
            for (int j = 0; j < nPeaks; ++j)
            {
                if (peaks[(size_t) j].m <= peaks[(size_t) i].m)
                    continue;
                const double D = std::abs (peaks[(size_t) i].f - peaks[(size_t) j].f) * T;
                if (D < 2.0 || D > 14.0)
                    continue;
                const double env = 0.03 * std::pow (2.5 / std::max (D, 2.5), 3.0);
                if (peaks[(size_t) i].m < 6.0 * env * peaks[(size_t) j].m)
                    peaks[(size_t) i].ok = false;
            }
    }

    //==========================================================================
    // Coarse: each string's first partial from the peaks, lowest string first, then its harmonics.

    void coarse()
    {
        nExplained = 0;
        const double T = spanN / fsD;
        const double thr = std::max (5.6 * noiseMag, strongest * std::pow (10.0, -50.0 / 20.0));

        if (spanHumHz > 0.0)
            for (int k = 1; k <= maxHumK; ++k)
                if ((spanHumMask >> k) & 1u)
                    addExplained (k * spanHumHz, true);

        const int S = tuning.strings;
        for (int oi = 0; oi < S; ++oi)
        {
            const int s = order[(size_t) oi];
            auto& st = strings[(size_t) s];
            const double t = target[(size_t) s];
            st = {};
            st.lo = t * std::pow (2.0, -reachCents / 1200.0);
            st.hi = t * std::pow (2.0, reachCents / 1200.0);
            if (oi > 0)
                st.lo = std::max (st.lo, std::sqrt (t * target[(size_t) order[(size_t) oi - 1]]));
            if (oi < S - 1)
                st.hi = std::min (st.hi, std::sqrt (t * target[(size_t) order[(size_t) oi + 1]]));

            const Peak* clean = nullptr;
            const Peak* amb   = nullptr;
            for (int i = 0; i < nPeaks; ++i)
            {
                const Peak& pk = peaks[(size_t) i];
                if (! pk.ok || pk.f < st.lo || pk.f > st.hi || pk.m < thr)
                    continue;
                if (isExplained (pk.f, T))
                {
                    // A peak another string explains is this string's only near its note — and
                    // then the nearest one: the strongest would be the other string's partial,
                    // which is what made it explained in the first place.
                    const double c = std::abs (1200.0 * std::log2 (pk.f / t));
                    if (c <= farCents && (amb == nullptr || c < std::abs (1200.0 * std::log2 (amb->f / t))))
                        amb = &pk;
                }
                else if (clean == nullptr || pk.m > clean->m)
                    clean = &pk;
            }

            double f1 = 0.0;
            if (clean != nullptr)
            {
                f1 = clean->f;
                st.status = 1;
            }
            else
            {
                // The second partial, when the first is too weak to stand out: within a semitone of
                // its octave only, where no other string's first partial can sit.
                const double lo2 = 2.0 * t * std::pow (2.0, -110.0 / 1200.0), hi2 = 2.0 * t * std::pow (2.0, 110.0 / 1200.0);
                // Not where another string's own note is: in drop D the low D's octave IS the D
                // string, and a muted low D would read the D string's pluck as its own.
                bool octaveIsAString = false;
                for (int r = 0; r < S; ++r)
                    octaveIsAString |= r != s && target[(size_t) r] >= lo2 && target[(size_t) r] <= hi2;
                const Peak* second = nullptr;
                for (int i = 0; i < nPeaks && ! octaveIsAString; ++i)
                {
                    const Peak& pk = peaks[(size_t) i];
                    if (pk.ok && pk.f >= lo2 && pk.f <= hi2 && pk.m >= thr && ! isExplained (pk.f, T)
                        && ! onMainsLine (pk.f, 1.0 / T) && (second == nullptr || pk.m > second->m))
                        second = &pk;
                }
                if (second != nullptr)
                {
                    f1 = second->f / 2.0 * std::sqrt ((1.0 + Bnom) / (1.0 + 4.0 * Bnom));
                    st.status = 1;
                }
                else if (amb != nullptr)
                {
                    f1 = amb->f;
                    st.status = 2;
                    st.underMains = explainedBy (amb->f, T) == 2;
                }
            }
            if (st.status == 0)
                continue;

            walkHarmonics (s, f1, T);
            for (int h = 1; h <= st.harmonics; ++h)
                addExplained (walk[(size_t) s][(size_t) h]);
        }
    }

    /** Where a 50 or 60 Hz mains's k-th harmonic may stand, learned or not: within mainsWanderHz k of k x 50
        (k x 60) — the grid wanders, its harmonics k times as far — or `unison` Hz, whichever is wider. The
        second-partial path never takes a peak there, and a first partial there must be seen to die away
        (decide). */
    static bool onMainsLine (double f, double unison) noexcept
    {
        for (const int m : { 50, 60 })
        {
            const double k = std::round (f / m);
            if (k >= 1.0 && std::abs (f - k * m) < std::max (unison, mainsWanderHz * k))
                return true;
        }
        return false;
    }

    /** On a harmonic of the mains as learned for this span, within `unison` Hz and the mains's own drift. */
    bool onLearnedLine (double f, double unison) const noexcept
    {
        if (spanHumHz <= 0.0)
            return false;
        const double k = std::round (f / spanHumHz);
        return k >= 1.0 && k <= maxHumK && ((spanHumMask >> (int) k) & 1u)
            && std::abs (f - k * spanHumHz) < unison + mainsDriftHz * k;
    }

    /** String s's partials stand on an exactly harmonic series of its first one (component k1) and none of
        them dies away: a mains series — or a hum off any grid — not a string. A string's partials stray off
        the harmonic series (stiffness, its ends: cents), and a string dies away. Takes two partials measured
        to half a cent to say so. */
    bool exactSeries (int s, int k1) const
    {
        const auto& c1 = comps[(size_t) k1];
        const double sd1 = std::sqrt (c1.varW) / c1.w * centsPerLog;
        int on = 0;
        for (int k = 0; k < nComps; ++k)
        {
            const auto& c = comps[(size_t) k];
            if (c.string != s || ! c.active || c.harm < 2 || ! std::isfinite (c.varW))
                continue;
            const double sd = std::sqrt (c.varW) / c.w * centsPerLog;
            if (sd > 0.5)
                continue;
            const double dev = std::log (c.w / (c.harm * c1.w)) * centsPerLog;
            if (std::abs (dev) > std::max (0.25, 3.0 * std::sqrt (sd * sd + sd1 * sd1)) || c.d * fsD >= 1.0 / mainsDecaySec)
                return false;
            ++on;
        }
        return on >= 2;
    }

    void addExplained (double f, bool mains = false)
    {
        if (nExplained < (int) explained.size())
        {
            explainedByMains[(size_t) nExplained] = mains;
            explained[(size_t) nExplained++] = f;
        }
    }

    /** 0: nobody explains a peak at f; 1: a string's partial does; 2: only a mains line. */
    int explainedBy (double f, double T) const
    {
        int by = 0;
        for (int i = 0; i < nExplained; ++i)
            if (std::abs (f - explained[(size_t) i]) < 1.5 / T + 0.002 * f)
            {
                if (! explainedByMains[(size_t) i])
                    return 1;
                by = 2;
            }
        return by;
    }

    bool isExplained (double f, double T) const { return explainedBy (f, T) != 0; }

    /** From the first partial up: predict the next partial from the ones found, take the peak
        nearest the prediction, refit — so a bass B's 10th partial, 4 Hz off a plain harmonic series
        on a stiff string, is still where its component starts. */
    void walkHarmonics (int s, double f1, double T)
    {
        auto& st = strings[(size_t) s];
        auto& wk = walk[(size_t) s];
        st.harmonics = std::max (1, std::min (maxHarm, (int) (fModel / f1)));
        wk[1] = f1;

        // Weighted LS for (ln f0, B) on y_h = ln(f_h/h) = ln f0 + B h^2 / 2, with the B prior. A
        // coarse peak is trusted to a fraction of a bin and no better: two partials half a bin
        // apart are ONE peak here, standing between them.
        double B = Bnom, lnf0 = std::log (f1) - 0.5 * std::log1p (Bnom);
        double sw = 0.0, swa = 0.0, swaa = 0.0, swy = 0.0, sway = 0.0;
        double c00 = 0.0, c01 = 0.0, c11 = Bsd * Bsd;
        auto add = [&] (int h, double f)
        {
            // The first partial was picked as a peak of its own (or the string is only a guess
            // anyway); the rest are whatever peak stood nearest a prediction.
            const double v = std::pow ((h == 1 ? 0.1 : 0.4) / (T * f), 2.0);
            const double a = 0.5 * h * h, yv = std::log (f / h);
            sw += 1.0 / v; swa += a / v; swaa += a * a / v; swy += yv / v; sway += a * yv / v;
            const double A00 = sw, A01 = swa, A11 = swaa + 1.0 / (Bsd * Bsd);
            const double g0 = swy, g1 = sway + Bnom / (Bsd * Bsd);
            const double det = A00 * A11 - A01 * A01;
            if (det > 0.0)
            {
                lnf0 = (A11 * g0 - A01 * g1) / det;
                B    = std::clamp ((A00 * g1 - A01 * g0) / det, 0.0, Bmax);
                c00 = A11 / det; c01 = -A01 / det; c11 = A00 / det;
            }
        };
        add (1, f1);

        for (int h = 2; h <= st.harmonics; ++h)
        {
            // Look only as far as the prediction is uncertain: the nearest peak beyond that is
            // another string's partial, and taking it bends B until every prediction above runs off.
            const double pred = h * std::exp (lnf0) * std::sqrt (1.0 + B * h * h);
            const double a    = 0.5 * h * h;
            const double sd   = pred * std::sqrt (std::max (0.0, c00 + 2.0 * a * c01 + a * a * c11));
            const double rad  = std::max (0.6 / T, 2.5 * sd);
            const Peak* best = nullptr;
            for (int i = 0; i < nPeaks; ++i)
            {
                const Peak& pk = peaks[(size_t) i];
                if (pk.ok && std::abs (pk.f - pred) < rad && pk.m > 3.0 * noiseMag
                    && (best == nullptr || std::abs (pk.f - pred) < std::abs (best->f - pred)))
                    best = &pk;
            }
            if (best != nullptr)
            {
                wk[(size_t) h] = best->f;
                add (h, best->f);
            }
            else
                wk[(size_t) h] = pred;
        }
    }

    //==========================================================================
    // Components and clusters.

    void buildComponents()
    {
        nComps = 0;
        for (int s = 0; s < tuning.strings; ++s)
        {
            const auto& st = strings[(size_t) s];
            if (st.status == 0)
                continue;
            for (int h = 1; h <= st.harmonics && nComps < maxComps; ++h)
            {
                Comp c;
                c.string = s;
                c.harm   = h;
                c.coincident = st.status == 2;
                c.w = 2.0 * pi * walk[(size_t) s][(size_t) h] / fsD;
                c.d = 1.0 / (1.5 * fsD);
                c.shield = walk[(size_t) s][(size_t) h] > fUse * 1.02;
                comps[(size_t) nComps++] = c;
            }
        }
        if (spanHumHz > 0.0)
            for (int k = 1; k <= maxHumK && nComps < maxComps; ++k)
                if (((spanHumMask >> k) & 1u) && k * spanHumHz <= fModel)
                {
                    Comp c;
                    c.string = -1;
                    c.harm   = k;
                    c.w  = 2.0 * pi * k * spanHumHz / fsD;
                    c.d  = 0.0;
                    c.pw = c.w;
                    c.psw = 2.0 * pi * mainsDriftHz * k / fsD;
                    c.pd = 0.0;
                    c.psd = 0.3 / spanN;
                    comps[(size_t) nComps++] = c;
                }
    }

    void buildClusters()
    {
        std::sort (comps.begin(), comps.begin() + nComps, [] (const Comp& a, const Comp& b) { return a.w < b.w; });
        nClusters = 0;
        for (int k = 0; k < nComps; ++k)
        {
            const bool join = nClusters > 0
                           && clusters[(size_t) nClusters - 1].count < maxClusterComps
                           && comps[(size_t) k].w - comps[(size_t) k - 1].w < mergeBins * binW;
            if (join)
                ++clusters[(size_t) nClusters - 1].count;
            else
                clusters[(size_t) nClusters++] = { k, 1, 0, 0, 0.0, 0.0 };
        }

        const double toBin = L / (2.0 * pi);
        for (int ci = 0; ci < nClusters; ++ci)
        {
            auto& cl = clusters[(size_t) ci];
            const double wa = comps[(size_t) cl.first].w, wb = comps[(size_t) (cl.first + cl.count - 1)].w;
            cl.p0 = std::max (1, (int) std::floor ((wa - marginBins * binW) * toBin));
            cl.p1 = std::min (L / 2 - 1, (int) std::ceil ((wb + marginBins * binW) * toBin));
            cl.p1 = std::min (cl.p1, cl.p0 + maxProbes - 1);
            for (int k = cl.first; k < cl.first + cl.count; ++k)
            {
                auto& c = comps[(size_t) k];
                c.wLo = (cl.p0 + 0.5) / toBin;
                c.wHi = (cl.p1 - 0.5) / toBin;
                c.w   = std::clamp (c.w, c.wLo, c.wHi);
            }
        }
    }

    //==========================================================================
    // The window's kernel: S(sigma) = sum_{n<N} w[n] e^{sigma n} and dS/dsigma, Hann over N, along
    // the probes p0..p0+P-1 for one component — by rotation from one bin to the next.

    void kernelRow (double w, double d, int p0, int P, cd* S1, cd* dS1, cd* S2, cd* dS2, bool image) const
    {
        work += 16.0 * P;
        const double N  = spanN;
        const double wp = 2.0 * pi * p0 / L;
        const cd stepq = std::polar (1.0, -2.0 * pi / L);
        const cd stepQ = std::polar (1.0, -2.0 * pi * N / L);
        cd q1 = std::exp (cd (-d, w - wp)), Q1 = std::exp (cd (-d * N, (w - wp) * N));
        cd q2 = std::exp (cd (-d, -(w + wp))), Q2 = std::exp (cd (-d * N, -(w + wp) * N));
        for (int i = 0; i < P; ++i)
        {
            hann (q1, Q1, S1[i], dS1[i]);
            q1 *= stepq;
            Q1 *= stepQ;
            if (image)
            {
                hann (q2, Q2, S2[i], dS2[i]);
                q2 *= stepq;
                Q2 *= stepQ;
            }
            else
                S2[i] = dS2[i] = cd {};
        }
    }

    void hann (cd q, cd Q, cd& S, cd& dS) const
    {
        const double N = spanN;
        const cd Qm = Q - 1.0;
        const cd qs[3] = { q, q * rot, q * std::conj (rot) };
        cd g[3], dg[3];
        for (int i = 0; i < 3; ++i)
        {
            const cd den = qs[i] - 1.0;
            if (std::norm (den) < 1.0e-20)
            {
                g[i]  = N + den * (N * (N - 1.0) * 0.5);
                dg[i] = N * (N - 1.0) * 0.5 + den * (N * (N - 1.0) * (2.0 * N - 1.0) / 6.0);
            }
            else
            {
                g[i]  = Qm / den;
                dg[i] = (N * Q * den - Qm * qs[i]) / (den * den);
            }
        }
        S  = 0.5 * g[0] - 0.25 * (g[1] + g[2]);
        dS = 0.5 * dg[0] - 0.25 * (dg[1] + dg[2]);
    }

    bool needsImage (double w) const noexcept { return 2.0 * w / binW < 80.0; }

    //==========================================================================
    // One cluster's fit.

    /** The spectrum at the cluster's probes, less every component outside it that leaks there. */
    void clusterTarget (Cluster& cl)
    {
        const int P = cl.p1 - cl.p0 + 1;
        for (int p = 0; p < P; ++p)
            ybuf[(size_t) p] = spec[(size_t) (cl.p0 + p)];

        const double wa = 2.0 * pi * cl.p0 / L - leakBins * binW, wb = 2.0 * pi * cl.p1 / L + leakBins * binW;
        cd* S1 = kern.data();
        cd* dS1 = S1 + maxProbes;
        cd* S2 = dS1 + maxProbes;
        cd* dS2 = S2 + maxProbes;
        for (int k = 0; k < nComps; ++k)
        {
            if (k >= cl.first && k < cl.first + cl.count)
                continue;
            const auto& c = comps[(size_t) k];
            if (! c.active || c.w < wa || c.w > wb || c.c == cd {})
                continue;
            kernelRow (c.w, c.d, cl.p0, P, S1, dS1, S2, dS2, needsImage (c.w));
            for (int p = 0; p < P; ++p)
                ybuf[(size_t) p] -= c.c * S1[p] + std::conj (c.c) * S2[p];
        }

        double e = 0.0, peak = 0.0;
        for (int p = 0; p < P; ++p)
        {
            e += std::norm (ybuf[(size_t) p]);
            peak = std::max (peak, std::abs (ybuf[(size_t) p]));
        }
        cl.energy = e;

        // No member can be louder than twice what the loudest probe shows (a component's peak is
        // |c| N/2): two members a tenth of a hertz apart could otherwise stand on ONE frequency
        // with huge amplitudes cancelling each other, and fit better than the truth.
        const double ampScale = std::max (2.0 * peak / (0.5 * spanN), 1.0e-30);
        ampPrecision = 1.0 / (ampScale * ampScale);
    }

    /** Residual (ybuf - model) and its Jacobian in the parameters (re c, im c, w, d) per member;
        returns the data part of the cost, sum |r|^2. */
    double evaluate (const Cluster& cl, int K, const double* th, double* r, double* J)
    {
        const int P = cl.p1 - cl.p0 + 1;
        for (int p = 0; p < P; ++p)
        {
            r[2 * p]     = ybuf[(size_t) p].real();
            r[2 * p + 1] = ybuf[(size_t) p].imag();
        }

        for (int k = 0; k < K; ++k)
        {
            const cd c (th[4 * k], th[4 * k + 1]);
            const double w = th[4 * k + 2], d = th[4 * k + 3];
            cd* S1 = kern.data() + (size_t) k * 4 * maxProbes;
            cd* dS1 = S1 + maxProbes;
            cd* S2 = dS1 + maxProbes;
            cd* dS2 = S2 + maxProbes;
            kernelRow (w, d, cl.p0, P, S1, dS1, S2, dS2, needsImage (w));
            const cd j (0.0, 1.0);
            for (int p = 0; p < P; ++p)
            {
                const cd v = c * S1[p] + std::conj (c) * S2[p];
                r[2 * p]     -= v.real();
                r[2 * p + 1] -= v.imag();

                const cd dRe = S1[p] + S2[p];
                const cd dIm = j * (S1[p] - S2[p]);
                const cd dW  = j * c * dS1[p] - j * std::conj (c) * dS2[p];
                const cd dD  = -c * dS1[p] - std::conj (c) * dS2[p];
                double* row0 = J + (size_t) (2 * p) * maxParams;
                double* row1 = row0 + maxParams;
                row0[4 * k] = dRe.real(); row1[4 * k] = dRe.imag();
                row0[4 * k + 1] = dIm.real(); row1[4 * k + 1] = dIm.imag();
                row0[4 * k + 2] = dW.real(); row1[4 * k + 2] = dW.imag();
                row0[4 * k + 3] = dD.real(); row1[4 * k + 3] = dD.imag();
            }
        }

        double e = 0.0;
        for (int i = 0; i < 2 * P; ++i)
            e += r[i] * r[i];
        return e;
    }

    double priorCost (const int* idx, int K, const double* th) const
    {
        double e = 0.0;
        for (int k = 0; k < K; ++k)
        {
            const auto& c = comps[(size_t) idx[k]];
            if (c.psw > 0.0)
                e += std::pow ((th[4 * k + 2] - c.pw) / c.psw, 2.0);
            if (c.psd > 0.0)
                e += std::pow ((th[4 * k + 3] - c.pd) / c.psd, 2.0);
            e += (th[4 * k] * th[4 * k] + th[4 * k + 1] * th[4 * k + 1]) * ampPrecision;
        }
        return e;
    }

    /** A = J^T J / s2 (+ the priors' precisions), g = J^T r / s2 (- the priors' gradients). */
    void normal (int P, int np, const double* J, const double* r, const int* idx, int K, const double* th,
                 double* A, double* g, bool withPriors) const
    {
        work += 2.0 * P * np * (np + 1);
        for (int a = 0; a < np; ++a)
        {
            g[a] = 0.0;
            for (int b = 0; b < np; ++b)
                A[a * maxParams + b] = 0.0;
        }
        for (int i = 0; i < 2 * P; ++i)
        {
            const double* row = J + (size_t) i * maxParams;
            for (int a = 0; a < np; ++a)
            {
                const double ra = row[a];
                if (ra == 0.0)
                    continue;
                g[a] += ra * r[i];
                for (int b = a; b < np; ++b)
                    A[a * maxParams + b] += ra * row[b];
            }
        }
        for (int a = 0; a < np; ++a)
        {
            g[a] /= s2;
            for (int b = a; b < np; ++b)
            {
                A[a * maxParams + b] /= s2;
                A[b * maxParams + a] = A[a * maxParams + b];
            }
        }
        if (! withPriors)
            return;
        for (int k = 0; k < K; ++k)
        {
            const auto& c = comps[(size_t) idx[k]];
            for (int a = 4 * k; a < 4 * k + 2; ++a)
            {
                A[a * maxParams + a] += ampPrecision;
                g[a] -= th[a] * ampPrecision;
            }
            if (c.psw > 0.0)
            {
                A[(4 * k + 2) * maxParams + 4 * k + 2] += 1.0 / (c.psw * c.psw);
                g[4 * k + 2] -= (th[4 * k + 2] - c.pw) / (c.psw * c.psw);
            }
            if (c.psd > 0.0)
            {
                A[(4 * k + 3) * maxParams + 4 * k + 3] += 1.0 / (c.psd * c.psd);
                g[4 * k + 3] -= (th[4 * k + 3] - c.pd) / (c.psd * c.psd);
            }
        }
    }

    /** Cholesky solve of the n x n system M x = b (M row-major with stride maxParams), in place. */
    static bool cholSolve (double* M, double* b, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j <= i; ++j)
            {
                double sum = M[i * maxParams + j];
                for (int k = 0; k < j; ++k)
                    sum -= M[i * maxParams + k] * M[j * maxParams + k];
                if (i == j)
                {
                    if (! (sum > 0.0))
                        return false;
                    M[i * maxParams + i] = std::sqrt (sum);
                }
                else
                    M[i * maxParams + j] = sum / M[j * maxParams + j];
            }
        }
        for (int i = 0; i < n; ++i)
        {
            double sum = b[i];
            for (int k = 0; k < i; ++k)
                sum -= M[i * maxParams + k] * b[k];
            b[i] = sum / M[i * maxParams + i];
        }
        for (int i = n - 1; i >= 0; --i)
        {
            double sum = b[i];
            for (int k = i + 1; k < n; ++k)
                sum -= M[k * maxParams + i] * b[k];
            b[i] = sum / M[i * maxParams + i];
        }
        return true;
    }

    double priorAt (const Comp& c, double w, double d) const
    {
        double e = 0.0;
        if (c.psw > 0.0)
            e += std::pow ((w - c.pw) / c.psw, 2.0);
        if (c.psd > 0.0)
            e += std::pow ((d - c.pd) / c.psd, 2.0);
        return e;
    }

    bool permuteByPriors (const int* idx, int K, double* th) const
    {
        bool any = false;
        for (int pass = 0; pass < K; ++pass)
        {
            bool swapped = false;
            for (int a = 0; a < K; ++a)
                for (int b = a + 1; b < K; ++b)
                {
                    const auto& ca = comps[(size_t) idx[a]];
                    const auto& cb = comps[(size_t) idx[b]];
                    const double now  = priorAt (ca, th[4 * a + 2], th[4 * a + 3]) + priorAt (cb, th[4 * b + 2], th[4 * b + 3]);
                    const double swap = priorAt (ca, th[4 * b + 2], th[4 * b + 3]) + priorAt (cb, th[4 * a + 2], th[4 * a + 3]);
                    if (swap < now * (1.0 - 1.0e-9) && th[4 * a + 2] >= cb.wLo && th[4 * a + 2] <= cb.wHi
                        && th[4 * b + 2] >= ca.wLo && th[4 * b + 2] <= ca.wHi)
                    {
                        for (int i = 0; i < 4; ++i)
                            std::swap (th[4 * a + i], th[4 * b + i]);
                        swapped = any = true;
                    }
                }
            if (! swapped)
                break;
        }
        return any;
    }

    /** Levenberg-Marquardt over the cluster's active members; the result is written back. With
        amplitudesOnly, the members keep their frequencies and decays and only their amplitudes are
        solved — what the masked test asks of a string's neighbours. */
    void fitCluster (int ci, int maxIter, bool amplitudesOnly = false)
    {
        auto& cl = clusters[(size_t) ci];
        clusterTarget (cl);

        int idx[maxClusterComps];
        int K = 0;
        for (int k = cl.first; k < cl.first + cl.count; ++k)
            if (comps[(size_t) k].active)
                idx[K++] = k;
        if (K == 0)
        {
            cl.resid = cl.energy;
            return;
        }

        const int P = cl.p1 - cl.p0 + 1;
        const int np = 4 * K;
        double th[maxParams], tt[maxParams], g[maxParams];
        double A[maxParams * maxParams], M[maxParams * maxParams];
        for (int k = 0; k < K; ++k)
        {
            const auto& c = comps[(size_t) idx[k]];
            th[4 * k] = c.c.real(); th[4 * k + 1] = c.c.imag(); th[4 * k + 2] = c.w; th[4 * k + 3] = c.d;
        }

        // The amplitudes are linear: solve them outright first, so the frequencies start with
        // gradients that mean something.
        double data = evaluate (cl, K, th, res.data(), jac.data());
        normal (P, np, jac.data(), res.data(), idx, K, th, A, g, false);
        {
            const int nc = 2 * K;
            double Mc[maxParams * maxParams], gc[maxParams];
            for (int a = 0; a < nc; ++a)
            {
                const int ia = 4 * (a / 2) + (a & 1);
                gc[a] = g[ia];
                for (int b = 0; b < nc; ++b)
                    Mc[a * maxParams + b] = A[ia * maxParams + 4 * (b / 2) + (b & 1)];
                Mc[a * maxParams + a] *= 1.0 + 1.0e-9;
            }
            if (cholSolve (Mc, gc, nc))
                for (int a = 0; a < nc; ++a)
                    th[4 * (a / 2) + (a & 1)] += gc[a];
        }

        data = evaluate (cl, K, th, res.data(), jac.data());
        if (amplitudesOnly)
        {
            for (int k = 0; k < K; ++k)
                comps[(size_t) idx[k]].c = cd (th[4 * k], th[4 * k + 1]);
            cl.resid = data;
            cl.cost  = data / s2 + priorCost (idx, K, th);
            return;
        }
        double cost = data / s2 + priorCost (idx, K, th);
        double lambda = 1.0e-3;

        for (int it = 0; it < maxIter; ++it)
        {
            normal (P, np, jac.data(), res.data(), idx, K, th, A, g, true);
            bool accepted = false;
            double newCost = cost, newData = data;
            for (int tries = 0; tries < 10; ++tries)
            {
                double maxDiag = 0.0;
                for (int a = 0; a < np; ++a)
                    maxDiag = std::max (maxDiag, A[a * maxParams + a]);
                for (int a = 0; a < np; ++a)
                {
                    for (int b = 0; b < np; ++b)
                        M[a * maxParams + b] = A[a * maxParams + b];
                    M[a * maxParams + a] += lambda * std::max (A[a * maxParams + a], 1.0e-12 * maxDiag) + 1.0e-300;
                    tt[a] = g[a];
                }
                if (! cholSolve (M, tt, np))
                {
                    lambda *= 10.0;
                    continue;
                }
                for (int a = 0; a < np; ++a)
                    tt[a] += th[a];
                for (int k = 0; k < K; ++k)
                {
                    const auto& c = comps[(size_t) idx[k]];
                    tt[4 * k + 2] = std::clamp (tt[4 * k + 2], c.wLo, c.wHi);
                    tt[4 * k + 3] = std::clamp (tt[4 * k + 3], -1.0 / spanN, 40.0 / spanN);
                }
                newData = evaluate (cl, K, tt, resTry.data(), jacTry.data());
                newCost = newData / s2 + priorCost (idx, K, tt);
                if (newCost < cost)
                {
                    accepted = true;
                    break;
                }
                lambda *= 8.0;
            }
            if (! accepted)
                break;

            const double gain = cost - newCost;
            std::copy (tt, tt + np, th);
            res.swap (resTry);
            jac.swap (jacTry);
            cost = newCost;
            data = newData;
            lambda = std::max (lambda * 0.2, 1.0e-12);
            if (gain < 1.0e-10 * cost + 1.0e-12)
                break;
        }

        // The model is the same whichever member wears which parameters, so the data cannot say
        // which is which — two partials a cent apart fit as well swapped as not, and the fit
        // keeps whichever way it started. The priors can: each member takes the parameters
        // nearest where its own string puts it.
        if (permuteByPriors (idx, K, th))
            data = evaluate (cl, K, th, res.data(), jac.data());

        for (int k = 0; k < K; ++k)
        {
            auto& c = comps[(size_t) idx[k]];
            c.c = cd (th[4 * k], th[4 * k + 1]);
            c.w = th[4 * k + 2];
            c.d = th[4 * k + 3];
        }
        cl.resid = data;
        cl.cost  = data / s2 + priorCost (idx, K, th);

        // What each string partial's frequency is known to from the DATA and the other members'
        // priors — never its own: that one came from its own string, and must not vouch twice. The
        // data's weight is what the fit LEFT, not the hiss: a residual the model cannot explain (a
        // pitch still gliding, two planes of vibration beating, a partial off the formula) is the
        // measure of how far the model is from the string, and every variance grows with it.
        const double fitNoise = data / std::max (1.0, P - 2.0 * K * (double) L / spanN);
        const double scale    = s2 / (0.5 * std::max (noisePow, fitNoise) * (double) L / spanN);
        normal (P, np, jac.data(), res.data(), idx, K, th, A, g, false);
        for (int k = 0; k < K; ++k)
        {
            auto& c = comps[(size_t) idx[k]];
            if (c.string < 0)
                continue;
            for (int a = 0; a < np; ++a)
            {
                for (int b = 0; b < np; ++b)
                    M[a * maxParams + b] = A[a * maxParams + b] * scale;
                tt[a] = a == 4 * k + 2 ? 1.0 : 0.0;
            }
            for (int j = 0; j < K; ++j)
            {
                const auto& o = comps[(size_t) idx[j]];
                M[(4 * j) * maxParams + 4 * j]         += ampPrecision;
                M[(4 * j + 1) * maxParams + 4 * j + 1] += ampPrecision;
                if (o.psw > 0.0 && j != k)
                    M[(4 * j + 2) * maxParams + 4 * j + 2] += 1.0 / (o.psw * o.psw);
                if (o.psd > 0.0)
                    M[(4 * j + 3) * maxParams + 4 * j + 3] += 1.0 / (o.psd * o.psd);
            }
            for (int a = 0; a < np; ++a)
                M[a * maxParams + a] *= 1.0 + 1.0e-12;
            c.varW = cholSolve (M, tt, np) && tt[4 * k + 2] > 0.0 ? tt[4 * k + 2]
                                                               : std::numeric_limits<double>::infinity();
        }
    }

    //==========================================================================
    // The string model: (ln w0, B) from a string's partials, each weighted by how well it is known.

    ModelFit fitString (int s, int skip) const
    {
        // The string's measured partials: ln(w/h) and its variance.
        struct Meas { double h2, y, v; bool use; bool keep; };
        std::array<Meas, maxHarm + 1> m {};
        int n = 0;
        for (int k = 0; k < nComps && n < maxHarm + 1; ++k)
        {
            const auto& c = comps[(size_t) k];
            if (c.string != s || k == skip || ! c.active || c.shield || ! std::isfinite (c.varW))
                continue;
            m[(size_t) n++] = { (double) c.harm * c.harm, std::log (c.w / c.harm),
                                c.varW / (c.w * c.w) + std::pow (measFloorCents / centsPerLog, 2.0), true,
                                c.harm == 1 && strings[(size_t) s].status == 1 };
        }

        ModelFit f;
        // A partial that disagrees with the rest by far more than its own error is another
        // string's, taken for this one's in a cluster where the two sit close: dropped, or it
        // bends the inharmonicity and drags the whole string with it.
        f = fitMeasurements (m.data(), n);
        for (int drop = 0; drop < 4 && f.ok; ++drop)
        {
            int worst = -1;
            double worstZ = 5.0;
            int used = 0;
            for (int i = 0; i < n; ++i)
            {
                if (! m[(size_t) i].use)
                    continue;
                ++used;
                // A string found by a peak of its own keeps that peak as its first partial: in drop D
                // its even partials ARE the D string's, and a majority of them can vote for a
                // series an octave up.
                if (m[(size_t) i].keep)
                    continue;
                const double r = m[(size_t) i].y - f.L - 0.5 * std::log1p (f.B * m[(size_t) i].h2);
                const double z = std::abs (r) / std::sqrt (m[(size_t) i].v + std::pow (outlierFloorCents / centsPerLog, 2.0));
                if (z > worstZ)
                {
                    worstZ = z;
                    worst = i;
                }
            }
            if (worst < 0 || used <= 2)
                break;
            m[(size_t) worst].use = false;
            f = fitMeasurements (m.data(), n);
        }
        return f;
    }

    template <typename M>
    static ModelFit fitMeasurements (const M* m, int n)
    {
        ModelFit f;
        double L0 = 0.0, B = Bnom, sw = 0.0;
        for (int i = 0; i < n; ++i)
            if (m[i].use)
            {
                L0 += (m[i].y - 0.5 * std::log1p (B * m[i].h2)) / m[i].v;
                sw += 1.0 / m[i].v;
            }
        if (sw <= 0.0)
            return f;
        L0 /= sw;

        double A00 = 0, A01 = 0, A11 = 0;
        bool bound = false;
        for (int it = 0; it < 4; ++it)
        {
            A00 = A01 = 0.0;
            A11 = 1.0 / (Bsd * Bsd);
            double g0 = 0.0, g1 = -(B - Bnom) / (Bsd * Bsd);
            for (int i = 0; i < n; ++i)
            {
                if (! m[i].use)
                    continue;
                const double a = 0.5 * m[i].h2 / (1.0 + B * m[i].h2);
                const double r = m[i].y - L0 - 0.5 * std::log1p (B * m[i].h2);
                A00 += 1.0 / m[i].v; A01 += a / m[i].v; A11 += a * a / m[i].v;
                g0 += r / m[i].v;    g1 += a * r / m[i].v;
            }
            const double det = A00 * A11 - A01 * A01;
            if (! (det > 0.0))
                return f;
            const double Bfree = B + (A00 * g1 - A01 * g0) / det;
            bound = Bfree < 0.0 || Bfree > Bmax;
            if (! bound)
            {
                L0 += (A11 * g0 - A01 * g1) / det;
                B   = Bfree;
                continue;
            }
            // The stiffness at its bound: the intercept is solved again with B HELD there — moved by the
            // free step and then clamped, it published a fundamental none of the partials implies.
            B = std::clamp (Bfree, 0.0, Bmax);
            double sl = 0.0, sv = 0.0;
            for (int i = 0; i < n; ++i)
                if (m[i].use)
                {
                    sl += (m[i].y - 0.5 * std::log1p (B * m[i].h2)) / m[i].v;
                    sv += 1.0 / m[i].v;
                }
            L0 = sl / sv;
        }
        const double det = A00 * A11 - A01 * A01;
        f.ok  = true;
        f.L   = L0;
        f.B   = B;
        if (bound)
        {
            f.cLL = 1.0 / A00;
            f.cLB = f.cBB = 0.0;
            return f;
        }
        f.cLL = A11 / det;
        f.cLB = -A01 / det;
        f.cBB = A00 / det;
        return f;
    }

    /** Where a fit puts partial h, in ln w, and that place's variance. */
    static void predict (const ModelFit& f, int h, double& lnw, double& var)
    {
        const double h2 = (double) h * h;
        const double a  = 0.5 * h2 / (1.0 + f.B * h2);
        lnw = std::log ((double) h) + f.L + 0.5 * std::log1p (f.B * h2);
        var = f.cLL + 2.0 * a * f.cLB + a * a * f.cBB;
    }

    void setPriors (const Cluster& cl)
    {
        for (int k = cl.first; k < cl.first + cl.count; ++k)
        {
            auto& c = comps[(size_t) k];
            if (c.string < 0)
                continue;
            const ModelFit f = fitString (c.string, k);
            if (! f.ok)
            {
                c.psw = 0.0;
                continue;
            }
            double lnw, var;
            predict (f, c.harm, lnw, var);
            c.pw  = std::exp (lnw);
            c.psw = c.pw * std::sqrt (var + std::pow (priorFloorCents / centsPerLog, 2.0));
        }
    }

    /** A string the coarse pass saw only on other strings' partials was put on the one nearest
        its note — a guess. The fit says where it really is: on the partial of another string that
        had to be dragged furthest from where its own string puts it, to soak up energy that is
        not its own. A lost string (its first partial holding next to nothing) moves there, its
        partials after it. */
    void rehomeCoincident()
    {
        for (int s = 0; s < tuning.strings; ++s)
        {
            if (strings[(size_t) s].status != 2)
                continue;
            const double t = target[(size_t) s];
            double bestStrain = 25.0, bestW = 0.0;
            for (int k = 0; k < nComps; ++k)
            {
                const auto& c = comps[(size_t) k];
                if (c.string < 0 || c.string == s || c.psw <= 0.0 || c.c == cd {})
                    continue;
                const double hz = c.w * fsD / (2.0 * pi);
                if (std::abs (1200.0 * std::log2 (hz / t)) > farCents)
                    continue;
                const double strain = std::pow ((c.w - c.pw) / c.psw, 2.0);
                if (strain > bestStrain)
                {
                    bestStrain = strain;
                    bestW = c.w;
                }
            }
            if (bestW <= 0.0)
                continue;

            // Only a string that is LOST moves: one already standing on its own partial keeps it,
            // whatever strain some weak neighbour shows.
            double own = 0.0, strained = 0.0;
            for (int k = 0; k < nComps; ++k)
            {
                const auto& c = comps[(size_t) k];
                if (c.string == s && c.harm == 1)
                    own = amplitudeOf (c);
                if (c.string >= 0 && c.string != s && c.w == bestW)
                    strained = amplitudeOf (c);
            }
            if (own > 0.1 * strained)
                continue;

            for (int k = 0; k < nComps; ++k)
            {
                auto& c = comps[(size_t) k];
                if (c.string != s)
                    continue;
                const double h2 = (double) c.harm * c.harm;
                c.w = bestW * c.harm * std::sqrt ((1.0 + Bnom * h2) / (1.0 + Bnom));
                c.c = cd {};
                c.varW = std::numeric_limits<double>::infinity();
            }
        }
    }

    /** A partial its own string's model disowns — far from where the string's other partials put
        it — started in the wrong place (or was taken by a neighbour) and cannot walk out of its
        cluster on its own: it starts again where the model says, its amplitude solved afresh. */
    void relocateStrays()
    {
        for (int k = 0; k < nComps; ++k)
        {
            auto& c = comps[(size_t) k];
            if (c.string < 0)
                continue;
            const ModelFit f = fitString (c.string, k);
            if (! f.ok)
                continue;
            double lnw, var;
            predict (f, c.harm, lnw, var);
            const double w  = std::exp (lnw);
            const double sd = w * std::sqrt (var + std::pow (outlierFloorCents / centsPerLog, 2.0));
            if (std::abs (c.w - w) > std::max (5.0 * sd, 0.5 * binW))
            {
                c.w = w;
                c.c = cd {};
                c.varW = std::numeric_limits<double>::infinity();
            }
        }
    }

    void fitClusterWithPriors (int ci, int sweep)
    {
        auto& cl = clusters[(size_t) ci];
        setPriors (cl);

        bool hasCoincident = false, manyStrings = false;
        for (int k = cl.first; k < cl.first + cl.count; ++k)
        {
            const auto& c = comps[(size_t) k];
            hasCoincident |= c.coincident;
            for (int j = cl.first; j < k; ++j)
                manyStrings |= c.string >= 0 && comps[(size_t) j].string >= 0 && comps[(size_t) j].string != c.string;
        }

        if (! hasCoincident && ! manyStrings)
        {
            fitCluster (ci, sweep == 0 ? 40 : 20);
            return;
        }

        // Partials of different strings a fraction of a bin apart are a fit with many bad corners:
        // both on one frequency with huge amplitudes cancelling, one taking the other's energy. So
        // two starts are tried and the better fit stays: carrying on from where the last sweep left
        // off, and starting FROM THE PRIORS — every member where its own string puts it, only the
        // amplitudes solved, and a string seen only on others' partials (coincident) put on what
        // they then leave unexplained. Left free in that first step the others would run onto the
        // missing string's energy and leave no trace of it. On the first sweep, with nothing carried
        // yet, the start from the priors is the only one.
        std::array<Comp, maxClusterComps> before {}, carried {};
        std::copy (comps.begin() + cl.first, comps.begin() + cl.first + cl.count, before.begin());
        double carriedCost = std::numeric_limits<double>::infinity();
        if (sweep > 0)
        {
            fitCluster (ci, 20);
            carriedCost = cl.cost;
            std::copy (comps.begin() + cl.first, comps.begin() + cl.first + cl.count, carried.begin());
            std::copy (before.begin(), before.begin() + cl.count, comps.begin() + cl.first);
        }

        for (int k = cl.first; k < cl.first + cl.count; ++k)
        {
            auto& c = comps[(size_t) k];
            if (c.coincident)
                c.active = false;
            else if (c.psw > 0.0)
                c.w = std::clamp (c.pw, c.wLo, c.wHi);
            c.c = cd {};
        }
        fitCluster (ci, 0, true);

        if (hasCoincident)
        {
            const int P = cl.p1 - cl.p0 + 1;
            for (int p = 0; p < P; ++p)
                scratch[(size_t) p] = std::norm (ybuf[(size_t) p]);
            for (int k = cl.first; k < cl.first + cl.count; ++k)
            {
                const auto& c = comps[(size_t) k];
                if (c.active && c.c != cd {})
                    subtractFrom (c, cl.p0, P);
            }
            for (int k = cl.first; k < cl.first + cl.count; ++k)
            {
                auto& c = comps[(size_t) k];
                if (! c.coincident)
                    continue;
                c.active = true;
                c.c = cd {};
                int best = -1;
                for (int p = 0; p < P; ++p)
                {
                    const double wp = 2.0 * pi * (cl.p0 + p) / L;
                    if (wp >= c.wLo && wp <= c.wHi && (best < 0 || scratch[(size_t) p] > scratch[(size_t) best]))
                        best = p;
                }
                if (best >= 0)
                    c.w = 2.0 * pi * (cl.p0 + best) / L;
            }
        }

        fitCluster (ci, sweep == 0 ? 40 : 20);
        if (carriedCost < cl.cost)
        {
            std::copy (carried.begin(), carried.begin() + cl.count, comps.begin() + cl.first);
            fitCluster (ci, 0, true);   // the bookkeeping (residual, cost) of the fit that stays
        }
    }

    /** scratch[p] = |ybuf - this component|^2 at the cluster's probes, for the residual's peak. */
    void subtractFrom (const Comp& c, int p0, int P)
    {
        cd* S1 = kern.data();
        cd* dS1 = S1 + maxProbes;
        cd* S2 = dS1 + maxProbes;
        cd* dS2 = S2 + maxProbes;
        kernelRow (c.w, c.d, p0, P, S1, dS1, S2, dS2, needsImage (c.w));
        for (int p = 0; p < P; ++p)
            ybuf[(size_t) p] -= c.c * S1[p] + std::conj (c.c) * S2[p];
        for (int p = 0; p < P; ++p)
            scratch[(size_t) p] = std::norm (ybuf[(size_t) p]);
    }

    //==========================================================================
    // What each string is taken for.

    /** A partial's amplitude in the middle of the span, as a sinusoid's peak. */
    double amplitudeOf (const Comp& c) const { return 2.0 * std::abs (c.c) * std::exp (-c.d * spanN * 0.5); }

    /** What each string is taken for, into `r`. A number is published only where the evidence holds:
        the string's OWN first partial, measured (not the model's extrapolation — real partials stray off
        the stiff-string formula by cents), vouched for by its other partials, known to `sigmaGate`, and
        not standing on another string's partial or on a mains line it cannot be told from. */
    void decideBegin()
    {
        dLevel = {};
        dLoudest = 0.0;
        for (int k = 0; k < nComps; ++k)
        {
            const auto& c = comps[(size_t) k];
            if (c.string >= 0 && c.harm <= 2)
                dLevel[(size_t) c.string] = std::max (dLevel[(size_t) c.string], amplitudeOf (c));
        }
        for (int s = 0; s < tuning.strings; ++s)
            dLoudest = std::max (dLoudest, dLevel[(size_t) s]);
    }

    /** String s's verdict into `r` (decideBegin first). */
    void decideString (int s, Reading& r, double sigmaGate)
    {
        const auto& level = dLevel;
        const double loudest = dLoudest;
        // The geometry is the WHOLE span's, also when a half is analysed: what is published is resolved
        // by the whole span, and a half must not mask what the whole tells apart.
        const double unisonHz = unisonBins / geometrySec;
        {
            auto& o = r.string[(size_t) s];
            const auto& st = strings[(size_t) s];
            o = {};
            o.targetHz = (float) target[(size_t) s];
            o.levelDb  = level[(size_t) s] > 0.0 ? (float) (20.0 * std::log10 (level[(size_t) s])) : -200.0f;
            if (st.status == 0)
                return;

            // Masked only under another STRING: a mains line is held to its line and to no decay,
            // so a string standing on it would still show — one that does not is not there.
            const Heard unheard = st.status == 2 && ! st.underMains ? Heard::masked : Heard::no;
            const bool loudEnough = level[(size_t) s] >= std::max (0.1 * rmsFloor, loudest * std::pow (10.0, heardRelDb / 20.0));
            if (! loudEnough || (st.status == 2 && ! addsSomething (s)))
            {
                o.heard = unheard;
                return;
            }

            // The first partial as measured.
            int k1 = -1;
            for (int k = 0; k < nComps; ++k)
                if (comps[(size_t) k].string == s && comps[(size_t) k].harm == 1 && comps[(size_t) k].active)
                    k1 = k;
            if (k1 < 0 || ! std::isfinite (comps[(size_t) k1].varW))
            {
                o.heard = unheard;
                return;
            }
            const auto&  c1 = comps[(size_t) k1];
            const double hz = c1.w * fsD / (2.0 * pi);
            const double sd = std::sqrt (c1.varW) / c1.w * centsPerLog;
            if (hz < st.lo * 0.999 || hz > st.hi * 1.001)
            {
                o.heard = unheard;
                return;
            }

            // On another string's partial — where it was fitted, or where its own string puts it —
            // within a bin of the span: the two cannot be told apart, and a muted string would read
            // its neighbour's harmonic. Masked, by geometry, whatever the residual says.
            bool onOther = false;
            for (int k = 0; k < nComps; ++k)
            {
                const auto& c = comps[(size_t) k];
                if (c.string < 0 || c.string == s || ! c.active || strings[(size_t) c.string].status == 0)
                    continue;
                // Where its own string puts it is known only to the stray of real partials off the formula
                // (priorFloorCents, up to 3.5 sd of it) — never to a poorly known string's own uncertainty,
                // which would mask everything near a string that barely exists.
                const double fw = c.w * fsD / (2.0 * pi), fp = c.psw > 0.0 ? c.pw * fsD / (2.0 * pi) : fw;
                const double sp = c.psw > 0.0 ? 3.5 * fp * priorFloorCents / centsPerLog : 0.0;
                onOther |= std::abs (hz - fw) < unisonHz || std::abs (hz - fp) < unisonHz + sp;
            }
            if (onOther)
            {
                o.heard = Heard::masked;
                return;
            }

            // On a mains line: never on a line the mains is known to hold (it is tracked: the line is where it
            // stands, not where 50 x k would be), and elsewhere where the mains may stand only a string seen to
            // die away.
            const bool dies = c1.d * fsD >= 1.0 / mainsDecaySec;
            if (onLearnedLine (hz, unisonHz) || (onMainsLine (hz, unisonHz) && ! dies))
            {
                o.heard = Heard::no;
                return;
            }
            // ... nor a first partial whose series is exactly harmonic and never dies: a mains series off any
            // grid, a hum, a synth — not a string.
            if (! dies && exactSeries (s, k1))
            {
                o.heard = Heard::no;
                return;
            }

            // Vouched for: the string's other partials, through its model, put the first one within
            // vouchCents of where it was measured — or it is not this string's.
            const ModelFit loo = fitString (s, k1);
            if (loo.ok)
            {
                const double a   = 0.5 / (1.0 + loo.B);
                const double var = loo.cLL + 2.0 * a * loo.cLB + a * a * loo.cBB;
                const double dc  = (std::log (c1.w) - loo.L - 0.5 * std::log1p (loo.B)) * centsPerLog;
                if (std::abs (dc) > std::max (vouchCents, 3.0 * std::sqrt (var * centsPerLog * centsPerLog + sd * sd)))
                {
                    o.heard = unheard;
                    return;
                }
            }

            if (sd > sigmaGate)
            {
                o.heard = Heard::no;   // the lane waits
                return;
            }

            decayOf[(size_t) s] = c1.d * fsD;
            o.hz         = (float) hz;
            o.cents      = (float) (1200.0 * std::log2 (hz / target[(size_t) s]));
            o.sigmaCents = (float) sd;
            o.heard      = std::abs (o.cents) > farCents ? Heard::far : Heard::near;
        }
    }

    /** Does string s explain anything its neighbours in the clusters do not? Take it out and let
        the others re-solve only their AMPLITUDES, each held to the frequency and decay it was fitted
        to: they can soak up this string only where it coincides with one of them. A string that is
        not there leaves the residual where it was; one that is there leaves a share of its own
        energy behind — all of it when it stands apart, less the closer it sits to another string's
        partial, nothing in exact unison. Measured against the string's own energy, not the
        clusters': a strong neighbour in the same cluster must not drown it. (Letting the others
        move was too kind to "absent": a low E's 4th partial slid half a hertz onto a high E four
        times its size and explained it away.) */
    bool addsSomething (int s)
    {
        std::copy (comps.begin(), comps.begin() + nComps, savedComps.begin());
        std::copy (clusters.begin(), clusters.begin() + nClusters, savedClusters.begin());

        double with = 0.0, without = 0.0, own = 0.0;
        cd* S1 = kern.data();
        cd* dS1 = S1 + maxProbes;
        cd* S2 = dS1 + maxProbes;
        cd* dS2 = S2 + maxProbes;
        for (int ci = 0; ci < nClusters; ++ci)
        {
            const auto& cl = clusters[(size_t) ci];
            bool has = false;
            for (int k = cl.first; k < cl.first + cl.count; ++k)
            {
                const auto& c = comps[(size_t) k];
                if (c.string != s || ! c.active)
                    continue;
                has = true;
                const int P = cl.p1 - cl.p0 + 1;
                kernelRow (c.w, c.d, cl.p0, P, S1, dS1, S2, dS2, needsImage (c.w));
                for (int p = 0; p < P; ++p)
                    own += std::norm (c.c * S1[p] + std::conj (c.c) * S2[p]);
            }
            if (has)
                with += cl.resid;
        }
        for (int k = 0; k < nComps; ++k)
            if (comps[(size_t) k].string == s)
                comps[(size_t) k].active = false;
        for (int ci = 0; ci < nClusters; ++ci)
        {
            const auto& cl = clusters[(size_t) ci];
            bool has = false;
            for (int k = cl.first; k < cl.first + cl.count; ++k)
                has |= comps[(size_t) k].string == s;
            if (! has)
                continue;
            // The others start where their own strings put them and may move only as far as those
            // priors allow (3 sd, a real partial's stray): held where they were fitted, a neighbour
            // that took the absent string's part kept it, and the residual "proved" the string; left
            // free, a neighbour slides onto a string that IS there and explains it away.
            for (int k = cl.first; k < cl.first + cl.count; ++k)
            {
                auto& c = comps[(size_t) k];
                if (! c.active || c.string < 0 || c.psw <= 0.0)
                    continue;
                c.w   = std::clamp (c.pw, c.wLo, c.wHi);
                c.wLo = std::max (c.wLo, c.pw - 3.0 * c.psw);
                c.wHi = std::min (c.wHi, c.pw + 3.0 * c.psw);
                c.c   = cd {};
            }
            fitCluster (ci, 20);
            without += clusters[(size_t) ci].resid;
        }

        std::copy (savedComps.begin(), savedComps.begin() + nComps, comps.begin());
        std::copy (savedClusters.begin(), savedClusters.begin() + nClusters, clusters.begin());
        return without - with > std::max (maskedShare * own, 50.0 * s2);
    }

    //==========================================================================
    static constexpr int maxHarm = 40;
    static constexpr double riseRatio = 2.0;   // +3 dB over the last 150 ms' crest

    Tuning tuning = tunings[0];
    double a4 = 440.0;
    std::array<double, maxStrings> target {};
    std::array<int, maxStrings> order {};
    double fUse = 750.0, fModel = 800.0;

    double fs = 48000.0, fsD = 2400.0;
    int    decim = 20;
    Biquad aa[4] {};
    double aaZ[4][2] {};
    Biquad hpf {};
    double hpZ[2] {}, hpSum = 0.0;
    int    phaseCount = 0;
    double rmsFloor = 1.0e-3;

    std::vector<float> hist;
    std::uint64_t histCount = 0;
    double env1 = 0.0, env2 = 0.0, envA = 0.1;
    int    blockLen = 24, blockFill = 0;
    std::uint64_t blocksSeen = 0;
    std::array<double, 15> ring {};   // the last 150 ms of the envelope, newest first
    bool   anyAttack = false;
    std::uint64_t lastAttack = 0, lastAnalysed = 0, settleN = 600, cadenceN = 480;
    int    minN = 720;
    unsigned strumCount = 0, analysedStrum = ~0u;
    double humHz = 0.0;              // the mains as tracked: its fundamental (0: none learned)
    unsigned humMask = 0;            // bit k: the k-th mains harmonic stands
    double spanHumHz = 0.0;          // ... as the span in analysis holds it — see startJob
    unsigned spanHumMask = 0;
    std::vector<cd> humSpec;         // learnHum's own spectrum: the span in analysis keeps `spec` across ticks
    std::vector<double> humMag;
    Job job {};
    mutable double work = 0.0;       // what the analysis has done: its inner loops' arithmetic, counted where it is done
                                     // (FFTs, kernels, normal equations, single-bin DFTs) — the tick's budget is spent in it
    std::array<double, maxStrings> dLevel {};   // decideBegin, for decideString
    double dLoudest = 0.0;
    double bbLevel = 0.0, levelA = 0.05, strumPeak = 0.0;   // the broadband level, and its peak since the last strum
    bool   strummed = false;
    std::uint64_t lastHumAt = 0;

    bool tapValid = false;
    unsigned tapCursor = 0;          // TunerTap::writtenCount() as far as the history has it
    std::vector<float> chunk;        // one tick's samples: as many as TunerTap::readSince ever hands over

    // The analysis.
    int spanN = 0, L = 0;
    double binW = 0.0;
    cd rot {};
    std::vector<cd> spec, twiddle;
    std::vector<double> mag, scratch;
    double noisePow = 0.0, noiseMag = 0.0, s2 = 1.0, strongest = 0.0;
    double ampPrecision = 0.0;   // the cluster being fitted — see clusterTarget

    std::array<Peak, maxPeaks> peaks {};
    int nPeaks = 0;
    std::array<double, 128> explained {};
    std::array<bool, 128> explainedByMains {};
    int nExplained = 0;
    std::array<StringState, maxStrings> strings {};
    std::array<std::array<double, maxHarm + 1>, maxStrings> walk {};

    std::array<Comp, maxComps> comps {};
    int nComps = 0;
    std::array<Cluster, maxComps> clusters {};
    int nClusters = 0;
    std::array<Comp, maxComps> savedComps {};
    std::array<Cluster, maxComps> savedClusters {};

    std::vector<cd> ybuf, kern;
    std::vector<double> res, resTry, jac, jacTry;

    Reading out {}, whole {}, firstHalf {}, secondHalf {};
    double geometrySec = 1.0;   // the whole span's length — see decide
    std::array<double, maxStrings> decayOf {};   // each published string's first-partial decay, 1/s — see analyse
};

} // namespace orbitamp::core
