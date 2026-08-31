#pragma once

#include <felitronics/eq/MatchedBiquad.h>
#include <felitronics/measurement/ReferenceUnity.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>

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
        int   hpfSlope     = 12;       // dB/oct: 6, 12, 18, 24 or 48 — the consoles' ladder
        bool  lpfOn        = false;
        float lpfHz        = 7000.0f;
        int   lpfSlope     = 12;
        bool  phase        = false;    // flipped

        bool operator== (const Post& o) const noexcept
        {
            return trimOn == o.trimOn && juce::approximatelyEqual (trimFraction, o.trimFraction)
                && hpfOn == o.hpfOn && juce::approximatelyEqual (hpfHz, o.hpfHz)
                && hpfSlope == o.hpfSlope
                && lpfOn == o.lpfOn && juce::approximatelyEqual (lpfHz, o.lpfHz)
                && lpfSlope == o.lpfSlope
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

        // The trimmed edge leaves on a half-cosine, a few milliseconds wide: an abrupt cut rings
        // as spectral ripple, and the trim is meant to remove room, not to add zipper.
        if (keep < full)
        {
            const int fade = juce::jmin (keep / 4, juce::roundToInt (0.004 * rawRate));
            for (int ch = 0; ch < ir.getNumChannels(); ++ch)
            {
                float* d = ir.getWritePointer (ch);
                for (int i = 0; i < fade; ++i)
                {
                    const float t = (float) (i + 1) / (float) fade;
                    d[keep - fade + i] *= 0.5f + 0.5f * std::cos (t * juce::MathConstants<float>::pi);
                }
            }
        }

        // The cuts, run over the impulse itself: matched-biquad Butterworth cascades — the same
        // ladder EqLink runs live (odd slopes lead with a first-order section), so the picture,
        // the consoles and the baked IR all mean the same steepness.
        const auto run = [&] (bool isHighpass, float hz, int slopeDb)
        {
            namespace m = felitronics::eq::matched;
            const double f = std::clamp ((double) hz, 10.0, 0.49 * rawRate);

            felitronics::eq::BiquadCoeffs sections[4];
            int count = 0;

            const auto fo = [&] { return isHighpass ? m::highpass1 (f, rawRate) : m::lowpass1 (f, rawRate); };
            const auto bq = [&] (double q) { return isHighpass ? m::highpass (f, rawRate, q) : m::lowpass (f, rawRate, q); };

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

            for (int s = 0; s < count; ++s)
                for (int ch = 0; ch < ir.getNumChannels(); ++ch)
                {
                    felitronics::eq::Biquad biquad;
                    biquad.setCoeffs (sections[s]);
                    float* d = ir.getWritePointer (ch);
                    for (int i = 0; i < keep; ++i)
                        d[i] = biquad.processSample (d[i]);
                }
        };

        if (post.hpfOn) run (true,  post.hpfHz, post.hpfSlope);
        if (post.lpfOn) run (false, post.lpfHz, post.lpfSlope);

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
