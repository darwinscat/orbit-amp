// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** Monophonic pitch from a window of raw input — the tuner's brain.

    McLeod's normalized square difference (the MPM algorithm): autocorrelation, normalized so a
    perfect repeat scores 1 wherever it falls in the window, with the classic first-key-maximum
    pick that keeps a strong second harmonic from reading an octave up. Two refinements carry it
    from "correct note" to "tuner": parabolic interpolation of the chosen peak, and re-measuring
    at several periods' lag — an interpolation error divides by the number of periods it is
    spread across, which is what makes the top of the fretboard readable at all.

    The window is decimated to ~12 kHz first. A guitar's fundamental lives below 1.4 kHz, so
    everything above is only noise to the correlation — and the cost drops by the square of the
    factor, which is what lets a 30 Hz repaint run this on the message thread without anyone
    noticing.

    JUCE-free on purpose, like ToneStack: the whole tuner brain is testable headless, against
    signals with a known answer. Not real-time-safe (it allocates); it is built to be fed
    snapshots on the message thread, not run in processBlock. */
class PitchTracker
{
public:
    struct Reading
    {
        float hz      = 0.0f;   // 0 = nothing confident enough to show
        float clarity = 0.0f;   // the peak's height, 0..1 — how much the window repeats itself
    };

    void prepare (double sampleRate)
    {
        sr    = std::max (8000.0, sampleRate);
        decim = std::max (1, (int) std::lround (sr / targetRate));
        designAntiAlias();

        work.reserve ((size_t) windowCap);
        nsdf.reserve ((size_t) windowCap / 2 + 1);
        cum.reserve ((size_t) windowCap + 1);
    }

    /** One window, oldest sample first. Every call is its own snapshot — no state carries over,
        so a torn or repeated window costs one reading, never a stuck tuner. */
    Reading analyse (const float* x, int n)
    {
        if (x == nullptr || n < decim * 256)
            return {};

        decimate (x, n);

        const int W = (int) work.size();

        // Remove the mean — DC is a "period" of every length, and the normalization would
        // happily tune to it.
        double sum = 0.0;
        for (float v : work)
            sum += v;
        const float mean = (float) (sum / W);

        double energy = 0.0;
        for (float& v : work)
        {
            v -= mean;
            energy += (double) v * v;
        }

        // A window this quiet is the gap between notes, not a note.
        if (std::sqrt (energy / W) < rmsFloor)
            return {};

        // Prefix sums of squares: the normalization term for ANY lag in O(1), which is what
        // makes the long-lag re-measure below almost free.
        cum.assign ((size_t) W + 1, 0.0);
        for (int i = 0; i < W; ++i)
            cum[(size_t) i + 1] = cum[(size_t) i] + (double) work[(size_t) i] * work[(size_t) i];

        const double rd     = sr / decim;
        const int    minLag = std::max (2, (int) std::floor (rd / fMax));
        const int    maxLag = std::min ((int) (rd / fMin), W / 2);
        if (maxLag <= minLag + 2)
            return {};

        nsdf.assign ((size_t) maxLag + 1, 0.0f);
        nsdf[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
            nsdf[(size_t) tau] = (float) nsdfAt (tau);

        // Skip the zero-lag lobe: candidates only count after the correlation has let go of
        // "everything matches itself at no shift".
        int start = 1;
        while (start <= maxLag && nsdf[(size_t) start] > 0.0f)
            ++start;
        if (start > maxLag)
            return {};
        start = std::max (start, minLag);

        // McLeod's pick: among the local maxima, the FIRST that rivals the best. Taking the
        // global max alone reads an octave down when two periods match a hair better than one;
        // taking the first alone reads an octave up on a strong second harmonic.
        double best = 0.0;
        for (int t = start; t < maxLag; ++t)
            if (nsdf[(size_t) t] > nsdf[(size_t) t - 1] && nsdf[(size_t) t] >= nsdf[(size_t) t + 1])
                best = std::max (best, (double) nsdf[(size_t) t]);
        if (best <= 0.0)
            return {};

        int tau0 = 0;
        for (int t = start; t < maxLag; ++t)
            if (nsdf[(size_t) t] > nsdf[(size_t) t - 1] && nsdf[(size_t) t] >= nsdf[(size_t) t + 1]
                && nsdf[(size_t) t] >= (float) (keyMaxShare * best))
            {
                tau0 = t;
                break;
            }
        if (tau0 == 0)
            return {};

        const double d0      = parabola (nsdf[(size_t) tau0 - 1], nsdf[(size_t) tau0], nsdf[(size_t) tau0 + 1]);
        double       period  = tau0 + d0;
        const double clarity = std::min (1.0, nsdf[(size_t) tau0]
                                                  + 0.25 * (nsdf[(size_t) tau0 - 1] - nsdf[(size_t) tau0 + 1]) * d0);

        if (clarity < clarityFloor)
            return { 0.0f, (float) clarity };

        // Re-measure across as many whole periods as the window affords. The parabola's bias is
        // a fixed fraction of a SAMPLE, so spreading it over m periods divides the cents error
        // by m — on the high strings, where one period is nine samples, this is the difference
        // between a tuner and a guess.
        const int m = std::min (8, (int) (W / (2.0 * period)));
        if (m >= 2)
        {
            const int center = (int) std::lround (period * m);
            if (center + 2 <= W - 1 && center - 2 >= 1)
            {
                double local[5];
                for (int k = 0; k < 5; ++k)
                    local[k] = nsdfAt (center - 2 + k);

                int b = 1;
                for (int k = 2; k <= 3; ++k)
                    if (local[k] > local[b])
                        b = k;

                // Only trust the long lag while it still looks like the same pitch — a decayed
                // or inharmonic tail scores low there, and the single-period answer stands.
                if (local[b] > local[b - 1] && local[b] >= local[b + 1] && local[b] > 0.75 * clarity)
                    period = (center - 2 + b + parabola (local[b - 1], local[b], local[b + 1])) / m;
            }
        }

        return { (float) (rd / period), (float) clarity };
    }

    //==========================================================================
    /** Nearest equal-tempered note, A4 = 440. What the needle shows is the DISTANCE, in cents. */
    struct Note
    {
        int   midi  = 0;      // MIDI number of the nearest note
        float cents = 0.0f;   // signed distance to it, -50..+50
    };

    static Note nearestNote (float hz)
    {
        const float semis = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int   midi  = (int) std::lround (semis);
        return { midi, (semis - (float) midi) * 100.0f };
    }

    static const char* noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names[((midi % 12) + 12) % 12];
    }

    static int noteOctave (int midi) { return midi / 12 - 1; }   // MIDI convention: 69 = A4

private:
    // The analysis band. fMax clears the last fret of a 24-fret guitar (~1319 Hz); fMin reaches
    // a bass low E with margin, so drop tunings never fall off the bottom of the ruler.
    static constexpr double targetRate   = 12000.0;
    static constexpr double aaCutoff     = 1500.0;
    static constexpr double fMin         = 40.0;
    static constexpr double fMax         = 1600.0;
    static constexpr double keyMaxShare  = 0.90;
    static constexpr double clarityFloor = 0.85;    // below this the window is noise, not note
    static constexpr double rmsFloor     = 1.0e-4;  // ~-80 dBFS
    static constexpr int    windowCap    = 3072;    // decimated samples analysed, ~280 ms

    /** Normalized square difference at one lag, from the prefix sums. */
    double nsdfAt (int tau) const
    {
        const int len = (int) work.size() - tau;
        double r = 0.0;
        const float* a = work.data();
        const float* b = work.data() + tau;
        for (int i = 0; i < len; ++i)
            r += (double) a[i] * b[i];

        const double m = cum[(size_t) len] + cum[work.size()] - cum[(size_t) tau];
        return m > 1.0e-12 ? 2.0 * r / m : 0.0;
    }

    /** Vertex offset of the parabola through three neighbours, clamped to the middle bin. */
    static double parabola (double ym, double y0, double yp)
    {
        const double d = ym - 2.0 * y0 + yp;
        return d < -1.0e-12 || d > 1.0e-12 ? std::clamp (0.5 * (ym - yp) / d, -1.0, 1.0) : 0.0;
    }

    /** Anti-alias + decimate the snapshot into `work`, keeping at most the last windowCap
        samples. Fresh filter state per call: the warm-up costs a few corrupt milliseconds at the
        head of a ~300 ms window, and statelessness is what makes torn snapshots harmless. */
    void decimate (const float* x, int n)
    {
        work.clear();
        double z[2][2] = {};
        int phase = 0;

        for (int i = 0; i < n; ++i)
        {
            double v = x[i];
            for (int s = 0; s < 2; ++s)
            {
                const auto& c = aa[s];
                const double y = c.b0 * v + z[s][0];
                z[s][0] = c.b1 * v - c.a1 * y + z[s][1];
                z[s][1] = c.b2 * v - c.a2 * y;
                v = y;
            }

            if (++phase == decim)
            {
                phase = 0;
                work.push_back ((float) v);
            }
        }

        if ((int) work.size() > windowCap)
            work.erase (work.begin(), work.end() - windowCap);
    }

    /** 4th-order Butterworth lowpass at aaCutoff, as two RBJ biquads. */
    void designAntiAlias()
    {
        static constexpr double pi = 3.14159265358979323846;
        static constexpr double q[2] = { 0.54119610, 1.30656296 };

        const double w0 = 2.0 * pi * aaCutoff / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);

        for (int s = 0; s < 2; ++s)
        {
            const double alpha = sw / (2.0 * q[s]);
            const double a0    = 1.0 + alpha;
            aa[s] = { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0,
                      -2.0 * cw / a0,        (1.0 - alpha) / a0 };
        }
    }

    struct Biquad { double b0, b1, b2, a1, a2; };

    double sr    = 48000.0;
    int    decim = 4;
    Biquad aa[2] {};

    std::vector<float>  work;
    std::vector<float>  nsdf;
    std::vector<double> cum;
};

} // namespace orbitamp::core
