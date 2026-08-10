#pragma once

#include <array>
#include <atomic>

namespace orbitamp::core
{

/** The window the tuner listens to: the raw input, before any block colours it.

    ScopeTap's contract, mono and longer: the audio thread writes, the tuner panel snapshots
    whenever its timer fires. No lock — a torn read costs one slightly wrong pitch reading, which
    the panel's median swallows, and a lock on the audio thread costs a dropout. The generation
    counter is only there to say "never written yet".

    16384 samples is ~340 ms at 48 kHz: several periods of anything a string can play, and still
    a couple of full cycles of a bass low E at the rates where decimation eats most of it. */
class TunerTap
{
public:
    static constexpr int size = 16384;

    void write (const float* in, int numSamples) noexcept
    {
        generation.fetch_add (1, std::memory_order_release);

        for (int i = 0; i < numSamples; ++i)
        {
            const int w = writePos;
            buf[(size_t) w] = in[i];
            writePos = (w + 1) % size;
        }

        generation.fetch_add (1, std::memory_order_release);
    }

    /** Copies the window out, oldest first. Returns false when the tap has never been written. */
    bool read (std::array<float, size>& out) const noexcept
    {
        if (generation.load (std::memory_order_acquire) == 0)
            return false;

        const int start = writePos;
        for (int i = 0; i < size; ++i)
            out[(size_t) i] = buf[(size_t) ((start + i) % size)];

        return true;
    }

private:
    std::array<float, size> buf {};
    int writePos = 0;
    std::atomic<unsigned> generation { 0 };
};

} // namespace orbitamp::core
