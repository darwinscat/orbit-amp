// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

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
    /** The CAPACITY, not the resolution — see setResolution. Sized so the widest window a 4x
        faceplate can show still gets a column per pixel. It costs 64 kB. */
    static constexpr int buckets = 4096;
    static constexpr double seconds = 3.0;

    struct Column { float dryLo, dryHi, wetLo, wetHi; };

    /** How many columns the picture actually wants — one per pixel of its width.

        Keeping more than that is not extra detail, it is noise: a dozen columns crammed into one
        pixel are re-grouped every frame as the ribbon slides, so their extremes hop between pixels
        and the waveform shimmers. One column per pixel means the ribbon moves a pixel at a time,
        which is what a scrolling waveform is supposed to do. */
    void setResolution (int columns)
    {
        const int wanted = juce::jlimit (64, buckets, columns);

        if (wanted != active)
        {
            active = wanted;
            prepare (rate);
        }
    }

    int resolution() const noexcept { return active; }

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        perBucket = juce::jmax (1, (int) (sampleRate * seconds / (double) active));

        // A one-pole per sample would be a per-sample exp() budget for nothing; the estimate only has
        // to move over seconds, so it moves once per column.
        const double columnsPerSecond = rate / (double) juce::jmax (1, perBucket);
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
        match = 1.0f;
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
                filled = 0;                       // starts the next column AT its first sample
                writePos = (writePos + 1) % active;
                written.fetch_add (1, std::memory_order_release);
            }
        }
    }

    /** Copies the ribbon out oldest-first and says how many columns it wrote. Already matched — see
        write(). False when nothing has been written yet. */
    bool read (std::array<Column, buckets>& out, int& count, float& phase) const noexcept
    {
        count = active;

        // How far into the next column the audio has got, 0..1. The picture shifts by this fraction
        // of a pixel, which is what makes the ribbon SLIDE instead of stepping: a column is about nine
        // milliseconds, so without it the whole waveform jumps a few pixels at a time and reads as a
        // sequence of stills rather than a scroll.
        phase = (float) filled / (float) juce::jmax (1, perBucket);

        if (written.load (std::memory_order_acquire) == 0)
            return false;

        // Skip the bucket being written. It holds a fraction of a column's worth of the newest audio,
        // and it sits at the OLDEST end of the ribbon — so a live signal was being drawn as a stripe
        // at the far left, three seconds away from where it happened.
        const int start = (writePos + 1) % active;

        for (int i = 0; i < active - 1; ++i)
        {
            const auto s = (size_t) ((start + i) % active);
            out[(size_t) i] = { dryLo[s], dryHi[s], wetLo[s], wetHi[s] };
        }

        out[(size_t) (active - 1)] = out[(size_t) (active - 2)];   // the one being written
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

        // Below this, nothing is playing — and the picture has to say so. The vertical scale lifts the
        // small end on purpose, so a converter's noise floor, left to itself, is drawn as a thick band
        // across an idle plugin: the display is loudest exactly when there is nothing to show. A floor
        // is the honest fix, not a gentler curve, because the noise is not quiet enough to draw small,
        // it is meaningless.
        if (juce::jmax (dryRms, wetRms) < silenceFloor)
        {
            const auto w = (size_t) writePos;
            dryLo[w] = dryHi[w] = wetLo[w] = wetHi[w] = 0.0f;

            dryLevel *= levelCoeff;
            wetLevel *= levelCoeff;
            return;
        }

        dryLevel = dryLevel * levelCoeff + dryRms * (1.0f - levelCoeff);
        wetLevel = wetLevel * levelCoeff + wetRms * (1.0f - levelCoeff);

        // Silence has no ratio of its own — but it still has to be drawn at the ratio that was true
        // around it. Skipping the scaling there left quiet columns raw while loud ones were matched,
        // so the two waveforms agreed through a note and disagreed everywhere between: the picture
        // changed its mind about what it was showing depending on how loud the moment was.
        if (wetLevel > 1.0e-6f && dryLevel > 1.0e-6f)
            match = juce::jlimit (0.05f, 20.0f, dryLevel / wetLevel);

        const auto w = (size_t) writePos;

        wetLo[w] *= match;
        wetHi[w] *= match;
    }

    static constexpr double matchSeconds = 1.75;   // slower than a note, faster than a knob
    static constexpr float silenceFloor = 3.2e-4f; // -70 dBFS: quieter than any note, louder than hiss

    std::array<float, buckets> dryLo {}, dryHi {}, wetLo {}, wetHi {};

    int perBucket = 240;
    int active = 640;      // columns in use; the array is sized for the widest window
    double rate = 48000.0;
    int writePos = 0;
    int filled = 0;

    double dryEnergy = 0.0, wetEnergy = 0.0;   // this column's, reset as it closes
    float dryLevel = 0.0f, wetLevel = 0.0f;    // the slow estimates the matching rides on
    float match = 1.0f;                        // the last ratio worth believing, held through silence
    float levelCoeff = 0.99f;

    std::atomic<unsigned> written { 0 };
};

} // namespace orbitamp::core
