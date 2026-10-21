// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE INPUT'S LEVEL: how hard a capture is fed is its pack's to say (`chain[].input_db`), and the
// input trim — OrbitAmp's IN fader, the cockpit's and the rovers' INPUT lever — is unity at its 0, for every
// amp, with no machine setting under it:
//
//   - every amp, fresh, hears its input at unity with its control at 0, and feeds its pack at the level the
//     pack states (-9 dB on the set's packs) — on a machine where 0.8.0 left a lever's zero written down too;
//   - a cockpit's or a rover's state as 0.8.0 wrote it — the trim absolute, the lever's zero it had folded in
//     noted beside it (Product::formerInputZeroId) — opens feeding its model what it fed it then, with the
//     pack's -9 dB where 0.8.0's -9 dB zero stood, and its gate opening on the same guitar as then; every
//     register of it, and a preset of 0.8.0's, likewise. One made under a zero the player had chosen
//     shifts by that zero's distance from -9 dB, and its gate still opens on the same guitar;
//   - a state no 0.8.0 window ever saw, and OrbitAmp's, are left as they are;
//   - a rover's channels keep no INPUT of their own.
//
// The 0.8.0 states are made here the way 0.8.0's face wrote them (git show v0.8.0:src/face/Console.cpp,
// settleOnSettings): the trim, absolute, in its parameter, and `rover_input_zero` / `catpit_input_zero` on
// the state's root. The input is heard where the IN meter and the gate's key hear it, straight after the
// trim (gateKeyDb); what the model is fed adds the pack's own level to that. Out of the build's factory
// folder, so every amp plays a pack. It writes the machine's settings only in the home run-gates.sh made
// for it. main() returns 0 when every check held.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "channels/Engine.h"
#include "channels/Presets.h"
#include "ui/Prefs.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

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

    const Product& orbitAmp()    { static const Product p = quiet (products::orbitAmp());    return p; }
    const Product& catpitAmp()   { static const Product p = quiet (products::catpitAmp());   return p; }
    const Product& redPawAmp()   { static const Product p = quiet (products::redPawAmp());   return p; }
    const Product& frostPawAmp() { static const Product p = quiet (products::frostPawAmp()); return p; }
    const Product& amberPawAmp() { static const Product p = quiet (products::amberPawAmp()); return p; }

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

    float param (AmpProcessor& amp, const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    void setParam (AmpProcessor& amp, const char* id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float trim (AmpProcessor& amp) { return param (amp, params::inTrim); }
    float threshold (AmpProcessor& amp) { return param (amp, params::gateThreshold); }

    /** How hard the model is fed, in dB from what comes in: the input trim and the pack's own level
        (`chain[].input_db`). TURBO's push and the files' alias trims aside, which no check here moves. */
    float fed (AmpProcessor& amp) { return trim (amp) + (float) amp.preamp.packInputDb(); }

    /** Whether the gate's key hears a guitar peaking at `peakDb` over the threshold: where the gate opens. */
    bool opensOn (AmpProcessor& amp, float peakDb) { return heard (amp, peakDb) > threshold (amp); }

    juce::String say (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }

    juce::MemoryBlock session (AmpProcessor& amp)
    {
        juce::MemoryBlock m;
        amp.getStateInformation (m);
        return m;
    }

    juce::ValueTree treeOf (const juce::MemoryBlock& m)
    {
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize()))
            return juce::ValueTree::fromXml (*xml);
        return {};
    }

    juce::MemoryBlock bytesOf (const juce::ValueTree& t)
    {
        juce::MemoryBlock m;
        if (auto xml = t.createXml())
            juce::AudioProcessor::copyXmlToBinary (*xml, m);
        return m;
    }

    void setInTree (juce::ValueTree state, const char* id, float value)
    {
        if (auto t = state.getChildWithProperty ("id", juce::String (id)); t.isValid())
            t.setProperty ("value", value, nullptr);
    }

    /** A state tree as 0.8.0's face left it: the trim absolute, the gate where the player had it, and the
        lever's zero it had folded in noted under the product's former name (none: a state no 0.8.0 window
        ever saw). */
    void asIn080 (juce::ValueTree state, const Product& p, float absolute, float gate, std::optional<int> dialled)
    {
        setInTree (state, params::inTrim, absolute);
        setInTree (state, params::gateThreshold, gate);
        if (dialled.has_value() && p.formerInputZeroId != nullptr)
            state.setProperty (p.formerInputZeroId, *dialled, nullptr);
    }

    /** Whether any tree in `t` still carries the product's former note. */
    bool carriesFormer (const juce::ValueTree& t, const Product& p)
    {
        if (p.formerInputZeroId == nullptr)
            return false;
        if (t.hasProperty (p.formerInputZeroId))
            return true;
        for (const auto& c : t)
            if (carriesFormer (c, p))
                return true;
        return false;
    }
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // a gate that dies says how far it got
    const juce::ScopedJuceInitialiser_GUI juce;
    std::printf ("The input's level\n");

    check (orbitAmp().formerInputZeroId == nullptr
               && juce::String (catpitAmp().formerInputZeroId) == "catpit_input_zero"
               && juce::String (redPawAmp().formerInputZeroId) == "rover_input_zero"
               && juce::String (frostPawAmp().formerInputZeroId) == "rover_input_zero"
               && juce::String (amberPawAmp().formerInputZeroId) == "rover_input_zero",
           "the cockpit and the rovers know the name 0.8.0 noted their lever's zero under; OrbitAmp has none");

    // EVERY AMP, FRESH — on a machine where 0.8.0 left a lever's zero of -12 dB written down, which nothing
    // reads now.
    for (const auto* each : { &orbitAmp(), &catpitAmp(), &redPawAmp(), &frostPawAmp(), &amberPawAmp() })
    {
        const auto& p = *each;
        prefs::store (p).set (juce::Identifier ("input_zero_db"), -12);

        auto a = make (p);
        const float h = heard (*a);
        a->pumpDeviceWork();
        const float stated = (float) a->preamp.packInputDb();      // the pack's own chain[].input_db
        check (near (trim (*a), 0.0f, 0.01f) && near (h, -6.0f) && stated < 0.0f
                   && near (fed (*a), stated) && ! a->apvts.state.hasProperty ("input_zero"),
               juce::String (p.name) + ", fresh: unity at its control's 0, a -6 dB peak heard at " + say (h)
                   + ", its pack fed at the level the pack states, " + say (fed (*a)));
    }

    // A REDPAWAMP SESSION OF 0.8.0'S, made under the lever's zero of -9 dB that 0.8.0 had unless the player
    // chose another: INPUT at -6 dB absolute (the lever a little over its middle), the gate at -50 dB; register
    // B at -12 dB and -60 dB. Its packs stated no level, so its model was fed -6 dB, and B's -12 dB, and the
    // gate opened on a guitar over -44 dB (-50 less the -6 its key heard after), B's over -48 dB.
    auto a = make (redPawAmp());
    a->history.copyRegister (a->history.active(), 1);
    auto workspace = treeOf (session (*a));
    auto live = workspace.getChildWithName ("Live").getChild (0);
    auto snap = workspace.getChildWithName ("Snaps").getChild (1).getChild (0);
    check (live.isValid() && snap.isValid(), "a RedPawAmp session holds its live state and register B");
    asIn080 (live, redPawAmp(), -6.0f, -50.0f, -9);
    asIn080 (snap, redPawAmp(), -12.0f, -60.0f, -9);
    const auto old = bytesOf (workspace);

    auto b = make (redPawAmp());
    b->setStateInformation (old.getData(), (int) old.getSize());
    b->pumpDeviceWork();
    check (near (trim (*b), 3.0f, 0.01f) && near (fed (*b), 3.0f + (float) b->preamp.packInputDb()),
           "RedPawAmp's 0.8.0 session keeps its lever at +3 and feeds its model " + say (fed (*b))
               + ": the lever over the pack's own level, " + say ((float) b->preamp.packInputDb()));
    check (near (threshold (*b), -41.0f, 0.01f) && opensOn (*b, -43.0f) && ! opensOn (*b, -45.0f),
           "...its gate at " + say (threshold (*b)) + " opens on a guitar over -44 dB, as it did");
    check (! carriesFormer (b->apvts.state, redPawAmp()) && ! carriesFormer (treeOf (session (*b)), redPawAmp()),
           "...and the note of 0.8.0's zero is gone from what it plays and what it saves");
    b->history.switchTo (1);
    b->pumpDeviceWork();
    check (near (trim (*b), -3.0f, 0.01f) && near (fed (*b), -3.0f + (float) b->preamp.packInputDb()) && near (threshold (*b), -51.0f, 0.01f)
               && opensOn (*b, -47.0f) && ! opensOn (*b, -49.0f),
           "...its register B, the lever at -3, fed " + say (fed (*b)) + ", its gate opening on a guitar over -48 dB, as it did");

    // THE COCKPIT, made under a zero the player had chosen, -12 dB: INPUT at -10 dB absolute, the gate at
    // -50 dB. 0.8.0 fed its model -10 dB and opened the gate over a -40 dB guitar. The lever keeps its place
    // over the zero it had, +2, and the pack's own measured level stands under it now where 0.8.0's zero did;
    // its gate still opens on the same guitar.
    {
        auto c = make (catpitAmp());
        auto state = c->apvts.copyState();
        asIn080 (state, catpitAmp(), -10.0f, -50.0f, -12);
        const auto bare = bytesOf (state);
        auto d = make (catpitAmp());
        d->setStateInformation (bare.getData(), (int) bare.getSize());
        d->pumpDeviceWork();
        check (near (trim (*d), 2.0f, 0.01f) && near (fed (*d), 2.0f + (float) d->preamp.packInputDb()),
               "CatpitAmp's 0.8.0 state under a chosen -12 dB: the lever +2, its model fed " + say (fed (*d))
                   + ", the lever over the pack's own level");
        check (near (threshold (*d), -38.0f, 0.01f) && opensOn (*d, -39.0f) && ! opensOn (*d, -41.0f),
               "...its gate at " + say (threshold (*d)) + " opening on a guitar over -40 dB, as it did");
    }

    // ONE NO 0.8.0 WINDOW EVER SAW: no note. Its trim was the engine's own then and is now.
    {
        auto f = make (frostPawAmp());
        auto unseen = f->apvts.copyState();
        asIn080 (unseen, frostPawAmp(), 2.0f, -50.0f, std::nullopt);
        const auto never = bytesOf (unseen);
        auto g = make (frostPawAmp());
        g->setStateInformation (never.getData(), (int) never.getSize());
        g->pumpDeviceWork();
        check (near (trim (*g), 2.0f, 0.01f) && near (threshold (*g), -50.0f, 0.01f) && near (fed (*g), 2.0f + (float) g->preamp.packInputDb()),
               "a 0.8.0 state with no note keeps its trim and its gate as they were: fed " + say (fed (*g)));
    }

    // ORBITAMP'S, from before: its fader and its gate as they were — OrbitAmp's trim was never more than that.
    {
        auto o = make (orbitAmp());
        auto state = o->apvts.copyState();
        asIn080 (state, orbitAmp(), 3.0f, -50.0f, -9);
        state.setProperty ("rover_input_zero", -9, nullptr);    // a name that is not OrbitAmp's does nothing
        const auto bytes = bytesOf (state);
        auto q = make (orbitAmp());
        q->setStateInformation (bytes.getData(), (int) bytes.getSize());
        q->pumpDeviceWork();
        check (near (trim (*q), 3.0f, 0.01f) && near (threshold (*q), -50.0f, 0.01f) && near (fed (*q), 3.0f + (float) q->preamp.packInputDb()),
               "an OrbitAmp state from before keeps its fader at +3 and its gate, and feeds its pack " + say (fed (*q)));
    }

    // A ROVER'S PRESET OF 0.8.0'S: a rover's preset holds no INPUT, and still does not; its gate is converted.
    {
        b->history.switchTo (0);
        auto& engine = channels::Engine::of (*b);
        channels::Presets presets (*b, engine.channels);
        auto file = b->presetForSaving();
        asIn080 (file, redPawAmp(), 5.0f, -60.0f, -9);
        check (presets.store().write ("From 0.8.0", file), "a 0.8.0 preset written to RedPawAmp's folder");
        setParam (*b, params::inTrim, 1.0f);
        check (presets.load ("From 0.8.0") && near (trim (*b), 1.0f, 0.01f) && near (threshold (*b), -51.0f, 0.01f)
                   && ! carriesFormer (b->apvts.state, redPawAmp()),
               "...loaded, INPUT stays where the player has it, +1, and its gate comes at " + say (threshold (*b)));

        // A rover's channels keep no INPUT of their own.
        const bool went = engine.go (2);
        b->pumpDeviceWork();
        check (went && engine.channels.active() == 2 && near (trim (*b), 1.0f, 0.01f),
               "a channel changed leaves INPUT where it was: a rover's channel keeps no INPUT of its own");
    }

    std::printf (failed == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failed;
}
