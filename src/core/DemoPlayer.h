#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace orbitamp::core
{

/** A guitar loop played into the chain, so the plugin can be heard without an instrument plugged in.

    TEMPORARY, and marked as such: it exists to audition the blocks and the pictures while the device
    packs are being built. It replaces the input rather than mixing with it — a demo you can hear
    over your own playing is a demo you cannot judge anything by.

    The chosen loop repeats until it is stopped or another is picked. Decoding happens when a loop is
    selected, on the message thread; the audio thread only reads an array that is already there. */
class DemoPlayer
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        position = 0;
    }

    /** Decode a loop from a file on disk. Message thread. A missing or unreadable file leaves the
        player as it was — the loops are dev-machine material, not a shipped asset. */
    void setLoop (const juce::File& file, double sourceRate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return;

        juce::AudioBuffer<float> decoded ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&decoded, 0, (int) reader->lengthInSamples, 0, true, true);

        const juce::SpinLock::ScopedLockType lock (swapLock);
        loop = std::move (decoded);
        loopRate = reader->sampleRate > 0.0 ? reader->sampleRate : sourceRate;
        position = 0;
    }

    void setPlaying (bool shouldPlay) noexcept
    {
        if (! shouldPlay)
            position = 0;   // a stopped demo starts from the top, not from where it was interrupted

        playing.store (shouldPlay);
    }

    bool isPlaying() const noexcept { return playing.load(); }

    /** Fills the buffer with the loop when playing, and leaves it alone when not. */
    void fill (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! playing.load())
            return;

        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (! lock.isLocked() || loop.getNumSamples() == 0)
            return;

        const int n = buffer.getNumSamples();
        const int len = loop.getNumSamples();

        // Rate difference is taken as a step through the source — good enough for auditioning, and
        // honest about it: this is a demo player, not the sample engine.
        const double step = loopRate > 0.0 && sampleRate > 0.0 ? loopRate / sampleRate : 1.0;

        for (int i = 0; i < n; ++i)
        {
            const int src = (int) position % len;

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.setSample (c, i, loop.getSample (juce::jmin (c, loop.getNumChannels() - 1), src));

            position += step;
            if (position >= (double) len)
                position -= (double) len;   // round and round until stopped
        }
    }

private:
    juce::AudioBuffer<float> loop;
    juce::SpinLock swapLock;

    double sampleRate = 48000.0;
    double loopRate   = 48000.0;
    double position   = 0.0;
    std::atomic<bool> playing { false };
};

} // namespace orbitamp::core
