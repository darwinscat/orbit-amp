// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
// Gate for THE AMP'S BYPASS (AmpProcessor::bypassParameter, the owner 11.11): the whole amp out, the guitar through as
// it came in —
//
//   - it is the host's bypass (getBypassParameter), and it is in no state: not in the tree a session, a preset or a
//     register is written from, so recalling one never takes the amp out;
//   - out, once the fade is done, what leaves is the input as it came in, as late as the amp reports, bit for bit, on
//     both planes;
//   - back in, once the fade is done, the amp plays what an amp that was never taken out plays, bit for bit: it went on
//     playing under the bypass.
//
// OrbitAmp and a cockpit, out of the build's factory folder. main() returns 0 when every check held.

#include "GateMain.h"

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace orbitamp;

namespace
{
    int failed = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    /** A product as it plays, without its first start: nothing gathered from the player's folders. */
    Product quiet (const Product& p)
    {
        auto q = p;
        q.firstStart = nullptr;
        return q;
    }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (p);
        amp->inlineLoads = true;
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        return amp;
    }

    /** The guitar: two partials on the left, the same at half on the right — so a plane taken for the other shows. */
    float guitar (long long n, int plane)
    {
        const double t = (double) n / sampleRate;
        const double s = 0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * t)
                       + 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * 1318.5 * t);
        return (float) (plane == 0 ? s : 0.5 * s);
    }

    /** One block through `amp`, the guitar from sample `at`: what left, both planes. */
    void play (AmpProcessor& amp, long long at, std::vector<float>& l, std::vector<float>& r)
    {
        juce::AudioBuffer<float> b (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            b.setSample (0, i, guitar (at + i, 0));
            b.setSample (1, i, guitar (at + i, 1));
        }
        juce::MidiBuffer midi;
        amp.processBlock (b, midi);
        l.insert (l.end(), b.getReadPointer (0), b.getReadPointer (0) + blockSize);
        r.insert (r.end(), b.getReadPointer (1), b.getReadPointer (1) + blockSize);
    }

    void setBypass (AmpProcessor& amp, bool out)
    {
        amp.bypassParameter().setValueNotifyingHost (out ? 1.0f : 0.0f);
    }

    void gate (const Product& product)
    {
        const juce::String name (product.name);

        // THE HOST'S, AND IN NO STATE.
        {
            auto amp = make (product);
            setBypass (*amp, true);
            const auto tree = amp->stateForSaving();
            check (amp->getBypassParameter() == &amp->bypassParameter()
                       && ! tree.getChildWithProperty ("id", "bypass").isValid() && ! tree.hasProperty ("bypass")
                       && ! amp->apvts.copyState().getChildWithProperty ("id", "bypass").isValid(),
                   name + ": the bypass is the host's, and no tree a session, a preset or a register is written from carries it");
        }

        // OUT: the input as it came in, as late as the amp reports.
        {
            auto amp = make (product);
            setBypass (*amp, true);
            std::vector<float> l, r;
            constexpr int blocks = 40, settled = 8;
            for (int k = 0; k < blocks; ++k)
                play (*amp, (long long) k * blockSize, l, r);

            const int late = amp->getLatencySamples();
            float worst = 0.0f;
            for (int i = settled * blockSize; i < blocks * blockSize; ++i)
            {
                const long long was = (long long) i - late;
                worst = juce::jmax (worst, std::abs (l[(size_t) i] - (was >= 0 ? guitar (was, 0) : 0.0f)),
                                           std::abs (r[(size_t) i] - (was >= 0 ? guitar (was, 1) : 0.0f)));
            }
            check (late >= 0 && worst == 0.0f,
                   name + ": out, what leaves is the input as it came in, " + juce::String (late)
                       + " samples late as the amp reports, bit for bit on both planes (worst " + juce::String (worst, 9) + ")");
        }

        // BACK IN: what an amp never taken out plays.
        {
            auto taken = make (product), never = make (product);
            std::vector<float> tl, tr, nl, nr;
            constexpr int blocks = 60, outAt = 10, inAt = 25, settled = 30;
            for (int k = 0; k < blocks; ++k)
            {
                if (k == outAt) setBypass (*taken, true);
                if (k == inAt)  setBypass (*taken, false);
                play (*taken, (long long) k * blockSize, tl, tr);
                play (*never, (long long) k * blockSize, nl, nr);
            }

            float worst = 0.0f, apart = 0.0f;
            for (int i = settled * blockSize; i < blocks * blockSize; ++i)
                worst = juce::jmax (worst, std::abs (tl[(size_t) i] - nl[(size_t) i]), std::abs (tr[(size_t) i] - nr[(size_t) i]));
            for (int i = (outAt + 3) * blockSize; i < inAt * blockSize; ++i)
                apart = juce::jmax (apart, std::abs (tl[(size_t) i] - nl[(size_t) i]));

            check (apart > 1.0e-3f && worst == 0.0f,
                   name + ": out, it is not the amp (" + juce::String (apart, 4) + " apart); back in, once the fade is done, "
                       "it plays what an amp never taken out plays, bit for bit (worst " + juce::String (worst, 9) + ")");
        }
    }
}

int ORBITAMP_GATE_MAIN()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // a gate that dies says how far it got
    const juce::ScopedJuceInitialiser_GUI juce;
    std::printf ("The amp's bypass\n");

    gate (quiet (products::orbitAmp()));
    gate (quiet (products::catpitAmp()));

    std::printf (failed == 0 ? "ALL PASSED\n" : "%d FAILED\n", failed);
    return failed == 0 ? 0 : 1;
}
