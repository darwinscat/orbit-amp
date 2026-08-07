// Acceptance gate for core::PowerAmp — the calibration, not the module. felitronics tests the tube
// stage itself; what is ours is the knob mapping, and the claims worth pinning are:
//
//   * the Drive sweep goes from essentially clean to clearly saturated, and stops before the mush
//   * Drive changes the SOUND, not the loudness — the measured make-up holds the level steady
//   * bypass costs exactly the same latency as the stage, so a toggle cannot shift the timing
//
// Returns non-zero on failure.

#include "core/PowerAmp.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::PowerAmp;

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail)
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    constexpr double sampleRate = 48000.0;
    constexpr double toneHz     = 1000.0;
    constexpr int    blockSize  = 512;

    struct Measured { double levelDb, thdPercent; };

    /** A 1 kHz sine at -12 dBFS through the stage; returns its level relative to the input and the
        total harmonic distortion. */
    Measured run (float driveKnob, float sagKnob,
                  PowerAmp::Tube tube = PowerAmp::Tube::sixL6, int tubeCount = 2)
    {
        PowerAmp amp;
        amp.prepare (sampleRate, blockSize, 2);
        amp.setTube (tube);
        amp.setTubeCount (tubeCount);
        amp.setDrive (driveKnob);
        amp.setSag (sagKnob);

        constexpr int n = 24000;
        constexpr double peak = 0.251;   // -12 dBFS

        std::vector<float> left ((size_t) n), right ((size_t) n);
        for (int i = 0; i < n; ++i)
            left[(size_t) i] = right[(size_t) i] = (float) (peak * std::sin (2.0 * juce::MathConstants<double>::twoPi
                                                                             * 0.5 * toneHz * i / sampleRate));

        for (int off = 0; off < n; off += blockSize)
        {
            float* io[2] = { left.data() + off, right.data() + off };
            amp.process (io, 2, juce::jmin (blockSize, n - off), true);
        }

        // Second half only — past the smoothing and the oversampler's fill.
        const int from = n / 2;
        double total = 0.0, re = 0.0, im = 0.0;

        for (int i = from; i < n; ++i)
        {
            const double s = (double) left[(size_t) i];
            const double a = juce::MathConstants<double>::twoPi * toneHz * (i - from) / sampleRate;
            total += s * s;
            re    += s * std::cos (a);
            im    += s * std::sin (a);
        }

        const int    len  = n - from;
        const double rms  = std::sqrt (total / len);
        const double fund = 2.0 * std::sqrt (re * re + im * im) / len / std::sqrt (2.0);
        const double thd  = fund > 1.0e-9 ? std::sqrt (juce::jmax (0.0, rms * rms - fund * fund)) / fund : 0.0;

        return { 20.0 * std::log10 (juce::jmax (1.0e-9, rms / (peak / std::sqrt (2.0)))), thd * 100.0 };
    }
}

int main()
{
    std::printf ("orbitamp::core::PowerAmp calibration gate\n\n");
    std::printf ("knob   level dB   THD %%\n");

    double minLevel = 1.0e9, maxLevel = -1.0e9;

    for (float knob = 0.0f; knob <= 10.001f; knob += 1.0f)
    {
        const auto m = run (knob, 0.0f);
        std::printf ("%4.1f   %8.2f   %6.2f\n", knob, m.levelDb, m.thdPercent);
        minLevel = juce::jmin (minLevel, m.levelDb);
        maxLevel = juce::jmax (maxLevel, m.levelDb);
    }

    std::printf ("\n");

    const auto clean  = run (0.0f, 0.0f);
    const auto middle = run (5.0f, 0.0f);
    const auto hot    = run (10.0f, 0.0f);

    // The sweep has to actually go somewhere: clean at the bottom, obviously driven at the top.
    report ("knob 0 is essentially clean", clean.thdPercent < 1.0,
            juce::String (clean.thdPercent, 2) + " %");
    report ("knob 5 is breaking up, not clean", middle.thdPercent > 1.5 && middle.thdPercent < 8.0,
            juce::String (middle.thdPercent, 2) + " %");
    report ("knob 10 is cooking", hot.thdPercent > 15.0,
            juce::String (hot.thdPercent, 2) + " %");

    // ...and stops before the range where the stage stops responding: past about +18 dB of drive the
    // THD flattens near 43 % and only the level falls, which is mush rather than a hotter amp.
    report ("knob 10 stops short of the mush", hot.thdPercent < 35.0,
            juce::String (hot.thdPercent, 2) + " % (flat-out is ~43 %)");

    // THE load-bearing one: Drive is a character control, so the loudness must not follow it. Without
    // the measured make-up this span is about 7 dB.
    report ("level holds across the whole sweep", (maxLevel - minLevel) < 2.0,
            juce::String (maxLevel - minLevel, 2) + " dB spread");

    // ---- the tube table -------------------------------------------------------------------------
    // The four bottles have to be four different amps, ordered by headroom: at the same knob an EL84
    // must be further into compression than a KT88, or the list is four names for one sound.
    {
        const auto el84 = run (8.0f, 0.0f, PowerAmp::Tube::el84);
        const auto el34 = run (8.0f, 0.0f, PowerAmp::Tube::el34);
        const auto sixl6 = run (8.0f, 0.0f, PowerAmp::Tube::sixL6);
        const auto kt88 = run (8.0f, 0.0f, PowerAmp::Tube::kt88);

        report ("tubes are ordered by headroom at one knob setting",
                el84.thdPercent > el34.thdPercent && el34.thdPercent > sixl6.thdPercent
                    && sixl6.thdPercent > kt88.thdPercent,
                "EL84 " + juce::String (el84.thdPercent, 1) + " > EL34 " + juce::String (el34.thdPercent, 1)
                    + " > 6L6 " + juce::String (sixl6.thdPercent, 1) + " > KT88 " + juce::String (kt88.thdPercent, 1));

        report ("the spread is worth having, not a rounding difference",
                (el84.thdPercent - kt88.thdPercent) > 8.0,
                juce::String (el84.thdPercent - kt88.thdPercent, 1) + " points apart");
    }

    // One bottle is single-ended class A and has to measure as such: more distortion at the same
    // drive than the push-pull pair, whose symmetry cancels the even harmonics.
    {
        const auto one = run (5.0f, 0.0f, PowerAmp::Tube::sixL6, 1);
        const auto two = run (5.0f, 0.0f, PowerAmp::Tube::sixL6, 2);

        report ("one bottle distorts more than two (class A vs push-pull)",
                one.thdPercent > two.thdPercent * 1.5,
                "1x " + juce::String (one.thdPercent, 2) + " % vs 2x " + juce::String (two.thdPercent, 2) + " %");
    }

    // The make-up is per tube AND per topology, so the level has to hold on every combination — not
    // just on the one the curve was first measured for.
    {
        double worst = 0.0;
        juce::String worstName;

        for (int t = 0; t < (int) PowerAmp::Tube::count; ++t)
            for (int c = 1; c <= 2; ++c)
            {
                double lo = 1.0e9, hi = -1.0e9;
                for (float knob = 0.0f; knob <= 10.001f; knob += 2.0f)
                {
                    const auto m = run (knob, 0.0f, (PowerAmp::Tube) t, c);
                    lo = juce::jmin (lo, m.levelDb);
                    hi = juce::jmax (hi, m.levelDb);
                }

                if (hi - lo > worst)
                {
                    worst = hi - lo;
                    worstName = juce::String (c) + "x tube " + juce::String (t);
                }
            }

        report ("level holds on EVERY tube and count", worst < 2.0,
                "worst spread " + juce::String (worst, 2) + " dB (" + worstName + ")");
    }

    // A switch that changes the reported latency makes hosts re-align mid-song, so the bypass path
    // has to cost exactly what the stage costs.
    {
        PowerAmp amp;
        amp.prepare (sampleRate, blockSize, 2);
        const int latency = amp.latencySamples();

        constexpr int n = 2048;
        std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
        left[0] = right[0] = 1.0f;

        for (int off = 0; off < n; off += blockSize)
        {
            float* io[2] = { left.data() + off, right.data() + off };
            amp.process (io, 2, juce::jmin (blockSize, n - off), false);
        }

        int peakAt = -1;
        float best = 0.0f;
        for (int i = 0; i < n; ++i)
            if (std::abs (left[(size_t) i]) > best) { best = std::abs (left[(size_t) i]); peakAt = i; }

        report ("bypass delays by exactly the stage's latency", peakAt == latency,
                "impulse at " + juce::String (peakAt) + ", latency " + juce::String (latency));
        report ("bypass passes the signal through untouched", std::abs (best - 1.0f) < 1.0e-4f,
                juce::String (best, 4));
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
