#pragma once

#include <juce_dsp/juce_dsp.h>

namespace orbitamp::core
{

/** The cabinet as a convolution: one IR, chosen from the shelf, at the end of the chain.

    juce::dsp::Convolution does the heavy lifting AND the thread safety: loadImpulseResponse is
    documented safe to call while process runs — the background loader prepares the new IR and
    swaps it in without a click. Zero latency (uniform partitioning), which is what a monitoring
    chain wants. Normalised on load so switching cabinets compares voicings, not loudnesses. */
class CabinetIr
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels)
    {
        channels = juce::jmax (1, numChannels);
        conv.prepare ({ sampleRate, (juce::uint32) blockSize, (juce::uint32) channels });
    }

    /** Message thread. The data is copied by the loader — the caller's buffer may die. */
    void load (const void* data, size_t size)
    {
        conv.loadImpulseResponse (data, size,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes, 0,
                                  juce::dsp::Convolution::Normalise::no);
    }

    void process (float* const* io, int numChannels, int numSamples, bool on)
    {
        if (! on)
            return;

        juce::dsp::AudioBlock<float> block (const_cast<float**> (io),
                                            (size_t) juce::jmin (channels, numChannels),
                                            (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        conv.process (ctx);
    }

    void reset() { conv.reset(); }

private:
    juce::dsp::Convolution conv;
    int channels = 2;
};

} // namespace orbitamp::core
