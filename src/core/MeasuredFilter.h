#pragma once

#include <felitronics/lineareq/MagnitudeCurve.h>
#include <juce_dsp/juce_dsp.h>

#include <namz_rig.h>

#include <vector>

namespace orbitamp::core
{

/** A pedal's measured control, playing.

    The pack ships a magnitude table per captured position; core turns wherever the knob sits into a
    minimum-phase FIR and this convolves with it. Everything hard — reading between the measured
    positions against the producer's stated norm, holding the curve outside the band it was shown to
    reproduce in, the cepstral design — is `felitronics::lineareq`, and none of it is repeated here.

    What IS here is the pack's opinion, which core refuses to hold: what namz's `trusted` block means.
    A band that collapsed was tested and failed, and goes to core as an empty band, which zeroes the
    curve. A band with nothing behind it was never tested at all, and gets the assumed one — see
    bandFor, which is the only place either decision is made.

    Designing is message-thread work. The audio thread only ever convolves with a kernel already
    built. */
class MeasuredFilter
{
public:
    /** The band a measured curve is applied in — a ceiling over whatever the pack claims, and the
        whole band when it claims nothing.

        Currently the near-full range: an .orbitrig grid runs 20 Hz to 20 kHz, so this trims only the
        very top and leaves the bottom to the pack. Where exactly to put it is UNSETTLED — narrow
        enough and a measurement's noisiest corners stop being played (SM7 has no trusted block at
        all, and both its EQ curves turn upwards at 20 Hz by four and seven decibels, which is a noise
        floor rather than a pedal); wide enough and nothing measured is thrown away. Two numbers, and
        they are meant to be moved.

        Held rather than rolled off: nothing is invented past the edge, the curve simply stops
        claiming. A collapsed band survives intact — "tested and failed everywhere" must not be
        widened into a band by a clamp.

        POLICY, which is why it is here and not in the library doing the maths: felitronics::lineareq
        takes two numbers and asks no questions, which is right for a library and wrong for a decision
        about what a measurement is worth. */
    struct Band { double lo, hi; };

    static Band bandFor (const namz::rig::Measured& m)
    {
        // Two levels or more means the pack tested it. A collapsed band is that test coming back
        // negative everywhere — pass it through untouched so core zeroes the curve.
        if (m.trusted.levels >= 2)
        {
            if (m.trusted.hiHz <= m.trusted.loHz)
                return { m.trusted.loHz, m.trusted.hiHz };

            return { juce::jmax (bandLoHz, m.trusted.loHz),
                     juce::jmin (bandHiHz, m.trusted.hiHz) };
        }

        return { bandLoHz, bandHiHz };
    }

    static constexpr double bandLoHz = 10.0;
    static constexpr double bandHiHz = 19000.0;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        rate = sampleRate;

        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlock),
                                      (juce::uint32) juce::jmax (1, numChannels) };
        convolver.prepare (spec);
        convolver.reset();
        active = false;
    }

    void reset() { convolver.reset(); }

    /** Rebuild for a knob position, 0..1 across the sweep. Message thread. */
    void setPosition (const namz::rig::Measured& m, double norm)
    {
        const auto freq = felitronics::lineareq::logFreqGrid (m.grid.fLo, m.grid.fHi, m.grid.points);

        std::vector<std::vector<double>> curves;
        std::vector<double> norms;
        curves.reserve (m.positions.size());
        norms.reserve (m.positions.size());

        for (const auto& p : m.positions)
        {
            curves.push_back (p.db);
            norms.push_back (p.norm);
        }

        const auto db = felitronics::lineareq::curveAtPosition (curves, norms, norm);

        const auto band = bandFor (m);
        const auto fir = felitronics::lineareq::magnitudeCurveToFir (db, freq, rate, firTaps,
                                                                     designSize, band.lo, band.hi);
        if (fir.empty())            // flat, untrustworthy, or nothing to apply
        {
            active = false;
            return;
        }

        juce::AudioBuffer<float> kernel (1, (int) fir.size());
        juce::FloatVectorOperations::copy (kernel.getWritePointer (0), fir.data(), (int) fir.size());

        convolver.loadImpulseResponse (std::move (kernel), rate,
                                       juce::dsp::Convolution::Stereo::yes,
                                       juce::dsp::Convolution::Trim::no,
                                       juce::dsp::Convolution::Normalise::no);
        active = true;
    }

    void clear() { active = false; }
    bool isActive() const noexcept { return active; }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! active)
            return;

        juce::dsp::AudioBlock<float> block (buffer);
        convolver.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    static constexpr int firTaps    = 512;
    static constexpr int designSize = 8 * firTaps;   // the cepstral design aliases in time below this

    juce::dsp::Convolution convolver;
    double rate = 48000.0;
    bool active = false;
};

} // namespace orbitamp::core
