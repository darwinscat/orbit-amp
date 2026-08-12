#pragma once

#include <felitronics/measurement/ReferenceUnity.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace orbitamp::core
{

/** The cabinet as a convolution: one IR, chosen from the shelf, at the end of the chain.

    juce::dsp::Convolution does the heavy lifting AND the thread safety: loadImpulseResponse is
    documented safe to call while process runs — the background loader prepares the new IR and
    swaps it in without a click. Zero latency (uniform partitioning), which is what a monitoring
    chain wants.

    Level: the family reference-unity contract (felitronics::measurement::referenceUnityGain),
    the same one OrbitCab ships — the IR passes the guitar-band reference at unity RMS, so a cab
    contributes TONE, not gain, and swapping cabinets compares voicings at matched loudness.
    JUCE's Normalise::yes is NOT usable here: it scales the IR to 0.125 total energy (−18 dB
    full-band), which lands a band-limited guitar signal ~12 dB low and drifts with the session
    rate. Never load a cab IR raw either — 61k taps sum to enormous gain (solid crackle); the
    reference gain is clamped ±30 dB, so a pathological IR can't blast. */
class CabinetIr
{
public:
    void prepare (double sampleRate, int blockSize, int numChannels)
    {
        channels = juce::jmax (1, numChannels);
        conv.prepare ({ sampleRate, (juce::uint32) blockSize, (juce::uint32) channels });
    }

    /** Message thread. The data is copied here — the caller's buffer may die. */
    void load (const void* data, size_t size)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wav.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return;

        juce::AudioBuffer<float> ir ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&ir, 0, ir.getNumSamples(), 0, true, true);

        ir.applyGain (felitronics::measurement::referenceUnityGain (
            ir.getArrayOfReadPointers(), ir.getNumChannels(), ir.getNumSamples(),
            reader->sampleRate));

        conv.loadImpulseResponse (std::move (ir), reader->sampleRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
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
