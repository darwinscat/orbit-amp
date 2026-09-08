// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/core/StreamResampler.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** THE WIRE A BYPASSED BLOCK IS, delayed by exactly what the block costs.

    A captured block that rate-matches has a latency — sixty-one samples at 44.1 kHz against a
    48 kHz pack, ninety-six at 96 kHz — and none at all when the pack's rate is the session's,
    which is the usual case. The plugin reports it so the host can line the track up.

    Bypass it and the signal arrives EARLY: the block's delay left the path but the host is still
    compensating for it. A latency that changes with a switch is precisely what makes a host
    re-align mid-song, and the comment beside `reportLatency` had been promising for a long time
    that the bypass path carried the same delay. It did not; this is that promise.

    It also makes the crossfade honest. Blending the block's output against an undelayed input is
    blending a signal with a slightly early copy of itself, which is a comb — at sixty-one samples
    and 44.1 kHz the first notch lands near 362 Hz, in the body of the guitar. Swept over fifteen
    milliseconds it reads as a tick rather than a filter, but it is a tick that need not exist.

    Small, but no longer a handful: the history is `capacity()` floats per channel and the whole
    thing is two copies. At zero delay the caller skips it entirely. */
class BypassWire
{
public:
    /** THE LOWEST RATE A CAPTURE MAY HAVE BEEN RECORDED AT and still be given a wire the full
        length of its own delay — the wire's DOMAIN, which is where the one remaining constant in
        this file belongs. It is not the bound on the delay; the bound is `rateMatchDelay` below,
        and that one moves with the kernel.

        It has to be stated rather than derived, because `D · (1 + h/m)` has no supremum: the delay
        grows without limit as a capture's rate falls, and nothing upstream closes that. Checked in
        the core rather than assumed — `nam::NamStage` takes `GetExpectedSampleRate()` from the file
        as it comes and runs at any positive rate; the `8000.0` that does appear there is a floor on
        the HOST rate inside its own scratch arithmetic and says nothing whatever about a model, so
        it is deliberately NOT what this number leans on.

        Why eight kilohertz: below it a capture's own Nyquist is under 4 kHz — beneath the top of
        the instrument it claims to be a capture of — so a file below this is not a rig. And
        covering the whole of it costs nothing worth counting: 800 samples per channel at a 192 kHz
        session, six kilobytes for a stereo pair.

        If the core ever declares a minimum model rate of its own, this stops being a product
        number and becomes that one — the domain follows, and nothing else in this file changes.
        Until then, outside this domain the wire refuses OUT LOUD; see `everShortened()`. */
    static constexpr double lowestPackRate = 8000.0;

    /** WHAT A RATE-MATCHING CAPTURE COSTS, in host samples, at `hostRate` against a pack recorded
        at `packRate`:

            D · (1 + hostRate / packRate),  D = StreamResampler::delayInputSamples()

        A round trip is two stages: down to the model's rate and back. Each costs `D` of ITS OWN
        input samples, so the pair costs `D` host samples plus `D` model samples expressed in host
        ones — which is where the `(1 + hostRate/packRate)` comes from, and why it is not twice one
        number. Derived in `felitronics/core/StreamResampler.h`; `nam::NamStage::latencySamples()`
        is the same arithmetic and is what actually reaches this class.

        🔴 `D` IS ASKED FOR AT THE MOMENT OF THE CALCULATION, never copied here as a number and
        never read once into a constant of our own. This bound has already outlived its
        justification twice — the comment that used to stand where this one does claimed
        `ceil (3·hostSR/modelSR) + 3` and "sixty-four is room to spare", which was true of a kernel
        two generations dead, and the clamp below went on cutting silently after both replacements.
        A bound derived from a formula has to NAME the formula and be re-derived with it, or it
        becomes a lie that compiles.

        ⚠️ THIS BODY IS A PLACEHOLDER FOR ONE CALL. The composition below is the third generation of
        the same retelling, and the cure is that the core owns it: P36 puts it in the core header as
        `StreamResampler::roundTripDelaySamples (hostRate, packRate)`. When that lands, the two
        lines below become one call to it and NOTHING ELSE IN THIS FILE MOVES — including if the
        kernel starts scaling its taps with the ratio, which makes `D` a function of the rates
        rather than a number. Written this way on purpose: a temporary retelling outlives its
        reason, which is the whole lesson this class was rewritten to carry.

        Rounds UP where the stage rounds to nearest: the stage reports a length, this reserves room
        for one, and a wire that is half a sample too long costs nothing while one half a sample too
        short is the comb this class exists to prevent. */
    static int rateMatchDelay (double hostRate, double packRate) noexcept
    {
        const double d = felitronics::core::StreamResampler::delayInputSamples();
        return (int) std::ceil (d * (1.0 + juce::jmax (1.0, hostRate) / juce::jmax (1.0, packRate)));
    }

    /** The longest delay a wire prepared for `hostRate` can be: the formula above against the
        lowest pack rate the wire promises to cover. 224 samples at 48 kHz, 800 at 192 kHz — six
        kilobytes of history for a stereo pair at the worst of it. */
    static int capacityFor (double hostRate) noexcept
    {
        return rateMatchDelay (hostRate, lowestPackRate);
    }

    /** Allocates. Message thread / `prepareToPlay` only — `process` never does.

        The history and the two scratch rows are sized TOGETHER and from the same number, because
        the failure they replace was exactly that: a capacity raised without the buffer under it
        moving is not a shortened delay any more, it is a read past the end. */
    void prepare (double hostRate)
    {
        cap = juce::jmax (0, capacityFor (hostRate));

        for (auto& h : hist)
            h.assign ((size_t) cap, 0.0f);

        prevScratch.assign ((size_t) cap, 0.0f);
        nextScratch.assign ((size_t) cap, 0.0f);
        shortened.store (false, std::memory_order_relaxed);
    }

    void reset()
    {
        for (auto& h : hist)
            std::fill (h.begin(), h.end(), 0.0f);
    }

    /** The longest delay this wire can actually carry right now. */
    int capacity() const noexcept { return cap; }

    /** 🔴 THE REFUSAL, MADE VISIBLE. True once the wire has been asked for more delay than it was
        prepared for and has handed back less — which puts a comb in the crossfade. Latched, and
        cleared only by `prepare`.

        The clamp it replaces was `jlimit` and nothing else: a session above 48 kHz got a bypass
        path shorter than the block it stood for, and no line of code anywhere could tell. Inside
        the domain this wire states (`lowestPackRate` and up, at any session rate) this can no
        longer be true; outside it, the wire says so instead of combing quietly. */
    bool everShortened() const noexcept { return shortened.load (std::memory_order_relaxed); }

    /** Writes `in` delayed by `delay` into `out`. In-place is allowed (`out == in`) — that is what
        a fully bypassed block wants; a crossfading one hands its own scratch instead. */
    void process (const float* const* in, float* const* out, int numChannels, int numSamples,
                  int delay) noexcept
    {
        const int want = juce::jmax (0, delay);
        const int d    = juce::jmin (want, cap);

        if (d != want && ! shortened.exchange (true, std::memory_order_relaxed))
        {
            // A capture below `lowestPackRate` at this session rate. The bypass path is about to
            // be short by `want - d` samples, which is a comb whose first null sits at
            // hostRate / (2 * (want - d)). Nothing on the audio thread can fix it — it can only
            // stop being silent about it, which is the flag above and this line. Not `jassertfalse`
            // on purpose: the test that PINS this behaviour has to be able to reach it, and an
            // assertion that aborts a debug build is an assertion the pin cannot survive.
            DBG ("BypassWire: asked for " << want << " samples of delay, carries " << d
                 << " — the bypass path will comb");
        }

        if (numSamples <= 0)
            return;

        if (d == 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                if (out[ch] != in[ch])
                    juce::FloatVectorOperations::copy (out[ch], in[ch], numSamples);

            return;
        }

        for (int ch = 0; ch < juce::jmin (numChannels, (int) hist.size()); ++ch)
        {
            auto& h = hist[(size_t) ch];

            // `h[k]` is the input sample that stood at position `k - d` — the d samples that came
            // before this block, oldest first.
            for (int i = 0; i < d; ++i)
                prevScratch[(size_t) i] = h[(size_t) i];

            // What the NEXT block will need: the last d samples of (prev ++ in). Taken before a
            // single byte of `out` is written, because `out` may be `in`.
            for (int i = 0; i < d; ++i)
            {
                const int at = numSamples - d + i;     // position of this sample in `in`
                nextScratch[(size_t) i] = at >= 0 ? in[ch][at] : prevScratch[(size_t) (at + d)];
            }

            // Backwards, so an in-place shift never eats what it has not read yet.
            for (int i = numSamples - 1; i >= d; --i)
                out[ch][i] = in[ch][i - d];

            for (int i = juce::jmin (d, numSamples) - 1; i >= 0; --i)
                out[ch][i] = prevScratch[(size_t) i];

            for (int i = 0; i < d; ++i)
                h[(size_t) i] = nextScratch[(size_t) i];
        }
    }

private:
    int cap = 0;

    std::array<std::vector<float>, 2> hist {};
    std::vector<float> prevScratch, nextScratch;

    std::atomic<bool> shortened { false };
};

} // namespace orbitamp::core
