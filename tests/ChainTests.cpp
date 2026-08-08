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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
        effect. */
    std::vector<float> run (orbitamp::AmpProcessor& amp)
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
                const float s = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                          * 220.0 * phase / sampleRate);
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
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("orbitamp chain gate\n\n");

    orbitamp::AmpProcessor amp;
    amp.prepareToPlay (sampleRate, blockSize);

    if (amp.devicePacks.isEmpty())
    {
        std::printf ("no packs installed — nothing to check\n");
        return 0;
    }

    std::printf ("device: %s\n\n", amp.devicePacks.getReference (0).name.toRawUTF8());

    // Everything else off, so what is measured is the boost and nothing standing in front of it.
    set (amp, orbitamp::params::eqOn, 0.0f);
    set (amp, orbitamp::params::reverbOn, 0.0f);
    set (amp, orbitamp::params::powerOn, 0.0f);

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
    {
        const auto* measured = amp.boost.measured();

        int sweep = -1;
        if (measured != nullptr)
            for (int i = 0; i < (int) measured->size() && i < orbitamp::params::boostNumMeasured; ++i)
                if ((*measured)[(size_t) i].positions.size() > 2) { sweep = i; break; }

        if (sweep < 0)
        {
            std::printf ("no swept measured control on this device — skipping the tone check\n");
        }
        else
        {
            const auto id = orbitamp::params::boostMeasured (sweep);
            std::printf ("measured control: %s\n",
                         juce::String ((*measured)[(size_t) sweep].name).toRawUTF8());

            set (amp, id, 0.0f);
            const auto dark = run (amp);

            set (amp, id, 1.0f);
            const auto bright = run (amp);

            std::printf ("min -> rms %.5f, max -> rms %.5f\n\n", rms (dark), rms (bright));

            // A tone control DOES change the level at a single frequency, so this one can say dB.
            report ("a measured knob reaches the audio too",
                    std::abs (20.0 * std::log10 (juce::jmax (1.0e-9, rms (bright) / rms (dark)))) > 1.0,
                    juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, rms (bright) / rms (dark))), 1)
                        + " dB apart");
        }
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
