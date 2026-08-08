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

    Level-matched ON THE WAY IN, which is the part that is easy to get wrong. Matching at read time
    means one factor for the whole window, recomputed every frame from whatever is playing now — so
    turning the gain up rescaled the entire history with it, and a peak recorded clipped an instant
    ago drifted off the left edge rounded. The picture showed the past rearranged by the present.

    So each column is matched as it is written, against a SLOW estimate of both levels, and read back
    untouched. Slow on purpose: an estimate that tracked quickly would flatten the difference between
    the two sides within a note, which is exactly what the picture exists to show. Seconds, not
    milliseconds — slower than a note, fast enough to follow a knob. */
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

        // A one-pole per sample would be a per-sample exp() budget for nothing; the estimate only has
        // to move over seconds, so it moves once per column.
        const double columnsPerSecond = sampleRate / (double) juce::jmax (1, perBucket);
        levelCoeff = (float) std::exp (-1.0 / (juce::jmax (1.0, columnsPerSecond) * matchSeconds));

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
        dryLevel = wetLevel = 0.0f;
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

            dryEnergy += (double) dry[i] * dry[i];
            wetEnergy += (double) wet[i] * wet[i];

            if (++filled >= perBucket)
            {
                closeColumn();
                writePos = (writePos + 1) % buckets;
                written.fetch_add (1, std::memory_order_release);
            }
        }
    }

    /** Copies the ribbon out oldest-first. Already matched — see write(). False when nothing has
        been written yet. */
    bool read (std::array<Column, buckets>& out) const noexcept
    {
        if (written.load (std::memory_order_acquire) == 0)
            return false;

        // Skip the bucket being written. It holds a fraction of a column's worth of the newest audio,
        // and it sits at the OLDEST end of the ribbon — so a live signal was being drawn as a stripe
        // at the far left, three seconds away from where it happened.
        const int start = (writePos + 1) % buckets;

        for (int i = 0; i < buckets - 1; ++i)
        {
            const auto s = (size_t) ((start + i) % buckets);
            out[(size_t) i] = { dryLo[s], dryHi[s], wetLo[s], wetHi[s] };
        }

        out[(size_t) (buckets - 1)] = out[(size_t) (buckets - 2)];   // keep the array's length
        return true;
    }

private:
    /** Finish the column: fold its energy into the running levels, then store it at the ratio those
        levels say — the one that was true WHEN IT HAPPENED, which is the whole fix. */
    void closeColumn() noexcept
    {
        const float dryRms = (float) std::sqrt (dryEnergy / (double) juce::jmax (1, perBucket));
        const float wetRms = (float) std::sqrt (wetEnergy / (double) juce::jmax (1, perBucket));

        dryEnergy = wetEnergy = 0.0;

        dryLevel = dryLevel * levelCoeff + dryRms * (1.0f - levelCoeff);
        wetLevel = wetLevel * levelCoeff + wetRms * (1.0f - levelCoeff);

        // Silence has no ratio to speak of. Leaving the column as it is beats inventing a huge factor
        // out of two numbers that are both nearly zero.
        if (wetLevel < 1.0e-6f || dryLevel < 1.0e-6f)
            return;

        const float k = juce::jlimit (0.05f, 20.0f, dryLevel / wetLevel);
        const auto w = (size_t) writePos;

        wetLo[w] *= k;
        wetHi[w] *= k;
    }

    static constexpr double matchSeconds = 1.75;   // slower than a note, faster than a knob

    std::array<float, buckets> dryLo {}, dryHi {}, wetLo {}, wetHi {};

    int perBucket = 240;
    int writePos = 0;
    int filled = 0;

    double dryEnergy = 0.0, wetEnergy = 0.0;   // this column's, reset as it closes
    float dryLevel = 0.0f, wetLevel = 0.0f;    // the slow estimates the matching rides on
    float levelCoeff = 0.99f;

    std::atomic<unsigned> written { 0 };
};

} // namespace orbitamp::core
