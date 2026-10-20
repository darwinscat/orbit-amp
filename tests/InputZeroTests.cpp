// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE INPUT'S ZERO (ui/Prefs.h: prefs::inputZero) — where the engine's input trim stands with the
// hand's control at its 0: OrbitAmp's IN fader, the cockpit's and the rovers' INPUT lever. Every amp's engine
// plays the zero and the trim added (AmpProcessor::inputTrimDb), with no window open:
//
//   - every amp, fresh, plays its input -9 dB down with its control at its 0;
//   - a zero chosen moves the sound and leaves the control where it stood, from the next block on, and is
//     no step to undo; each amp keeps its own;
//   - every amp offers it the same way, under one name (ui/InputZeroMenu.h) — OrbitAmp in its SETUP window;
//   - a state dialled against another zero, or before there was one, keeps the control where it stood and
//     plays it over the zero in force, and notes the zero in force beside it;
//   - a cockpit's or a rover's state as 0.8.0 wrote it — an absolute trim, and beside it under the lineup's
//     own name the zero its face had folded in (Product::formerInputZeroId) — opens playing exactly what it
//     played then, its registers too, and a preset of 0.8.0's leaves INPUT where the player has it;
//   - a rover's channels keep no INPUT of their own: a channel changed leaves it where it was.
//
// The 0.8.0 states are made here the way 0.8.0's face wrote them (git show v0.8.0:src/face/Console.cpp,
// settleOnSettings): the trim, absolute, in its parameter, and `rover_input_zero` / `catpit_input_zero` on
// the state's root. What the engine plays is heard where the IN meter and the gate's key hear it: straight
// after the trim, before any block (gateKeyDb). Out of the build's factory folder, so that a rover has the
// packs to change channel to. It writes the machine's settings only in the home run-gates.sh made for it.
// main() returns 0 when every check held.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "channels/Engine.h"
#include "channels/Presets.h"
#include "ui/InputZeroMenu.h"
#include "ui/SetupPanel.h"

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

    void setParam (AmpProcessor& amp, const char* id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float trim (AmpProcessor& amp)
    {
        auto* p = amp.apvts.getParameter (params::inTrim);
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

    /** A state tree as 0.8.0's face left it: the trim absolute, and the zero it had folded in noted under
        the product's former name (none: a state no 0.8.0 window ever saw). */
    void asIn080 (juce::ValueTree state, const Product& p, float absolute, std::optional<int> dialled)
    {
        state.removeProperty (params::inTrimZero, nullptr);
        if (auto t = state.getChildWithProperty ("id", juce::String (params::inTrim)); t.isValid())
            t.setProperty ("value", absolute, nullptr);
        if (dialled.has_value())
            state.setProperty (p.formerInputZeroId, *dialled, nullptr);
    }

    /** Whether any tree in `t` still carries the product's former note. */
    bool carriesFormer (const juce::ValueTree& t, const Product& p)
    {
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
    std::printf ("The input's zero\n");

    const std::vector<const Product*> family { &orbitAmp(), &catpitAmp(), &redPawAmp(), &frostPawAmp(), &amberPawAmp() };

    // THE SETTING: the values the cockpit's lever has always had, and the key it has always kept it under.
    check (prefs::inputZeroKey.toString() == "input_zero_db" && prefs::inputZeroDefault == -9
               && prefs::inputZeroHighest == 12 && prefs::inputZeroLowest == -24 && prefs::inputZeroStep == 3,
           "the input's zero is kept as input_zero_db, -9 dB unless said, from +12 to -24 dB in steps of 3");
    check (prefs::snapInputZero (-4) == -3 && prefs::snapInputZero (40) == 12 && prefs::snapInputZero (-40) == -24,
           "...and a stored zero off the steps lands on the nearest");
    check (orbitAmp().formerInputZeroId == nullptr
               && juce::String (catpitAmp().formerInputZeroId) == "catpit_input_zero"
               && juce::String (redPawAmp().formerInputZeroId) == "rover_input_zero"
               && juce::String (frostPawAmp().formerInputZeroId) == "rover_input_zero"
               && juce::String (amberPawAmp().formerInputZeroId) == "rover_input_zero",
           "the cockpit and the rovers know the name 0.8.0 noted their zero under; OrbitAmp has none");

    // ONE SECTION, ONE NAME, for every amp's settings.
    {
        const auto lines = prefs::inputZeroMenu (redPawAmp());
        int ticked = 0;
        for (const auto& l : lines)
            ticked += l.ticked ? 1 : 0;
        check (prefs::inputZeroTitle == "INPUT'S ZERO" && lines.size() == 13 && lines.front().text == "+12 dB"
                   && lines.back().text == "-24 dB" && lines[7].text == "-9 dB" && lines[7].ticked && ticked == 1,
               "every amp's settings offer INPUT'S ZERO: +12 dB to -24 dB, the one in force ticked");
        check (prefs::inputZeroState (catpitAmp()) == "INPUT'S ZERO  -9 dB", "...titled with the zero in force where it is a submenu");

        const auto rows = SetupPanel::inputRows (orbitAmp());
        bool same = rows.size() == 1 && rows[0].name == prefs::inputZeroTitle && rows[0].choices.size() == (int) lines.size();
        for (size_t k = 0; same && k < lines.size(); ++k)
            same = rows[0].choices[(int) k] == lines[k].text;
        check (same && SettingsList::opensAsMenu (rows[0]) && rows[0].chosen() == 7,
               "OrbitAmp's SETUP has it on its INPUT page: the same thirteen, opened as a menu, -9 dB standing");
        rows[0].choose (5);
        check (prefs::inputZero (orbitAmp()) == -3 && rows[0].chosen() == 5, "...and a pick there is OrbitAmp's zero");
        rows[0].choose (7);
    }

    // EVERY AMP, FRESH, on a machine that never chose — and a zero chosen.
    for (const auto* each : family)
    {
        const auto& p = *each;
        const juce::String name (p.name);

        auto a = make (p);
        const float h = heard (*a);
        settle (*a);
        check (prefs::inputZero (p) == -9 && near (trim (*a), 0.0f, 0.01f) && near (a->inputTrimDb(), -9.0f)
                   && near (h, -15.0f) && noted (*a) == -9,
               name + ", fresh and with no window, plays -9 dB at its control's 0: a -6 dB peak heard at " + say (h)
                   + ", the zero noted beside the trim");

        const int steps = a->history.undoDepth();
        prefs::setInputZero (p, -3);
        const float moved = heard (*a);
        settle (*a);
        check (near (trim (*a), 0.0f, 0.01f) && near (moved, -9.0f) && noted (*a) == -3 && a->history.undoDepth() == steps
                   && stored (p) == -3,
               "...a zero of -3 dB moves the sound to " + say (moved) + " and leaves the control at its 0, no step to undo");

        setParam (*a, params::inTrim, 3.0f);
        const float up = heard (*a);
        check (near (up, -6.0f), "...and the control 3 dB up over it plays unity: " + say (up));
        prefs::setInputZero (p, -9);
    }

    // EACH ITS OWN: one amp's zero is not another's.
    {
        prefs::setInputZero (catpitAmp(), -6);
        auto c = make (catpitAmp());
        auto r = make (redPawAmp());
        const float hc = heard (*c), hr = heard (*r);
        check (near (hc, -12.0f) && near (hr, -15.0f),
               "CatpitAmp at -6 dB and RedPawAmp at -9 dB each play their own: " + say (hc) + ", " + say (hr));
        prefs::setInputZero (catpitAmp(), -9);
    }

    // ORBITAMP'S STATES dialled against another zero, or before there was one.
    {
        auto a = make (orbitAmp());
        setParam (*a, params::inTrim, 3.0f);
        a->history.copyRegister (a->history.active(), 1);
        const auto saved = session (*a);
        check (treeOf (saved).toXmlString().contains ("input_zero=\"-9\""), "an OrbitAmp session made against -9 dB carries the zero beside the trim");

        prefs::setInputZero (orbitAmp(), -3);
        auto b = make (orbitAmp());
        b->setStateInformation (saved.getData(), (int) saved.getSize());
        const float h = heard (*b);
        settle (*b);
        check (near (trim (*b), 3.0f, 0.01f) && near (b->inputTrimDb(), 0.0f) && near (h, -6.0f) && noted (*b) == -3,
               "opened under -3 dB, its fader stays at +3 and plays over the zero in force: " + say (h));
        b->history.switchTo (1);
        b->pumpDeviceWork();
        check (near (trim (*b), 3.0f, 0.01f) && near (b->inputTrimDb(), 0.0f) && noted (*b) == -3,
               "...and so does the register dialled against -9 dB");

        // A session from before there was a zero: a bare state, no note — dialled on the engine's own scale.
        auto bare = a->apvts.copyState();
        bare.removeProperty (params::inTrimZero, nullptr);
        auto c = make (orbitAmp());
        const auto old = bytesOf (bare);
        c->setStateInformation (old.getData(), (int) old.getSize());
        c->pumpDeviceWork();
        check (near (trim (*c), 3.0f, 0.01f) && near (c->inputTrimDb(), 0.0f) && noted (*c) == -3,
               "an OrbitAmp session from before there was a zero keeps its fader at +3 and plays it over the zero in force");

        // A preset made against another zero, loaded the way the preset menu loads one (ui/Chrome.cpp).
        prefs::setInputZero (orbitAmp(), -9);
        setParam (*a, params::inTrim, -2.0f);
        const auto preset = a->presetForSaving();
        prefs::setInputZero (orbitAmp(), -6);
        auto d = make (orbitAmp());
        d->history.applyEdit (d->history.active(), preset, "Load");
        d->pumpDeviceWork();
        check ((int) preset.getProperty (params::inTrimZero, 1000) == -9 && near (trim (*d), -2.0f, 0.01f)
                   && near (d->inputTrimDb(), -8.0f) && noted (*d) == -6,
               "an OrbitAmp preset made against -9 dB, loaded under -6 dB: its fader at -2, played at " + say (d->inputTrimDb()));
        prefs::setInputZero (orbitAmp(), -9);
    }

    // THE COCKPIT'S AND THE ROVERS' STATES AS 0.8.0 WROTE THEM: absolute, the zero noted under their own name.
    {
        // A RedPawAmp session under -9 dB, INPUT at -6 dB absolute (the lever a little over its middle), and
        // register B at -12 dB: it must play -6 dB again, and B -12 dB.
        auto a = make (redPawAmp());
        a->history.copyRegister (a->history.active(), 1);
        auto workspace = treeOf (session (*a));
        auto live = workspace.getChildWithName ("Live").getChild (0);
        auto snap = workspace.getChildWithName ("Snaps").getChild (1).getChild (0);
        check (live.isValid() && snap.isValid(), "a RedPawAmp session holds its live state and register B");
        asIn080 (live, redPawAmp(), -6.0f, -9);
        asIn080 (snap, redPawAmp(), -12.0f, -9);
        const auto old = bytesOf (workspace);

        auto b = make (redPawAmp());
        b->setStateInformation (old.getData(), (int) old.getSize());
        const float h = heard (*b);
        settle (*b);
        check (near (trim (*b), 3.0f, 0.01f) && near (b->inputTrimDb(), -6.0f) && near (h, -12.0f),
               "RedPawAmp's 0.8.0 session plays its INPUT at -6 dB as it did, now its lever +3 over the zero: heard at " + say (h));
        check (noted (*b) == -9 && ! carriesFormer (b->apvts.state, redPawAmp()) && ! carriesFormer (treeOf (session (*b)), redPawAmp()),
               "...noted under the one name every amp's state has now, the old one gone from what it saves");
        b->history.switchTo (1);
        b->pumpDeviceWork();
        check (near (trim (*b), -3.0f, 0.01f) && near (b->inputTrimDb(), -12.0f),
               "...and its register B plays -12 dB as it did: " + say (b->inputTrimDb()));

        // The cockpit, on a machine that had chosen -12 dB: INPUT at -10 dB absolute.
        prefs::setInputZero (catpitAmp(), -12);
        auto c = make (catpitAmp());
        auto state = c->apvts.copyState();
        asIn080 (state, catpitAmp(), -10.0f, -12);
        const auto bare = bytesOf (state);
        auto d = make (catpitAmp());
        d->setStateInformation (bare.getData(), (int) bare.getSize());
        const float hd = heard (*d);
        check (near (trim (*d), 2.0f, 0.01f) && near (hd, -16.0f),
               "CatpitAmp's 0.8.0 state, made under -12 dB, plays its INPUT at -10 dB as it did: heard at " + say (hd));
        prefs::setInputZero (catpitAmp(), -9);

        // Made under -9 dB, opened under -3 dB: the lever stays where it stood, the sound goes with the zero.
        prefs::setInputZero (redPawAmp(), -3);
        auto e = make (redPawAmp());
        e->setStateInformation (old.getData(), (int) old.getSize());
        check (near (trim (*e), 3.0f, 0.01f) && near (e->inputTrimDb(), 0.0f),
               "a 0.8.0 session made under -9 dB, opened under -3 dB, keeps its lever +3 and plays it over the zero in force");
        prefs::setInputZero (redPawAmp(), -9);

        // One no 0.8.0 window ever saw: no note. 0.8.0's face read it as dialled on the engine's own scale,
        // 0 dB in the middle, and put the lever there over its zero — as it opens now.
        auto f = make (frostPawAmp());
        auto unseen = f->apvts.copyState();
        asIn080 (unseen, frostPawAmp(), 0.0f, std::nullopt);
        const auto never = bytesOf (unseen);
        auto g = make (frostPawAmp());
        g->setStateInformation (never.getData(), (int) never.getSize());
        g->pumpDeviceWork();
        check (near (trim (*g), 0.0f, 0.01f) && near (g->inputTrimDb(), -9.0f) && noted (*g) == -9,
               "a 0.8.0 state with no note keeps its lever in the middle, over the zero in force, as 0.8.0's window would");

        // A rover's preset of 0.8.0's: a rover's preset holds no INPUT, and still does not.
        auto& engine = channels::Engine::of (*b);
        channels::Presets presets (*b, engine.channels);
        auto file = b->presetForSaving();
        asIn080 (file, redPawAmp(), 5.0f, -9);
        check (presets.store().write ("From 0.8.0", file), "a 0.8.0 preset written to RedPawAmp's folder");
        b->history.switchTo (0);
        setParam (*b, params::inTrim, 1.0f);
        check (presets.load ("From 0.8.0") && near (trim (*b), 1.0f, 0.01f) && near (b->inputTrimDb(), -8.0f)
                   && ! carriesFormer (b->apvts.state, redPawAmp()),
               "...loaded, it leaves INPUT where the player has it: the lever at +1, played at " + say (b->inputTrimDb()));

        // A rover's channels keep no INPUT of their own.
        const bool went = engine.go (2);
        b->pumpDeviceWork();
        check (went && engine.channels.active() == 2 && near (trim (*b), 1.0f, 0.01f) && near (b->inputTrimDb(), -8.0f),
               "a channel changed leaves INPUT where it was: a rover's channel keeps no INPUT of its own");
    }

    std::printf (failed == 0 ? "ALL PASSED\n" : "FAILED\n");
    return failed;
}
