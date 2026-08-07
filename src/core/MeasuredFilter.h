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
    A band with no levels behind it was never tested, so the curve applies everywhere; a band that
    collapsed was tested and failed, and gets handed to core as an empty band, which zeroes it.

    Designing is message-thread work. The audio thread only ever convolves with a kernel already
    built. */
class MeasuredFilter
{
public:
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

        // Never measured is not the same as measured and failed. A pack that says nothing about trust
        // gets its curve applied across the band; one that says the curve reproduced nowhere gets an
        // empty band, which core reads as "apply nothing".
        const bool tested = m.trusted.levels >= 2;
        const double loHz = tested ? m.trusted.loHz : 0.0;
        const double hiHz = tested ? m.trusted.hiHz : 0.0;

        const auto fir = felitronics::lineareq::magnitudeCurveToFir (db, freq, rate, firTaps,
                                                                     designSize, loHz, hiHz);
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
