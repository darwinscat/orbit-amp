#pragma once

#include "../../core/MeasuredFilter.h"
#include "../Theme.h"
#include "ScopeFrame.h"

#include <felitronics/analysis/PlotMap.h>
#include <felitronics/analysis/SpectrumPane.h>
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
                const std::function<double (double)>& toneDb, const Frame& f,
                felitronics::analysis::SpectrumPane* pane = nullptr, double paneRate = 48000.0)
    {
        if (toneDb == nullptr)
            return;

        const double lo = core::MeasuredFilter::bandLoHz;
        const double hi = core::MeasuredFilter::bandHiHz;

        drawGrid (g, r, lo, hi);

        // The EQ's own spectrum when a tap is wired — liquid columns with their peak hold —
        // mapped onto THIS view's trusted band; the homemade fog only when it is not.
        if (pane != nullptr)
        {
            felitronics::analysis::PlotMap pm;
            pm.width      = r.getWidth();
            pm.height     = r.getHeight();
            pm.plotBottom = r.getHeight();
            pm.freqMin    = lo;
            pm.freqMax    = hi;
            pm.specTop    = 0.0;
            pm.specBottom = -90.0;

            juce::Path fill, peak;
            fill.startNewSubPath (r.getX(), r.getBottom());

            pane->buildColumns (pm, paneRate, 4.5, 1000.0,
                                [&] (int, float x, float yFill, float yPeak)
                                {
                                    fill.lineTo (r.getX() + x, r.getY() + yFill);
                                    if (peak.isEmpty()) peak.startNewSubPath (r.getX() + x, r.getY() + yPeak);
                                    else                peak.lineTo (r.getX() + x, r.getY() + yPeak);
                                });

            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();

            g.setColour (theme::violet.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (theme::violet.withAlpha (0.30f));
            g.strokePath (peak, juce::PathStrokeType (1.0f));
        }
        else
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

        g.setColour (theme::orange);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    /** The EQ's grid grammar on THIS view's trusted band: the 1-2-5 series carries the weight
        and the numbers, the steps between stay hair-thin; +-6/+-12 rules with figures, the zero
        line brighter — the reference the curve is judged against. */
    static void drawGrid (juce::Graphics& g, juce::Rectangle<float> r, double lo, double hi)
    {
        const auto xOf = [&] (double hz)
        {
            return r.getX() + (float) (std::log (hz / lo) / std::log (hi / lo)) * r.getWidth();
        };

        for (double decade : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int mult = 1; mult <= 9; ++mult)
            {
                const double hz = decade * mult;
                if (hz < lo || hz > hi)
                    continue;

                const bool major = mult == 1 || mult == 2 || mult == 5;
                g.setColour (major ? theme::hair2.withAlpha (0.20f) : theme::hair.withAlpha (0.07f));
                g.fillRect (xOf (hz), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);
            }

        constexpr float range = 20.0f;

        g.setColour (theme::hair);
        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            g.fillRect (r.getX() + 4.0f, r.getCentreY() - db / range * r.getHeight() * 0.5f,
                        r.getWidth() - 8.0f, 1.0f);

        g.setColour (theme::hair2);
        g.fillRect (r.getX() + 4.0f, r.getCentreY(), r.getWidth() - 8.0f, 1.0f);

        g.setColour (theme::txFaint);

        static const std::pair<double, const char*> hzLabels[] = {
            { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
            { 1000.0, "1K" }, { 2000.0, "2K" }, { 5000.0, "5K" }, { 10000.0, "10K" },
        };

        for (const auto& [hz, text] : hzLabels)
            if (hz >= lo && hz <= hi)
                theme::drawTracked (g, text,
                                    { xOf (hz) - 24.0f, r.getBottom() - 18.0f, 48.0f, 13.0f },
                                    theme::displayFont (12.0f), 0.08f, juce::Justification::centred);

        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            theme::drawTracked (g, (db > 0 ? "+" : "") + juce::String ((int) db),
                                { r.getX() + 6.0f,
                                  r.getCentreY() - db / range * r.getHeight() * 0.5f - 15.0f,
                                  40.0f, 13.0f },
                                theme::displayFont (12.0f), 0.08f, juce::Justification::centredLeft);
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
