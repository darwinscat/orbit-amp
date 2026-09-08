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
    thing is two copies.

    🔴 AND IT KNOWS NOTHING ABOUT SAMPLE RATES. It is told a DELAY — the one the block actually
    reports, derived by `nam::NamStage::rateMatch` from the rates that are really in play — and it
    is sized from that same number. Every earlier version of this class sized itself from a rate it
    ASSUMED: first `ceil (3 * hostSR / modelSR) + 3` behind a constant 64, then a computed bound
    against a guessed lowest pack rate. Both are the same mistake, and the second is not a smaller
    version of the first: a ceiling derived from a model rate nobody promised is not too low, it is
    WRONG — the run rate can walk, without limit while audio runs, and no static number survives
    that. So there is no ceiling here at all; there is a capacity, and it follows the delay. */
class BypassWire
{
public:
    /** The largest delay this class will build a buffer for. NOT a bound on what a rate match can
        cost — nothing bounds that — but a bound on believing a number: a megabyte of history per
        channel is already past the point where the figure came from a real capture, and past it
        the wire refuses out loud rather than allocating whatever arrived. */
    static constexpr int maxSaneDelay = 1 << 18;

    /** Allocates, and forgets everything. Message thread / `prepareToPlay` only.

        ⚠️ AND NOT WHILE AUDIO RUNS. The buffers were fixed arrays once and were immune to being
        re-prepared under the audio thread's feet; vectors are not. Growing a LIVE wire is what
        `reserve` + `commit` are for, and they exist precisely so this one never has to run twice.

        The history and the two scratch rows are sized TOGETHER and from the same number, because
        the failure they replace was exactly that: a capacity raised without the buffer under it
        moving is not a shortened delay any more, it is a read past the end. */
    void prepare (int maxDelay)
    {
        const int n = clampDelay (maxDelay);

        for (auto& h : hist)
            h.assign ((size_t) n, 0.0f);

        prevScratch.assign ((size_t) n, 0.0f);
        nextScratch.assign ((size_t) n, 0.0f);

        cap.store (n, std::memory_order_relaxed);
        pending.store (0, std::memory_order_release);   // any growth in flight is void now
        shortened.store (false, std::memory_order_relaxed);
    }

    /** GROWING A LIVE WIRE, HALF ONE: build the bigger rows. Message thread. ALLOCATES — all of
        it, which is the entire reason this is a separate call from the adoption below.

        Returns false when nothing was built: the wire already carries `maxDelay`, or a growth is
        still in flight and the audio thread has not taken it yet. Either way the caller simply
        asks again on its next pump, thirty times a second.

        🔴 NO LOCK ANYWHERE IN THIS CLASS, and that is not simplification, it is correctness. The
        first version of this handed the swap to the message thread under
        `AudioProcessor::getCallbackLock()` — which VST3, AU and the standalone do hold around
        `processBlock`, and **CLAP does not**: the shipped wrapper calls `processBlock` with no lock
        of any kind. A lock only some of the shipped formats take is not a lock; it is a data race
        with a comment on it. So the audio thread adopts its own buffers, and the message thread
        never touches live state at all. */
    [[nodiscard]] bool reserve (int maxDelay)
    {
        const int want = clampDelay (maxDelay);

        if (want <= cap.load (std::memory_order_relaxed)
             || pending.load (std::memory_order_acquire) != 0)
            return false;

        for (auto& h : spareHist)
            h.assign ((size_t) want, 0.0f);

        spareA.assign ((size_t) want, 0.0f);
        spareB.assign ((size_t) want, 0.0f);

        // Publish LAST: everything above has to be visible to the audio thread before the flag
        // that tells it to look.
        pending.store (want, std::memory_order_release);
        return true;
    }

    /** True while `reserve` has built something the audio thread has not taken yet. The message
        thread must leave the spare rows alone until this clears — `reserve` enforces it. */
    bool growthInFlight() const noexcept { return pending.load (std::memory_order_acquire) != 0; }

    /** FED EVERY BLOCK, WHATEVER THE BLOCK IS DOING — the window takes the signal and writes
        nothing. This is the half that keeps the wire WARM.

        It replaces clearing the wire whenever it was not carrying, and the difference is a click.
        A window filled only while the delay is live is COLD the first time the delay becomes live:
        a model landing at another rate into a block that stands bypassed made the through-path
        open with `delay` samples of silence — 96 of them at 96 kHz, and the boost ships switched
        off, so that is an ordinary Tuesday and not a corner. Fed every block there is nothing to
        go cold. `felitronics::core::DryAligner` learned this first and states the same rule: a
        ring fed only while a stage runs emits its latency in zeros. */
    void advance (const float* const* in, int numChannels, int numSamples) noexcept
    {
        process (in, nullptr, numChannels, numSamples, 0);
    }

    /** Forgets what it has heard. `prepare` does this; the audio path does NOT — see `advance`.
        A test that wants to prove the window is real clears it and listens for the silence. */
    void reset()
    {
        for (auto& h : hist)
            std::fill (h.begin(), h.end(), 0.0f);
    }

    /** The longest delay this wire can actually carry right now. */
    int capacity() const noexcept { return cap.load (std::memory_order_relaxed); }

    /** 🔴 THE REFUSAL, MADE VISIBLE. True once the wire has been asked for more delay than it was
        prepared for and has handed back less — which puts a comb in the crossfade. Latched until
        the next `prepare`, and read OFF the audio thread: `AmpProcessor::reportLatency` picks it
        up on the same message-thread pump that noticed the latency, so the refusal reaches a log
        in a release build and not only an assertion in a debug one.

        The clamp it replaces was `jlimit` and nothing else: a session above 48 kHz got a bypass
        path shorter than the block it stood for, and no line of code anywhere could tell. There is
        no domain any more to be inside or outside of — the wire grows to whatever it is asked for.
        What this reports is what is left: the blocks between a model landing and the wire being
        told, and an ask past `maxSaneDelay`, which is refused rather than believed. */
    bool everShortened() const noexcept { return shortened.load (std::memory_order_relaxed); }

    /** Writes `in` delayed by `delay` into `out`. In-place is allowed (`out == in`) — that is what
        a fully bypassed block wants; a crossfading one hands its own scratch instead.

        `delay` may CHANGE from block to block and does: a model landing at another rate moves it
        without anybody re-preparing anything. That is why the history is a window of the last
        `capacity()` samples rather than of the last `delay` of them — the window does not know
        what the delay is, so a delay that moves reads the right samples on its very first block
        instead of replaying the tail of the old alignment.

        TWO CHANNELS. That is what the wire carries, and what the chain hands it; anything past the
        pair passes through UNDELAYED rather than being left untouched, because an untouched output
        buffer is a silent lie and undelayed is at least an audible one.

        `out` may be null, which means FEED THE WINDOW AND WRITE NOTHING — that is `advance`, and
        it shares this body rather than copying the window formula into a second one. */
    void process (const float* const* in, float* const* out, int numChannels, int numSamples,
                  int delay) noexcept
    {
        // GROWING A LIVE WIRE, HALF TWO — and the audio thread does it itself, because it is the
        // only thread that can do it without a lock. Pointer swaps and one copy of the window;
        // not one allocation and not one free. The small rows it hands back are released later, on
        // the message thread, by the next `reserve`.
        if (const int want = pending.load (std::memory_order_acquire); want > cap.load (std::memory_order_relaxed))
            adopt (want);

        const int c    = cap.load (std::memory_order_relaxed);
        const int ask  = juce::jmax (0, delay);
        const int d    = juce::jmin (ask, c);

        if (d != ask)
            shortened.store (true, std::memory_order_relaxed);

        if (numSamples <= 0)
            return;

        const int paired = juce::jmin (numChannels, (int) hist.size());

        // ONE window formula, used by every channel — the ones the caller handed over and the ones
        // it did not. `src == nullptr` is a channel that carried SILENCE this block, which is what
        // a channel outside `numChannels` did: the chain drops to one channel in MONO, and a
        // window that simply stops being written is a window holding whatever the last STEREO
        // stretch left in it. That is a ghost, and it is the one the old `reset()` used to sweep up
        // on this very path.
        const auto slide = [this, numSamples, c] (std::vector<float>& h, const float* src)
        {
            // The window after this block: the last `cap` samples of (h ++ src). Taken before a
            // single byte of `out` is written, because `out` may be `in`. Kept even at zero delay:
            // the wire SAW those samples, and throwing them away is what makes the first block
            // after a model lands open on silence instead of on the signal.
            for (int i = 0; i < c; ++i)
            {
                const int at = numSamples - c + i;     // position of this sample in `src`
                nextScratch[(size_t) i] = at >= 0 ? (src != nullptr ? src[at] : 0.0f)
                                                  : h[(size_t) (at + c)];
            }
        };

        for (int ch = 0; ch < paired; ++ch)
        {
            auto& h = hist[(size_t) ch];

            // The `d` samples that stood immediately before this block are the TAIL of the window,
            // not its head. Reading the head is what made a delay that moved replay the wrong
            // samples for one block.
            for (int i = 0; i < d; ++i)
                prevScratch[(size_t) i] = h[(size_t) (c - d + i)];

            slide (h, in[ch]);

            if (out != nullptr)
            {
                if (d > 0)
                {
                    // Backwards, so an in-place shift never eats what it has not read yet.
                    for (int i = numSamples - 1; i >= d; --i)
                        out[ch][i] = in[ch][i - d];

                    for (int i = juce::jmin (d, numSamples) - 1; i >= 0; --i)
                        out[ch][i] = prevScratch[(size_t) i];
                }
                else if (out[ch] != in[ch])
                {
                    juce::FloatVectorOperations::copy (out[ch], in[ch], numSamples);
                }
            }

            for (int i = 0; i < c; ++i)
                h[(size_t) i] = nextScratch[(size_t) i];
        }

        // A channel the caller did not hand over this block still has a window, and it has to go on
        // moving — filled with the silence that channel actually carried. Otherwise MONO freezes
        // the right-hand window, and the next STEREO block reads a stretch of audio from before the
        // switch. Costs one slide of a channel nobody is listening to.
        for (int ch = paired; ch < (int) hist.size(); ++ch)
        {
            auto& h = hist[(size_t) ch];
            slide (h, nullptr);

            for (int i = 0; i < c; ++i)
                h[(size_t) i] = nextScratch[(size_t) i];
        }

        if (out != nullptr)
            for (int ch = paired; ch < numChannels; ++ch)
                if (out[ch] != in[ch])
                    juce::FloatVectorOperations::copy (out[ch], in[ch], numSamples);
    }

private:
    // The latch is read from the message thread while the audio thread writes it, so it has to be
    // a real atomic and not a lock the audio thread might wait on.
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "BypassWire's refusal latch is written from the audio thread");

    /** A delay, believed only as far as it can be built. Negative is nothing; absurd is refused
        rather than allocated for — see `maxSaneDelay`. */
    static int clampDelay (int d) noexcept
    {
        return d < 0 ? 0 : (d > maxSaneDelay ? maxSaneDelay : d);
    }

    /** ADOPTION — audio thread only, and it allocates nothing. The old window lands at the RECENT
        end of the new one, because that is where "the samples immediately before this block" have
        to be; what the wire has genuinely never heard stays zero, which `reserve` already made it. */
    void adopt (int want) noexcept
    {
        const int old = cap.load (std::memory_order_relaxed);

        for (size_t ch = 0; ch < hist.size(); ++ch)
        {
            std::copy (hist[ch].begin(), hist[ch].end(), spareHist[ch].begin() + (want - old));
            hist[ch].swap (spareHist[ch]);
        }

        prevScratch.swap (spareA);      // contents are rewritten every block; only the SIZE matters
        nextScratch.swap (spareB);

        cap.store (want, std::memory_order_relaxed);
        pending.store (0, std::memory_order_release);   // the spare rows are the message thread's again
    }

    std::atomic<int> cap { 0 };

    /** Non-zero while `reserve` has built rows the audio thread has not taken. Written by both,
        which is why it is the only handshake: message thread sets it after building, audio thread
        clears it after adopting, and `reserve` refuses to build while it is set. */
    std::atomic<int> pending { 0 };

    std::array<std::vector<float>, 2> hist {};
    std::vector<float> prevScratch, nextScratch;

    // Built by `reserve` on the message thread, swapped in by `commit` under the callback's lock.
    std::array<std::vector<float>, 2> spareHist {};
    std::vector<float> spareA, spareB;

    std::atomic<bool> shortened { false };
};

} // namespace orbitamp::core
