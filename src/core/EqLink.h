#pragma once

#include <felitronics/eq/MatchedBiquad.h>

#include <algorithm>
#include <cmath>

namespace orbitamp::core
{

/** One EQ link of the chain — the console grammar, spoken in matched biquads.

    HPF and LPF with a real slope choice (6/12/18/24/48 dB/oct, Butterworth cascades), two
    shelves with free corners, two tone bells, a third NARROW bell that switches in — the
    surgical slot search→treat will land in — and an output LEVEL, ramped so it never zippers.
    No presence: with free shelf corners a second fixed high shelf is a duplicate. No linear
    phase either: it buys latency and pre-ring for a benefit a serial chain into a nonlinearity
    cannot hear — analog EQs are minimum-phase, and so is this.

    JUCE-free, allocation-free after prepare; magnitudeDb reads the same coefficients process
    runs, so the drawing can never drift from the sound. filterMagnitudeDb answers for the cut
    filters alone — the face draws them as their own walls. */
class EqLink
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        bool   hpfOn    = false;
        double hpfHz    = 80.0;
        int    hpfSlope = 12;       // dB/oct: 6, 12, 18, 24 or 48

        double loDb = 0.0, loHz = 100.0;

        double b1Db = 0.0, b1Hz = 400.0,  b1Q = 1.0;
        double b2Db = 0.0, b2Hz = 2500.0, b2Q = 1.0;

        bool   b3On = false;
        double b3Db = 0.0, b3Hz = 3000.0, b3Q = 8.0;

        double hiDb = 0.0, hiHz = 8000.0;

        bool   lpfOn    = false;
        double lpfHz    = 10000.0;
        int    lpfSlope = 12;

        double levelDb = 0.0;

        bool operator== (const Settings&) const = default;
    };

    void prepare (double sampleRate, int numChannels) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        ch = std::clamp (numChannels, 1, maxChannels);
        design (current, true);
        levelGain = std::pow (10.0, current.levelDb / 20.0);
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

    const Settings& getSettings() const noexcept { return current; }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        const int n = std::min (numChannels, ch);

        for (int b = 0; b < numSections; ++b)
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

        // The LEVEL: a fader, not a step — ramped over five milliseconds, however long the block.
        // Ramping across a whole block sounds identical at host sizes and lies at test sizes.
        const double target = std::pow (10.0, current.levelDb / 20.0);

        if (numSamples > 0 && std::abs (target - levelGain) > 1.0e-9)
        {
            const int    rampLen = std::min (numSamples, std::max (1, (int) (fs * 0.005)));
            const double step    = (target - levelGain) / (double) rampLen;

            for (int c = 0; c < n; ++c)
            {
                auto* data = channels[c];
                double gain = levelGain;

                for (int i = 0; i < rampLen; ++i)
                {
                    gain += step;
                    data[i] = (float) ((double) data[i] * gain);
                }

                for (int i = rampLen; i < numSamples; ++i)
                    data[i] = (float) ((double) data[i] * target);
            }

            levelGain = target;
        }
        else if (numSamples > 0 && std::abs (target - 1.0) > 1.0e-9)
        {
            for (int c = 0; c < n; ++c)
            {
                auto* data = channels[c];
                for (int i = 0; i < numSamples; ++i)
                    data[i] = (float) ((double) data[i] * target);
            }
        }
    }

    /** The full response in dB at a frequency — filters, shelves and bells, WITHOUT the level:
        the level lives on its own meter, and a curve that slid with a fader would say nothing. */
    double magnitudeDb (double freqHz) const noexcept
    {
        return sumDb (freqHz, 0, numSections);
    }

    /** The cut filters alone — what the face draws as the red walls. */
    double filterMagnitudeDb (double freqHz) const noexcept
    {
        return sumDb (freqHz, 0, filterSections);
    }

    /** The tone sections alone — shelves and bells, the bright curve's own voice. */
    double toneMagnitudeDb (double freqHz) const noexcept
    {
        return sumDb (freqHz, filterSections, numSections);
    }

private:
    // Sections: HPF (up to 4), LPF (up to 4), then LO / B1 / B2 / B3 / HI — cut filters first so
    // one index split serves both magnitude views.
    static constexpr int hpfSlots      = 4;
    static constexpr int lpfSlots      = 4;
    static constexpr int filterSections = hpfSlots + lpfSlots;
    static constexpr int numSections   = filterSections + 5;

    double sumDb (double freqHz, int from, int to) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * std::clamp (freqHz, 1.0, 0.499 * fs) / fs;

        double db = 0.0;
        for (int b = from; b < to; ++b)
            if (active[b])
                db += coeffs[b].magnitudeDb (w);

        return db;
    }

    /** Fills a run of slots with a Butterworth cascade of the asked slope. The Q ladder is the
        textbook one; the odd slopes lead with a first-order section. */
    void designCascade (int firstSlot, bool on, bool isHighpass, double hz, int slopeDb) noexcept
    {
        namespace m = felitronics::eq::matched;

        const double nyquist = 0.49 * fs;
        const double f = std::clamp (hz, 10.0, nyquist);

        felitronics::eq::BiquadCoeffs sections[4];
        int count = 0;

        const auto fo = [&] { return isHighpass ? m::highpass1 (f, fs) : m::lowpass1 (f, fs); };
        const auto bq = [&] (double q) { return isHighpass ? m::highpass (f, fs, q) : m::lowpass (f, fs, q); };

        switch (slopeDb)
        {
            case 6:  sections[count++] = fo(); break;
            default:
            case 12: sections[count++] = bq (0.70710678); break;
            case 18: sections[count++] = fo();
                     sections[count++] = bq (1.0); break;
            case 24: sections[count++] = bq (0.54119610);
                     sections[count++] = bq (1.30656296); break;
            case 48: sections[count++] = bq (0.50979558);
                     sections[count++] = bq (0.60134489);
                     sections[count++] = bq (0.89997622);
                     sections[count++] = bq (2.56291545); break;
        }

        for (int i = 0; i < 4; ++i)
            set (firstSlot + i, on && i < count,
                 i < count ? sections[i] : felitronics::eq::BiquadCoeffs {});
    }

    void design (const Settings& s, bool force) noexcept
    {
        namespace m = felitronics::eq::matched;

        const double nyquist = 0.49 * fs;
        const auto   clampHz = [nyquist] (double f) { return std::clamp (f, 10.0, nyquist); };

        designCascade (0,        s.hpfOn, true,  s.hpfHz, s.hpfSlope);
        designCascade (hpfSlots, s.lpfOn, false, s.lpfHz, s.lpfSlope);

        // A band at exactly 0 dB is a bypass — skipping it keeps a flat link bit-transparent.
        int b = filterSections;
        set (b++, s.loDb != 0.0,          m::lowShelfDb  (clampHz (s.loHz), fs, s.loDb));
        set (b++, s.b1Db != 0.0,          m::peakingDb   (clampHz (s.b1Hz), fs, s.b1Q, s.b1Db));
        set (b++, s.b2Db != 0.0,          m::peakingDb   (clampHz (s.b2Hz), fs, s.b2Q, s.b2Db));
        set (b++, s.b3On && s.b3Db != 0.0, m::peakingDb  (clampHz (s.b3Hz), fs, s.b3Q, s.b3Db));
        set (b++, s.hiDb != 0.0,          m::highShelfDb (clampHz (s.hiHz), fs, s.hiDb));

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

    bool                          active[numSections] {};
    felitronics::eq::BiquadCoeffs coeffs[numSections] {};
    felitronics::eq::Biquad       state[numSections][maxChannels] {};

    Settings current;
    double   fs = 48000.0;
    int      ch = 2;
    double   levelGain = 1.0;
};

} // namespace orbitamp::core
