#pragma once

#include <felitronics/eq/MatchedBiquad.h>

#include <algorithm>
#include <cmath>

namespace orbitamp::core
{

/** The tone stack: fixed-corner Low shelf / Mid bell / High shelf, plus an optional 12 dB/oct HPF
    and LPF pair. Measured DSP, not a capture — this is the linear half of the design's honesty
    claim, and it is rebuilt rather than profiled because a linear response has nothing to profile.

    JUCE-free on purpose: it takes raw channel pointers, allocates nothing after prepare(), and is
    a candidate to MOVE to felitronics-core once the voicing settles. OrbitCab has the same shape in
    its own cab::AmpEq (same corners, one stage further down the chain); when either stops moving,
    one of the two becomes the shared one and the other consumes it.

    Real-time rule: process() and setSettings() never allocate, lock, or throw. */
class ToneStack
{
public:
    // The voicing corners. Fixed and generic for now, and public so the curve view draws exactly what
    // the filters do. A later step can fit these per-device from a measured tone-stack sweep without
    // anything else in the chain noticing.
    static constexpr double lowHz  = 100.0;    // low shelf
    static constexpr double midHz  = 600.0;    // bell
    static constexpr double midQ   = 0.8;
    static constexpr double highHz = 2800.0;   // high shelf
    // Presence is a SECOND high shelf, deliberately far above the first. Put the two at the same
    // corner and you have one control in two copies — which is what the measured hardware turned out
    // to have (1655 Hz and 1132 Hz, half an octave apart). The main shelf colours the whole top from
    // 2.8k; presence works the 5k band where a guitar cuts through a mix.
    static constexpr double presenceHz = 5000.0;
    static constexpr double cutQ   = 0.7071067811865476;   // Butterworth, 12 dB/oct

    static constexpr int maxChannels = 2;

    struct Settings
    {
        double lowDb      = 0.0;
        double midDb      = 0.0;
        double highDb     = 0.0;
        double presenceDb = 0.0;
        bool   hpfOn  = false;
        double hpfHz  = 80.0;
        bool   lpfOn  = false;
        double lpfHz  = 10000.0;

        // Exact comparison is the point: this asks "did anything move", not "are these near enough".
        bool operator== (const Settings&) const = default;
    };

    void prepare (double sampleRate, int numChannels) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        ch = std::clamp (numChannels, 1, maxChannels);
        design (current, true);
        reset();
    }

    void reset() noexcept
    {
        for (auto& band : state)
            for (auto& biquad : band)
                biquad.reset();
    }

    void setSettings (const Settings& s) noexcept
    {
        if (! (s == current))
            design (s, false);
    }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int n = std::min (numChannels, ch);

        for (int b = 0; b < numBands; ++b)
        {
            if (! active[b])
                continue;

            for (int c = 0; c < n; ++c)
            {
                auto& biquad = state[b][(size_t) c];
                auto* data   = channels[c];

                for (int i = 0; i < numSamples; ++i)
                    data[i] = biquad.processSample (data[i]);

                biquad.flushDenormals();
            }
        }
    }

    /** The stack's response in dB at a frequency — what the curve view draws. Reads the same
        coefficients process() runs, so the drawing can never drift from the sound. */
    double magnitudeDb (double freqHz) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * std::clamp (freqHz, 1.0, 0.499 * fs) / fs;

        double db = 0.0;
        for (int b = 0; b < numBands; ++b)
            if (active[b])
                db += coeffs[b].magnitudeDb (w);

        return db;
    }

    const Settings& getSettings() const noexcept { return current; }

private:
    // HPF, low shelf, mid bell, high shelf, presence, LPF — in chain order.
    static constexpr int numBands = 6;

    void design (const Settings& s, bool force) noexcept
    {
        namespace m = felitronics::eq::matched;

        const double nyquist = 0.49 * fs;
        const auto   clampHz = [nyquist] (double f) { return std::clamp (f, 10.0, nyquist); };

        // A tone band at exactly 0 dB is a bypass — skipping it costs nothing and keeps a flat
        // stack bit-transparent rather than "flat within rounding".
        set (0, s.hpfOn,             m::highpass    (clampHz (s.hpfHz), fs, cutQ));
        set (1, s.lowDb      != 0.0, m::lowShelfDb  (lowHz,  fs, s.lowDb));
        set (2, s.midDb      != 0.0, m::peakingDb   (midHz,  fs, midQ, s.midDb));
        set (3, s.highDb     != 0.0, m::highShelfDb (highHz, fs, s.highDb));
        set (4, s.presenceDb != 0.0, m::highShelfDb (presenceHz, fs, s.presenceDb));
        set (5, s.lpfOn,             m::lowpass     (clampHz (s.lpfHz), fs, cutQ));

        current = s;

        if (force)
            reset();
    }

    void set (int band, bool isActive, const felitronics::eq::BiquadCoeffs& c) noexcept
    {
        active[band] = isActive;
        coeffs[band] = c;

        for (auto& biquad : state[band])
            biquad.setCoeffs (c);
    }

    bool                          active[numBands] {};
    felitronics::eq::BiquadCoeffs coeffs[numBands] {};
    felitronics::eq::Biquad       state[numBands][maxChannels] {};

    Settings current;
    double   fs = 48000.0;
    int      ch = 2;
};

} // namespace orbitamp::core
