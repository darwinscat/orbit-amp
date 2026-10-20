// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE INPUT'S ZERO (ui/Prefs.h: prefs::inputZero) — where the engine's input trim stands with the
// hand's control at its 0. OrbitAmp's IN fader stands over it (Product::inputTrimOverZero), and its engine
// plays the zero and the fader added (AmpProcessor::inputTrimDb):
//
//   - a fresh OrbitAmp plays its input -9 dB down with the fader at its 0;
//   - a zero chosen moves the trim and leaves the fader where it stood, from the next block on;
//   - a session, a session from before there was a zero, and a preset, each dialled against another zero,
//     open with the fader where it stood, play it over the zero in force and note that zero beside it —
//     the rule the cockpit's and the rovers' lever keeps (face/Console.h);
//   - the cockpit and the rovers play their Input Trim as it stands, their zero being their lever's, and
//     their states carry no note of OrbitAmp's.
//
// What the engine plays is heard where the IN meter and the gate's key hear it: straight after the trim,
// before any block (gateKeyDb). No factory folder: none of it needs a pack. It writes the machine's
// settings only in the home run-gates.sh made for it. main() returns 0 when every check held.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "ui/Prefs.h"

#include <cmath>
#include <cstdio>
#include <memory>

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

    const Product& orbitAmp()
    {
        static const Product p = quiet (products::orbitAmp());
        return p;
    }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (p);
        amp->inlineLoads = true;
        amp->prepareToPlay (sampleRate, blockSize);
        return amp;
    }

    /** What the engine hears of a 220 Hz sine peaking at `peakDb`, straight after its input trim: the IN
        meter's reading, which is the gate's key. A few blocks, so a ramp has done. */
    float heard (AmpProcessor& amp, float peakDb = -6.0f)
    {
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        const float a = juce::Decibels::decibelsToGain (peakDb);

        for (int b = 0, n = 0; b < 4; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                const float s = a * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * n / sampleRate);
                buf.setSample (0, i, s);
                buf.setSample (1, i, s);
            }
            amp.processBlock (buf, midi);
        }
        return amp.gateKeyDb.load();
    }

    bool near (float a, float b, float within = 0.25f) { return std::abs (a - b) <= within; }

    void setParam (AmpProcessor& amp, const char* id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float param (AmpProcessor& amp, const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    /** The zero the state notes beside its trim: -1000 when it notes none. */
    int noted (AmpProcessor& amp)
    {
        const auto v = amp.apvts.state.getProperty (params::inTrimZero);
        return v.isVoid() ? -1000 : (int) v;
    }

    /** The settings file's own word, read past the cache. */
    int stored (const Product& p)
    {
        return (int) prefs::store (p).get (prefs::inputZeroKey, 1000);
    }

    juce::String say (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }

    /** What the processor's timer does in a host, a second of it: the pump, and the history settling
        whatever moved into a step to undo — or into its baseline, where it was no edit. */
    void settle (AmpProcessor& amp)
    {
        for (int i = 0; i < 30; ++i)
        {
            amp.pumpDeviceWork();
            amp.history.tick();
        }
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;
    std::printf ("The input's zero\n");

    // THE SETTING: the values the rovers' lever has always had, and the key it has always been kept under.
    check (prefs::inputZeroKey.toString() == "input_zero_db" && prefs::inputZeroDefault == -9
               && prefs::inputZeroHighest == 12 && prefs::inputZeroLowest == -24 && prefs::inputZeroStep == 3,
           "the input's zero is kept as input_zero_db, -9 dB unless said, from +12 to -24 dB in steps of 3");
    check (prefs::snapInputZero (-4) == -3 && prefs::snapInputZero (40) == 12 && prefs::snapInputZero (-40) == -24,
           "...and a stored zero off the steps lands on the nearest");

    // A FRESH ORBITAMP, on a machine that never chose.
    {
        check (stored (orbitAmp()) == 1000, "a machine that never chose has no zero written down");

        auto a = make (orbitAmp());
        check (prefs::inputZero (orbitAmp()) == -9, "...and its input's zero is -9 dB");
        check (near (param (*a, params::inTrim), 0.0f, 0.01f), "a fresh OrbitAmp's IN fader stands at its 0");

        const float h = heard (*a);
        check (near (a->inputTrimDb(), -9.0f) && near (h, -15.0f),
               "...and its engine plays the input 9 dB down: a -6 dB peak is heard at " + say (h));

        settle (*a);
        check (noted (*a) == -9, "its state notes the zero its trim was dialled against: " + juce::String (noted (*a)));

        // A ZERO CHOSEN: the fader stays, the trim follows, from the next block — and it is no edit.
        const int steps = a->history.undoDepth();
        prefs::setInputZero (orbitAmp(), -3);
        check (stored (orbitAmp()) == -3, "a zero chosen is kept with the machine's settings: " + juce::String (stored (orbitAmp())));
        const float moved = heard (*a);
        check (near (param (*a, params::inTrim), 0.0f, 0.01f) && near (moved, -9.0f),
               "a zero of -3 dB leaves the fader at its 0 and moves the trim: the peak is heard at " + say (moved));
        settle (*a);
        check (noted (*a) == -3 && a->history.undoDepth() == steps, "...the state notes it, and that is no step to undo");

        setParam (*a, params::inTrim, 3.0f);
        const float up = heard (*a);
        check (near (up, -6.0f), "the fader 3 dB up over a zero of -3 dB plays unity: " + say (up));
    }

    // A SESSION SAVED AGAINST ONE ZERO, opened under another.
    {
        prefs::setInputZero (orbitAmp(), -9);
        auto a = make (orbitAmp());
        setParam (*a, params::inTrim, 3.0f);

        juce::MemoryBlock session;
        a->getStateInformation (session);
        const auto saved = juce::AudioProcessor::getXmlFromBinary (session.getData(), (int) session.getSize());
        check (saved != nullptr && saved->toString().contains ("input_zero=\"-9\""),
               "a session made against -9 dB carries the zero beside the trim");

        prefs::setInputZero (orbitAmp(), -3);
        auto b = make (orbitAmp());
        b->setStateInformation (session.getData(), (int) session.getSize());
        const float h = heard (*b);
        check (near (param (*b, params::inTrim), 3.0f, 0.01f), "opened under -3 dB, its fader stays where it stood: +3");
        check (near (b->inputTrimDb(), 0.0f) && near (h, -6.0f), "...and its trim follows the zero in force: heard at " + say (h));
        b->pumpDeviceWork();
        check (noted (*b) == -3, "...and the state notes the zero in force: " + juce::String (noted (*b)));

        // A register dialled against the old zero, switched to: the same.
        a->history.copyRegister (a->history.active(), 1);
        juce::MemoryBlock twoRegisters;
        a->getStateInformation (twoRegisters);
        auto c = make (orbitAmp());
        c->setStateInformation (twoRegisters.getData(), (int) twoRegisters.getSize());
        c->history.switchTo (1);
        c->pumpDeviceWork();
        check (near (param (*c, params::inTrim), 3.0f, 0.01f) && near (c->inputTrimDb(), 0.0f) && noted (*c) == -3,
               "a register dialled against -9 dB plays its fader over the zero in force too");
    }

    // A SESSION FROM BEFORE THERE WAS A ZERO: a bare state, no note — the trim dialled on the engine's own
    // scale, whose 0 was unity.
    {
        auto a = make (orbitAmp());
        setParam (*a, params::inTrim, 3.0f);
        auto bare = a->apvts.copyState();
        bare.removeProperty (params::inTrimZero, nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*bare.createXml(), old);

        auto b = make (orbitAmp());
        b->setStateInformation (old.getData(), (int) old.getSize());
        b->pumpDeviceWork();
        check (near (param (*b, params::inTrim), 3.0f, 0.01f) && near (b->inputTrimDb(), 0.0f) && noted (*b) == -3,
               "a session from before there was a zero keeps its fader at +3 and plays it over the zero in force");
    }

    // A PRESET MADE AGAINST ANOTHER ZERO, loaded the way the preset menu loads one (ui/Chrome.cpp).
    {
        prefs::setInputZero (orbitAmp(), -9);
        auto a = make (orbitAmp());
        setParam (*a, params::inTrim, -2.0f);
        const auto preset = a->presetForSaving();
        check ((int) preset.getProperty (params::inTrimZero, 1000) == -9, "a preset carries the zero beside its trim, as a session does");

        prefs::setInputZero (orbitAmp(), -6);
        auto b = make (orbitAmp());
        b->history.applyEdit (b->history.active(), preset, "Load");
        b->pumpDeviceWork();
        check (near (param (*b, params::inTrim), -2.0f, 0.01f) && near (b->inputTrimDb(), -8.0f) && noted (*b) == -6,
               "loaded under -6 dB, its fader stays at -2 and plays over the zero in force: " + say (b->inputTrimDb()));
    }

    // THE COCKPIT AND THE ROVERS: their zero is their lever's, folded into the trim by their face, and
    // their engine plays the Input Trim as it stands — whatever their own zero, and OrbitAmp's.
    {
        prefs::setInputZero (orbitAmp(), 12);

        for (const auto* each : { &products::catpitAmp(), &products::redPawAmp(), &products::frostPawAmp(), &products::amberPawAmp() })
        {
            const auto product = quiet (*each);
            const juce::String name (product.name);
            prefs::store (product).set (prefs::inputZeroKey, -12);     // their lever's zero, as their menu writes it

            auto amp = make (product);
            const float atZero = heard (*amp);
            setParam (*amp, params::inTrim, -9.0f);
            const float down = heard (*amp);
            amp->pumpDeviceWork();

            check (! product.inputTrimOverZero && near (amp->inputTrimDb(), -9.0f) && near (atZero, -6.0f) && near (down, -15.0f),
                   name + "'s engine plays its Input Trim as it stands: " + say (atZero) + " at 0, " + say (down) + " at -9");
            check (! amp->apvts.state.hasProperty (params::inTrimZero), "...and its state carries no note of OrbitAmp's");
        }
    }

    std::printf (failed == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failed;
}
