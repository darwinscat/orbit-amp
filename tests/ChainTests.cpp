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

    orbitamp::AmpProcessor amp;
    std::printf ("  processor constructed\n");

    amp.inlineLoads = true;   // no message loop here: a model lands inside the pump
    amp.prepareToPlay (sampleRate, blockSize);
    std::printf ("  prepared\n\n");
    set (amp, orbitamp::params::stereoMode, 0.0f);   // the gate measures the chain, not the environment default

    if (amp.boost.packs.isEmpty())
    {
        std::printf ("no packs installed — nothing to check\n");
        return 0;
    }

    std::printf ("device: %s\n\n", amp.boost.packs.getReference (0).displayName().toRawUTF8());

    // Everything else off, so what is measured is the boost and nothing standing in front of it.
    // The EQ consoles have no enable of their own — they ship flat, and a flat link is
    // bit-transparent, so there is nothing to switch off here.
    set (amp, orbitamp::params::reverbOn, 0.0f);
    set (amp, orbitamp::params::powerOn, 0.0f);

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

            orbitamp::AmpProcessor solo;
            solo.inlineLoads = true;
            solo.prepareToPlay (sampleRate, blockSize);
            // The bare pack stands at list position 0; the device parameter's factory default is a
            // NAMED pack now, and the pump would walk away to it mid-check.
            set (solo, orbitamp::params::boostDevice, 0.0f);
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
    {
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
    // identical. STEREO SPACE: mono up to the reverb, stereo from it — with the reverb on the two
    // channels differ (the space is wide), with it off they are still identical (the copy at the
    // seam feeds the power amp and the cabinet the same signal). Measured on the plugin's own
    // output, both channels kept.
    {
        const auto runBoth = [&] (float mode, bool reverbOn)
        {
            set (amp, orbitamp::params::stereoMode, mode);
            set (amp, orbitamp::params::reverbOn, reverbOn ? 1.0f : 0.0f);
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
        report ("...and with the reverb off they are one signal again",
                differencePercent (spaceDry.first, spaceDry.second) < 0.01);

        set (amp, orbitamp::params::stereoMode, (float) orbitamp::params::StereoMode::mono);
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
