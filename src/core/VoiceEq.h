#pragma once

#include <felitronics/eq/MatchedBiquad.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace orbitamp::core
{

/** The EQ a captured block gets that its device never had — and the reason it can sit BEFORE the
    capture as well as after it.

    A captured voice is material, not a product to be reproduced, so what stands around it is where
    the instrument actually gets made. Post is colour: the voice comes out and gets shaped. PRE is
    something the hardware mostly could not do at all, because there was nowhere to put it — it
    changes what arrives at the nonlinearity, so it changes the KIND of distortion rather than the
    tint of it. Roll the bass off in front of a fuzz and the mush becomes a growl, without touching a
    thing behind it. One captured voice, two instruments.

    Three controls, chosen so they earn their place at both positions: a low cut, a high cut, and a
    tilt. The cuts are what makes PRE worth having; the tilt is what makes POST worth having. Nothing
    here is fitted to any device — it is ours, it is the same in every block, and it is meant to be
    learned once.

    JUCE-free, allocation-free after prepare, and a candidate to MOVE to felitronics-core once it
    stops changing shape. */
class VoiceEq
{
public:
    static constexpr int maxChannels = 2;

    /** Where the filters sit relative to the capture. */
    enum class Placement { pre, post };

    struct Settings
    {
        bool   enabled = false;
        Placement placement = Placement::post;

        float hpfHz = 20.0f;      // at the ends of their ranges these do nothing and are skipped
        float lpfHz = 20000.0f;

        // Positive tilts bright, negative tilts dark: one control, two shelves moving in opposite
        // directions. A tilt is the honest single knob for "warmer / brighter", where a single shelf
        // is only half of the gesture and a mid bell is a different question entirely.
        float tiltDb = 0.0f;

        bool operator== (const Settings&) const = default;
    };

    static constexpr double hingeHz = 900.0;    // the tilt's pivot: the two shelves cross here
    static constexpr double shelfQ  = 0.7071067811865476;
    static constexpr double cutQ    = 0.7071067811865476;   // Butterworth, 12 dB/oct

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = std::clamp (numChannels, 1, maxChannels);
        design (current, true);
    }

    void reset() noexcept
    {
        for (auto& ch : state)
            for (auto& b : ch)
                b.reset();
    }

    void setSettings (const Settings& s) noexcept
    {
        if (! (s == current))
            design (s, false);
    }

    const Settings& getSettings() const noexcept { return current; }

    bool isActive() const noexcept
    {
        if (! current.enabled)
            return false;

        for (int b = 0; b < numBands; ++b)
            if (active[b])
                return true;

        return false;
    }

    bool isPre() const noexcept { return current.placement == Placement::pre; }

    /** The response in dB at a frequency, off the same coefficients process() runs — so the drawing
        can never drift from the sound. */
    double magnitudeDb (double freqHz) const noexcept
    {
        if (! current.enabled)
            return 0.0;

        const double w = 2.0 * 3.14159265358979323846 * std::clamp (freqHz, 1.0, 0.499 * fs) / fs;

        double db = 0.0;
        for (int b = 0; b < numBands; ++b)
            if (active[b])
                db += coeffs[b].magnitudeDb (w);

        return db;
    }

    void process (float* const* data, int numChannels, int numSamples) noexcept
    {
        if (! isActive())
            return;

        const int n = std::min (numChannels, channels);

        for (int ch = 0; ch < n; ++ch)
            for (int b = 0; b < numBands; ++b)
            {
                if (! active[b])
                    continue;

                auto& biquad = state[(size_t) ch][(size_t) b];
                float* x = data[ch];

                for (int i = 0; i < numSamples; ++i)
                    x[i] = biquad.processSample (x[i]);
            }
    }

private:
    // HPF, low shelf, high shelf, LPF — in chain order. The two shelves are the tilt.
    static constexpr int numBands = 4;

    void design (const Settings& s, bool force) noexcept
    {
        namespace m = felitronics::eq::matched;

        const double nyquist = 0.49 * fs;
        const auto   clampHz = [nyquist] (double f) { return std::clamp (f, 10.0, nyquist); };

        // Half the tilt each way, so the ends move apart by the full amount and the hinge stays put.
        const double half = s.tiltDb * 0.5;

        // A band parked where it does nothing is skipped rather than run flat — bit-transparency
        // where a control is not being used costs nothing to arrange.
        set (0, s.enabled && s.hpfHz > 21.0f,          m::highpass     (clampHz (s.hpfHz), fs, cutQ));
        set (1, s.enabled && std::abs (half) > 0.005,  m::lowShelfDb   (hingeHz, fs, -half));
        set (2, s.enabled && std::abs (half) > 0.005,  m::highShelfDb  (hingeHz, fs,  half));
        set (3, s.enabled && s.lpfHz < 19500.0f,       m::lowpass      (clampHz (s.lpfHz), fs, cutQ));

        current = s;

        if (force)
            reset();
    }

    void set (int band, bool isActive, const felitronics::eq::BiquadCoeffs& c) noexcept
    {
        active[band] = isActive;
        coeffs[band] = c;

        for (auto& ch : state)
            ch[(size_t) band].setCoeffs (c);
    }

    felitronics::eq::BiquadCoeffs coeffs[numBands] {};
    bool active[numBands] {};
    std::array<std::array<felitronics::eq::Biquad, numBands>, maxChannels> state {};

    Settings current;
    double fs = 48000.0;
    int channels = 2;
};

} // namespace orbitamp::core
