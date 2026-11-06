// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Probe: a click in the amp's output. A DI take looped through the whole processor, first left alone for a long
// while, then every block's switch flipped and flipped back, one after another, each followed by seconds of
// playing — a click heard "not at once, after a switch" or "when nobody touches anything" is a jump in the
// output that the take does not have.
//
//   ClickProbe <take.wav> [--state <the standalone's .settings>] [--threads [--plain-thread]] [--window]
//              [--block <n>] [--rate <Hz>] [--ragged] [--quiet <s>] [--hold <s>] [--only <id>,...] [--set <id>=<v>]
//              [--against <out.wav>] [--inject] [--ratio <x>] [--show <id>,...] [--out <dir>]
//
//   --state     plays the player's own session: the standalone's settings file, its filterState
//   --threads   as a player's amp runs: the audio on a real-time thread of its own at the take's pace, the switches
//               and the processor's own timer on the message thread, models loaded by the pool — not in the pump
//   --window    the window built and every picture the audio thread writes for watched
//   --ragged    each block handed over in pieces of any length up to it
//   --only      those switches flipped and no other; --set moves a parameter before the take begins
//   --against   another run's output taken away, and only the difference read (see below)
//   --inject    three clicks laid in by hand, to show they are heard
//   --out       writes the output there, and a half-second around every click
//
// A click is a sample its stretch's own linear prediction misses by far more than it misses the rest (surprise()):
// the amp, its cabinet and the bass leave a wave a predictor follows, and a click, a step or a lost block it cannot.
// The take's own attacks surprise too, and are left out — where the take played in surprises, or the same moment
// a lap away. A block lost to silence or a held sample is read apart. Each block's time is read against the budget
// a device gives it — over it, a dropout. Diagnostic, not a gate.

#include "PluginProcessor.h"
#include "Parameters.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
    using namespace orbitamp;

    double rate = 48000.0;   // --rate

    /** What the probe does at a moment of the take's time. */
    struct Action
    {
        double at;
        juce::String id;
        float to;           // the parameter's plain value
        juce::String what;
    };

    /** A click: where, how far above its neighbours, and what was done last before it. */
    struct Click
    {
        long long at;
        float jump, ratio;
        int channel;
    };

    /** HOW MUCH A SAMPLE SURPRISES: each 2048 samples' own linear prediction, 32 deep, fitted on them, and what it
        misses by — against the median miss of the same stretch. The bass, its amp and its cabinet are a few
        resonances a predictor follows; a click, a lost block, a step is the one thing it cannot see coming. */
    struct Surprise
    {
        std::vector<float> miss, ratio;
    };

    Surprise surprise (const float* x, int n)
    {
        constexpr int order = 32, win = 2048, hop = win / 2;
        Surprise s { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
        std::vector<double> w (win), r (order + 1), a (order + 1), was (order + 1);
        std::vector<float> e (hop);

        for (int start = 0; start + win <= n; start += hop)
        {
            for (int i = 0; i < win; ++i)
                w[(size_t) i] = x[start + i] * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (win - 1)));
            for (int k = 0; k <= order; ++k)
            {
                double sum = 0.0;
                for (int i = k; i < win; ++i)
                    sum += w[(size_t) i] * w[(size_t) (i - k)];
                r[(size_t) k] = sum;
            }
            r[0] = r[0] * (1.0 + 1.0e-6) + 1.0e-15;   // a whisper of white noise keeps silence solvable

            std::fill (a.begin(), a.end(), 0.0);
            a[0] = 1.0;
            double err = r[0];
            for (int m = 1; m <= order && err > 0.0; ++m)
            {
                double acc = r[(size_t) m];
                for (int k = 1; k < m; ++k)
                    acc += a[(size_t) k] * r[(size_t) (m - k)];
                const double kk = -acc / err;
                was = a;
                for (int k = 1; k < m; ++k)
                    a[(size_t) k] = was[(size_t) k] + kk * was[(size_t) (m - k)];
                a[(size_t) m] = kk;
                err *= 1.0 - kk * kk;
            }

            // The stretch's middle half, so the stretches tile the take.
            const int lo = start + win / 4, hi = lo + hop;
            for (int i = lo; i < hi; ++i)
            {
                double p = 0.0;
                for (int k = 0; k <= order; ++k)
                    p += a[(size_t) k] * x[i - k];
                e[(size_t) (i - lo)] = (float) std::abs (p);
                s.miss[(size_t) i] = (float) std::abs (p);
            }
            std::nth_element (e.begin(), e.begin() + hop / 2, e.end());
            const float scale = std::max (e[(size_t) hop / 2] * 1.4826f, 1.0e-7f);
            for (int i = lo; i < hi; ++i)
                s.ratio[(size_t) i] = s.miss[(size_t) i] / scale;
        }
        return s;
    }

    std::vector<Click> findClicks (const std::vector<Surprise>& seen, const juce::AudioBuffer<float>& out, long long from,
                                   float ratioAt, float floorAt)
    {
        std::vector<Click> found;
        const int n = out.getNumSamples();

        for (int ch = 0; ch < (int) seen.size(); ++ch)
        {
            const float* x = out.getReadPointer (ch);
            const auto& s = seen[(size_t) ch];
            long long last = -100000;
            for (int i = (int) from; i < n; ++i)
            {
                if (i - last < (long long) (0.02 * rate))
                    continue;
                if (! std::isfinite (x[i]))
                {
                    found.push_back ({ i, std::numeric_limits<float>::infinity(), 0.0f, ch });
                    last = i;
                }
                else if (s.miss[(size_t) i] > floorAt && s.ratio[(size_t) i] > ratioAt)
                {
                    int top = i;   // the click's own peak, within two milliseconds
                    for (int k = i; k < std::min (n, i + (int) (0.002 * rate)); ++k)
                        if (s.ratio[(size_t) k] > s.ratio[(size_t) top])
                            top = k;
                    found.push_back ({ top, s.miss[(size_t) top], s.ratio[(size_t) top], ch });
                    last = top;
                }
            }
        }

        std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.at < b.at; });
        return found;
    }

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, int from, int count)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), rate,
                                                                         (unsigned) b.getNumChannels(), 24, {}, 0));
        if (w != nullptr)
            w->writeFromAudioSampleBuffer (b, from, count);
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::StringArray args (argv + 1, argc - 1);
    const auto option = [&] (const char* name, const juce::String& fallback) {
        const int i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    const bool threads = args.contains ("--threads");
    const int  block   = option ("--block", "128").getIntValue();
    rate = option ("--rate", "48000").getDoubleValue();
    // --ragged: each block handed over in pieces of any length up to it, as an aggregate device or a host that
    // splits its calls hands them — the processor prepared for `block`, and never told of the pieces.
    const bool ragged = args.contains ("--ragged");
    juce::Random dice (1234);
    const double quietFor = option ("--quiet", "90").getDoubleValue();
    const double holdFor  = option ("--hold", "3").getDoubleValue();
    const float ratioAt   = option ("--ratio", "15").getFloatValue();
    const juce::File outDir (option ("--out", ""));
    const juce::File stateFile (option ("--state", ""));

    if (args.isEmpty() || args[0].startsWith ("--"))
    {
        std::printf ("ClickProbe <take.wav> [--state <settings>] [--threads] [--block <n>] [--quiet <s>] [--hold <s>] [--out <dir>]\n");
        return 2;
    }

    // The take: mono, looped with half a second of silence between, faded at its ends so the loop adds no step.
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File (args[0])));
    if (reader == nullptr)
    {
        std::printf ("no take at %s\n", args[0].toRawUTF8());
        return 1;
    }
    juce::AudioBuffer<float> take (1, (int) reader->lengthInSamples + (int) (0.5 * rate));
    take.clear();
    reader->read (&take, 0, (int) reader->lengthInSamples, 0, true, false);
    const int fade = (int) (0.005 * rate);
    take.applyGainRamp (0, fade, 0.0f, 1.0f);
    take.applyGainRamp ((int) reader->lengthInSamples - fade, fade, 1.0f, 0.0f);
    std::printf ("take %s: %.1f s at %.0f Hz (read as %.0f Hz), block %d, %s\n", args[0].toRawUTF8(),
                 (double) reader->lengthInSamples / reader->sampleRate, reader->sampleRate, rate, block,
                 threads ? "audio on its own thread, at its pace" : "one thread");

    static Product product = ORBITAMP_GATE_PRODUCT();
    product.firstStart = nullptr;
    auto amp = std::make_unique<AmpProcessor> (product);
    amp->inlineLoads = ! threads;
    amp->setPlayConfigDetails (2, 2, rate, block);
    amp->prepareToPlay (rate, block);

    if (stateFile != juce::File())
    {
        juce::PropertiesFile::Options o;
        juce::PropertiesFile props (stateFile, o);
        juce::MemoryBlock state;
        if (! state.fromBase64Encoding (props.getValue ("filterState")))
        {
            std::printf ("no filterState in %s\n", stateFile.getFullPathName().toRawUTF8());
            return 1;
        }
        amp->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);   // the restore lands on the message thread
        std::printf ("state: %s\n", stateFile.getFullPathName().toRawUTF8());
    }
    // --set <id>=<value>, as often as wanted: a parameter moved before the take begins (a reference to hold a
    // finding against — the same take with that block out from the start).
    for (int i = args.indexOf ("--set"); i >= 0 && i + 1 < args.size(); i = args.indexOf ("--set", false, i + 2))
        if (auto* p = amp->apvts.getParameter (args[i + 1].upToFirstOccurrenceOf ("=", false, false)))
        {
            p->setValueNotifyingHost (p->convertTo0to1 (args[i + 1].fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
            std::printf ("set %s\n", args[i + 1].toRawUTF8());
        }

    // --window: the amp's window built, as a host opens it (never shown, as the memory probe builds it), and every
    // picture the audio thread writes for taken as on screen — what the audio thread does more with the face open.
    std::unique_ptr<juce::AudioProcessorEditor> window;
    if (args.contains ("--window"))
    {
        window.reset (amp->createEditor());
        for (auto* w : { &amp->delayWatched, &amp->reverbWatched, &amp->compWatched, &amp->octaveWatched,
                         &amp->filterWatched, &amp->chorusWatched })
            w->store (true);
        std::printf ("window built, every picture watched\n");
    }
    amp->pumpDeviceWork();

    // Every switch the chain holds, as it stands after the state: flipped, then back.
    std::vector<Action> plan;
    const double warm = 4.0;
    double t = warm + quietFor;
    // --only <id>[,<id>...]: those switches flipped and no other ("-" for none).
    const auto only = juce::StringArray::fromTokens (option ("--only", ""), ",", "");
    const auto flip = [&] (const juce::String& id, const juce::String& name) {
        auto* p = amp->apvts.getParameter (id);
        if (p == nullptr || (! only.isEmpty() && ! only.contains (id)))
            return;
        const float now = p->convertFrom0to1 (p->getValue());
        const float other = now > 0.5f ? 0.0f : 1.0f;
        plan.push_back ({ t, id, other, name + (other > 0.5f ? " on" : " off") });
        t += holdFor;
        plan.push_back ({ t, id, now, name + (now > 0.5f ? " on" : " off") });
        t += holdFor;
    };
    for (const auto& link : params::chainLinks)
        if (link.onParam != nullptr)
            flip (link.onParam, link.name);
    for (const auto& link : params::chainLinks)
        if (link.presentParam != nullptr)
            flip (link.presentParam, juce::String (link.name) + " in the strip");
    for (const char* id : { params::bassHpfOn, params::bassLpfOn })
        flip (id, id);
    const double total = t + 10.0;

    std::printf ("as it stands:");
    for (size_t i = 0; i < plan.size(); i += 2)
        std::printf ("  %s", plan[i + 1].what.toRawUTF8());
    for (const auto& id : juce::StringArray::fromTokens (option ("--show", ""), ",", ""))   // --show <id>[,<id>...]
        if (auto* p = amp->apvts.getParameter (id))
            std::printf ("  %s=%g", id.toRawUTF8(), (double) p->convertFrom0to1 (p->getValue()));
    std::printf ("\n");

    std::printf ("plan: %.0f s warm, %.0f s left alone, %d flips %.1f s apart, 10 s alone — %.0f s of take\n", warm,
                 quietFor, (int) plan.size(), holdFor, total);

    const int blocks = (int) (total * rate / block);
    juce::AudioBuffer<float> out (2, blocks * block);
    std::vector<float> took ((size_t) blocks, 0.0f);
    std::atomic<long long> played { 0 };
    size_t next = 0;

    const auto apply = [&] (const Action& a) {
        auto* p = amp->apvts.getParameter (a.id);
        p->setValueNotifyingHost (p->convertTo0to1 (a.to));
    };

    auto playBlock = [&, at = 0LL] (int b) mutable {
        juce::AudioBuffer<float> buf (out.getArrayOfWritePointers(), 2, b * block, block);
        for (int i = 0; i < block; ++i)
        {
            const float s = take.getSample (0, (int) (at % take.getNumSamples()));
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
            ++at;
        }
        juce::MidiBuffer midi;
        const auto start = std::chrono::steady_clock::now();
        if (! ragged)
            amp->processBlock (buf, midi);
        else
            for (int done = 0; done < block;)
            {
                const int n = juce::jmin (block - done, 1 + dice.nextInt (block));
                juce::AudioBuffer<float> piece (out.getArrayOfWritePointers(), 2, b * block + done, n);
                amp->processBlock (piece, midi);
                done += n;
            }
        took[(size_t) b] = std::chrono::duration<float, std::micro> (std::chrono::steady_clock::now() - start).count();
        played = (long long) (b + 1) * block;
    };

    if (threads)
    {
        std::atomic<bool> done { false };
        const auto pace = [&] {
            const auto begin = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                std::this_thread::sleep_until (begin + std::chrono::duration<double> ((double) b * block / rate));
                playBlock (b);
            }
            done = true;
        };

        // A device's audio thread is a real-time one (the system's audio workgroup); --plain-thread plays on an
        // ordinary thread instead, which the system parks and slows as it likes.
        struct Audio final : juce::Thread
        {
            std::function<void()> body;
            explicit Audio (std::function<void()> b) : juce::Thread ("ClickProbe audio"), body (std::move (b)) {}
            void run() override { body(); }
        } audio (pace);
        if (args.contains ("--plain-thread"))
            audio.startThread();
        else
            (void) audio.startRealtimeThread (juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime (block, rate));
        std::printf ("audio thread: %s\n", args.contains ("--plain-thread") ? "plain" : "real-time");

        while (! done)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);   // the processor's own timer pumps it
            while (next < plan.size() && (double) played / rate >= plan[next].at)
                apply (plan[next++]);
        }
        audio.stopThread (1000);
    }
    else
    {
        const int pumpEvery = std::max (1, (int) (rate / 30.0 / block));
        for (int b = 0; b < blocks; ++b)
        {
            playBlock (b);
            while (next < plan.size() && (double) played / rate >= plan[next].at)
                apply (plan[next++]);
            if (b % pumpEvery == 0)
                amp->pumpDeviceWork();
        }
    }

    // What was done last before a moment, and how long before.
    const auto before = [&] (double sec) -> juce::String {
        const Action* last = nullptr;
        for (const auto& a : plan)
            if (a.at <= sec)
                last = &a;
        if (last == nullptr)
            return sec < warm + quietFor ? "left alone" : "-";
        return juce::String (sec - last->at, 3) + " s after " + last->what;
    };

    // --inject: three clicks laid into the output by hand, to show the probe hears them — a block lost to
    // silence at 30 s, a one-sample spike of 0.005 at 50 s, a step of 0.005 held a millisecond at 70 s.
    if (args.contains ("--inject"))
        for (int ch = 0; ch < 2; ++ch)
        {
            out.clear (ch, (int) (30.0 * rate), block);
            out.getWritePointer (ch)[(int) (50.0 * rate)] += 0.005f;
            for (int i = 0; i < (int) (0.001 * rate); ++i)
                out.getWritePointer (ch)[(int) (70.0 * rate) + i] += 0.005f;
        }

    // --against <out.wav>: another run's output taken away from this one, and only what is left read — the same
    // take, the same chain, one block left out there from the start: what remains is that block's own sound, and
    // a click it made. The take's attacks are gone from the difference, so nothing is left out as theirs.
    const juce::File against (option ("--against", ""));
    if (against != juce::File())
    {
        std::unique_ptr<juce::AudioFormatReader> ref (fm.createReaderFor (against));
        if (ref == nullptr)
        {
            std::printf ("no run to hold against at %s\n", against.getFullPathName().toRawUTF8());
            return 1;
        }
        const int length = (int) std::min ((juce::int64) out.getNumSamples(), ref->lengthInSamples);
        juce::AudioBuffer<float> other (2, length);
        ref->read (&other, 0, length, 0, true, true);
        for (int ch = 0; ch < 2; ++ch)
        {
            out.addFrom (ch, 0, other, ch, 0, length, -1.0f);
            out.clear (ch, length, out.getNumSamples() - length);
        }
        std::printf ("against %s: the difference read, %.1f s\n", against.getFullPathName().toRawUTF8(), length / rate);
    }

    std::vector<Surprise> seen;
    for (int ch = 0; ch < 2; ++ch)
        seen.push_back (surprise (out.getReadPointer (ch), out.getNumSamples()));

    // The take's own attacks: where what was played in surprises, the amp's answer may too — and so may the same
    // moment a lap before or after, whatever the chorus and the echo did to it meanwhile. Neither is a click.
    const int lap = take.getNumSamples();
    std::vector<float> fed ((size_t) out.getNumSamples());
    for (int i = 0; i < out.getNumSamples(); ++i)
        fed[(size_t) i] = take.getSample (0, i % lap);
    const auto heard = surprise (fed.data(), (int) fed.size());
    const auto most = [] (const std::vector<float>& r, long long from, long long to) {
        float m = 0.0f;
        for (long long i = std::max (0LL, from); i < std::min ((long long) r.size(), to); ++i)
            m = std::max (m, r[(size_t) i]);
        return m;
    };
    std::vector<Click> clicks;
    int laps = 0;
    for (const auto& c : findClicks (seen, out, (long long) (warm * rate), ratioAt, 2.0e-4f))
    {
        bool again = std::isfinite (c.jump) && against == juce::File()
                  && most (heard.ratio, c.at - (long long) (0.04 * rate), c.at + (long long) (0.005 * rate)) >= 10.0f;
        for (int k : { -2, -1, 1, 2 })
        {
            const long long there = c.at + (long long) k * lap;
            if (there < (long long) (warm * rate) || there + 8 >= out.getNumSamples() || ! std::isfinite (c.jump)
                || against != juce::File())
                continue;
            const long long near = (long long) (0.01 * rate);
            again = again || most (seen[(size_t) c.channel].ratio, there - near, there + near) >= 8.0f;
        }
        if (again)
            ++laps;
        else
            clicks.push_back (c);
    }
    std::printf ("\n%d surprise(s) at the take's own attacks, left out\n", laps);

    // A block lost — the device given silence, or the last sample held — is a run of samples that do not move
    // while the sound around them is loud: no attack hides that, so it is read apart from the surprises.
    int stuck = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* x = out.getReadPointer (ch);
        const int around = (int) (0.01 * rate);
        int run = 0;
        for (int i = (int) (warm * rate); i < out.getNumSamples() - around; ++i)
        {
            run = juce::exactlyEqual (x[i], x[i - 1]) ? run + 1 : 0;
            if (run != 16)
                continue;
            float loud = 0.0f;
            for (int k = i - 16 - around; k < i - 16; ++k)
                loud = std::max (loud, std::abs (x[k]));
            if (loud > 1.0e-2f)
            {
                ++stuck;
                std::printf ("  %8.3f s  ch %d  STUCK at %.5f after %.3f around   %s\n", (double) (i - 16) / rate, ch,
                             (double) x[i], (double) loud, before ((double) i / rate).toRawUTF8());
            }
        }
    }
    std::printf ("%d stuck run(s)\n", stuck);

    std::printf ("\n%d click(s) after the warm-up\n", (int) clicks.size());
    for (const auto& c : clicks)
    {
        const double sec = (double) c.at / rate;
        std::printf ("  %8.3f s  ch %d  jump %8.5f  x%6.1f  in the take %6.3f s   %s\n", sec, c.channel, (double) c.jump,
                     (double) c.ratio, (double) (c.at % take.getNumSamples()) / rate, before (sec).toRawUTF8());
    }

    // The blocks' time against the budget a device gives one.
    const float budget = (float) (1.0e6 * block / rate);
    std::vector<float> sorted (took.begin() + (long) (warm * rate / block), took.end());
    std::sort (sorted.begin(), sorted.end());
    std::printf ("\nblock time (budget %.0f us): median %.0f, 99.9%% %.0f, worst %.0f us\n", (double) budget,
                 (double) sorted[sorted.size() / 2], (double) sorted[(size_t) (0.999 * (double) sorted.size())],
                 (double) sorted.back());
    int over = 0;
    for (int b = (int) (warm * rate / block); b < blocks; ++b)
        if (took[(size_t) b] > 0.5f * budget)
        {
            if (++over <= 20)
                std::printf ("  block at %8.3f s took %6.0f us   %s\n", (double) b * block / rate, (double) took[(size_t) b],
                             before ((double) b * block / rate).toRawUTF8());
        }
    std::printf ("%d block(s) over half the budget\n", over);

    if (outDir != juce::File())
    {
        outDir.createDirectory();
        writeWav (outDir.getChildFile ("out.wav"), out, 0, out.getNumSamples());
        int k = 0;
        for (const auto& c : clicks)
        {
            const int from = std::max (0, (int) c.at - (int) (0.25 * rate));
            const int count = std::min ((int) (0.5 * rate), out.getNumSamples() - from);
            writeWav (outDir.getChildFile (juce::String::formatted ("click-%02d-%.3fs.wav", ++k, (double) c.at / rate)),
                      out, from, count);
            if (k >= 30)
                break;
        }
        std::printf ("\nwritten to %s\n", outDir.getFullPathName().toRawUTF8());
    }
    return 0;
}
