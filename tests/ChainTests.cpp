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
        effect. The frequency is the caller's, because what is audible depends on what is asked:
        a low shelf does nothing to a 220 Hz probe and everything to one inside its band. */
    std::vector<float> run (orbitamp::AmpProcessor& amp, double toneHz = 220.0)
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

    if (amp.boost.packs.isEmpty())
    {
        std::printf ("no packs installed — nothing to check\n");
        return 0;
    }

    std::printf ("device: %s\n\n", amp.boost.packs.getReference (0).displayName().toRawUTF8());

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
            const auto& m = (*measured)[(size_t) sweep];

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

    // The advanced EQ, and the thing it exists for: the SAME filter before and after the capture is
    // not the same sound. Post colours what came out. Pre changes what arrives at the nonlinearity,
    // so it changes what kind of distortion happens at all — which is how one captured voice becomes
    // two instruments, and is the reason the placement is a control rather than a constant.
    {
        set (amp, orbitamp::params::boostGain, 8.0f);   // well into the dirt, or there is no
                                                        // nonlinearity for PRE to act on
        set (amp, orbitamp::params::advOn (orbitamp::params::boostId), 0.0f);
        const auto clean = run (amp);

        set (amp, orbitamp::params::advOn (orbitamp::params::boostId), 1.0f);
        set (amp, orbitamp::params::advHpf (orbitamp::params::boostId), 800.0f);
        set (amp, orbitamp::params::advPre (orbitamp::params::boostId), 0.0f);
        const auto post = run (amp);

        set (amp, orbitamp::params::advPre (orbitamp::params::boostId), 1.0f);
        const auto pre = run (amp);

        std::printf ("\nadvanced EQ: off %.5f, post %.5f, pre %.5f\n",
                     rms (clean), rms (post), rms (pre));
        std::printf ("post differs from off by %.1f%%, pre from post by %.1f%%\n\n",
                     differencePercent (clean, post), differencePercent (post, pre));

        report ("the advanced EQ reaches the audio",
                differencePercent (clean, post) > 5.0,
                juce::String (differencePercent (clean, post), 1) + "% different");

        report ("...and PRE is not POST", differencePercent (post, pre) > 5.0,
                juce::String (differencePercent (post, pre), 1) + "% apart");

        set (amp, orbitamp::params::advOn (orbitamp::params::boostId), 0.0f);
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
            bareStage->measured.clear();
            bareStage->device.files.resize (1);

            orbitamp::AmpProcessor solo;
            solo.prepareToPlay (sampleRate, blockSize);
            set (solo, orbitamp::params::eqOn, 0.0f);
            set (solo, orbitamp::params::reverbOn, 0.0f);
            set (solo, orbitamp::params::powerOn, 0.0f);
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

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
