#pragma once

#include <felitronics/eq/Svf.h>
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

    WHAT A PLAYER DOES TO THE IR is baked into the IR, not run on the audio: a second-order
    high-pass and low-pass over the impulse itself, a trim of its tail, a flip of its sign — the
    raw shot is kept here and rebuilt whenever one of them moves, then handed to the convolver as
    a new IR. The convolution costs exactly what it cost before, whatever is switched on.

    Level: the family reference-unity contract (felitronics::measurement::referenceUnityGain),
    the same one OrbitCab ships — the IR passes the guitar-band reference at unity RMS, so a cab
    contributes TONE, not gain, and swapping cabinets compares voicings at matched loudness. Taken
    after the post, so a cut IR is as loud as the whole one was. JUCE's Normalise::yes is NOT
    usable here: it scales the IR to 0.125 total energy (−18 dB full-band), which lands a
    band-limited guitar signal ~12 dB low and drifts with the session rate. Never load a cab IR
    raw either — 61k taps sum to enormous gain; the reference gain is clamped ±30 dB. */
class CabinetIr
{
public:
    struct Post
    {
        bool  trimOn       = false;
        float trimFraction = 1.0f;     // of the IR's length, kept
        bool  hpfOn        = false;
        float hpfHz        = 80.0f;
        bool  lpfOn        = false;
        float lpfHz        = 7000.0f;
        bool  phase        = false;    // flipped

        bool operator== (const Post& o) const noexcept
        {
            return trimOn == o.trimOn && juce::approximatelyEqual (trimFraction, o.trimFraction)
                && hpfOn == o.hpfOn && juce::approximatelyEqual (hpfHz, o.hpfHz)
                && lpfOn == o.lpfOn && juce::approximatelyEqual (lpfHz, o.lpfHz)
                && phase == o.phase;
        }
    };

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

        raw.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&raw, 0, raw.getNumSamples(), 0, true, true);
        rawRate = reader->sampleRate;
        rebuild();
    }

    /** Message thread. Rebuilds only when something actually moved. */
    void setPost (const Post& p)
    {
        if (p == post)
            return;

        post = p;
        if (raw.getNumSamples() > 0)
            rebuild();
    }

    const Post& currentPost() const noexcept { return post; }

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
    void rebuild()
    {
        const int full = raw.getNumSamples();
        const int keep = post.trimOn ? juce::jlimit (juce::jmin (full, 64), full,
                                                     juce::roundToInt ((float) full * post.trimFraction))
                                     : full;

        juce::AudioBuffer<float> ir (raw.getNumChannels(), keep);
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
            ir.copyFrom (ch, 0, raw, ch, 0, keep);

        // The cuts, run over the impulse itself: a TPT state-variable filter at Butterworth Q, the
        // same second-order shape the picture draws.
        const auto run = [&] (felitronics::eq::FilterType type, float hz)
        {
            felitronics::eq::Svf f;
            f.prepare (rawRate, ir.getNumChannels());
            f.setParams (type, (double) hz, 0.7071, 0.0);
            f.reset();
            for (int ch = 0; ch < ir.getNumChannels(); ++ch)
            {
                float* d = ir.getWritePointer (ch);
                for (int i = 0; i < keep; ++i)
                    d[i] = f.processSample (ch, d[i]);
            }
        };

        if (post.hpfOn) run (felitronics::eq::FilterType::HighPass, post.hpfHz);
        if (post.lpfOn) run (felitronics::eq::FilterType::LowPass,  post.lpfHz);

        if (post.phase)
            ir.applyGain (-1.0f);

        ir.applyGain (felitronics::measurement::referenceUnityGain (
            ir.getArrayOfReadPointers(), ir.getNumChannels(), ir.getNumSamples(), rawRate));

        conv.loadImpulseResponse (std::move (ir), rawRate,
                                  juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes,
                                  juce::dsp::Convolution::Normalise::no);
    }

    juce::dsp::Convolution conv;
    int channels = 2;

    juce::AudioBuffer<float> raw;    // the shot, as it came — the post is baked into a copy
    double rawRate = 48000.0;
    Post post;
};

} // namespace orbitamp::core
