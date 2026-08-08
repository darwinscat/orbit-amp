#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace orbitamp::core
{

/** A few seconds of what went in and what came out, as a waveform you can see a note in.

    ScopeTap holds forty milliseconds — enough to draw one cycle, and far too little to show an
    attack decaying into a sustain, which is the thing a compressor actually does to a note. This
    keeps SECONDS by throwing away everything except the extremes: one bucket per column of pixels,
    holding the highest and lowest sample that fell in it. That is what a waveform display is, and it
    is why one costs almost nothing to keep.

    The audio thread only ever compares and stores. No locks: a reader that catches a bucket
    mid-update draws one column from half of the newest window, which is a pixel, and a lock on the
    audio thread is a dropout.

    Level-matched on the way out, like ScopeTap and for the same reason: a boost that is merely
    louder would swamp the picture, and what the picture is for is what the boost DID. */
class WaveRibbon
{
public:
    /** Deliberately more columns than any window will have pixels.

        The ribbon is sampled per pixel, so too FEW columns is what shows: neighbouring pixels land on
        the same column and the waveform turns into a picket fence — which is what a zoomed window did
        at 640. The faceplate is 880 units wide and the boost block is a third of it, so even at 4x
        this is three times what the widest picture can ask for. It costs 64 kB. */
    static constexpr int buckets = 4096;
    static constexpr double seconds = 3.0;

    struct Column { float dryLo, dryHi, wetLo, wetHi; };

    void prepare (double sampleRate)
    {
        perBucket = juce::jmax (1, (int) (sampleRate * seconds / (double) buckets));
        reset();
    }

    void reset()
    {
        for (auto& c : dryLo) c = 0.0f;
        for (auto& c : dryHi) c = 0.0f;
        for (auto& c : wetLo) c = 0.0f;
        for (auto& c : wetHi) c = 0.0f;

        writePos = 0;
        filled = 0;
        written.store (0, std::memory_order_release);
    }

    void write (const float* dry, const float* wet, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto w = (size_t) writePos;

            if (filled == 0)   // a fresh bucket starts AT the sample, not at zero, or every column
            {                  // would be pinned open around silence
                dryLo[w] = dryHi[w] = dry[i];
                wetLo[w] = wetHi[w] = wet[i];
            }
            else
            {
                dryLo[w] = juce::jmin (dryLo[w], dry[i]);
                dryHi[w] = juce::jmax (dryHi[w], dry[i]);
                wetLo[w] = juce::jmin (wetLo[w], wet[i]);
                wetHi[w] = juce::jmax (wetHi[w], wet[i]);
            }

            if (++filled >= perBucket)
            {
                filled = 0;
                writePos = (writePos + 1) % buckets;
                written.fetch_add (1, std::memory_order_release);
            }
        }
    }

    /** Copies the ribbon out oldest-first, with the wet side scaled to the dry side's level. False
        when nothing has been written yet. */
    bool read (std::array<Column, buckets>& out) const noexcept
    {
        if (written.load (std::memory_order_acquire) == 0)
            return false;

        // Skip the bucket being written. It holds a fraction of a column's worth of the newest audio,
        // and it sits at the OLDEST end of the ribbon — so a live signal was being drawn as a stripe
        // at the far left, three seconds away from where it happened.
        const int start = (writePos + 1) % buckets;

        double dryEnergy = 0.0, wetEnergy = 0.0;

        for (int i = 0; i < buckets - 1; ++i)
        {
            const auto s = (size_t) ((start + i) % buckets);
            out[(size_t) i] = { dryLo[s], dryHi[s], wetLo[s], wetHi[s] };

            dryEnergy += (double) dryHi[s] * dryHi[s] + (double) dryLo[s] * dryLo[s];
            wetEnergy += (double) wetHi[s] * wetHi[s] + (double) wetLo[s] * wetLo[s];
        }

        out[(size_t) (buckets - 1)] = out[(size_t) (buckets - 2)];   // keep the array's length

        if (wetEnergy > 1.0e-12 && dryEnergy > 1.0e-12)
        {
            const float k = (float) std::sqrt (dryEnergy / wetEnergy);

            for (auto& c : out)
            {
                c.wetLo *= k;
                c.wetHi *= k;
            }
        }

        return true;
    }

private:
    std::array<float, buckets> dryLo {}, dryHi {}, wetLo {}, wetHi {};

    int perBucket = 240;
    int writePos = 0;
    int filled = 0;
    std::atomic<unsigned> written { 0 };
};

} // namespace orbitamp::core
