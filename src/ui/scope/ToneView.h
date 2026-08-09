#pragma once

#include "../../core/MeasuredFilter.h"
#include "../Theme.h"
#include "ScopeFrame.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <functional>

namespace orbitamp::scope
{

/** The measured tone controls, summed, at wherever their knobs sit — over a spectrum of what is
    actually coming out.

    The curve is a claim and the spectrum is the sound keeping it: with a phrase playing, the shape
    the knobs promise should be visible in what the spectrum does. Two different kinds of statement
    on one picture, so they are drawn as different kinds of thing — the curve exact and on top, the
    spectrum dim and filled behind. They must not compete for the same reading.

    The axis IS the band the curve is believed in. Drawn from 20 Hz to 20 kHz, most of the width went
    to the shelves where the curve is held flat — a long dead line at each end, and the bass one read
    as the pedal doing something. It is not doing anything out there; it is not KNOWN out there, and
    the honest way to show that is not to show it. */
class ToneView
{
public:
    void paint (juce::Graphics& g, juce::Rectangle<float> r,
                const std::function<double (double)>& toneDb, const Frame& f)
    {
        if (toneDb == nullptr)
            return;

        const double lo = core::MeasuredFilter::bandLoHz;
        const double hi = core::MeasuredFilter::bandHiHz;

        paintSpectrum (g, r, f, lo, hi);

        constexpr float range = 20.0f;

        juce::Path p;
        const int w = juce::jmax (2, (int) r.getWidth());

        for (int x = 0; x < w; ++x)
        {
            const double t  = (double) x / (w - 1);
            const double hz = lo * std::pow (hi / lo, t);
            const float  y  = r.getCentreY() - juce::jlimit (-range, range, (float) toneDb (hz))
                                                 / range * r.getHeight() * 0.5f;

            if (x == 0) p.startNewSubPath (r.getX() + (float) x, y);
            else        p.lineTo (r.getX() + (float) x, y);
        }

        g.setColour (theme::hair);
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        g.setColour (theme::orange);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    /** The sample rate the tap was filled at. Without it every partial sits at the wrong frequency —
        at 96 kHz the whole spectrum would be drawn an octave low. */
    void setSampleRate (double rate) noexcept { sampleRate = rate > 0.0 ? rate : 48000.0; }

private:
    /** What is coming out of the pedal, averaged and dim.

        Averaged because a single frame of a chord is a comb and unreadable; fast up and slow down, so
        an attack registers and then decays rather than flickering. */
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r, const Frame& f,
                        double lo, double hi)
    {
        if (f.isEmpty() || f.size < fftSize)
            return;

        // Hann first. It costs a little resolution and saves the picture from the skirts a
        // rectangular window grows around every partial.
        std::array<float, fftSize * 2> work {};
        const int from = f.size - fftSize;

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                     * (float) i / (float) (fftSize - 1));
            work[(size_t) i] = f.wet[from + i] * w;
        }

        fft.performFrequencyOnlyForwardTransform (work.data());

        for (int b = 0; b < bins; ++b)
        {
            const float db = juce::Decibels::gainToDecibels (work[(size_t) b] * 2.0f / (float) fftSize,
                                                             floorDb);
            auto& s = spectrum[(size_t) b];
            s = db > s ? db : s * 0.85f + db * 0.15f;
        }

        const int w = juce::jmax (2, (int) r.getWidth());
        const double perBin = sampleRate / (double) fftSize;

        juce::Path p;
        p.startNewSubPath (r.getX(), r.getBottom());

        for (int x = 0; x < w; ++x)
        {
            // Every bin the pixel spans, at its loudest. Below a few hundred hertz a pixel is
            // narrower than a bin and the same one is read twice; up top it covers dozens, and taking
            // one of them would make a partial flicker as it drifts between pixels.
            const double a = lo * std::pow (hi / lo, (double) x / (w - 1));
            const double b = lo * std::pow (hi / lo, (double) (x + 1) / (w - 1));

            const int first = juce::jlimit (1, bins - 1, (int) std::floor (a / perBin));
            const int last  = juce::jlimit (first, bins - 1, (int) std::ceil (b / perBin));

            float db = floorDb;
            for (int i = first; i <= last; ++i)
                db = juce::jmax (db, spectrum[(size_t) i]);

            const float y = r.getBottom() - (db - floorDb) / -floorDb * r.getHeight();
            p.lineTo (r.getX() + (float) x, juce::jlimit (r.getY(), r.getBottom(), y));
        }

        p.lineTo (r.getRight(), r.getBottom());
        p.closeSubPath();

        g.setColour (theme::violet.withAlpha (0.14f));
        g.fillPath (p);
        g.setColour (theme::violet.withAlpha (0.4f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    static constexpr int   fftOrder = 11;            // 2048 — the tap's whole window
    static constexpr int   fftSize  = 1 << fftOrder;
    static constexpr int   bins     = fftSize / 2;
    static constexpr float floorDb  = -78.0f;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, bins> spectrum { };
    double sampleRate = 48000.0;
};

} // namespace orbitamp::scope
