#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** The echo before the space — repeats of what the preamp made, which the reverb then rooms.

    A tape head, not a sampler: the delay TIME is a target the line glides toward, and while it
    glides the read head moves at the wrong speed — the repeats bend in pitch, the way a tape
    echo bends when its motor is dragged. Turning the knob IS the effect; there is no crossfade
    hiding the move.

    The loop is dark on purpose. Every pass through the line — the first echo included — goes
    through a one-pole low pass and a soft saturator, the record head's own voice: repeats
    darken and compress as they recirculate instead of piling up as clean copies. DARK is the
    low pass corner; the saturator is fixed by taste, not a knob.

    OFFSET spreads the wet: one channel's repeats arrive later than the other's by a signed
    handful of milliseconds — the whole comb shifts as one, a constant width, not a progressive
    drift. The offset line glides like the time does, so turning it bends too. Dry is untouched:
    the dry path is the amp's law here as it is in the reverb — unity always, the wet ADDED.

    Real-time rule: process() never allocates, locks, or throws — every buffer is sized in
    prepare(); reset() is cheap-idempotent so an off block may call it every time. */
class DelayStage
{
public:
    void prepare (double newSampleRate, int maxBlockSize) noexcept
    {
        juce::ignoreUnused (maxBlockSize);
        sampleRate = newSampleRate;

        // Sized for the ranges, not the moment: the slowest division at the slowest BPM
        // (a whole note at 40) is six seconds, and the offset rides on top.
        const auto lineMax = (size_t) std::ceil (maxDelaySeconds * sampleRate) + 8;
        const auto offMax  = (size_t) std::ceil (maxOffsetSeconds * sampleRate) + 8;

        for (size_t ch = 0; ch < 2; ++ch)
        {
            line[ch].assign (lineMax, 0.0f);
            offLine[ch].assign (offMax, 0.0f);
        }

        writePos = 0;
        offPos   = 0;
        primed   = false;
        cleared  = true;

        // The glides, seeded: a fresh line starts ON its targets rather than sweeping in
        // from wherever the last session left the heads.
        currentDelay = targetDelaySamples();
        currentOff[0] = currentOff[1] = 0.0f;

        updateCoeffs();
    }

    void reset() noexcept
    {
        if (cleared)
            return;   // an off block calls this every block; the memset must not be per-block

        for (size_t ch = 0; ch < 2; ++ch)
        {
            std::fill (line[ch].begin(), line[ch].end(), 0.0f);
            std::fill (offLine[ch].begin(), offLine[ch].end(), 0.0f);
            lpfState[ch] = 0.0f;
        }

        primed  = false;
        cleared = true;
    }

    /** The head's destination, in milliseconds — free or computed from BPM by the caller.
        The line glides there; big moves bend. */
    void setTimeMs (float ms) noexcept
    {
        timeMs = juce::jmax (1.0f, ms);
        shownTimeMs.store (timeMs, std::memory_order_relaxed);   // process() overwrites with the glide
    }

    /** Feedback, 0..1. The dark filter and the saturator live inside the loop, so even 1 is a
        long compressed bloom rather than a runaway. */
    void setRepeats (float amount) noexcept { repeats = juce::jlimit (0.0f, 1.0f, amount); }

    /** The loop low pass corner — LOWER is darker, and every pass darkens again. */
    void setDarkHz (float hz) noexcept
    {
        if (! juce::approximatelyEqual (hz, darkHz))
        {
            darkHz = hz;
            updateCoeffs();
        }
    }

    /** The stereo shift of the wet, signed: positive holds the RIGHT repeats back, negative the
        left. Dry never moves. */
    void setOffsetMs (float ms) noexcept
    {
        offsetMs = juce::jlimit (-1000.0f * (float) maxOffsetSeconds,
                                  1000.0f * (float) maxOffsetSeconds, ms);
        shownOffsetMs.store (offsetMs, std::memory_order_relaxed);
    }

    /** 0 = fully dry, 1 = the repeats added at unity. Dry never moves. */
    void setMix (float newMix) noexcept { mix = juce::jlimit (0.0f, 1.0f, newMix); }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels < 1 || numSamples <= 0)
            return;

        const int nch  = juce::jmin (2, numChannels);
        const int lineN = (int) line[0].size();
        const int offN  = (int) offLine[0].size();

        // Waking up, the heads SNAP to their targets: a first block should not spend six
        // seconds sweeping in from wherever the knob stood when the block was last on.
        const float tgtDelay = targetDelaySamples();
        if (! primed)
        {
            currentDelay = tgtDelay;
            currentOff[0] = offsetMs < 0.0f ? -offsetMs * 0.001f * (float) sampleRate : 0.0f;
            currentOff[1] = offsetMs > 0.0f ?  offsetMs * 0.001f * (float) sampleRate : 0.0f;
            primed  = true;
        }
        cleared = false;

        const float tgtOff[2] = {
            offsetMs < 0.0f ? -offsetMs * 0.001f * (float) sampleRate : 0.0f,
            offsetMs > 0.0f ?  offsetMs * 0.001f * (float) sampleRate : 0.0f,
        };

        // The comb's pulse listens at the door: the block peak of what the line is being FED,
        // taken before the loop overwrites the channel in place.
        {
            float pk = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                pk = juce::jmax (pk, std::abs (channels[0][i]));
            envIn.store (pk, std::memory_order_relaxed);
        }

        for (int i = 0; i < numSamples; ++i)
        {
            // The motor: one exponential glide shared by both channels, so the comb never
            // splits while it travels.
            currentDelay += (tgtDelay - currentDelay) * glideCoeff;
            const float dly = juce::jlimit (1.0f, (float) (lineN - 4), currentDelay);

            for (int ch = 0; ch < nch; ++ch)
            {
                const float x = channels[ch][i];

                // The play head, fractional: this read at a moving distance IS the repitch.
                float pos = (float) writePos - dly;
                if (pos < 0.0f)
                    pos += (float) lineN;

                const int   i0 = (int) pos;
                const int   i1 = i0 + 1 == lineN ? 0 : i0 + 1;
                const float f  = pos - (float) i0;
                const float tap = line[(size_t) ch][(size_t) i0] * (1.0f - f)
                                + line[(size_t) ch][(size_t) i1] * f;

                // The record head: input plus the recirculation, darkened and pressed — the
                // first echo already wears one pass, the n-th wears n.
                float v = x + tap * repeats;
                lpfState[ch] += lpfCoeff * (v - lpfState[ch]);
                v = std::tanh (lpfState[ch] * satDrive) * satNorm;
                line[(size_t) ch][(size_t) writePos] = v;

                // The offset line rides the WET only — one channel's comb held back whole. It
                // runs on its OWN write position: it is a short line, and the long line's clock
                // would walk straight past its end (it did — the heap smash behind the first
                // crash the block ever produced).
                currentOff[ch] += (tgtOff[ch] - currentOff[ch]) * offGlideCoeff;
                offLine[(size_t) ch][(size_t) offPos] = tap;

                float opos = (float) offPos - juce::jlimit (0.0f, (float) (offN - 4), currentOff[ch]);
                if (opos < 0.0f)
                    opos += (float) offN;

                const int   o0 = (int) opos;
                const int   o1 = o0 + 1 == offN ? 0 : o0 + 1;
                const float of = opos - (float) o0;
                const float wet = offLine[(size_t) ch][(size_t) o0] * (1.0f - of)
                                + offLine[(size_t) ch][(size_t) o1] * of;

                channels[ch][i] = x + wet * mix;
            }

            writePos = writePos + 1 == lineN ? 0 : writePos + 1;
            offPos   = offPos   + 1 == offN  ? 0 : offPos   + 1;
        }

        // Where the heads actually STAND, for the picture: the glided values, so the comb
        // slides with the motor instead of jumping to the target.
        shownTimeMs.store ((float) (currentDelay / sampleRate * 1000.0), std::memory_order_relaxed);
        shownOffsetMs.store ((float) ((currentOff[1] - currentOff[0]) / sampleRate * 1000.0),
                             std::memory_order_relaxed);
    }

    /** The picture's taps — the glided time and offset while the block runs (the set targets
        while it does not), and the block peak of what the line is fed. Audio thread writes,
        the face reads on its repaint clock. */
    std::atomic<float> shownTimeMs   { 350.0f };
    std::atomic<float> shownOffsetMs { 0.0f };
    std::atomic<float> envIn         { 0.0f };

    static constexpr double maxDelaySeconds  = 6.1;    // 1/1 at 40 BPM, with margin
    static constexpr double maxOffsetSeconds = 0.031;  // the offset knob's reach

private:
    float targetDelaySamples() const noexcept
    {
        return (float) ((double) timeMs * 0.001 * sampleRate);
    }

    void updateCoeffs() noexcept
    {
        // The motor's inertia: ~150 ms to the new time — enough travel to hear the bend.
        glideCoeff    = 1.0f - std::exp ((float) (-1.0 / (0.15 * sampleRate)));
        offGlideCoeff = 1.0f - std::exp ((float) (-1.0 / (0.03 * sampleRate)));
        lpfCoeff      = 1.0f - std::exp ((float) (-juce::MathConstants<double>::twoPi
                                                  * (double) darkHz / sampleRate));
    }

    // The saturator, fixed by taste: gentle at echo level, a press at full recirculation.
    static constexpr float satDrive = 1.2f;
    static constexpr float satNorm  = 1.0f / satDrive;

    float timeMs   = 350.0f;
    float repeats  = 0.35f;
    float darkHz   = 4000.0f;
    float offsetMs = 0.0f;
    float mix      = 0.25f;

    double sampleRate = 48000.0;
    float  glideCoeff = 0.0f, offGlideCoeff = 0.0f, lpfCoeff = 1.0f;

    std::array<std::vector<float>, 2> line, offLine;
    int   writePos = 0, offPos = 0;
    float currentDelay = 0.0f;
    float currentOff[2] { 0.0f, 0.0f };
    float lpfState[2]   { 0.0f, 0.0f };
    bool  primed = false, cleared = true;
};

} // namespace orbitamp::core
