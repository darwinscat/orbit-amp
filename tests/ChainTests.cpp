// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for the whole plugin, not a part of it: build the processor, move a control, and hear whether
// the output changed.
//
// The pieces each have their own gate and each of them passes — the pack loads, the gain sweep moves
// the sound, a measured curve becomes a filter that bends by what the pack promised. None of that
// proves the assembled thing responds, because between a knob and the audio there is a parameter, a
// timer that loads models off the message thread, a bypass switch, and an editor that is not running
// here. This is the check that a player's report — "it does not react to anything" — has an answer
// other than a shrug.
//
// Skips cleanly when no pack is installed.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "core/BypassWire.h"

#include <felitronics/core/StreamResampler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#if defined(_WIN32)
  #include <malloc.h>
#endif
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

// EVERY ALLOCATION THIS THREAD MAKES, COUNTED. RT-safety claims are worth what their instrument
// is worth, and "I read the code and saw no `new`" is not an instrument: `std::vector::assign` on a
// larger size allocates, `juce::String` allocates, a capturing lambda can.
//
// 🔴 WHAT THIS COUNTS, EXACTLY, because an instrument that overstates its reach is worse than none.
// It counts every `operator new` form the standard defines — plain, array, nothrow and aligned —
// so it sees what `std::vector` does, which is what `BypassWire` is made of. It does NOT count
// `malloc`/`calloc`/`realloc`, so a class built on JUCE's `HeapBlock` would slip past it; that is
// stated rather than glossed, and it is why the claim below is about THIS class and not about the
// binary.
//
// The counter is THREAD-LOCAL. A shared one would have been simpler and would have made every
// background thread's allocation look like this thread's — a false non-zero, i.e. a red test with
// no defect under it.
//
// And it is compiled out under a sanitizer. Overriding only some of the `operator new` family
// leaves the rest coming from the sanitizer's runtime while every `delete` goes to this file's —
// an allocator mismatch invented by the instrument, in the build whose whole job is to find those.
#if defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    #define ORBITAMP_COUNT_ALLOCATIONS 0
  #endif
#endif
#if ! defined(ORBITAMP_COUNT_ALLOCATIONS)
  #if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    #define ORBITAMP_COUNT_ALLOCATIONS 0
  #else
    #define ORBITAMP_COUNT_ALLOCATIONS 1
  #endif
#endif

static thread_local long long gAllocations = 0;

#if ORBITAMP_COUNT_ALLOCATIONS
static void* countedAlloc (std::size_t n)
{
    ++gAllocations;
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}

// Over-aligned allocation has no portable spelling: POSIX has `posix_memalign`, MSVC has
// `_aligned_malloc` and REQUIRES `_aligned_free` to undo it — mixing them is a heap corruption, not
// a warning. Both halves are switched together for that reason, and the free below is the only
// thing allowed to release what this returns.
static void* countedAlignedAlloc (std::size_t n, std::align_val_t a)
{
    ++gAllocations;
    const std::size_t want = (std::size_t) a;
    const std::size_t al   = want < sizeof (void*) ? sizeof (void*) : want;

   #if defined(_WIN32)
    if (void* p = _aligned_malloc (n ? n : 1, al)) return p;
    throw std::bad_alloc();
   #else
    void* p = nullptr;
    if (::posix_memalign (&p, al, n ? n : 1) != 0) throw std::bad_alloc();
    return p;
   #endif
}

static void countedAlignedFree (void* p) noexcept
{
   #if defined(_WIN32)
    _aligned_free (p);
   #else
    std::free (p);
   #endif
}

void* operator new   (std::size_t n) { return countedAlloc (n); }
void* operator new[] (std::size_t n) { return countedAlloc (n); }
void* operator new   (std::size_t n, const std::nothrow_t&) noexcept { ++gAllocations; return std::malloc (n ? n : 1); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { ++gAllocations; return std::malloc (n ? n : 1); }
void* operator new   (std::size_t n, std::align_val_t a) { return countedAlignedAlloc (n, a); }
void* operator new[] (std::size_t n, std::align_val_t a) { return countedAlignedAlloc (n, a); }

void operator delete   (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete   (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete   (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete   (void* p, std::align_val_t) noexcept { countedAlignedFree (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { countedAlignedFree (p); }
void operator delete   (void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { countedAlignedFree (p); }
#endif

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail = {})
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    void set (orbitamp::AmpProcessor& amp, const juce::String& id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    /** The plugin's output for a guitar-ish sine, kept. The message-thread work a real host would
        drive — loading the model the gain knob selects, designing the measured filters — is pumped
        here the same way, because a knob that only takes effect on a timer still has to take
        effect. The frequency is the caller's, because what is audible depends on what is asked:
        a low shelf does nothing to a 220 Hz probe and everything to one inside its band. */
    std::vector<float> run (orbitamp::AmpProcessor& amp, double toneHz = 220.0, float amplitude = 0.25f)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;

        double sum = 0.0;
        int counted = 0;
        int phase = 0;

        for (int block = 0; block < 60; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float s = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * toneHz * phase / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            amp.processBlock (buf, midi);

            // What the 30 Hz timer does in a host. No message loop here — a console binary running
            // one would be testing JUCE's dispatcher rather than the plugin.
            amp.pumpDeviceWork();
            juce::Thread::sleep (2);   // the convolver loads its kernel on its own thread

            if (block < 30)          // let loads and filter swaps settle before measuring
                continue;

            for (int i = 0; i < blockSize; ++i)
            {
                sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
                ++counted;
                out.push_back (buf.getSample (0, i));
            }
        }

        juce::ignoreUnused (sum, counted);
        return out;
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (const float s : x)
            sum += (double) s * s;

        return std::sqrt (sum / std::max<size_t> (1, x.size()));
    }

    double peakDb (const std::vector<float>& x)
    {
        float peak = 0.0f;
        for (const float s : x)
            peak = std::max (peak, std::abs (s));

        return 20.0 * std::log10 (std::max (1.0e-9f, peak));
    }

    /** How DIFFERENT two outputs are, after matching their levels — as a percentage of signal.

        Level is the wrong question for a distortion. Turning a pedal up does not make it louder past
        a point; it makes it a different shape, and a check on loudness would call a working gain knob
        broken. So both are normalised and what is measured is what is left. */
    double differencePercent (const std::vector<float>& a, const std::vector<float>& b)
    {
        const double ra = rms (a), rb = rms (b);
        if (ra < 1.0e-9 || rb < 1.0e-9 || a.size() != b.size())
            return 0.0;

        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = a[i] / ra - b[i] / rb;
            diff += d * d;
        }

        return 100.0 * std::sqrt (diff / (double) a.size());
    }
}

int main()
{
    // UNBUFFERED, and the steps named as they pass. printf to a pipe or a file is block-buffered, so
    // a gate that dies takes everything it said with it — which is how this one spent days failing
    // on Windows CI with an empty log and a bare exit code, indistinguishable from a missing binary.
    // A gate's first duty when it falls over is to say where.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    std::printf ("orbitamp chain gate\n");

    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("  juce initialised\n");

    // ON THE HEAP, and it has to be. The processor is a megabyte and a half of taps, delay lines,
    // ribbons and IR buffers; Windows gives a main thread ONE megabyte of stack where macOS gives
    // eight. As a local it overflowed the stack in main's prologue — before a single statement ran,
    // which is why this gate died silently, with no output at all, on every Windows CI run there has
    // ever been. A host allocates the processor on the heap; so does its gate.
    std::printf ("  sizeof(AmpProcessor) = %.2f MB\n",
                 (double) sizeof (orbitamp::AmpProcessor) / (1024.0 * 1024.0));

    const auto ampOwned = std::make_unique<orbitamp::AmpProcessor>();
    auto& amp = *ampOwned;
    std::printf ("  processor constructed\n");

    amp.inlineLoads = true;   // no message loop here: a model lands inside the pump
    amp.prepareToPlay (sampleRate, blockSize);
    std::printf ("  prepared\n\n");
    set (amp, orbitamp::params::stereoMode, 0.0f);   // the gate measures the chain, not the environment default

    // THE BYPASS WIRE, on its own — the delay it carries, the geometry it is SIZED from, and the
    // comb that appears the moment the two disagree. It only runs when a pack's rate differs from
    // the session's, so the chain above — at 48 kHz against 48 kHz packs — never touches it, and
    // that is exactly why the last defect here lived for two kernel generations: a constant
    // `maxDelay = 64`, justified in a comment by a formula that had been replaced twice, clamping
    // silently on every session above 48 kHz.
    {
        using Wire = orbitamp::core::BypassWire;

        // WHAT THE LIVE BLOCK ACTUALLY ASKS THE WIRE FOR. `nam::NamStage` rounds the same core
        // geometry to nearest where the wire's bound rounds up, and THAT — the rounding policy —
        // is the only thing spelled here. The composition itself is asked of the same function the
        // stage asks, because a test that restates it stops being able to notice when it moves;
        // that is how the constant this branch removed survived two kernels.
        const auto stageLatency = [] (double host, double pack)
        {
            return (int) std::lround (
                felitronics::core::StreamResampler::pairDelayHostSamples (host, pack));
        };

        // TAKING A GROWTH, the way the audio thread takes one: a block. A zero-length block is the
        // cheapest real one — `process` adopts before it looks at `numSamples` — so the fixtures
        // below can hand the growth over without also sliding the window they are about to check.
        const auto takeGrowth = [] (Wire& w) { w.advance (nullptr, 0, 0); };

        // 1 · THERE IS NO BOUND ANY MORE — the wire GROWS to whatever the geometry produces, and
        //     the window survives the growth. This section used to assert that a computed ceiling
        //     covered every rate pair; the ceiling is gone, because one derived from a model rate
        //     nobody promised is not too low, it is wrong. What is checked instead is the property
        //     that replaced it: for every pair, `reserve` + `commit` leaves the wire carrying
        //     exactly what the stage asks — and still holding what it had heard.
        {
            static const double hosts[] { 8000.0, 22050.0, 32000.0, 44100.0, 48000.0, 48001.0,
                                          88200.0, 96000.0, 176400.0, 192000.0, 384000.0 };
            static const double packs[] { 4000.0, 8000.0, 11025.0, 16000.0, 22050.0, 32000.0,
                                          44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

            bool grows = true, staysWarm = true;
            int  pairs = 0, biggest = 0;
            double biggestHost = 0.0, biggestPack = 0.0;

            for (const double h : hosts)
                for (const double p : packs)
                {
                    ++pairs;
                    const int ask = stageLatency (h, p);

                    Wire w;
                    w.prepare (8);                     // deliberately far too small to begin with

                    // Something in the window BEFORE the growth, so the copy across can be checked
                    // rather than assumed: eight samples the wire has genuinely heard — and TWO
                    // CHANNELS carrying different eights, because a growth that copied the left
                    // history into both would pass a mono fixture and take the right channel's
                    // past away silently.
                    std::vector<float> seedL (8), seedR (8);
                    for (int i = 0; i < 8; ++i)
                    {
                        seedL[(size_t) i] = (float) (i + 1);
                        seedR[(size_t) i] = (float) (i + 1) + 1000.0f;
                    }

                    const float* sp[2] { seedL.data(), seedR.data() };
                    w.advance (sp, 2, 8);

                    if (w.reserve (ask))
                        takeGrowth (w);

                    grows = grows && w.capacity() >= ask;

                    // The eight it heard have to come back out of the grown wire, at the delay that
                    // reaches back to them — otherwise growth is a re-prepare wearing a swap's coat.
                    std::vector<float> quiet (8, 0.0f), outL (8), outR (8);
                    const float* qp[2] { quiet.data(), quiet.data() };
                    float*       op[2] { outL.data(), outR.data() };
                    w.process (qp, op, 2, 8, 8);

                    for (int i = 0; i < 8; ++i)
                        staysWarm = staysWarm
                                      && juce::approximatelyEqual (outL[(size_t) i], (float) (i + 1))
                                      && juce::approximatelyEqual (outR[(size_t) i],
                                                                   (float) (i + 1) + 1000.0f);

                    if (ask > biggest) { biggest = ask; biggestHost = h; biggestPack = p; }
                }

            report ("the wire grows to whatever the geometry asks, for every rate pair", grows,
                    juce::String (pairs) + " pairs, largest "
                      + juce::String (biggestHost, 0) + " Hz on a " + juce::String (biggestPack, 0)
                      + " Hz pack: " + juce::String (biggest) + " samples");

            report ("...and comes out of the growth still holding what it had heard", staysWarm);

            // The table F28 measured, kept as a REGRESSION and printed: what the old constant
            // would have handed back at each of these session rates against a 48 kHz pack.
            std::printf ("\nwire: session   asks   old 64   short by   first null of that comb\n");
            bool oldWasShort = false;

            for (const double h : { 44100.0, 48001.0, 88200.0, 96000.0, 192000.0 })
            {
                const int ask   = stageLatency (h, 48000.0);
                const int old   = juce::jmin (ask, 64);
                const int shortBy = ask - old;
                oldWasShort = oldWasShort || shortBy > 0;

                std::printf ("      %8.0f %6d %8d %10d   %s\n", h, ask, old, shortBy,
                             shortBy > 0 ? juce::String (h / (2.0 * (double) shortBy), 1).toRawUTF8()
                                         : "none");
            }

            report ("the constant DID cut above 48 kHz, and nothing computes a ceiling now",
                    oldWasShort && grows);
        }

        // 2 · THE WIRE CARRIES EXACTLY WHAT IT IS ASKED FOR — a property over the accepted domain,
        //     swept: every session rate, delays from nothing to the whole capacity, block sizes
        //     coprime with the delay and shorter than it, one channel and two, separate buffers
        //     and in place. Hand-rolled index arithmetic across block seams is exactly the kind of
        //     code that is right until it is not.
        {
            // A signal with no period at all, so a wrong delay cannot look right: the ramp is
            // strictly increasing and each channel is offset, which also catches a wire that
            // crosses channels.
            const auto carries = [] (double host, int d, int chunk, int channels, bool inPlace) -> bool
            {
                Wire w;
                w.prepare (d);
                juce::ignoreUnused (host);

                const int n = 4 * chunk + 2 * d + 13;
                std::vector<std::vector<float>> in ((size_t) channels), out ((size_t) channels);

                for (int ch = 0; ch < channels; ++ch)
                {
                    in[(size_t) ch].resize ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        in[(size_t) ch][(size_t) i] = (float) (i + 1) + 1000.0f * (float) ch;
                    // In place starts as a copy of the input; out of place starts as something the
                    // wire must OVERWRITE. Seeding it with the input in both cases made the whole
                    // zero-delay branch optional: delete the copy and the test still passed.
                    out[(size_t) ch] = inPlace ? in[(size_t) ch]
                                               : std::vector<float> ((size_t) n, -12345.0f);
                }

                for (int at = 0; at < n; at += chunk)
                {
                    const int len = juce::jmin (chunk, n - at);
                    std::vector<const float*> rp ((size_t) channels);
                    std::vector<float*>       wp ((size_t) channels);

                    for (int ch = 0; ch < channels; ++ch)
                    {
                        rp[(size_t) ch] = (inPlace ? out : in)[(size_t) ch].data() + at;
                        wp[(size_t) ch] = out[(size_t) ch].data() + at;
                    }

                    w.process (rp.data(), wp.data(), channels, len, d);
                }

                if (w.everShortened())
                    return false;

                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < n; ++i)
                    {
                        const float want = i >= d ? (float) (i - d + 1) + 1000.0f * (float) ch : 0.0f;
                        if (! juce::approximatelyEqual (out[(size_t) ch][(size_t) i], want))
                            return false;
                    }

                return true;
            };

            static const double hosts[] { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            static const int    chunks[] { 1, 7, 32, 64, 100, 512 };

            bool ok = true;
            int  cases = 0;
            juce::String firstBad;

            for (const double h : hosts)
            {
                std::vector<int> delays { 0, 1, 2, 3, 63, 64, 65, 223, 224, 415, 416, 800 };

                for (const double p : { 8000.0, 22050.0, 32000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
                    delays.push_back (stageLatency (h, p));

                for (const int d : delays)
                    for (const int c : chunks)
                        for (const int ch : { 1, 2 })
                            for (const bool inPlace : { false, true })
                            {
                                ++cases;
                                if (! carries (h, d, c, ch, inPlace))
                                {
                                    ok = false;
                                    if (firstBad.isEmpty())
                                        firstBad = juce::String (h, 0) + " Hz, d=" + juce::String (d)
                                                     + ", block " + juce::String (c) + ", "
                                                     + juce::String (ch) + " ch"
                                                     + (inPlace ? ", in place" : "");
                                }
                            }
            }

            report ("a bypassed block carries the delay it would have had", ok,
                    ok ? juce::String (cases) + " configurations"
                       : "first failure: " + firstBad);
        }

        // 2b · A DELAY THAT MOVES. `delay` is an argument, not a setting: a model landing at
        //      another rate changes it between two blocks with nobody re-preparing anything, and
        //      the delay it changes FROM is now sixty-odd samples rather than four. So the history
        //      is a window of the whole capacity and the read is its tail — check that by moving
        //      the delay mid-stream and demanding the stream still be a delay of the input.
        {
            const auto movesCleanly = [] (double host, int d1, int d2, int chunk) -> bool
            {
                Wire w;
                w.prepare (juce::jmax (d1, d2));
                juce::ignoreUnused (host);

                const int n = 6 * chunk;
                std::vector<float> in ((size_t) n), out ((size_t) n);

                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = (float) (i + 1);

                for (int at = 0, k = 0; at < n; at += chunk, ++k)
                {
                    const int len = juce::jmin (chunk, n - at);
                    const int d   = k < 3 ? d1 : d2;      // the switch lands mid-stream
                    const float* r[1] { in.data() + at };
                    float*       o[1] { out.data() + at };
                    w.process (r, o, 1, len, d);
                }

                // Only the blocks AFTER the switch are asserted, and against the delay in force
                // there: what a delay does across the instant it changes is a product question,
                // but every sample after it has to be the input `d2` samples earlier.
                for (int i = 3 * chunk; i < n; ++i)
                {
                    const float want = i >= d2 ? (float) (i - d2 + 1) : 0.0f;
                    if (! juce::approximatelyEqual (out[(size_t) i], want))
                        return false;
                }

                return true;
            };

            bool ok = true;
            int cases = 0;

            for (const double h : { 44100.0, 96000.0, 192000.0 })
                for (const int a : { 0, 1, 61, 64, 96, 160 })
                    for (const int b : { 0, 1, 61, 64, 96, 160 })
                        for (const int chunk : { 32, 64, 512 })
                        {
                            ++cases;
                            ok = ok && movesCleanly (h, a, b, chunk);
                        }

            report ("a delay that changes between blocks lands on the right samples", ok,
                    juce::String (cases) + " switches");
        }

        // 2c · WHAT `reset()` IS FOR. The caller runs it on every block the wire is not carrying,
        //      so that a model landing minutes later does not open with a handful of samples from
        //      whenever the wire last ran. Nothing else in the suite would notice if it stopped.
        {
            Wire w;
            w.prepare (61);

            constexpr int d = 61, n = 256;
            std::vector<float> loud ((size_t) n, 1.0f), quiet ((size_t) n, 0.0f), out ((size_t) n);

            const float* loudIn[1] { loud.data() };
            const float* quietIn[1] { quiet.data() };
            float*       o[1] { out.data() };

            w.process (loudIn, o, 1, n, d);      // fill the history with something audible
            w.reset();
            w.process (quietIn, o, 1, n, d);     // and now silence in: silence out, ghosts included

            bool clean = true;
            for (int i = 0; i < n; ++i)
                clean = clean && juce::approximatelyEqual (out[(size_t) i], 0.0f);

            report ("reset really forgets — no ghost from the last time it ran", clean);
        }

        // 2d · A WARM WINDOW. The wire is fed on every block, including the ones where it carries
        //      nothing, because a window filled only while the delay is live is EMPTY the first
        //      time the delay becomes live — and hands out its own length in silence. That is not
        //      a corner: the boost ships switched off, so a model landing at another rate into a
        //      bypassed block is the ordinary way this happens.
        {
            Wire w;
            w.prepare (96);

            constexpr int n = 256, d = 96;
            std::vector<float> in ((size_t) n), out ((size_t) n);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = (float) (i + 1);

            const float* r0[1] { in.data() };
            const float* r1[1] { in.data() };
            float*       o[1]  { out.data() };

            w.advance (r0, 1, n);                       // a block where the delay was still zero
            w.process (r1, o, 1, n, d);                 // and now a model has landed

            // The block after the model lands must look BACK into what already went past, not into
            // silence: the first `d` samples out are the last `d` samples of the block before.
            bool warm = true;
            for (int i = 0; i < d; ++i)
                warm = warm && juce::approximatelyEqual (out[(size_t) i], (float) (n - d + i + 1));

            report ("a wire fed while it carried nothing is warm when it starts to carry", warm,
                    "first sample out " + juce::String (out[0], 1) + ", wanted "
                      + juce::String ((float) (n - d + 1), 1));
        }

        // 2e · A CHANNEL THAT STOPPED BEING HANDED OVER. The chain drops to ONE channel in MONO,
        //      and a window that simply stops being written is not empty — it holds whatever the
        //      last STEREO stretch left in it. Feed one channel long enough to bury the other's
        //      window, come back to two, and the second one must be silent rather than replaying
        //      what it heard before the switch. (Clearing the wire used to sweep this up; feeding
        //      it is what put the ghost back, so this is the check that came with the cure.)
        {
            Wire w;
            w.prepare (96);

            constexpr int n = 256, d = 96;
            std::vector<float> loud ((size_t) n, 5.0f), ramp ((size_t) n), silent ((size_t) n, 0.0f);
            std::vector<float> outL ((size_t) n), outR ((size_t) n);

            for (int i = 0; i < n; ++i)
                ramp[(size_t) i] = (float) (i + 1);

            const float* stereo[2] { ramp.data(), loud.data() };
            const float* mono[1]   { ramp.data() };
            float*       out[2]    { outL.data(), outR.data() };

            w.advance (stereo, 2, n);                      // STEREO: the right channel is loud

            for (int k = 0; k * n < w.capacity() + n; ++k) // MONO for longer than the window
                w.advance (mono, 1, n);

            const float* back[2] { silent.data(), silent.data() };
            w.process (back, out, 2, n, d);                // and STEREO again, now carrying a delay

            bool quiet = true;
            for (int i = 0; i < n; ++i)
                quiet = quiet && juce::approximatelyEqual (outR[(size_t) i], 0.0f);

            report ("a channel that went away comes back silent, not haunted", quiet,
                    "right channel peaks at "
                      + juce::String (*std::max_element (outR.begin(), outR.end()), 3));
        }

        // 2f · RT-SAFETY, COUNTED RATHER THAN READ. Every allocation in this binary passes through
        //      a global `operator new` this file replaces, so these are not claims about the code,
        //      they are numbers. Two of them, because the law has two halves: `process` must not
        //      allocate at all, and `commit` — which runs with the audio callback's lock held —
        //      must not allocate either, or the callback ends up waiting on a heap.
        {
            Wire w;
            w.prepare (96);

            constexpr int n = 256;
            std::vector<float> a ((size_t) n, 0.25f), b ((size_t) n);
            const float* rp[2] { a.data(), a.data() };
            float*       wp[2] { b.data(), b.data() };

            w.process (rp, wp, 2, n, 96);                     // once to touch every path first

            const long long beforeProcess = gAllocations;
            for (int i = 0; i < 500; ++i)
            {
                w.process (rp, wp, 2, n, 96);
                w.advance (rp, 2, n);
                w.process (rp, wp, 2, n, 0);
            }
            const long long inProcess = gAllocations - beforeProcess;

            const bool owed = w.reserve (4096);               // ALLOCATES — and is meant to
            const long long beforeAdopt = gAllocations;
            w.process (rp, wp, 2, n, 96);                     // the audio thread takes the growth
            const long long inAdopt = gAllocations - beforeAdopt;

            std::printf ("\nwire: 1500 process/advance calls allocated %lld times; the block that"
                         " ADOPTS a growth allocated %lld\n", inProcess, inAdopt);

            report ("process allocates nothing, counted",
                    ORBITAMP_COUNT_ALLOCATIONS == 0 || inProcess == 0,
                    juce::String (inProcess) + " allocations in 1500 calls");
            report ("the audio thread adopts a growth without allocating, counted",
                    owed && w.capacity() == 4096
                      && (ORBITAMP_COUNT_ALLOCATIONS == 0 || inAdopt == 0),
                    juce::String (inAdopt) + " allocations, capacity now "
                      + juce::String (w.capacity()));
        }

        // 2g · THE RESIDUAL, MEASURED — and measured as a STEP, which is what a listener hears.
        //      Growth brings the old window across, so a delay born inside what the wire already
        //      remembered is seamless; born LONGER, it reaches further back than the wire has ever
        //      heard, and those samples are silence.
        //
        //      🔴 TWO CEILINGS, BOTH FROM CONSTRUCTION, and the first version of this block had
        //      only one and had it wrong. Coming OUT of the hole is a jump from zero to a sample of
        //      magnitude at most one: ceiling 1.0, which is 0 dBFS. But a delay that CHANGES at all
        //      — cold window or not — can put +1 next to −1, and that is a jump of 2.0, +6.02 dBFS.
        //      Reporting only the first would have called the smaller number the worst case.
        {
            constexpr int remembered = 64, born = 96, n = 256;
            const int cold = born - remembered;

            // The step across an index, in dBFS relative to full scale. `20·log10(2)` is +6.02, so
            // this scale says exactly what the ceilings above say.
            const auto stepDb = [] (float a, float b)
            {
                return 20.0 * std::log10 (std::max (1.0e-12, (double) std::abs (a - b)));
            };

            double worstOut = -200.0, worstWarm = -200.0;
            int    coldSamples = -1;
            bool   coldIsConstant = true;

            for (int phase = 0; phase < 360; ++phase)
            {
                std::vector<float> sig ((size_t) n), out ((size_t) n), warm ((size_t) n);
                for (int i = 0; i < n; ++i)
                    sig[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * (0.01 * (double) i
                                                             + (double) phase / 360.0));

                const float* sp[1] { sig.data() };

                // (a) THE COLD CASE: grown past what it remembered.
                {
                    Wire w;
                    w.prepare (remembered);
                    w.advance (sp, 1, n);

                    if (w.reserve (born))
                        takeGrowth (w);

                    std::vector<float> quiet ((size_t) n, 0.0f);
                    const float* qp[1] { quiet.data() };
                    float*       op[1] { out.data() };
                    w.process (qp, op, 1, n, born);

                    int silent = 0;
                    while (silent < n && juce::approximatelyEqual (out[(size_t) silent], 0.0f))
                        ++silent;

                    if (coldSamples < 0) coldSamples = silent;
                    coldIsConstant = coldIsConstant && silent == coldSamples;   // EVERY phase, not the first

                    if (silent > 0 && silent < n)
                        worstOut = std::max (worstOut, stepDb (out[(size_t) silent],
                                                               out[(size_t) (silent - 1)]));
                }

                // (b) THE WARM CASE: the same delay change with nothing cold about it. This is the
                //     discontinuity the wire has always had and did not invent, and it is the
                //     bigger of the two — worth publishing next to the other rather than omitted.
                //
                //     🔴 ITS SIGNAL IS CHOSEN SO THE CEILING IS REACHABLE, and that is the fixture
                //     doing its job rather than the threshold being loosened. At the seam the wire
                //     puts x[0] next to x[n-1], so the step is |x[0] − x[n-1]| and the sweep can
                //     only reach 2·|sin(pi·f·(n-1))| — with an arbitrary frequency that is a number
                //     like 1.975, and calling THAT the worst case would have been a fixture's
                //     accident published as a bound. Half a cycle across those n-1 samples makes
                //     the two ends antiphase, and then the ceiling is the topological one.
                {
                    std::vector<float> anti ((size_t) n);
                    for (int i = 0; i < n; ++i)
                        anti[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                               * (0.5 * (double) i / (double) (n - 1)
                                                                  + (double) phase / 360.0));

                    const float* ap[1] { anti.data() };

                    Wire w;
                    w.prepare (born);
                    w.advance (ap, 1, n);

                    float* wp[1] { warm.data() };
                    w.process (ap, wp, 1, n, 0);        // no delay…
                    w.process (ap, wp, 1, n, born);     // …and then all of it, between two blocks

                    for (int i = 1; i < n; ++i)
                        worstWarm = std::max (worstWarm, stepDb (warm[(size_t) i], warm[(size_t) (i - 1)]));
                }
            }

            std::printf ("\nwire: a delay born %d deep into a wire that remembered %d — %d cold"
                         " samples on every one of 360 phases\n"
                         "      worst step OUT of the hole   %8.4f dBFS   (ceiling  0.00, a jump from"
                         " zero to |x| <= 1)\n"
                         "      worst step of the CHANGE     %8.4f dBFS   (ceiling +6.02, +1 against"
                         " -1; not new, and the larger)\n",
                         born, remembered, coldSamples, worstOut, worstWarm);

            report ("the residual is exactly the part the wire never heard, on every phase",
                    coldSamples == cold && coldIsConstant,
                    juce::String (coldSamples) + " cold, arithmetic says " + juce::String (cold));

            // BOTH BOUNDS. Reached, so the number is a size and not a lower bound; and not
            // exceeded, so the construction ceiling is a ceiling and not a hope.
            report ("the step out of the hole reaches its ceiling and does not pass it",
                    worstOut > -0.01 && worstOut <= 0.0001,
                    juce::String (worstOut, 4) + " dBFS against 0.0000");

            report ("the step of the change itself reaches +6.02 and does not pass it",
                    worstWarm > 6.0 && worstWarm <= 6.0206,
                    juce::String (worstWarm, 4) + " dBFS against 6.0206");
        }

        // 2h · THE ABSURD ASK. `maxSaneDelay` is the one number left in the class, and it is not a
        //      bound on what a rate match can cost — nothing bounds that — but a bound on believing
        //      a figure. Nothing checked it, so "refuses out loud" was a claim about a path with no
        //      test on it.
        {
            Wire w;
            w.prepare (Wire::maxSaneDelay + 1000);
            const bool cappedAtPrepare = w.capacity() == Wire::maxSaneDelay;

            Wire g;
            g.prepare (64);
            const bool grewToTheCap = g.reserve (Wire::maxSaneDelay * 4);
            g.advance (nullptr, 0, 0);

            std::vector<float> x (128, 1.0f);
            const float* rp[1] { x.data() };
            float*       wp[1] { x.data() };
            g.process (rp, wp, 1, 128, Wire::maxSaneDelay * 4);

            report ("an absurd delay is refused at the cap, and said out loud",
                    cappedAtPrepare && grewToTheCap
                      && g.capacity() == Wire::maxSaneDelay && g.everShortened(),
                    "capped at " + juce::String (g.capacity()) + ", latch "
                      + juce::String (g.everShortened() ? "set" : "clear"));
        }

        // 3 · THE REFUSAL IS VISIBLE. There is no domain any more, so this is no longer about a
        //     pack rate falling out of one: what remains is a wire asked for more than it has YET
        //     grown to. What may never happen again is a refusal nobody can see.
        {
            Wire w;
            w.prepare (224);          // a stated capacity, so "more than it carries" has a meaning

            constexpr int n = 256;
            std::vector<float> in ((size_t) n), out ((size_t) n);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = (float) (i + 1);

            const float* rp[1] { in.data() };
            float*       wp[1] { out.data() };

            const bool freshIsQuiet = ! w.everShortened();

            w.process (rp, wp, 1, n, w.capacity());
            const bool legalStaysQuiet = ! w.everShortened();

            w.process (rp, wp, 1, n, w.capacity() + 1);
            const bool refusalSpeaks = w.everShortened();

            // AND WHAT IT DOES WHILE REFUSING: the most delay it has, not none of it. Handing back
            // zero would be a comb twice as deep as the one it is short by, and the flag alone
            // would not tell them apart.
            const int cap = w.capacity();
            bool carriesWhatItHas = true;
            for (int i = cap; i < n; ++i)
                carriesWhatItHas = carriesWhatItHas
                                     && juce::approximatelyEqual (out[(size_t) i], (float) (i - cap + 1));

            // The latch is a LATCH: a legal call afterwards must not wipe it, or the message
            // thread that reads it a tick later learns nothing.
            w.process (rp, wp, 1, n, 4);
            const bool latchHolds = w.everShortened();

            w.prepare (224);
            const bool prepareClears = ! w.everShortened();

            report ("a wire asked for more than it carries says so",
                    freshIsQuiet && legalStaysQuiet && refusalSpeaks && latchHolds && prepareClears,
                    "capacity " + juce::String (cap));

            report ("...and still carries every sample it does have", carriesWhatItHas);
        }

        // 4 · THE COMB — the thing this class exists to prevent, measured rather than reasoned
        //     about. The block's output is late by the model's own latency; the bypass copy is
        //     late by whatever the wire hands back. Aligned, the sum is one signal twice over and
        //     the response is flat. Short by `r`, it is 0.5·x(t-lat) + 0.5·x(t-lat+r), whose
        //     magnitude is |cos(pi·f·r/fs)| — a comb with its first null at fs/(2r).
        //
        //     🔴 THE FIXTURE IS ASSERTED LIVE BEFORE ANY OF IT IS BELIEVED: that the model delay
        //     is not zero, that the dry half is really in the sum, and that this instrument can
        //     SEE a comb when it is shown one. A flat sweep from an instrument that measures
        //     nothing is the failure P34 caught in its own bench, not a pass.
        {
            // Every probe is a whole number of periods long, so the rms is exact and no window,
            // bin or bucket is involved: the grid is 20 Hz and a probe is one twentieth of a
            // second, which is `k` periods of `20·k` Hz exactly, at any session rate here.
            constexpr int binHz = 20;

            const auto responseDb = [] (double fs, int modelDelay, int askOfWire, int k) -> double
            {
                const int n     = (int) (fs / (double) binHz);
                const int prime = modelDelay + askOfWire + 64;
                const int total = n + prime;
                const double hz = (double) binHz * (double) k;

                std::vector<float> x ((size_t) total), dry ((size_t) total);

                for (int i = 0; i < total; ++i)
                    x[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * hz * (double) i / fs);

                Wire w;
                w.prepare (askOfWire);

                for (int at = 0; at < total; at += 256)
                {
                    const int len = juce::jmin (256, total - at);
                    const float* r[1] { x.data() + at };
                    float*       o[1] { dry.data() + at };
                    w.process (r, o, 1, len, askOfWire);
                }

                double sum = 0.0, ref = 0.0;

                for (int i = prime; i < total; ++i)
                {
                    const double wet = 0.5 * (double) x[(size_t) (i - modelDelay)];
                    const double s   = wet + 0.5 * (double) dry[(size_t) i];
                    sum += s * s;
                    ref += (double) x[(size_t) i] * (double) x[(size_t) i];
                }

                return 20.0 * std::log10 (std::sqrt (sum / juce::jmax (1.0e-30, ref)));
            };

            // The same thing in closed form — a SECOND oracle of a different construction, because
            // one oracle is one grid. |0.5 + 0.5·e^{-j·2pi·f·r/fs}| = |cos(pi·f·r/fs)|.
            const auto predictedDb = [] (double fs, int residual, int hz)
            {
                const double c = std::cos (juce::MathConstants<double>::pi * (double) hz
                                             * (double) residual / fs);
                return 20.0 * std::log10 (std::max (1.0e-12, std::abs (c)));
            };

            constexpr int kLow = 100 / binHz, kHigh = 10000 / binHz;   // 100 Hz ... 10 kHz

            // WHERE THE FIRST NULL IS, found as a null and not as a corner: the first run of bins
            // that goes past -20 dB, and the deepest bin inside it. Zero means the sweep never
            // dipped that far — which is what a wire the right length is supposed to give.
            const auto firstNullHz = [&] (double fs, int modelDelay, int askOfWire)
            {
                for (int k = kLow; k <= kHigh; ++k)
                    if (responseDb (fs, modelDelay, askOfWire, k) < -20.0)
                    {
                        int best = k;
                        double deepest = responseDb (fs, modelDelay, askOfWire, k);

                        for (int j = k + 1; j <= kHigh; ++j)
                        {
                            const double db = responseDb (fs, modelDelay, askOfWire, j);
                            if (db >= -20.0)
                                break;
                            if (db < deepest) { deepest = db; best = j; }
                        }

                        return best * binHz;
                    }

                return 0;
            };

            std::printf ("\nwire: the comb, swept 100 Hz - 10 kHz on a %d Hz grid\n", binHz);
            std::printf ("      session   model   no wire at all      the old 64        the wire now\n");

            bool preconditionsHold = true, flatEverywhere = true, oracleAgrees = true;
            juce::String why;

            for (const double fs : { 44100.0, 96000.0 })
            {
                const int lat = stageLatency (fs, 48000.0);
                const int old = juce::jmin (lat, 64);          // what the constant used to allow

                // PRECONDITION A: there IS a rate match. A fixture whose model delay is zero is
                // flat for the wrong reason and proves nothing at all.
                if (lat <= 0)
                { preconditionsHold = false; why = "model delay is zero — the fixture is blind"; }

                // PRECONDITION B: the dry half is really in the sum. Drop it and the sum is the
                // wet alone, 6.02 dB down; if this reads 0 dB the two halves are not being added.
                const double both    = responseDb (fs, lat, lat, kLow);
                const double wetOnly = 20.0 * std::log10 (0.5);
                if (! (both - wetOnly > 5.5 && both - wetOnly < 6.5))
                { preconditionsHold = false; why = "the dry path is not in the sum"; }

                // PRECONDITION C: this instrument can see a comb. The old clamp is driven through
                // the very same measurement, and its null had better land where the arithmetic
                // says it does.
                const int    residual   = lat - old;
                const int    nullBefore = residual > 0 ? firstNullHz (fs, lat, old) : 0;
                const double predicted  = residual > 0 ? fs / (2.0 * (double) residual) : 0.0;

                if (residual > 0
                     && ! (nullBefore > 0 && std::abs ((double) nullBefore - predicted) <= 1.5 * binHz))
                { preconditionsHold = false; why = "the instrument cannot see a comb it is shown"; }

                // 🔴 ACROSS THE WHOLE SWEEP, not at one bin. It used to check a single bin at
                //    3 kHz, and 32 samples at 96 kHz is exactly one period of 3 kHz — so both
                //    oracles said 0 dB and would have gone on saying it with the closed form
                //    replaced by `return 0`. An oracle sampled at the period of the thing it
                //    measures does not measure it.
                if (residual > 0)
                    for (int k = kLow; k <= kHigh; ++k)
                    {
                        const double measured = responseDb (fs, lat, old, k);
                        const double want     = predictedDb (fs, residual, k * binHz);

                        // Only where the closed form is not falling off a cliff: within a bin of a
                        // null the two disagree by however steep the null is, which is arithmetic
                        // about the grid rather than about the wire.
                        if (want > -20.0 && std::abs (measured - want) > 0.05)
                        { oracleAgrees = false; break; }
                    }

                // AND THE ANSWER: with the wire the length of the block, nothing MOVES — in
                // either direction. Taking the minimum alone called a wire that delivered the
                // right delay at half again the gain "flat", because +3.5 dB is not a dip.
                double worst = 0.0;
                for (int k = kLow; k <= kHigh; ++k)
                    worst = std::min (worst, -std::abs (responseDb (fs, lat, lat, k)));

                const int nullNow = firstNullHz (fs, lat, lat);
                flatEverywhere = flatEverywhere && worst > -0.001 && nullNow == 0;

                // AND THE SAME MEASUREMENT WITH NO WIRE AT ALL, which is what a bypassed block
                // was before this class existed and what the stale comment beside the constant
                // described as "six samples, first notch near 3.7 kHz". It is not: at 44.1 kHz
                // against a 48 kHz pack it is sixty-one samples and the notch is in the body of
                // the guitar. This row is why the class is here at all, and it gives 44.1 kHz —
                // the one rate the old constant still fitted — a number of its own.
                const int    nullNone      = firstNullHz (fs, lat, 0);
                const double predictedNone = fs / (2.0 * (double) lat);

                if (! (nullNone > 0 && std::abs ((double) nullNone - predictedNone) <= 1.5 * binHz))
                { preconditionsHold = false; why = "no wire at all does not comb — the fixture is blind"; }

                const auto cell = [] (int hz, double want)
                {
                    return hz > 0 ? juce::String (hz) + " Hz (calc " + juce::String (want, 1) + ")"
                                  : juce::String ("no null");
                };

                std::printf ("      %7.0f %7d   %-18s %-18s %s, worst %.4f dB\n",
                             fs, lat,
                             cell (nullNone, predictedNone).toRawUTF8(),
                             residual > 0 ? cell (nullBefore, predicted).toRawUTF8()
                                          : "fitted, no null",
                             cell (nullNow, 0.0).toRawUTF8(), worst);
            }

            report ("the comb fixture is live: delay, dry path, and a comb it can see",
                    preconditionsHold, why);
            report ("the closed form agrees with the swept measurement", oracleAgrees);
            report ("with the wire the block's length, the blend has no comb", flatEverywhere);
        }

    }

    // A CABINET IR OF THE PLAYER'S OWN travels with the state, whole. Picked from a file, saved with
    // a session — from a register that is not even the active one — and the file thrown away: a
    // fresh instance on the same machine has to play the very same bytes, and must never have held
    // them in the tree the history copies every tick. Needs no pack, so it runs in front of the gate.
    {
        const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("orbitamp-chain-ir").getNonexistentSibling();
        work.createDirectory();

        const auto wavFile = work.getChildFile ("My Cab.wav");
        {
            juce::AudioBuffer<float> shot (1, 2400);
            for (int i = 0; i < shot.getNumSamples(); ++i)
                shot.setSample (0, i, (float) (std::exp (-i / 300.0) * std::cos (i * 0.07)));

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);

            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                               .withSampleRate (sampleRate)
                                                               .withNumChannels (1)
                                                               .withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (shot, 0, shot.getNumSamples());
        }

        juce::MemoryBlock original;
        wavFile.loadFileAsData (original);

        // A minute of audio fits the byte cap easily and is no cabinet: millions of taps.
        const auto tooLong = work.getChildFile ("Song.wav");
        {
            juce::AudioBuffer<float> song (1, (int) (orbitamp::core::CabinetIr::maxSeconds * 8000.0) + 8000);
            song.clear();
            song.setSample (0, 0, 1.0f);

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (tooLong);

            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                               .withSampleRate (8000.0)
                                                               .withNumChannels (1)
                                                               .withBitsPerSample (16)))
                writer->writeFromAudioSampleBuffer (song, 0, song.getNumSamples());
        }

        const auto notAudio = work.getChildFile ("readme.wav");
        notAudio.replaceWithText ("not an impulse");

        const auto a = std::make_unique<orbitamp::AmpProcessor>();
        a->inlineLoads = true;
        a->prepareToPlay (sampleRate, blockSize);

        report ("a file that is not audio is not chosen",   ! a->chooseCabFile (notAudio) && ! a->cabChoice().isUser());
        report ("an IR longer than a cabinet is not chosen",  ! a->chooseCabFile (tooLong) && ! a->cabChoice().isUser());
        report ("a player's own IR is chosen",               a->chooseCabFile (wavFile) && a->cabChoice().isUser()
                                                               && a->cabChoice().name == "My Cab");
        a->pumpDeviceWork();

        const auto preset = a->presetForSaving();
        report ("a preset carries it whole",                 preset.getChildWithName ("EmbeddedIRs").getNumChildren() == 1);
        report ("...and the live tree never does",           ! a->apvts.state.getChildWithName ("EmbeddedIRs").isValid());

        // Into register B, then register A goes back to the shelf: the only state naming the file
        // is one the history keeps out of sight.
        a->history.copyRegister (a->history.active(), 1);
        a->chooseCabFactory (3);
        report ("the shelf takes over again",                ! a->cabChoice().isUser() && a->cabChoice().factory == 3);

        juce::MemoryBlock session;
        a->getStateInformation (session);
        wavFile.deleteFile();

        const auto b = std::make_unique<orbitamp::AmpProcessor>();
        b->inlineLoads = true;
        b->prepareToPlay (sampleRate, blockSize);
        b->setStateInformation (session.getData(), (int) session.getSize());

        report ("a session reopens on register A's shelf IR", ! b->cabChoice().isUser() && b->cabChoice().factory == 3);
        report ("...holding no bytes in its tree",           ! b->apvts.state.getChildWithName ("EmbeddedIRs").isValid());

        b->history.switchTo (1);
        const auto choice = b->cabChoice();
        report ("register B plays the file, which is gone",  choice.isUser() && choice.bytes != nullptr
                                                               && *choice.bytes == original && choice.name == "My Cab");

        report ("a factory reset forgets it",                b->stateForSaving().hasProperty (orbitamp::params::cabIrUserKey)
                                                               && ! b->stateForSaving (true).hasProperty (orbitamp::params::cabIrUserKey));

        // ...and what comes out is that IR, not the last factory one: through the cabinet alone —
        // ALONE, and that is not a figure of speech. A captured block with no pack to play is
        // silent, and a runner has no packs: with the preamp left in the rig both runs were
        // silence, and silence compares as "0.0 % apart". The captured blocks stand out.
        //
        // The convolver also takes a new impulse on its own thread and swaps it in from the audio
        // thread's side, crossfading, at the machine's pace — so each case gets most of a second
        // of audio before it is measured.
        const auto settleCabinet = [&] (orbitamp::AmpProcessor& p)
        {
            juce::AudioBuffer<float> silence (2, blockSize);
            juce::MidiBuffer none;
            for (int i = 0; i < 150; ++i)
            {
                silence.clear();
                p.processBlock (silence, none);
                p.pumpDeviceWork();
                juce::Thread::sleep (5);
            }
        };

        set (*b, orbitamp::params::stereoMode, 0.0f);
        set (*b, orbitamp::params::cabOn, 1.0f);
        set (*b, orbitamp::params::boostPresent,  0.0f);
        set (*b, orbitamp::params::preampPresent, 0.0f);
        settleCabinet (*b);
        const auto own = run (*b);
        b->chooseCabFactory (0);
        settleCabinet (*b);
        const auto shelf = run (*b);
        report ("the player's IR is what sounds",            differencePercent (own, shelf) > 1.0,
                juce::String (differencePercent (own, shelf), 1) + " % apart");

        // THE AUTOMATION LANE WINS over a player's IR — and a recall is not a lane. A host moving
        // `cab_ir` lets the player's IR go; a session restored with the player's IR and a different
        // number underneath keeps playing the player's IR.
        {
            const auto h = std::make_unique<orbitamp::AmpProcessor>();
            h->inlineLoads = true;
            h->prepareToPlay (sampleRate, blockSize);
            h->pumpDeviceWork();

            juce::MemoryBlock ownIr;
            {
                juce::AudioBuffer<float> shot (1, 2400);
                for (int i = 0; i < shot.getNumSamples(); ++i)
                    shot.setSample (0, i, (float) (std::exp (-i / 200.0) * std::cos (i * 0.05)));
                const auto f = work.getChildFile ("Lane.wav");
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
                if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                                   .withSampleRate (sampleRate)
                                                                   .withNumChannels (1)
                                                                   .withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (shot, 0, shot.getNumSamples());
                h->chooseCabFile (f);
            }
            h->pumpDeviceWork();

            juce::MemoryBlock session;
            h->getStateInformation (session);   // the player's IR, over factory index 7

            set (*h, orbitamp::params::cabIr, 4.0f);   // the lane
            h->pumpDeviceWork();
            report ("a host moving cab_ir lets the player's IR go", ! h->cabChoice().isUser() && h->cabChoice().factory == 4);

            const auto r = std::make_unique<orbitamp::AmpProcessor>();
            r->inlineLoads = true;
            r->prepareToPlay (sampleRate, blockSize);
            set (*r, orbitamp::params::cabIr, 2.0f);
            r->pumpDeviceWork();
            r->setStateInformation (session.getData(), (int) session.getSize());
            r->pumpDeviceWork();
            r->pumpDeviceWork();
            report ("...a restored session is not a lane",      r->cabChoice().isUser());
        }

        work.deleteRecursively();
    }

    // THE DELAY COUNTS IN THE SESSION'S TEMPO unless told to keep its own. A quarter note at the
    // host's 100 BPM is 600 ms; the block's own 120 is 500; and a host that reports no tempo leaves
    // the own BPM conducting whatever the switch says. Read off the stage's shown time once its glide
    // has arrived — the motor slides to a new time rather than jumping. Needs no pack.
    {
        struct Head final : juce::AudioPlayHead
        {
            std::optional<double> bpm;

            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                if (bpm.has_value())
                    info.setBpm (*bpm);
                return info;
            }
        } head;

        const auto d = std::make_unique<orbitamp::AmpProcessor>();
        d->prepareToPlay (sampleRate, blockSize);
        d->setPlayHead (&head);

        set (*d, orbitamp::params::delaySync, 1.0f);
        set (*d, orbitamp::params::delayDiv, 5.0f);   // 1/4
        set (*d, orbitamp::params::delayBpm, 120.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        // Blocks until the glide stops moving: ten seconds of them at the most.
        const auto block = [&]
        {
            float last = -1.0f;
            for (int i = 0; i < (int) (10.0 * sampleRate / blockSize); ++i)
            {
                buf.clear();
                d->processBlock (buf, midi);

                const float now = d->delayTaps().shownTimeMs.load();
                if (std::abs (now - last) < 0.001f)
                    break;
                last = now;
            }
        };
        const auto timeMs = [&] { return d->delayTaps().shownTimeMs.load(); };

        head.bpm = 100.0;
        block();
        report ("a quarter note follows the host's 100 BPM",  std::abs (timeMs() - 600.0f) < 0.5f
                                                                && std::abs (d->hostTempoBpm() - 100.0f) < 0.01f,
                juce::String (timeMs(), 1) + " ms");

        set (*d, orbitamp::params::delayHostTempo, 0.0f);
        block();
        report ("...and its own 120 when told to keep it",   std::abs (timeMs() - 500.0f) < 0.5f,
                juce::String (timeMs(), 1) + " ms");

        set (*d, orbitamp::params::delayHostTempo, 1.0f);
        head.bpm.reset();
        block();
        report ("a host with no tempo leaves its own conducting", std::abs (timeMs() - 500.0f) < 0.5f
                                                                   && d->hostTempoBpm() == 0.0f,
                juce::String (timeMs(), 1) + " ms");

        d->setPlayHead (nullptr);
    }

    // ONE BAD SAMPLE MUST NOT SILENCE THE ECHO FOR EVER. The line is recursive: a NaN or an
    // infinity recorded into it used to go round for good, every sample after it not a number,
    // until the delay left the rig. Stage alone, host-sized blocks: a quiet tone, one poisoned
    // sample, the tone again — everything after the poisoned block a number, and repeats back.
    {
        for (const float poison : { std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity() })
        {
            orbitamp::core::DelayStage echo;
            echo.prepare (sampleRate, blockSize);
            echo.setTimeMs (120.0f);
            echo.setRepeats (0.6f);
            echo.setMix (0.5f);

            juce::AudioBuffer<float> buf (2, blockSize);
            long long phase = 0;
            int bad = 0;
            double lateWet = 0.0;

            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float s = b < 150 ? 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                        * 220.0 * (double) phase / sampleRate)
                                            : 0.0f;
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }

                if (b == 20)
                    buf.setSample (0, 100, poison), buf.setSample (1, 100, poison);

                echo.process (buf.getArrayOfWritePointers(), 2, blockSize);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const float v = buf.getSample (ch, i);
                        if (b > 20 && ! std::isfinite (v))
                            ++bad;
                        if (b >= 150 && b < 160)   // the input has stopped: what sounds is repeats
                            lateWet = std::max (lateWet, (double) std::abs (v));
                    }
            }

            const auto what = std::isnan (poison) ? "a NaN" : "an infinity";
            report ((juce::String ("delay: ") + what + " in, every sample after it a number").toRawUTF8(),
                    bad == 0, juce::String (bad) + " non-finite");
            report ("...and the repeats come back", lateWet > 1.0e-3, juce::String (lateWet, 4));
        }
    }

    // THE CABINET, one bad sample in: every sample after it is a number AT ONCE — not after the
    // impulse has carried it out, which is what macOS's convolver does, and not never, which is
    // what Windows' did. The shipping IR, a quiet tone, one NaN, the tone again.
    {
        orbitamp::core::CabinetIr cabinet;
        cabinet.prepare (sampleRate, blockSize, 2);
        const auto& shelf = orbitamp::AmpProcessor::cabIrBytes (orbitamp::params::cabIrDefault);
        cabinet.load (shelf.data, (size_t) shelf.size);

        juce::AudioBuffer<float> buf (2, blockSize);
        long long phase = 0;
        int bad = 0;
        double late = 0.0;

        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float s = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 220.0 * (double) phase / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            if (b == 100)
                buf.setSample (0, 100, std::numeric_limits<float>::quiet_NaN()),
                buf.setSample (1, 100, std::numeric_limits<float>::quiet_NaN());

            cabinet.process (buf.getArrayOfWritePointers(), 2, blockSize, true);
            juce::Thread::sleep (b < 100 ? 2 : 0);   // the impulse loads on the convolver's own thread

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (b >= 100 && ! std::isfinite (buf.getSample (ch, i)))
                        ++bad;

            if (b >= 390)
                late = std::max (late, (double) buf.getRMSLevel (0, 0, blockSize));
        }

        report ("cabinet: a NaN in, every sample after it a number", bad == 0, juce::String (bad) + " non-finite");
        report ("...and the cabinet still sounds",                 late > 1.0e-3, juce::String (late, 4) + " rms");
    }

    // THE CABINET'S CONVOLVER — felitronics-core's CabConvolver — kept as the cabinet it replaced.
    {
        const auto& shelfIr = orbitamp::AmpProcessor::cabIrBytes (orbitamp::params::cabIrDefault);
        constexpr int bs = 512;

        // Blocks of a signal through a cabinet; `on` false is the chain's out-of-the-path call.
        const auto blocks = [] (orbitamp::core::CabinetIr& cab, int count, bool on, bool tone, std::vector<float>* keep = nullptr)
        {
            juce::AudioBuffer<float> buf (2, bs);
            static long long phase = 0;
            for (int b = 0; b < count; ++b)
            {
                for (int i = 0; i < bs; ++i, ++phase)
                {
                    const float v = tone ? 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) phase / sampleRate) : 0.0f;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                if (on)
                    cab.process (buf.getArrayOfWritePointers(), 2, bs, true);
                else
                    cab.idle (bs);
                cab.flushPending();
                if (keep != nullptr && on)
                    for (int i = 0; i < bs; ++i)
                        keep->push_back (buf.getSample (0, i));
            }
        };

        // A NEW CABINET HANDED OVER WHILE IT STANDS OUT OF THE PATH lands. The convolver's reset
        // cancels a handover it has not picked up, so a cabinet reset every block while bypassed
        // never took the IR it was given and played silence when it came back.
        {
            orbitamp::core::CabinetIr cab;
            cab.prepare (sampleRate, bs, 2);
            blocks (cab, 20, true, true);                                  // it has sounded: history to forget
            cab.load (shelfIr.data, (size_t) shelfIr.size);                // a pick, while...
            blocks (cab, 300, false, false);                               // ...it stands out of the path
            std::vector<float> back;
            blocks (cab, 60, true, true, &back);
            double level = 0.0;
            for (size_t i = back.size() / 2; i < back.size(); ++i)
                level = std::max (level, (double) std::abs (back[i]));
            report ("cabinet: a pick made while it stood down plays when it returns", level > 0.05,
                    juce::String (level, 3) + " peak");
        }

        // NO GHOST: standing down long enough forgets what it heard, so returning on silence is silence.
        {
            orbitamp::core::CabinetIr cab;
            cab.prepare (sampleRate, bs, 2);
            cab.load (shelfIr.data, (size_t) shelfIr.size);
            blocks (cab, 100, true, true);                                  // loud, with a long tail
            blocks (cab, 300, false, false);                               // stood down past the impulse
            std::vector<float> back;
            blocks (cab, 4, true, false, &back);
            double ghost = 0.0;
            for (float v : back)
                ghost = std::max (ghost, (double) std::abs (v));
            report ("...and nothing of before comes back with it", ghost < 1.0e-6, juce::String (ghost, 8) + " peak");
        }

        // ONE RESPONSE AT EVERY SESSION RATE. The IR is recorded at 48 kHz; the convolver resamples it,
        // and a resampled impulse's gain grows with the ratio unless it is taken out — +6 dB at 96.
        {
            const auto gainAt220 = [&] (double sr)
            {
                orbitamp::core::CabinetIr cab;
                cab.prepare (sr, bs, 2);
                cab.load (shelfIr.data, (size_t) shelfIr.size);
                juce::AudioBuffer<float> buf (2, bs);
                double in = 0.0, out = 0.0;
                long long ph = 0;
                // Two seconds in before measuring — past the whole impulse at any rate, since a block at
                // 96 kHz is half the time it is at 48 — then one second measured.
                const int warm = (int) (2.0 * sr / bs), total = warm + (int) (sr / bs);
                for (int b = 0; b < total; ++b)
                {
                    for (int i = 0; i < bs; ++i, ++ph)
                    {
                        const float v = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) ph / sr);
                        buf.setSample (0, i, v); buf.setSample (1, i, v);
                        if (b >= warm) in += (double) v * v;
                    }
                    cab.process (buf.getArrayOfWritePointers(), 2, bs, true);
                    cab.flushPending();
                    if (b >= warm)
                        for (int i = 0; i < bs; ++i)
                            out += (double) buf.getSample (0, i) * buf.getSample (0, i);
                }
                return 10.0 * std::log10 (out / in);
            };

            const double at48 = gainAt220 (48000.0), at44 = gainAt220 (44100.0), at96 = gainAt220 (96000.0);
            report ("...and one response at 44.1, 48 and 96 kHz", std::abs (at44 - at48) < 0.1 && std::abs (at96 - at48) < 0.1,
                    juce::String (at44, 2) + " / " + juce::String (at48, 2) + " / " + juce::String (at96, 2) + " dB at 220 Hz");
        }
    }

    // ...AND ONE INFINITY MUST NOT MUTE THE SAFETY FOR EVER. The limiter's envelope took the peak
    // it heard, an infinity included, and no finite peak ever decays from that: the gain went to
    // zero and the plugin put out exact silence until the limiter was switched off. A quiet tone,
    // well under the ceiling, one infinite sample, the tone again: it must come back at unity.
    {
        orbitamp::core::SoftLimiter safety;
        safety.prepare (sampleRate);

        juce::AudioBuffer<float> buf (2, blockSize);
        long long phase = 0;
        double lateRms = 0.0;

        for (int b = 0; b < 100; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float s = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 220.0 * (double) phase / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            if (b == 20)
                buf.setSample (0, 100, std::numeric_limits<float>::infinity());

            safety.process (buf.getArrayOfWritePointers(), 2, blockSize, true, -1.0f);

            if (b == 99)
            {
                double sum = 0.0;
                for (int i = 0; i < blockSize; ++i)
                    sum += (double) buf.getSample (1, i) * buf.getSample (1, i);
                lateRms = std::sqrt (sum / blockSize);
            }
        }

        report ("limiter: an infinity in, the tone comes back at unity",
                std::abs (lateRms - 0.1 / std::sqrt (2.0)) < 0.002, juce::String (lateRms, 4) + " rms");
    }

    // ...and a huge FINITE spike — not a sound, a fault upstream — is met at its full height and
    // then let go of within half a second, not four.
    {
        orbitamp::core::SoftLimiter safety;
        safety.prepare (sampleRate);

        juce::AudioBuffer<float> buf (2, blockSize);
        long long phase = 0;
        float spikeOut = 0.0f;
        double rmsAfterHalfSecond = 0.0;
        const int spikeBlock = 20, checkBlock = spikeBlock + (int) (0.5 * sampleRate / blockSize) + 1;

        for (int b = 0; b <= checkBlock; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float s = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 220.0 * (double) phase / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            if (b == spikeBlock)
                buf.setSample (0, 100, 1.0e30f);

            safety.process (buf.getArrayOfWritePointers(), 2, blockSize, true, -1.0f);

            if (b == spikeBlock)
                spikeOut = buf.getSample (0, 100);

            if (b == checkBlock)
            {
                double sum = 0.0;
                for (int i = 0; i < blockSize; ++i)
                    sum += (double) buf.getSample (1, i) * buf.getSample (1, i);
                rmsAfterHalfSecond = std::sqrt (sum / blockSize);
            }
        }

        report ("limiter: a 1e30 spike is held to the lid",   std::abs (spikeOut) <= std::pow (10.0f, -1.0f / 20.0f) + 1.0e-4f,
                juce::String (spikeOut, 4));
        report ("...and let go within half a second",       std::abs (rmsAfterHalfSecond - 0.1 / std::sqrt (2.0)) < 0.002,
                juce::String (rmsAfterHalfSecond, 4) + " rms");
    }

    // THE WHOLE CHAIN, one bad sample at the door. Everything that holds state and needs no pack —
    // the gate, both EQ links, the delay, the reverb, the cabinet, the limiter — in the rig and on,
    // a quiet tone, one NaN on both channels, the tone again; the captured blocks stand out, since
    // without a pack they have nothing to play. Four seconds later every sample is a number and the
    // level is the level of the same run with no NaN in it — the delay and the room are still
    // building at that point, so the fair comparison is a twin, not the level before. The gate
    // needs nothing of its own for this: its key heals itself and its audio is a multiply, which is
    // what this is here to keep true.
    //
    // FOUR seconds, not one, because the cabinet DRAINS rather than heals: the convolution carries
    // what went in for exactly the length of the impulse — about 1.3 s for the shipping IR — and
    // then lets go by itself. That is a tail, not a latch, and measured as one: the late window
    // starts well past it.
    {
        int badEver = 0;   // over the whole run, every block from the poisoned one on

        const auto chainRun = [&] (bool poisoned, int& badLate)
        {
            const auto c = std::make_unique<orbitamp::AmpProcessor>();
            c->prepareToPlay (sampleRate, blockSize);

            set (*c, orbitamp::params::stereoMode, 0.0f);
            for (const auto* id : { orbitamp::params::gateOn, orbitamp::params::delayOn, orbitamp::params::reverbOn,
                                    orbitamp::params::cabOn, orbitamp::params::limiterOn,
                                    orbitamp::params::gatePresent, orbitamp::params::delayPresent,
                                    orbitamp::params::reverbPresent, orbitamp::params::cabPresent })
                set (*c, id, 1.0f);
            set (*c, orbitamp::params::boostPresent,  0.0f);
            set (*c, orbitamp::params::preampPresent, 0.0f);
            set (*c, orbitamp::params::gateThreshold, -70.0f);   // the tone stays open

            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            long long phase = 0;
            double late = 0.0;
            badLate = 0;

            for (int b = 0; b < 700; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float s = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * 220.0 * (double) phase / sampleRate);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }

                if (poisoned && b == 200)
                    buf.setSample (0, 100, std::numeric_limits<float>::quiet_NaN()),
                    buf.setSample (1, 100, std::numeric_limits<float>::quiet_NaN());

                c->processBlock (buf, midi);
                c->pumpDeviceWork();

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        if (! std::isfinite (buf.getSample (ch, i)))
                            ++badEver;

                if (b >= 600)
                {
                    double sum = 0.0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                        {
                            const float v = buf.getSample (ch, i);
                            if (! std::isfinite (v))
                                ++badLate;
                            else if (ch == 0)
                                sum += (double) v * v;
                        }

                    if (b >= 680)
                        late = std::max (late, std::sqrt (sum / blockSize));
                }
            }

            return late;
        };

        int cleanBad = 0, badLate = 0;
        const double clean = chainRun (false, cleanBad);
        const double after = chainRun (true,  badLate);
        const double db    = 20.0 * std::log10 (juce::jmax (1.0e-9, after) / juce::jmax (1.0e-9, clean));

        report ("the whole chain: a NaN at the door, later all numbers", badLate == 0,
                juce::String (badLate) + " non-finite");
        report ("...and not one sample that is not a number ever leaves the box", badEver == 0,
                juce::String (badEver) + " non-finite, the cabinet's drain included");
        report ("...and the level of the same run without it",  clean > 1.0e-3 && std::abs (db) < 1.0,
                juce::String (db, 2) + " dB");
    }

    // A BLOCK OF NO SAMPLES in the middle of a fade: nothing moves, and the fade is still a fade —
    // not finished, or the link fading out would be cleared with its tail still sounding.
    {
        orbitamp::core::BypassFade fade;
        fade.prepare (sampleRate);
        fade.snapTo (true);
        const auto first = fade.advance (64, false);   // under way, not done
        const auto empty = fade.advance (0, false);

        report ("a fade meets an empty block: it holds, still moving",
                first.moving && empty.moving && empty.ramp == 0
                    && juce::approximatelyEqual (empty.from, first.to) && juce::approximatelyEqual (empty.to, first.to),
                juce::String (empty.from, 3) + " -> " + juce::String (empty.to, 3));
    }

    // A HOST THAT SENDS MORE THAN IT PROMISED. Prepared for 512, handed 2048 at once, the chain
    // must play exactly what four blocks of 512 would have played — the room, the echo, the
    // cabinet and the fades all included. Two instances, the same tone, both ways.
    {
        const auto chained = [&] (bool oneBigBlock)
        {
            const auto c = std::make_unique<orbitamp::AmpProcessor>();
            c->prepareToPlay (sampleRate, blockSize);
            set (*c, orbitamp::params::stereoMode, 0.0f);
            for (const auto* id : { orbitamp::params::delayOn, orbitamp::params::reverbOn, orbitamp::params::cabOn })
                set (*c, id, 1.0f);
            set (*c, orbitamp::params::boostPresent,  0.0f);
            set (*c, orbitamp::params::preampPresent, 0.0f);

            juce::MidiBuffer midi;
            std::vector<float> out;
            long long phase = 0;

            // The cabinet's impulse arrives on the convolver's own thread, at the machine's pace: both
            // instances hear most of a second of silence first, so neither compares a room that has
            // its cabinet against one still waiting for it. (On a slow runner that wait is what made
            // this check come and go.)
            {
                juce::AudioBuffer<float> silence (2, blockSize);
                for (int i = 0; i < 150; ++i)
                {
                    silence.clear();
                    c->processBlock (silence, midi);
                    c->pumpDeviceWork();
                    juce::Thread::sleep (5);
                }
            }

            for (int round = 0; round < 40; ++round)
            {
                juce::AudioBuffer<float> big (2, blockSize * 4);
                for (int i = 0; i < big.getNumSamples(); ++i, ++phase)
                {
                    const float v = 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * 220.0 * (double) phase / sampleRate);
                    big.setSample (0, i, v);
                    big.setSample (1, i, v);
                }

                if (oneBigBlock)
                    c->processBlock (big, midi);
                else
                    for (int k = 0; k < 4; ++k)
                    {
                        juce::AudioBuffer<float> part (big.getArrayOfWritePointers(), 2, k * blockSize, blockSize);
                        c->processBlock (part, midi);
                    }

                for (int i = 0; i < big.getNumSamples(); ++i)
                    out.push_back (big.getSample (0, i));
            }

            return out;
        };

        const auto whole = chained (true);
        const auto parts = chained (false);

        double worst = 0.0;
        for (size_t i = 0; i < whole.size() && i < parts.size(); ++i)
            worst = std::max (worst, (double) std::abs (whole[i] - parts[i]));

        report ("a host block 4x the promise plays as four promised ones",
                whole.size() == parts.size() && worst < 1.0e-6, "worst difference " + juce::String (worst, 9));
    }

    // A PARAMETER THAT IS NOT A NUMBER must not silence a link for as long as it is held. The
    // room's DECAY set to NaN — the way a broken automation lane would set it — then a burst of
    // tone and silence after it: the room must still ring.
    {
        const auto tailWith = [&] (bool poisonDecay)
        {
            const auto c = std::make_unique<orbitamp::AmpProcessor>();
            c->prepareToPlay (sampleRate, blockSize);
            set (*c, orbitamp::params::stereoMode, 0.0f);
            set (*c, orbitamp::params::reverbOn, 1.0f);
            set (*c, orbitamp::params::reverbMix, 100.0f);
            set (*c, orbitamp::params::boostPresent,  0.0f);
            set (*c, orbitamp::params::preampPresent, 0.0f);
            set (*c, orbitamp::params::cabPresent,    0.0f);
            set (*c, orbitamp::params::delayPresent,  0.0f);

            if (poisonDecay)
                c->apvts.getParameter (orbitamp::params::reverbDecay)
                    ->setValueNotifyingHost (std::numeric_limits<float>::quiet_NaN());

            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            double tail = 0.0;
            long long phase = 0;

            for (int b = 0; b < 60; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float v = b < 20 ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                      * 220.0 * (double) phase / sampleRate) : 0.0f;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }

                c->processBlock (buf, midi);

                if (b >= 25 && b < 35)
                    tail = std::max (tail, (double) buf.getMagnitude (0, 0, blockSize));
            }

            return tail;
        };

        const double healthy  = tailWith (false);
        const double poisoned = tailWith (true);

        report ("a NaN on the room's DECAY: the room still rings",
                healthy > 1.0e-3 && poisoned > 0.5 * healthy,
                juce::String (poisoned, 4) + " vs " + juce::String (healthy, 4));
    }

    // THE TUNER'S MUTE: silence at the jack while the tuner works, and not a sample of it while the
    // tuner stands by — a link's action in STANDBY is ignored. A quiet tone through a chain with
    // nothing that needs a pack; the last block of each run is what the jack puts out.
    {
        const auto m = std::make_unique<orbitamp::AmpProcessor>();
        m->prepareToPlay (sampleRate, blockSize);
        set (*m, orbitamp::params::stereoMode, 0.0f);
        set (*m, orbitamp::params::boostPresent,  0.0f);
        set (*m, orbitamp::params::preampPresent, 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        long long phase = 0;

        const auto lastBlockPeak = [&]
        {
            for (int b = 0; b < 8; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float s = 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * 220.0 * (double) phase / sampleRate);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                m->processBlock (buf, midi);
            }
            return juce::jmax (buf.getMagnitude (0, 0, blockSize), buf.getMagnitude (1, 0, blockSize));
        };

        const float open = lastBlockPeak();

        set (*m, orbitamp::params::tunerMute, 1.0f);
        const float muted = lastBlockPeak();

        set (*m, orbitamp::params::tunerOn, 0.0f);
        const float standby = lastBlockPeak();

        report ("tuner mute: the jack goes silent",            open > 0.01f && muted == 0.0f,
                juce::String (open, 3) + " -> " + juce::String (muted, 5));
        report ("...and a tuner standing by does not mute",   std::abs (standby - open) < 0.01f,
                juce::String (standby, 3));
    }

    // A SAVE FROM A HOST'S WORKER THREAD hands out the session the message thread wrote down, and
    // never walks the live state. Driven the way the pump drives it — unforced refreshes, one per
    // tick — so the change detection is what is under test, not a forced write.
    {
        struct Worker final : juce::Thread
        {
            std::function<void()> job;
            bool loop = false;
            std::atomic<int> runs { 0 };

            Worker (std::function<void()> j, bool keepGoing) : juce::Thread ("host worker"), job (std::move (j)), loop (keepGoing) {}

            void run() override
            {
                do { job(); ++runs; } while (loop && ! threadShouldExit());
            }
        };

        const auto onWorker = [] (std::function<void()> job)
        {
            Worker w (std::move (job), false);
            w.startThread();
            w.waitForThreadToExit (5000);
        };

        const auto readMix = [] (orbitamp::AmpProcessor& p)
        {
            auto* prm = p.apvts.getParameter (orbitamp::params::reverbMix);
            return prm->convertFrom0to1 (prm->getValue());
        };

        const auto ticks = [] (orbitamp::AmpProcessor& p, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                p.history.tick();          // what the pump does first: it flushes the knobs into the tree
                p.refreshSavedState();
            }
        };

        const auto c = std::make_unique<orbitamp::AmpProcessor>();
        set (*c, orbitamp::params::reverbMix, 37.0f);
        ticks (*c, 12);

        juce::MemoryBlock fromWorker;
        onWorker ([&] { c->getStateInformation (fromWorker); });

        const auto d = std::make_unique<orbitamp::AmpProcessor>();
        d->setStateInformation (fromWorker.getData(), (int) fromWorker.getSize());
        report ("a worker's save carries what the pump wrote down", std::abs (readMix (*d) - 37.0f) < 0.01f,
                "mix " + juce::String (readMix (*d), 2));

        // A register copy changes no parameter and no tree — the copy notices by the register.
        {
            juce::MemoryBlock before, after;
            onWorker ([&] { c->getStateInformation (before); });
            set (*c, orbitamp::params::reverbMix, 11.0f);
            ticks (*c, 12);
            c->history.copyRegister (c->history.active(), 2);
            set (*c, orbitamp::params::reverbMix, 37.0f);
            ticks (*c, 12);
            onWorker ([&] { c->getStateInformation (after); });

            const auto e = std::make_unique<orbitamp::AmpProcessor>();
            e->setStateInformation (after.getData(), (int) after.getSize());
            e->history.switchTo (2);
            report ("...a copy into a register reaches it",  std::abs (readMix (*e) - 11.0f) < 0.01f,
                    "register C mix " + juce::String (readMix (*e), 2));
        }

        // Restored from a worker — queued for a message thread that is busy — while the pump keeps
        // ticking over a live state that has just changed: the save must still be what came in.
        {
            const auto r = std::make_unique<orbitamp::AmpProcessor>();
            set (*r, orbitamp::params::reverbMix, 90.0f);   // the live state the restore replaces, dirty
            onWorker ([&] { r->setStateInformation (fromWorker.getData(), (int) fromWorker.getSize()); });
            ticks (*r, 30);

            juce::MemoryBlock back;
            onWorker ([&] { r->getStateInformation (back); });
            report ("...a pending restore is not written over by the pump", back == fromWorker);

            juce::MemoryBlock backHere;
            r->getStateInformation (backHere);   // the message thread, the restore still queued
            report ("...nor by a save on the message thread", backHere == fromWorker);
        }

        // What neither load path takes is not a session, and does not become the saved one.
        {
            juce::MemoryBlock junk;
            juce::XmlElement workspaceWithoutLive ("Workspace");
            c->copyXmlToBinary (workspaceWithoutLive, junk);

            onWorker ([&] { c->setStateInformation (junk.getData(), (int) junk.getSize()); });
            juce::MemoryBlock back;
            onWorker ([&] { c->getStateInformation (back); });

            const auto f = std::make_unique<orbitamp::AmpProcessor>();
            f->setStateInformation (back.getData(), (int) back.getSize());
            report ("...an unloadable blob never becomes the saved session", std::abs (readMix (*f) - 37.0f) < 0.01f,
                    "mix " + juce::String (readMix (*f), 2));

            // ...and does not stop the copy from following the live state afterwards.
            set (*c, orbitamp::params::reverbMix, 64.0f);
            ticks (*c, 12);
            juce::MemoryBlock later;
            onWorker ([&] { c->getStateInformation (later); });
            const auto g = std::make_unique<orbitamp::AmpProcessor>();
            g->setStateInformation (later.getData(), (int) later.getSize());
            report ("...nor freezes it",                    std::abs (readMix (*g) - 64.0f) < 0.01f,
                    "mix " + juce::String (readMix (*g), 2));
        }

        // The stress: a worker saving without pause while this thread moves a knob and the pump
        // writes the session down. Every save it got must be a whole session.
        {
            std::atomic<int> torn { 0 };
            juce::MemoryBlock last;
            juce::CriticalSection lastLock;

            Worker w ([&]
            {
                juce::MemoryBlock blob;
                c->getStateInformation (blob);
                if (orbitamp::AmpProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize()) == nullptr)
                    ++torn;
                const juce::ScopedLock sl (lastLock);
                last = blob;
            }, true);

            w.startThread();
            for (int i = 0; i < 300; ++i)
            {
                set (*c, orbitamp::params::reverbMix, (float) (i % 100));
                ticks (*c, 1);
            }
            w.signalThreadShouldExit();
            w.waitForThreadToExit (5000);

            const auto f = std::make_unique<orbitamp::AmpProcessor>();
            f->setStateInformation (last.getData(), (int) last.getSize());
            const float mix = readMix (*f);
            report ("...and saving flat out beside a changing state never tears",
                    w.runs.load() > 0 && torn.load() == 0 && mix >= 0.0f && mix <= 99.0f
                        && std::abs (mix - std::round (mix)) < 0.01f,
                    juce::String (w.runs.load()) + " saves, " + juce::String (torn.load()) + " torn");
        }
    }

    if (amp.boost.packs.isEmpty())
    {
        // NOT a bare `return 0` any more. Everything above this line is the wire on its own bench
        // and needs no library at all — it was moved in front of this exit precisely so a machine
        // without packs still runs it. Returning zero here threw its verdict away: the whole
        // geometry could fail and the process would still exit green, which is the exact shape of
        // the hole this branch exists to close.
        std::printf ("no packs installed — the library half is skipped, the bench above is not\n");
        return failures == 0 ? 0 : 1;
    }

    std::printf ("device: %s\n\n", amp.boost.packs.getReference (0).displayName().toRawUTF8());

    // Everything else off, so what is measured is the boost and nothing standing in front of it.
    // The EQ consoles have no enable of their own — they ship flat, and a flat link is
    // bit-transparent, so there is nothing to switch off here.
    set (amp, orbitamp::params::reverbOn, 0.0f);

    // Both younger than this gate, both in every path, both OFF here for the same reason the
    // reverb is. The cabinet's convolution tail rings for over a second — longer than a run's
    // settle — so with it on, one measurement's tail plays into the next one's window and two
    // identical settings stop measuring identical. The limiter bends the very levels the checks
    // compare. Each gets its own check at the end, switched on deliberately.
    set (amp, orbitamp::params::cabOn, 0.0f);
    set (amp, orbitamp::params::limiterOn, 0.0f);

    // The state a player meets on first launch. Worth stating as a check rather than a comment: it is
    // the most likely answer to "it does not react", and if it ever changes this says so.
    report ("the boost ships switched OFF",
            amp.apvts.getRawParameterValue (orbitamp::params::boostOn)->load() < 0.5f);

    {
        set (amp, orbitamp::params::boostOn, 0.0f);
        set (amp, orbitamp::params::boostGain, 0.0f);
        const auto quiet = run (amp);

        set (amp, orbitamp::params::boostGain, 10.0f);
        const auto loud = run (amp);

        report ("...and while it is off, its gain does nothing",
                differencePercent (quiet, loud) < 0.001,
                juce::String (differencePercent (quiet, loud), 4) + "% different");
    }

    set (amp, orbitamp::params::boostOn, 1.0f);

    {
        set (amp, orbitamp::params::boostGain, 0.0f);
        const auto low = run (amp);

        set (amp, orbitamp::params::boostGain, 10.0f);
        const auto high = run (amp);

        std::printf ("\ngain 0 -> rms %.5f, gain 10 -> rms %.5f\n", rms (low), rms (high));
        std::printf ("shape differs by %.1f%%\n\n", differencePercent (low, high));

        report ("switched on, the gain knob reaches the audio",
                differencePercent (low, high) > 5.0,
                juce::String (differencePercent (low, high), 1) + "% different");
    }

    // A measured control, through the whole plugin rather than through the filter alone.
    //
    // This check sat out for a while behind a `measuredPinnedRaw` flag: the blocks were pinned raw,
    // a NAM player and nothing else, so a measured knob NOT reaching the audio was the contract.
    // The contract changed — a block wears the device's own controls unless you ask for ours — and
    // the check wakes up exactly as it was written.
    {
        const auto tones = amp.boost.tones();

        int sweep = -1;
        for (int i = 0; i < (int) tones.size() && i < orbitamp::params::boostNumMeasured; ++i)
            if (tones[(size_t) i].positions.size() > 2) { sweep = i; break; }

        if (sweep < 0)
        {
            std::printf ("no swept measured control on this device — skipping the tone check\n");
        }
        else
        {
            const auto& m = tones[(size_t) sweep];

            // The probe tone goes where the control's own tables promise the biggest swing, inside
            // the trusted band — a fixed 220 Hz hears a Big Muff tone control and is deaf to a low
            // shelf. The pack states what the knob does and where; what is checked is that the
            // CHAIN delivers it, not that every knob happens to work at one frequency.
            const auto& darkCurve   = m.positions.front().db;
            const auto& brightCurve = m.positions.back().db;
            const int   points      = m.grid.points;

            int lo = 0, hi = points - 1;
            if (m.trusted.hiIndex > m.trusted.loIndex)
            {
                lo = juce::jlimit (0, points - 1, m.trusted.loIndex);
                hi = juce::jlimit (0, points - 1, m.trusted.hiIndex);
            }

            int bestIndex = -1;
            double promisedDb = 0.0;
            for (int i = lo; i <= hi && i < (int) darkCurve.size() && i < (int) brightCurve.size(); ++i)
                if (const double d = std::abs (darkCurve[(size_t) i] - brightCurve[(size_t) i]); d > promisedDb)
                {
                    promisedDb = d;
                    bestIndex  = i;
                }

            if (bestIndex < 0 || promisedDb < 2.0)
            {
                std::printf ("the swept control promises under 2 dB anywhere trusted — skipping\n");
            }
            else
            {
                const double probeHz = m.grid.fLo * std::pow (m.grid.fHi / m.grid.fLo,
                                                              (double) bestIndex / (double) (points - 1));

                // The gain check above leaves the pedal at 10, and a capture at full drive
                // saturates — compression that eats exactly the level difference this probe
                // listens for. The knob is probed where the chain is linear enough to pass it.
                set (amp, orbitamp::params::boostGain, 0.0f);

                const auto id = orbitamp::params::boostMeasured (sweep);
                std::printf ("measured control: %s, probed at %.0f Hz where it promises %.1f dB\n",
                             juce::String (m.name).toRawUTF8(), probeHz, promisedDb);

                set (amp, id, 0.0f);
                const auto dark = run (amp, probeHz);

                set (amp, id, 1.0f);
                const auto bright = run (amp, probeHz);

                std::printf ("min -> rms %.5f, max -> rms %.5f\n\n", rms (dark), rms (bright));

                // A tone control DOES change the level at a single frequency, so this one can say dB.
                report ("a measured knob reaches the audio too",
                        std::abs (20.0 * std::log10 (juce::jmax (1.0e-9, rms (bright) / rms (dark)))) > 1.0,
                        juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, rms (bright) / rms (dark))), 1)
                            + " dB apart");
            }
        }
    }

    // The EQ links, and the thing their PLACES exist for: the SAME filter before and after the
    // capture is not the same sound. eq2, after the boost, colours what came out. eq1, ahead of it,
    // changes what arrives at the nonlinearity, so it changes what kind of distortion happens at
    // all — which is how one captured voice becomes two instruments, and is the reason the EQ is a
    // link of the chain rather than a section of a block.
    {
        set (amp, orbitamp::params::boostGain, 8.0f);   // well into the dirt, or there is no
                                                        // nonlinearity for eq1 to act on
        const auto clean = run (amp);

        // The same cut in both places: 800 Hz high-pass, well inside a guitar's body.
        for (int l = 0; l < orbitamp::params::numEqLinks; ++l)
        {
            set (amp, orbitamp::params::eqHpfOn (l), 1.0f);
            set (amp, orbitamp::params::eqHpfHz (l), 800.0f);
        }

        // A console is armed by its VALUES now, which is the whole point of dropping the enable:
        // there is no state in which the curve says one thing and the audio does another.
        for (int l = 0; l < orbitamp::params::numEqLinks; ++l)
            set (amp, orbitamp::params::eqHpfOn (l), 0.0f);

        set (amp, orbitamp::params::eqHpfOn (1), 1.0f);
        const auto post = run (amp);
        set (amp, orbitamp::params::eqHpfOn (1), 0.0f);

        set (amp, orbitamp::params::eqHpfOn (0), 1.0f);
        const auto pre = run (amp);
        set (amp, orbitamp::params::eqHpfOn (0), 0.0f);

        std::printf ("\nEQ consoles: flat %.5f, preamp's %.5f, boost's %.5f\n",
                     rms (clean), rms (post), rms (pre));
        std::printf ("the preamp's differs from flat by %.1f%%, the boost's from it by %.1f%%\n\n",
                     differencePercent (clean, post), differencePercent (post, pre));

        report ("a block's EQ reaches the audio",
                differencePercent (clean, post) > 5.0,
                juce::String (differencePercent (clean, post), 1) + "% different");

        // They are not interchangeable and must not be: the boost's console feeds the preamp, so it
        // changes what the next nonlinearity is given; the preamp's only colours what came out.
        report ("...and the boost's console is not the preamp's", differencePercent (post, pre) > 5.0,
                juce::String (differencePercent (post, pre), 1) + "% apart");
    }

    // A capture with no gain axis still answers its gain knob — the knob DRIVES where it cannot
    // select. Without this a lone .nam arrives with a dead control on the panel, and a preamp whose
    // gain does nothing is not a preamp.
    {
        auto* stageDef = const_cast<namz::rig::Stage*> (
            amp.boost.packs.getReference (0).rig.firstKnown());

        if (stageDef != nullptr)
        {
            auto bare = amp.boost.packs.getReference (0);
            auto* bareStage = const_cast<namz::rig::Stage*> (bare.rig.firstKnown());
            // One file and no controls: exactly what a dropped-in .nam becomes. Leaving the other
            // twenty in place would leave the stage with nothing it could load at all.
            bareStage->device.controls.clear();
            bareStage->tone.clear();
            bareStage->device.files.resize (1);
            bareStage->device.files.front().settings.clear();   // …and its one file names no position

            // The heap, for the reason the one in main() is there: a megabyte and a half does not
            // fit a Windows stack, and this one is nested deeper still.
            const auto soloOwned = std::make_unique<orbitamp::AmpProcessor>();
            auto& solo = *soloOwned;
            solo.inlineLoads = true;
            solo.prepareToPlay (sampleRate, blockSize);
            // The bare pack stands at list position 0; the device parameter's factory default is a
            // NAMED pack now, and the pump would walk away to it mid-check.
            set (solo, orbitamp::params::boostDevice, 0.0f);
            set (solo, orbitamp::params::reverbOn, 0.0f);
            set (solo, orbitamp::params::preampOn, 0.0f);
            set (solo, orbitamp::params::boostOn, 1.0f);

            solo.boost.packs.set (0, bare);
            solo.boost.select (0);

            set (solo, orbitamp::params::boostGain, 2.0f);
            const auto quiet = run (solo);

            set (solo, orbitamp::params::boostGain, 8.0f);
            const auto hard = run (solo);

            std::printf ("\nno gain axis: knob 2 -> rms %.5f, knob 8 -> rms %.5f\n\n",
                         rms (quiet), rms (hard));

            report ("a knob with nothing to select drives instead",
                    differencePercent (quiet, hard) > 5.0,
                    juce::String (differencePercent (quiet, hard), 1) + "% different");
        }
    }

    // The gate, through the whole plugin: under its threshold the chain goes quiet, over it
    // nothing is touched. Everything else off, so what is measured is the gate on a bare wire.
    {
        set (amp, orbitamp::params::boostOn, 0.0f);
        set (amp, orbitamp::params::preampOn, 0.0f);

        // -46 dBFS, under a -20 dB threshold: hum, as far as the gate is concerned. Both mute
        // positions have to kill it — the KEY and the decision are the same, only the place the
        // attenuation lands differs.
        set (amp, orbitamp::params::gateOn, 1.0f);
        set (amp, orbitamp::params::gateThreshold, -20.0f);

        set (amp, orbitamp::params::gatePos, 1.0f);   // pre-reverb (the default)
        const auto gated = run (amp, 220.0, 0.005f);

        set (amp, orbitamp::params::gatePos, 0.0f);   // start
        const auto gatedStart = run (amp, 220.0, 0.005f);

        // Caught NOW, while the gate is still closed — after the next run it is off and the
        // meter honestly reads unity.
        const float meterWhileClosed = amp.gateMeterDb.load();

        set (amp, orbitamp::params::gateOn, 0.0f);
        const auto open = run (amp, 220.0, 0.005f);

        std::printf ("\ngate: hum pre-reverb -> rms %.6f, at start -> %.6f, gate off -> %.6f\n",
                     rms (gated), rms (gatedStart), rms (open));

        report ("the gate closes on what is under its threshold",
                rms (gated) < rms (open) * 0.1,
                juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, rms (gated) / rms (open))), 1) + " dB");

        report ("...at either mute position",
                rms (gatedStart) < rms (open) * 0.1,
                juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, rms (gatedStart) / rms (open))), 1) + " dB");

        // The GR meter is the same fact published for the face — a closed gate that meters open
        // would send a player hunting a working gate for a broken picture.
        report ("...and the GR meter reports the pressure",
                meterWhileClosed < -60.0f,
                juce::String (meterWhileClosed, 1) + " dB");

        set (amp, orbitamp::params::gatePos, 1.0f);   // back to the default for the loud check

        // -12 dBFS over the same threshold: playing. The gate must not be audible at all.
        set (amp, orbitamp::params::gateOn, 1.0f);
        const auto loudGated = run (amp);

        set (amp, orbitamp::params::gateOn, 0.0f);
        const auto loudOpen = run (amp);

        report ("...and leaves what is over it alone",
                differencePercent (loudGated, loudOpen) < 0.5,
                juce::String (differencePercent (loudGated, loudOpen), 2) + "% different");
    }

    // The input trim, through the whole plugin: a linear gain ahead of everything, so on a bare
    // wire it must arrive as exactly itself.
    //
    // IN has to be put in the RIG first. It is not there out of the box any more — the volumes
    // that matter are the captured blocks' own, and a global input fader is a thing a player
    // reaches for when a rig needs fixing. A trim that is not in the rig is not applied, which is
    // the whole point of the switch, so a test that did not ask for it was testing the default
    // rather than the trim.
    {
        set (amp, orbitamp::params::inPresent, 1.0f);
        set (amp, orbitamp::params::inTrim, 0.0f);
        const auto unity = run (amp);

        set (amp, orbitamp::params::inTrim, -24.0f);
        const auto trimmed = run (amp);

        const double dropDb = 20.0 * std::log10 (juce::jmax (1.0e-9, rms (trimmed) / rms (unity)));
        std::printf ("\ntrim: 0 dB -> rms %.5f, -24 dB -> %.5f (%.1f dB)\n",
                     rms (unity), rms (trimmed), dropDb);

        report ("the input trim reaches the audio", dropDb < -22.0 && dropDb > -26.0,
                juce::String (dropDb, 1) + " dB");

        set (amp, orbitamp::params::inTrim, 0.0f);
    }

    // The cabinet, through the whole chain — the reason it sat out the checks above. ON, it has
    // to voice: a speaker treats the guitar's body and its fizz differently, so the same probe at
    // 220 Hz and at 6 kHz must come through with very different gains. And at reference unity it
    // contributes tone, not gain: the in-band level stays near what left the chain without it.
    {
        const auto dryBody = run (amp);
        const auto dryFizz = run (amp, 6000.0);

        set (amp, orbitamp::params::cabOn, 1.0f);
        const auto cabBody = run (amp);
        const auto cabFizz = run (amp, 6000.0);
        set (amp, orbitamp::params::cabOn, 0.0f);

        const double bodyDb = 20.0 * std::log10 (juce::jmax (1.0e-9, rms (cabBody) / rms (dryBody)));
        const double fizzDb = 20.0 * std::log10 (juce::jmax (1.0e-9, rms (cabFizz) / rms (dryFizz)));

        std::printf ("\ncab: 220 Hz %+.1f dB, 6 kHz %+.1f dB through the IR\n", bodyDb, fizzDb);

        report ("the cabinet voices — body through, fizz gone",
                bodyDb - fizzDb > 6.0,
                juce::String (bodyDb - fizzDb, 1) + " dB apart");

        report ("...and at reference unity it is tone, not gain",
                std::abs (bodyDb) < 8.0,
                juce::String (bodyDb, 1) + " dB at 220 Hz");
    }

    // The limiter, through the whole chain: ON it holds its ceiling, OFF it does not exist. The
    // probe peaks at -0.9 dBFS against a -3 dB ceiling — 2 dB of real work.
    {
        set (amp, orbitamp::params::limiterOn, 1.0f);
        set (amp, orbitamp::params::limiterCeiling, -3.0f);
        const auto held = run (amp, 220.0, 0.9f);

        set (amp, orbitamp::params::limiterOn, 0.0f);
        const auto free = run (amp, 220.0, 0.9f);

        std::printf ("\nlimiter: ceiling -3 -> peak %.1f dBFS, off -> %.1f dBFS\n",
                     peakDb (held), peakDb (free));

        report ("the limiter holds its ceiling",
                peakDb (held) < -2.4 && peakDb (held) > -4.5,
                juce::String (peakDb (held), 1) + " dBFS");

        report ("...and switched off it does not touch the sound",
                peakDb (free) > -1.3 && peakDb (free) < -0.5,
                juce::String (peakDb (free), 1) + " dBFS");
    }

    // The three channel modes. MONO: one chain, the copy after everything — the channels are
    // identical. STEREO SPACE: mono up to the reverb, stereo from it — with the reverb in the rig
    // the two channels differ (the space is wide), with it OUT of the rig they are identical (the
    // copy at the seam feeds the cabinet the same signal). Measured on the plugin's own output,
    // both channels kept.
    //
    // Out of the RIG, not merely standing by: standing by is the insert's bypass, and a bypassed
    // room rings out instead of being cut, so its tail would still be spreading the channels for
    // as long as it takes to decay. That is the next check's job, not this one's.
    {
        const auto runBoth = [&] (float mode, bool reverbIn)
        {
            set (amp, orbitamp::params::stereoMode, mode);
            set (amp, orbitamp::params::reverbPresent, reverbIn ? 1.0f : 0.0f);
            set (amp, orbitamp::params::reverbOn, 1.0f);
            set (amp, orbitamp::params::limiterOn, 0.0f);

            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            std::vector<float> l, r;
            int phase = 0;

            for (int block = 0; block < 60; ++block)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float s = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * 220.0 * phase / sampleRate);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                amp.processBlock (buf, midi);
                amp.pumpDeviceWork();
                if (block < 30)
                    continue;
                for (int i = 0; i < blockSize; ++i)
                {
                    l.push_back (buf.getSample (0, i));
                    r.push_back (buf.getSample (1, i));
                }
            }
            return std::make_pair (l, r);
        };

        const auto monoWet   = runBoth ((float) orbitamp::params::StereoMode::mono,        true);
        const auto spaceWet  = runBoth ((float) orbitamp::params::StereoMode::stereoSpace, true);
        const auto spaceDry  = runBoth ((float) orbitamp::params::StereoMode::stereoSpace, false);

        std::printf ("\nchannels: mono+reverb L/R %.2f%% apart, stereo space+reverb %.2f%%, stereo space dry %.2f%%\n",
                     differencePercent (monoWet.first, monoWet.second),
                     differencePercent (spaceWet.first, spaceWet.second),
                     differencePercent (spaceDry.first, spaceDry.second));

        report ("MONO leaves the two channels identical",
                differencePercent (monoWet.first, monoWet.second) < 0.01);
        report ("STEREO SPACE spreads the reverb across the channels",
                differencePercent (spaceWet.first, spaceWet.second) > 1.0
                    && rms (spaceWet.second) > 1.0e-4,
                juce::String (differencePercent (spaceWet.first, spaceWet.second), 2) + "% apart");
        report ("...and with the reverb out of the rig they are one signal again",
                differencePercent (spaceDry.first, spaceDry.second) < 0.01);

        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::mono);
    }

    // STANDBY IS THE INSERT'S BYPASS. Stand the room down and it stops taking new signal, but what
    // is already ringing rings OUT — it is not chopped. Measured in STEREO SPACE, where the room is
    // the only thing that can tell the two channels apart: right after the switch they still
    // differ, because the tail is still spreading; long after it, they are one signal again.
    //
    // The old contract was the opposite — off meant cleared, every block — so this is the check
    // that says which of the two the plugin is.
    {
        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::stereoSpace);
        set (amp, orbitamp::params::reverbPresent, 1.0f);
        set (amp, orbitamp::params::reverbOn, 1.0f);
        set (amp, orbitamp::params::limiterOn, 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        int phase = 0;

        const auto run = [&] (int blocks, bool silentInput, std::vector<float>* l, std::vector<float>* r)
        {
            for (int block = 0; block < blocks; ++block)
            {
                for (int i = 0; i < blockSize; ++i, ++phase)
                {
                    const float s = silentInput ? 0.0f
                                  : 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                              * 220.0 * phase / sampleRate);
                    buf.setSample (0, i, s);
                    buf.setSample (1, i, s);
                }
                amp.processBlock (buf, midi);
                amp.pumpDeviceWork();

                if (l == nullptr)
                    continue;

                for (int i = 0; i < blockSize; ++i)
                {
                    l->push_back (buf.getSample (0, i));
                    r->push_back (buf.getSample (1, i));
                }
            }
        };

        run (40, false, nullptr, nullptr);          // fill the room

        // Standing by, on silence: whatever is heard now is the tail alone.
        set (amp, orbitamp::params::reverbOn, 0.0f);

        std::vector<float> ringL, ringR;
        run (6, true, &ringL, &ringR);

        std::vector<float> goneL, goneR;
        run (400, true, nullptr, nullptr);          // ...and long enough for it to die
        run (6, true, &goneL, &goneR);

        std::printf ("\nbypass: tail right after standby %.2f%% apart, after it decays %.2f%%\n",
                     differencePercent (ringL, ringR), differencePercent (goneL, goneR));

        report ("a room on standby rings OUT instead of being cut",
                rms (ringL) > 1.0e-5 && differencePercent (ringL, ringR) > 1.0,
                juce::String (differencePercent (ringL, ringR), 2) + "% apart");

        report ("...and once it has decayed there is nothing left of it",
                rms (goneL) < 1.0e-5,
                juce::String (juce::Decibels::gainToDecibels ((double) rms (goneL), -120.0), 1) + " dBFS");

        set (amp, orbitamp::params::reverbOn, 1.0f);
        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::mono);
    }

    // A LINK THAT REPLACES has no tail to ride out, so standing it down is a crossfade against
    // what it was handed. The proof is the waveform: measured as the biggest jump between two
    // neighbouring samples across the switch. A 220 Hz sine at 0.25 climbs about 0.0072 per sample
    // of its own accord; dropping a cabinet out of the path in one sample steps by a good fraction
    // of the signal, which is an order of magnitude more and is what a click IS.
    {
        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::mono);
        set (amp, orbitamp::params::cabPresent, 1.0f);
        set (amp, orbitamp::params::cabOn, 1.0f);
        set (amp, orbitamp::params::limiterOn, 0.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        int phase = 0;
        float last = 0.0f, biggestJump = 0.0f;
        bool  measuring = false;

        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float s = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * 220.0 * phase / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }

            if (block == 20)
            {
                set (amp, orbitamp::params::cabOn, 0.0f);   // stand it down mid-note
                measuring = true;
            }

            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();

            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (0, i);
                if (measuring)
                    biggestJump = juce::jmax (biggestJump, std::abs (v - last));
                last = v;
            }
        }

        std::printf ("\nbypass: biggest sample-to-sample jump across the switch %.5f\n", biggestJump);

        report ("standing a cabinet down does not step the waveform",
                biggestJump < 0.02f, juce::String (biggestJump, 5));

        set (amp, orbitamp::params::cabOn, 1.0f);
    }

    // THE SAFETY, released. A limiter switched off used to hand back whatever it was holding in
    // ONE sample — three decibels of grip returned instantly is a step upward, and a step is a
    // click. Driven hard enough to be gripping, then switched off mid-note.
    {
        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::mono);
        set (amp, orbitamp::params::limitPresent, 1.0f);
        set (amp, orbitamp::params::limiterOn, 1.0f);
        set (amp, orbitamp::params::limiterCeiling, -6.0f);
        set (amp, orbitamp::params::boostOn, 1.0f);

        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        int   phase = 0;
        float last = 0.0f, quietJump = 0.0f, switchJump = 0.0f;
        int   measuring = 0;   // 1 = the calm before, 2 = across the switch

        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++phase)
            {
                const float v = 0.9f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                         * 220.0 * phase / sampleRate);
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }

            // The baseline has to be the SAME signal: a gripped sine is quieter and therefore
            // flatter, so measuring the calm while the limiter still held it would compare a
            // released waveform against a squashed one and call the difference a step.
            if (block == 20)
            {
                set (amp, orbitamp::params::limiterOn, 0.0f);   // let go, mid-note
                measuring = 2;                                  // across the release
            }

            if (block == 24) measuring = 1;                     // ...and long after it, settled

            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();

            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (0, i);
                const float jump = std::abs (v - last);

                if (measuring == 1) quietJump  = juce::jmax (quietJump, jump);
                if (measuring == 2) switchJump = juce::jmax (switchJump, jump);

                last = v;
            }
        }

        // Against ITSELF, not against a number I picked: a driven sine through a boost has a slope
        // of its own, and what matters is whether letting go adds to it.
        std::printf ("\nlimiter: biggest jump settled %.5f, across the release %.5f\n",
                     quietJump, switchJump);

        report ("letting the safety go does not step the waveform",
                switchJump < quietJump * 1.4f,
                juce::String (switchJump, 5) + " vs " + juce::String (quietJump, 5));

        set (amp, orbitamp::params::limiterOn, 1.0f);
        set (amp, orbitamp::params::boostOn, 0.0f);
        set (amp, orbitamp::params::limiterCeiling, -0.3f);
    }

    // THE BYPASS WIRE THROUGH THE WHOLE PLUGIN. Everything checked before the pack gate above is
    // the wire on its own bench; this is the half that needs a pack on disk, because it asks the
    // shipping stage for the number instead of spelling the geometry itself.
    {
        using Wire = orbitamp::core::BypassWire;

        const auto stageLatency = [] (double host, double pack)
        {
            return (int) std::lround (
                felitronics::core::StreamResampler::pairDelayHostSamples (host, pack));
        };

        // 5 · THE LIVE CONSUMER — because everything above is this TEST's own spelling of the
        //     geometry, and a spelling cannot catch a change of geometry. `stageLatency` here and
        //     `rateMatchDelay` in the wire agree because I wrote both; that proves arithmetic, not
        //     alignment. So the number is taken from the shipping stage instead: a processor
        //     prepared at 96 kHz against the packs actually installed on this machine reports a
        //     PDC that came out of `nam::NamStage::latencySamples()`, and the wire the plugin
        //     prepares at that rate has to be longer than it — whatever the kernel decides that
        //     number is next.
        {
            const auto hiOwned = std::make_unique<orbitamp::AmpProcessor>();
            auto& hi = *hiOwned;
            hi.inlineLoads = true;
            hi.prepareToPlay (96000.0, blockSize);

            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;

            for (int i = 0; i < 40; ++i)      // let the models land, the way the 30 Hz pump does
            {
                buf.clear();
                hi.processBlock (buf, midi);
                hi.pumpDeviceWork();
                juce::Thread::sleep (2);
            }

            const int pdc      = hi.getLatencySamples();
            const int perStage = stageLatency (96000.0, 48000.0);   // what THIS test would predict
            const int cap      = juce::jmax (hi.bypassWireCapacity (0), hi.bypassWireCapacity (1));

            // The PDC is the SUM over the blocks that actually hold a rate-matching model, and how
            // many that is depends on what is installed — so the number to compare is not the sum
            // but whether the sum is built out of this stage figure at all.
            std::printf ("\nwire: a live 96 kHz session reports %d samples of PDC = %d stage(s) of"
                         " %d; this test's own arithmetic says %d a stage, the wires carry %d / %d\n",
                         pdc, perStage > 0 ? pdc / perStage : 0, perStage, perStage,
                         hi.bypassWireCapacity (0), hi.bypassWireCapacity (1));

            // PRECONDITION: there has to BE a rate match, or this check passes by measuring
            // nothing — which is the failure mode the whole comb bench above is built to refuse.
            report ("the live 96 kHz session really is rate-matching", pdc > 0,
                    pdc > 0 ? juce::String (pdc) + " samples of PDC"
                            : "PDC is zero: no installed pack is off the session rate, so this "
                              "check would be blind");

            // PER BLOCK, not against the sum. Each captured block has its OWN wire, so a wire has
            // to outlast the longest single block and not the two of them added together — the
            // comparison against the sum both under- and over-states it, and two 16 kHz packs at
            // 96 kHz would have failed it while every wire was long enough.
            const int boostLat  = hi.boost.latencySamples();
            const int preampLat = hi.preamp.latencySamples();
            const int longest   = juce::jmax (boostLat, preampLat);

            // WHICH RATE EACH LIVE LATENCY BELONGS TO, found by asking the geometry forward rather
            // than inverting it by hand. The inversion `m = D·host/(L − D)` that used to stand here
            // was itself a restatement, and one that stops being true the day the two legs of a
            // round trip differ — exactly the change the core signature is now shaped to absorb.
            const auto rateBehind = [&] (double host, int lat)
            {
                if (lat <= 0)
                    return 0.0;

                for (const double m : { 8000.0, 11025.0, 16000.0, 22050.0, 32000.0, 44100.0,
                                        48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
                    if (stageLatency (host, m) == lat)
                        return m;

                return -1.0;   // a rate outside the grid: reported, never guessed at
            };

            std::printf ("      boost %d samples (a %.0f Hz pack), preamp %d (%.0f Hz)\n",
                         boostLat,  rateBehind (96000.0, boostLat),
                         preampLat, rateBehind (96000.0, preampLat));

            report ("what the host is told is what the blocks actually cost",
                    pdc == boostLat + preampLat,
                    juce::String (pdc) + " reported, " + juce::String (boostLat + preampLat)
                      + " summed");

            report ("every live latency belongs to a rate the geometry can produce",
                    longest > 0
                      && (boostLat  == 0 || rateBehind (96000.0, boostLat)  > 0.0)
                      && (preampLat == 0 || rateBehind (96000.0, preampLat) > 0.0));

            // 🔴 EXACTLY, not "at least", and the invariant has two terms because the wire has two
            //    jobs: it CARRIES the delay its block reports, and it REMEMBERS a block's worth of
            //    past so a delay born later has something to come back to. So the length is the
            //    larger of those, and nothing else — a rate-derived ceiling would read 224 at
            //    48 kHz or 416 at 96, and that is precisely what no longer exists.
            const int floorSamples = blockSize;
            report ("each wire is the larger of its block's delay and one block of memory",
                    hi.bypassWireCapacity (0) == juce::jmax (floorSamples, boostLat)
                      && hi.bypassWireCapacity (1) == juce::jmax (floorSamples, preampLat),
                    juce::String (hi.bypassWireCapacity (0)) + " and "
                      + juce::String (hi.bypassWireCapacity (1)) + " against delays "
                      + juce::String (boostLat) + "/" + juce::String (preampLat)
                      + ", floor " + juce::String (floorSamples));

            juce::ignoreUnused (cap);

            // 🔴 AND THE PRODUCT PROMISE ITSELF, THROUGH THE WHOLE PLUGIN: a bypassed block is a
            //    wire the same length as the block it replaces, so a session whose blocks are both
            //    switched off has to delay by exactly the PDC it reports — no more, no less. This
            //    is the only check here that LISTENS, and it is the one that a wire quietly
            //    shortened back to 64 samples, or a PDC quietly doubled, cannot survive.
            //
            //    🔴 BOTH BLOCKS FIRST. A model is not loaded until its block is RUN, so leaving
            //    the boost switched off left it at zero latency and this measured ONE wire while
            //    claiming to measure two — deleting the boost's compensation would have gone
            //    unnoticed. They are both switched on, both made to report a delay, and only then
            //    stood down.
            const auto blocksOut = [&] ()
            {
                for (const char* off : { orbitamp::params::boostOn,   orbitamp::params::preampOn,
                                         orbitamp::params::gateOn,    orbitamp::params::delayOn,
                                         orbitamp::params::reverbOn,  orbitamp::params::cabOn,
                                         orbitamp::params::limiterOn, orbitamp::params::delayPresent,
                                         orbitamp::params::reverbPresent, orbitamp::params::cabPresent,
                                         orbitamp::params::gatePresent })
                    set (hi, off, 0.0f);
            };

            set (hi, orbitamp::params::boostOn,  1.0f);
            set (hi, orbitamp::params::preampOn, 1.0f);

            for (int i = 0; i < 60 && (hi.boost.latencySamples() == 0
                                        || hi.preamp.latencySamples() == 0); ++i)
            {
                buf.clear();
                hi.processBlock (buf, midi);
                hi.pumpDeviceWork();
                juce::Thread::sleep (5);
            }

            const int bothBoost  = hi.boost.latencySamples();
            const int bothPreamp = hi.preamp.latencySamples();
            const int bothTotal  = bothBoost + bothPreamp;

            // PRECONDITION: two wires, both with something to carry. Without this the check below
            // passes on a chain where only one block was ever compensated.
            report ("both captured blocks really are rate-matching", bothBoost > 0 && bothPreamp > 0,
                    juce::String (bothBoost) + " + " + juce::String (bothPreamp));

            blocksOut();

            for (int i = 0; i < 12; ++i)     // every switch crossfades; let them all land
            {
                buf.clear();
                hi.processBlock (buf, midi);
                hi.pumpDeviceWork();
            }

            const int reported = hi.getLatencySamples();

            // COLLECTED ACROSS BLOCKS, not inside one. A legal configuration can delay by more than
            // a block — an 8 kHz pack at 96 kHz asks 416 a stage — and a window that stops at the
            // block boundary would fail a wire that was doing its job.
            std::vector<float> tail;
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            buf.setSample (1, 0, 1.0f);

            for (int b = 0; b < 4; ++b)
            {
                if (b > 0)
                    buf.clear();

                hi.processBlock (buf, midi);
                tail.insert (tail.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
            }

            int   landedAt = -1;
            float peak = 0.0f;
            for (size_t i = 0; i < tail.size(); ++i)
                if (const float v = std::abs (tail[i]); v > peak)
                { peak = v; landedAt = (int) i; }

            std::printf ("wire: an impulse through two bypassed blocks at 96 kHz lands at sample %d"
                         " (peak %.3f); the plugin reports %d samples of PDC, the blocks cost"
                         " %d + %d\n", landedAt, peak, reported, bothBoost, bothPreamp);

            // PRECONDITION: the impulse has to have SURVIVED. A chain that swallowed it would
            // report `landedAt == 0` off a peak of nothing and look like zero delay.
            report ("the bypassed chain passes the impulse at all", peak > 0.5f,
                    juce::String (peak, 3) + " out of 1.0");

            report ("a bypassed block delays by exactly what the host is told it does",
                    peak > 0.5f && landedAt == reported && reported == bothTotal,
                    "landed at " + juce::String (landedAt) + ", reported " + juce::String (reported)
                      + ", blocks cost " + juce::String (bothTotal));

            // 🔴 AND THE CROSSFADE, which is the other half of the wire and the half the comb bench
            //    above only simulates. A block on its way out blends what the model made against
            //    the signal it was handed; if the wire is skipped on THAT path, the dry copy is
            //    early by the model's whole latency and the blend is the comb this class exists to
            //    prevent. The pin does not need a spectrum: the model's own output cannot begin
            //    before its latency, so anything of size at the impulse's own position is the dry
            //    copy arriving undelayed.
            {
                set (hi, orbitamp::params::preampOn, 1.0f);

                for (int i = 0; i < 16; ++i)     // let the fade-IN finish: g has to reach 1
                {
                    buf.clear();
                    hi.processBlock (buf, midi);
                    hi.pumpDeviceWork();
                }

                // WHERE THE MARK IS EXPECTED, and it is not the preamp's latency alone: the boost
                // stands bypassed in front of it and its wire is a real delay, so the mark arrives
                // after BOTH. Reading only the preamp's number is what made the first version of
                // this probe measure an empty stretch of buffer and call it silence.
                const int ahead = hi.boost.latencySamples();
                const int lat   = hi.preamp.latencySamples();
                const int when  = ahead + lat;

                set (hi, orbitamp::params::preampOn, 0.0f);      // and now the ~15 ms fade-out

                // TWO MARKS IN ONE PASS, 300 samples apart, and both timed to land INSIDE the
                // fade. One mark proves only that something delayed came back — an instant switch,
                // or a fade that had already finished, gives exactly that. A blend in flight is the
                // one thing that makes the later mark read BIGGER: the dry's weight grows as the
                // blend walks across. Sending them a whole probe apart (which is what this did
                // before) put the second one PAST the end of a 15 ms fade, so the pair proved
                // "mid-fade, then bypass" rather than "the fade moved" — and with a legal 8 kHz
                // pack even the first one landed past the end and failed healthy code.
                constexpr int gap = 300;
                const int fadeLen = (int) std::ceil (orbitamp::core::BypassFade::lengthMs
                                                       * 96000.0 / 1000.0);

                // PRECONDITION: both marks are due before the fade is over. Stated rather than
                // assumed, because it is a fact about the installed library — a pack low enough
                // makes `when` outrun the fade, and then this fixture is measuring the bypass.
                report ("both crossfade marks are due while the blend is still moving",
                        gap + when + 2 < fadeLen,
                        juce::String (gap + when + 2) + " of " + juce::String (fadeLen) + " samples");

                const int need = (gap + when + 3) / blockSize + 1;
                std::vector<float> got;

                buf.clear();
                hi.processBlock (buf, midi);      // the fade's first block, so the marks sit inside it

                buf.clear();
                buf.setSample (0, 0,   1.0f);
                buf.setSample (1, 0,   1.0f);
                buf.setSample (0, gap, 1.0f);
                buf.setSample (1, gap, 1.0f);

                for (int b = 0; b < need; ++b)
                {
                    if (b > 0)
                        buf.clear();

                    hi.processBlock (buf, midi);
                    got.insert (got.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
                }

                // Every window is clamped to what was actually collected. The bound used to be
                // computed from the declared delays alone, so a low enough pack read past the end
                // of this vector — an out-of-bounds read in the instrument that exists to catch
                // out-of-bounds reads.
                const auto peakOver = [&got] (int from, int to)
                {
                    float m = 0.0f;
                    for (int i = juce::jmax (0, from); i <= juce::jmin ((int) got.size() - 1, to); ++i)
                        m = juce::jmax (m, std::abs (got[(size_t) i]));
                    return m;
                };

                const float early   = peakOver (0, when - 3);              // before anything is due
                const float onTime1 = peakOver (when - 2, when + 2);
                const float onTime2 = peakOver (gap + when - 2, gap + when + 2);

                std::printf ("wire: mid-crossfade, two marks 300 apart read %.4f before either is"
                             " due, then %.4f -> %.4f at +%d (%d bypassed ahead + %d fading,"
                             " fade is %d)\n",
                             early, onTime1, onTime2, when, ahead, lat, fadeLen);

                report ("the crossfade is actually running when the marks go in",
                        onTime1 > 0.05f && onTime2 > onTime1 * 1.05f,
                        juce::String (onTime1, 4) + " then " + juce::String (onTime2, 4));

                report ("the dry side of a crossfade is delayed too, so the fade cannot comb",
                        onTime1 > 0.05f && early < 0.1f * onTime1,
                        juce::String (early, 4) + " early against " + juce::String (onTime1, 4));

                blocksOut();

                for (int i = 0; i < 12; ++i)
                {
                    buf.clear();
                    hi.processBlock (buf, midi);
                }
            }

            // 🔴 GROWTH THROUGH THE PROCESSOR, WITH THE FLOOR OUT OF THE WAY. Everything above ran
            //    at a 512-sample block, so the memory floor was 512 and both delays were 96: the
            //    wire never had to grow, and `fitWire` could have been deleted entirely without a
            //    single check noticing. A small block puts the floor UNDER the delay, and then the
            //    only way the numbers come out right is if the growth path actually runs.
            {
                constexpr int tiny = 64;                    // below the 96 a 48 kHz pack asks for

                const auto smallOwned = std::make_unique<orbitamp::AmpProcessor>();
                auto& sm = *smallOwned;
                sm.inlineLoads = true;
                sm.prepareToPlay (96000.0, tiny);

                juce::AudioBuffer<float> tb (2, tiny);
                for (int i = 0; i < 80; ++i)
                {
                    tb.clear();
                    sm.processBlock (tb, midi);
                    sm.pumpDeviceWork();
                    juce::Thread::sleep (2);
                }

                const int smLat = juce::jmax (sm.boost.latencySamples(), sm.preamp.latencySamples());
                const int smCap = juce::jmax (sm.bypassWireCapacity (0), sm.bypassWireCapacity (1));

                std::printf ("wire: at a %d-sample block the floor is under the delay — longest"
                             " delay %d, wire grew to %d\n", tiny, smLat, smCap);

                // PRECONDITION: the floor really is under the delay, or this measures the floor.
                report ("a small block really does put the memory floor under the delay",
                        smLat > tiny, juce::String (smLat) + " against a floor of "
                                        + juce::String (tiny));

                report ("and the wire grew through the processor to reach it",
                        smLat > tiny && smCap == smLat,
                        juce::String (smCap) + " carried against " + juce::String (smLat)
                          + " asked");
            }

            // 🔴 THE MOMENT THE DELAY IS BORN, through the real plugin. A block that stands
            //    bypassed with no model yet costs nothing — and then a model lands at another rate
            //    and it costs 96 samples, between one block and the next, with nobody re-preparing
            //    anything. If the wire was only being CLEARED while it cost nothing, its first
            //    delayed block is 96 samples of silence: a millisecond of hole in the through-path,
            //    on the ordinary path where a player picks a device with the block still switched
            //    off. Fed instead, it has the signal that just went past and there is no hole.
            {
                const auto lateOwned = std::make_unique<orbitamp::AmpProcessor>();
                auto& late = *lateOwned;
                // WHAT ACTUALLY HOLDS THE LOAD BACK, said plainly because the first version of this
                // comment claimed the wrong mechanism: `inlineLoads = false` sends the request to
                // the pool, and delivering a pool result needs a pump that never comes — turning
                // the flag on later does not reclaim work already handed over, it makes the NEXT
                // request run inline. Either way the first blocks genuinely run at zero delay and
                // the model lands afterwards, which is the sequence being pinned; the two
                // preconditions below are what make that a checked fact rather than a hope.
                late.inlineLoads = false;
                late.prepareToPlay (96000.0, blockSize);
                set (late, orbitamp::params::stereoMode, 0.0f);

                for (const char* off : { orbitamp::params::boostOn,   orbitamp::params::preampOn,
                                         orbitamp::params::gateOn,    orbitamp::params::delayOn,
                                         orbitamp::params::reverbOn,  orbitamp::params::cabOn,
                                         orbitamp::params::limiterOn, orbitamp::params::delayPresent,
                                         orbitamp::params::reverbPresent, orbitamp::params::cabPresent,
                                         orbitamp::params::gatePresent })
                    set (late, off, 0.0f);

                juce::AudioBuffer<float> b (2, blockSize);

                // Every one of those switches CROSSFADES, and a fade still in flight colours the
                // through-path with whatever it is fading out of. Let them all land first — with no
                // pump, so the model stays where it is and the delay stays at zero.
                for (int i = 0; i < 8; ++i)
                {
                    b.clear();
                    late.processBlock (b, midi);
                }

                const bool bornCold = late.getLatencySamples() == 0;

                // The last block before the model lands, with a mark near its end: at zero delay it
                // goes straight through, and the wire — if it is being fed — has it.
                constexpr int mark = 472;
                b.clear();
                b.setSample (0, mark, 1.0f);
                b.setSample (1, mark, 1.0f);
                late.processBlock (b, midi);

                late.inlineLoads = true;

                for (int i = 0; i < 200 && late.getLatencySamples() == 0; ++i)
                {
                    late.pumpDeviceWork();
                    juce::Thread::sleep (5);
                }

                const int born = late.getLatencySamples();

                // Collected across blocks: the delay a block acquires can be longer than one, and
                // a window that stopped at the seam would fail a wire that was doing its job.
                std::vector<float> after;
                for (int k = 0; k < 3; ++k)
                {
                    b.clear();                // silence in — everything out came from the window
                    late.processBlock (b, midi);
                    after.insert (after.end(), b.getReadPointer (0),
                                  b.getReadPointer (0) + blockSize);
                }

                const int want = mark + born - blockSize;
                float here = 0.0f, anywhere = 0.0f;
                int   at = -1;

                for (size_t i = 0; i < after.size(); ++i)
                    if (const float v = std::abs (after[i]); v > anywhere)
                    { anywhere = v; at = (int) i; }

                if (want >= 0 && want < (int) after.size())
                    here = std::abs (after[(size_t) want]);

                std::printf ("wire: a delay BORN mid-session — 0 samples, then %d; the mark from the"
                             " block before comes back at %d (wanted %d), %.3f\n",
                             born, at, want, here);

                // PRECONDITIONS: the block really did start with no delay, and really did acquire
                // one. Without both, this measures a plugin that never changed and passes for it.
                report ("a block really can acquire its delay mid-session",
                        bornCold && born > 0 && want >= 0 && want < (int) after.size(),
                        bornCold ? juce::String ("0 -> ") + juce::String (born)
                                 : "it was never cold — this check would be blind");

                report ("the block after the delay is born looks back, not into silence",
                        bornCold && born > 0 && here > 0.5f && at == want,
                        juce::String (here, 3) + " at " + juce::String (at));
            }
        }
        }

    // WHAT THE PLUGIN TELLS A HOST ABOUT ITS OWN TAIL. It used to be a flat eight seconds, which
    // is wrong in both directions: a big room rings longer than that, a long echo very much
    // longer, and a rig with none of the ringing links in it rings for no time at all while the
    // host renders eight seconds of silence onto every bounce.
    {
        const auto tailOwned = std::make_unique<orbitamp::AmpProcessor>();
        auto& amp = *tailOwned;
        amp.prepareToPlay (sampleRate, blockSize);

        const auto tailWith = [&amp] (bool delayIn, bool reverbIn, bool cabIn)
        {
            set (amp, orbitamp::params::delayPresent,  delayIn  ? 1.0f : 0.0f);
            set (amp, orbitamp::params::reverbPresent, reverbIn ? 1.0f : 0.0f);
            set (amp, orbitamp::params::cabPresent,    cabIn    ? 1.0f : 0.0f);
            return amp.getTailLengthSeconds();
        };

        // A HALL breathing at double: room 0.98, 0.974 per pass round a 37.2 ms comb.
        set (amp, orbitamp::params::reverbType, 2.0f);   // Hall
        set (amp, orbitamp::params::reverbDecay, 2.0f);
        set (amp, orbitamp::params::reverbPredelay, 0.0f);

        // Two seconds of echo, and NO repeats: one echo, so the delay's own tail is exactly its
        // time — the number that makes the serial sum easy to read.
        set (amp, orbitamp::params::delaySync, 0.0f);
        set (amp, orbitamp::params::delayTimeMs, 2000.0f);
        set (amp, orbitamp::params::delayRepeats, 0.0f);
        set (amp, orbitamp::params::delayOffset, 0.0f);

        // The stages take their settings from the pump, not from the parameter write.
        for (int i = 0; i < 8; ++i)
        {
            juce::AudioBuffer<float> b (2, blockSize);
            juce::MidiBuffer m;
            b.clear();
            amp.processBlock (b, m);
        }

        const double bare  = tailWith (false, false, false);
        const double spkr  = tailWith (false, false, true);
        const double room  = tailWith (false, true,  false);
        const double echo  = tailWith (true,  false, false);
        const double two   = tailWith (true,  true,  false);

        set (amp, orbitamp::params::delayRepeats, 95.0f);
        for (int i = 0; i < 4; ++i)
        {
            juce::AudioBuffer<float> b (2, blockSize);
            juce::MidiBuffer m;
            b.clear();
            amp.processBlock (b, m);
        }
        const double runaway = tailWith (true, true, true);

        std::printf ("\ntail: nothing %.2f | speaker %.2f | room %.2f | echo %.2f | echo+room %.2f"
                     " | 95%% repeats %.2f  (seconds)\n", bare, spkr, room, echo, two, runaway);

        report ("nothing that rings asks the host for nothing", bare < 0.5,
                juce::String (bare, 2) + " s");
        report ("the speaker alone asks for its own impulse", spkr > bare + 0.5 && spkr < 3.0,
                juce::String (spkr, 2) + " s");
        report ("a hall at double decay outlives the old eight", room > 9.0 && room < 12.0,
                juce::String (room, 2) + " s");
        // Nothing can tell a host this number moved, so while a link that CAN be set to ring long
        // is in the rig the answer never drops below the flat eight that shipped before it. A
        // single two-second echo is really 2.1; a host that asked once and cached it would be
        // told 8, which is what it used to be told anyway.
        report ("a link that can ring long never asks below the old eight",
                echo >= 8.0 && echo < 8.01, juce::String (echo, 2) + " s");

        // The links are in SERIES: the echo is what the room rings ABOUT, so the two add rather
        // than compete. Taking the longest of them would report the room's number and cut the
        // echo's own two seconds off the end of every bounce.
        report ("an echo into a room asks for both, not the longer", two > room + 1.5,
                juce::String (two, 2) + " s vs " + juce::String (room, 2));
        report ("and never more than the cap, however long it rings", runaway <= 30.0 + 1.0e-6,
                juce::String (runaway, 2) + " s");
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
