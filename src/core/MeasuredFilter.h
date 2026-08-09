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
    /** The band a measured curve is worth applying in — read from the pack when it says, and assumed
        when it does not.

        A pack that measured its trust states it, and then the producer decides. SM7 does not: all
        three of its controls carry no trusted block, so the curve was applied edge to edge — and at
        the very bottom of the grid a measurement has almost no energy to work with. SM7's two EQ
        curves both turn UPWARDS at 20 Hz, by four and seven decibels, while at 50 Hz they both go
        down. That is not the pedal, that is the noise floor of the measurement, and it was being
        drawn as a spike and played as an infrasonic bass boost.

        40 Hz is below the lowest note a guitar makes; 16 kHz is above anything a pedal shapes. Held
        rather than rolled off, so nothing is invented past the edge — the curve simply stops having
        an opinion where it never had evidence.

        This is a POLICY, and policy is why it lives here rather than in the library that does the
        maths: felitronics::lineareq takes two numbers and asks no questions about them. */
    struct Band { double lo, hi; };

    static Band bandFor (const namz::rig::Measured& m)
    {
        // Two levels or more means the pack actually tested it — including the case where it tested
        // and failed everywhere, which arrives as a collapsed band and applies nothing.
        if (m.trusted.levels >= 2)
            return { m.trusted.loHz, m.trusted.hiHz };

        return { assumedLoHz, assumedHiHz };
    }

    static constexpr double assumedLoHz = 40.0;
    static constexpr double assumedHiHz = 16000.0;

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
