// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassMenuOpener.h"
#include "GuardRail.h"
#include "Knob.h"
#include "Theme.h"
#include "../Parameters.h"

#include <felitronics/appkit/chrome/FlatButtons.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <vector>

/** ORBITBASS'S CONTROLS on OrbitAmp's frame — COMP and BLEND beside the AMP's GAIN dial, DI/CAB and the cuts
    on the cabinet — and the menus their right clicks open. Dumb views: a parameter tree and the ids in,
    gestures out; what a setting does to the sound is the processor's. */
namespace orbitamp::bass
{

/** A parameter written as one gesture, in its own units. */
inline void setPlain (juce::AudioProcessorValueTreeState& state, const char* id, float value)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

inline float plain (juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* p = state.getParameter (id);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
}

/** THE BASS'S RAILS — COMP, OCTAVE, FILTER, CHORUS, BLEND — one look: an orange hand (what the hand sets), its amount
    written in percent at 13 px, and a click on the hand types it, in percent. */
inline void asBassRail (GuardRail& r)
{
    r.gripColour     = theme::orange;
    r.gripTextHeight = 13.0f;
    r.textForValue   = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)); };
    r.typeDecimals   = 0;
    r.typedScale     = 100.0f;
}

/** A knob whose right click asks for its menu. */
class MenuKnob final : public Knob
{
public:
    using Knob::Knob;
    std::function<void()> onMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu)
            onMenu();
        else
            Knob::mouseDown (e);
    }
};

/** The squeeze, a thin bar falling from its top: 0 to -12 dB, read 30 times a second. */
class GrMeter final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit GrMeter (const std::atomic<float>& grDb) : source (grDb)
    {
        setInterceptsMouseClicks (false, false);
        setTooltip ("COMP: how hard it squeezes, 0 to 12 dB");
        startTimerHz (30);
    }

    static constexpr float rangeDb = 12.0f;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::hair);
        g.fillRoundedRectangle (r, 1.5f);
        const float share = juce::jlimit (0.0f, 1.0f, -shown / rangeDb);
        if (share > 0.0f)
        {
            g.setColour (theme::orange);
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * share), 1.5f);
        }
    }

private:
    void timerCallback() override
    {
        const float now = juce::jmin (0.0f, source.load (std::memory_order_relaxed));
        if (std::abs (now - shown) > 0.05f)
        {
            shown = now;
            repaint();
        }
    }

    const std::atomic<float>& source;
    float shown = 0.0f;
};

/** A word that is a switch: a click walks its positions, a right click asks for its menu. */
class WordSwitch final : public felitronics::appkit::chrome::FlatItem
{
public:
    WordSwitch()
    {
        theme = felitronics::appkit::chrome::ChromeTheme { .fill       = orbitamp::theme::panel,
                                                           .underline  = orbitamp::theme::hair,
                                                           .accent     = orbitamp::theme::violet,
                                                           .attention  = orbitamp::theme::orange,
                                                           .text       = orbitamp::theme::tx,
                                                           .textDim    = orbitamp::theme::txDim,
                                                           .activeText = juce::Colours::white };
        textHeight = 13.0f;
    }

    std::function<void()> onMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu)
            onMenu();
        else
            FlatItem::mouseDown (e);
    }
};

/** A BLOCK'S MENU (the owner, 01.11: no settings windows on OrbitBass — its gear and its right click open this, one
    place per block): what the block holds beyond its rail and its picture, as CHOICES — a choice parameter's choices,
    a number's STEPS in a submenu of its own, a switch ticked when on. Every item is one undo step: what it sets moves
    in one instant, and the history settles it as one. */
struct Menu
{
    juce::PopupMenu items;
    std::vector<std::function<void()>> acts;   // what item id k + 1 does

    int act (std::function<void()> f)
    {
        acts.push_back (std::move (f));
        return (int) acts.size();
    }
};

/** A choice parameter's choices in a submenu named `heading`, the current one ticked. */
inline void addChoices (Menu& m, juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& heading)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
    if (p == nullptr)
        return;
    juce::PopupMenu sub;
    for (int i = 0; i < p->choices.size(); ++i)
        sub.addItem (m.act ([&state, id, i] { setPlain (state, id, (float) i); }), p->choices[i].toUpperCase(), true, p->getIndex() == i);
    m.items.addSubMenu (heading, sub, true, nullptr, false);
}

/** A number's STEPS into `into`, each worded by `word`, the one it stands at ticked — and where it stands off them (typed,
    automated, an older session), that value too, ticked and worded "(custom)", so nothing set is hidden. `here` says
    whether the number is the one in use (the cuts: the frequency of the position chosen); `also` runs after a pick. */
inline void addSteps (Menu& m, juce::PopupMenu& into, juce::AudioProcessorValueTreeState& state, const char* id,
                      std::initializer_list<float> steps, const std::function<juce::String (float)>& word,
                      bool here = true, std::function<void()> also = {})
{
    const float now = plain (state, id);
    const auto  at  = [now] (float v) { return std::abs (now - v) <= 0.005f * std::max (1.0f, std::abs (v)); };
    bool onStep = false, customShown = false;
    for (const float v : steps)
    {
        if (here && ! customShown && ! onStep && now < v && std::none_of (steps.begin(), steps.end(), at))
        {
            into.addItem (m.act ({}), word (now) + " (custom)", false, true);   // in its place among the steps
            customShown = true;
        }
        onStep = onStep || at (v);
        into.addItem (m.act ([&state, id, v, also] { setPlain (state, id, v); if (also != nullptr) also(); }), word (v), true, here && at (v));
    }
    if (here && ! customShown && ! onStep)
        into.addItem (m.act ({}), word (now) + " (custom)", false, true);
}

/** A number's steps in a submenu named `heading`. */
inline void addStepsMenu (Menu& m, juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& heading,
                          std::initializer_list<float> steps, const std::function<juce::String (float)>& word)
{
    juce::PopupMenu sub;
    addSteps (m, sub, state, id, steps, word);
    m.items.addSubMenu (heading, sub);
}

/** A switch: ticked when on, a pick turns it over. */
inline void addToggle (Menu& m, juce::PopupMenu& into, juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& word)
{
    const bool on = plain (state, id) > 0.5f;
    into.addItem (m.act ([&state, id, on] { setPlain (state, id, on ? 0.0f : 1.0f); }), word, true, on);
}

/** Shows `m` at `target`, and runs what was picked — while the target still stands: a menu outliving its face (a host
    closing the editor) finds nothing to call. */
inline void show (Menu m, juce::Component* target)
{
    m.items.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                           [acts = std::move (m.acts), safe = juce::Component::SafePointer<juce::Component> (target)] (int r)
                           {
                               if (safe != nullptr && r > 0 && r <= (int) acts.size() && acts[(size_t) (r - 1)])
                                   acts[(size_t) (r - 1)]();
                           });
}

inline bool isWhole (float v) { return std::abs (v - std::round (v)) < 1.0e-4f; }

inline juce::String hzWord (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, v < 10.0f ? 1 : 0) + " Hz"; }
inline juce::String msWord (float v) { return juce::String (v, v < 10.0f && ! isWhole (v) ? 1 : 0) + " ms"; }
inline juce::String dbWord (float v) { return juce::String (juce::roundToInt (v)) + " dB"; }
inline juce::String pcWord (float v) { return juce::String (juce::roundToInt (v)) + " %"; }

/** The words the cuts' choices wear, under their switches. */
inline juce::String hpfWord (int position) { return position == (int) params::BassHpf::four ? "4 STR" : "5 STR"; }

inline juce::String lpfWord (int position) { return position == (int) params::BassLpf::slap ? "SLAP" : "FINGERS"; }

/** The cuts' frequencies, each string count's (the HPF) and each hand's (the LPF), under headings: a pick chooses that
    count or hand and switches the cut on. */
inline void addHpf (Menu& m, juce::PopupMenu& into, juce::AudioProcessorValueTreeState& state)
{
    const int position = juce::roundToInt (plain (state, params::bassHpf));
    const auto pick = [&state] (params::BassHpf place)
    {
        return [&state, place] { setPlain (state, params::bassHpf, (float) place); setPlain (state, params::bassHpfOn, 1.0f); };
    };
    into.addSectionHeader ("5 STR");
    addSteps (m, into, state, params::bassHpfHz5, { 18.0f, 20.0f, 22.0f, 25.0f, 28.0f }, hzWord, position == (int) params::BassHpf::five,
              pick (params::BassHpf::five));
    into.addSectionHeader ("4 STR");
    addSteps (m, into, state, params::bassHpfHz4, { 28.0f, 30.0f, 32.0f, 35.0f, 40.0f }, hzWord, position == (int) params::BassHpf::four,
              pick (params::BassHpf::four));
}

inline void addLpf (Menu& m, juce::PopupMenu& into, juce::AudioProcessorValueTreeState& state)
{
    const int position = juce::roundToInt (plain (state, params::bassLpf));
    const auto pick = [&state] (params::BassLpf place)
    {
        return [&state, place] { setPlain (state, params::bassLpf, (float) place); setPlain (state, params::bassLpfOn, 1.0f); };
    };
    into.addSectionHeader ("FINGERS");
    addSteps (m, into, state, params::bassLpfHzFingers, { 3500.0f, 4000.0f, 4500.0f, 5000.0f, 6000.0f }, hzWord,
              position == (int) params::BassLpf::fingers, pick (params::BassLpf::fingers));
    into.addSectionHeader ("SLAP");
    addSteps (m, into, state, params::bassLpfHzSlap, { 8000.0f, 9000.0f, 9500.0f, 10500.0f, 12000.0f }, hzWord,
              position == (int) params::BassLpf::slap, pick (params::BassLpf::slap));
}

/** COMP's menu: exactly its ATTACK, its RELEASE and its CHARACTER, each a submenu (the owner, 01.11). */
inline Menu compMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addChoices (m, state, params::compAttack,    "ATTACK");
    addChoices (m, state, params::compRelease,   "RELEASE");
    addChoices (m, state, params::compCharacter, "CHARACTER");
    return m;
}

/** OCTAVE's: the -2 at the slider's top, the sub's TONE and its SUB HP, the tracking GATE. */
inline Menu octaveMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addToggle (m, m.items, state, params::octaveSub2, "-2 OCTAVE AT THE TOP");
    addStepsMenu (m, state, params::octaveToneHz,  "TONE",   { 60.0f, 90.0f, 120.0f, 200.0f, 400.0f }, hzWord);
    addStepsMenu (m, state, params::octaveSubHpHz, "SUB HP", { 10.0f, 15.0f, 20.0f, 25.0f, 35.0f, 50.0f, 80.0f }, hzWord);
    addStepsMenu (m, state, params::octaveGateDb,  "GATE",   { -70.0f, -60.0f, -50.0f, -40.0f, -30.0f }, dbWord);
    return m;
}

/** ENVELOPE's: LP or BP, up or down, its range and resonance, the clean lows and their corner, the follower. Its start
    is typed on its picture. */
inline Menu filterMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addChoices (m, state, params::filterMode,      "MODE");
    addChoices (m, state, params::filterDirection, "DIRECTION");
    addChoices (m, state, params::filterRange,     "RANGE");
    addStepsMenu (m, state, params::filterResonance, "RESONANCE", { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f, 12.0f },
                  [] (float v) { return "Q " + juce::String (v, isWhole (v) ? 0 : 1); });
    addToggle (m, m.items, state, params::filterKeepLows, "KEEP LOWS CLEAN");
    addStepsMenu (m, state, params::filterKeepHz,    "KEEP LOWS AT", { 80.0f, 100.0f, 120.0f, 150.0f, 200.0f, 250.0f, 300.0f }, hzWord);
    addStepsMenu (m, state, params::filterAttackMs,  "ATTACK",       { 1.0f, 3.0f, 10.0f, 30.0f }, msWord);
    addStepsMenu (m, state, params::filterReleaseMs, "RELEASE",      { 60.0f, 120.0f, 250.0f, 500.0f }, msWord);
    return m;
}

/** CHORUS's: the centre its head swings about. Its rate and its crossover are typed on its picture. */
inline Menu chorusMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addStepsMenu (m, state, params::chorusCentreMs, "CENTRE", { 6.0f, 8.0f, 10.0f, 14.0f, 20.0f }, msWord);
    return m;
}

/** DELAY's: what the guitar's DELAY block offers but MIX (its rail). */
inline Menu delayMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addToggle (m, m.items, state, params::delaySync, "SYNC");
    addChoices (m, state, params::delayDiv, "DIVISION");
    addStepsMenu (m, state, params::delayTimeMs, "TIME (FREE)", { 60.0f, 120.0f, 250.0f, 350.0f, 375.0f, 500.0f, 750.0f, 1000.0f, 1500.0f }, msWord);
    addToggle (m, m.items, state, params::delayHostTempo, "HOST TEMPO");
    addStepsMenu (m, state, params::delayBpm, "BPM", { 60.0f, 80.0f, 90.0f, 100.0f, 120.0f, 140.0f, 160.0f },
                  [] (float v) { return juce::String (v, isWhole (v) ? 0 : 1) + " BPM"; });
    addStepsMenu (m, state, params::delayRepeats, "REPEATS", { 0.0f, 15.0f, 25.0f, 35.0f, 50.0f, 65.0f, 80.0f }, pcWord);
    addStepsMenu (m, state, params::delayDark,    "DARK",    { 0.0f, 15.0f, 30.0f, 45.0f, 60.0f, 80.0f }, pcWord);
    addStepsMenu (m, state, params::delayOffset,  "OFFSET",  { -20.0f, -10.0f, -5.0f, 0.0f, 5.0f, 10.0f, 20.0f }, msWord);
    return m;
}

/** REVERB's: its type, its decay and its pre-delay — the guitar's REVERB block's but MIX. */
inline Menu reverbMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addChoices (m, state, params::reverbType, "TYPE");
    addStepsMenu (m, state, params::reverbDecay,    "DECAY",     { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f },
                  [] (float v) { return juce::String (v, 2) + " x"; });
    addStepsMenu (m, state, params::reverbPredelay, "PRE-DELAY", { 0.0f, 10.0f, 20.0f, 40.0f, 60.0f, 100.0f }, msWord);
    return m;
}

/** The AMP's: where BLEND's clean lows give way to the capture and the clean leg's polarity; the HPF and the LPF, their
    switches and their frequencies as under the switches on the face. */
inline Menu ampMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addStepsMenu (m, state, params::ampBlendHz, "BLEND CROSSOVER", { 80.0f, 100.0f, 120.0f, 150.0f, 180.0f, 200.0f, 250.0f }, hzWord);
    addToggle (m, m.items, state, params::ampBlendInvert, "INVERT POLARITY");
    juce::PopupMenu hpf, lpf;
    addToggle (m, hpf, state, params::bassHpfOn, "HPF ON");
    addHpf (m, hpf, state);
    m.items.addSubMenu ("HPF", hpf);
    addToggle (m, lpf, state, params::bassLpfOn, "LPF ON");
    addLpf (m, lpf, state);
    m.items.addSubMenu ("LPF", lpf);
    return m;
}

/** The CAB's: DI/CAB, and the IR's trim, its switch and what it keeps. */
inline Menu cabMenu (juce::AudioProcessorValueTreeState& state)
{
    Menu m;
    addStepsMenu (m, state, params::diMix, "DI / CAB", { 0.0f, 10.0f, 20.0f, 30.0f, 50.0f, 70.0f, 100.0f },
                  [] (float v) { return juce::String (juce::roundToInt (v)) + " % DI"; });
    addToggle (m, m.items, state, params::cabTrimOn, "TRIM");
    addStepsMenu (m, state, params::cabTrim, "TRIM KEEPS", { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f },
                  [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " % of the IR"; });
    return m;
}

/** A block's menu by its name on the face. */
inline Menu blockMenu (juce::AudioProcessorValueTreeState& state, const juce::String& block)
{
    return block == "COMP" ? compMenu (state) : block == "OCTAVE" ? octaveMenu (state) : block == "ENVELOPE" ? filterMenu (state)
         : block == "CHORUS" ? chorusMenu (state) : block == "DELAY" ? delayMenu (state) : block == "REVERB" ? reverbMenu (state)
         : block == "AMP" ? ampMenu (state) : block == "CAB IR" ? cabMenu (state) : Menu {};
}

/** A block's menu from one of its controls: through the face, which shows it at the block (and says which it was);
    with no face to ask, here. */
inline void showBlockMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target, const juce::String& block)
{
    if (! openMenuFrom (target, block))
        show (blockMenu (state, block), target);
}

inline void showCompMenu   (juce::AudioProcessorValueTreeState& s, juce::Component* t) { showBlockMenu (s, t, "COMP"); }
inline void showOctaveMenu (juce::AudioProcessorValueTreeState& s, juce::Component* t) { showBlockMenu (s, t, "OCTAVE"); }
inline void showFilterMenu (juce::AudioProcessorValueTreeState& s, juce::Component* t) { showBlockMenu (s, t, "ENVELOPE"); }
inline void showChorusMenu (juce::AudioProcessorValueTreeState& s, juce::Component* t) { showBlockMenu (s, t, "CHORUS"); }
inline void showAmpMenu    (juce::AudioProcessorValueTreeState& s, juce::Component* t) { showBlockMenu (s, t, "AMP"); }

/** The HPF's menu, under its switch on the face: each string count's frequency. */
inline void showHpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    Menu m;
    addHpf (m, m.items, state);
    show (std::move (m), target);
}

/** The LPF's menu, under its switch: each hand's frequency. */
inline void showLpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    Menu m;
    addLpf (m, m.items, state);
    show (std::move (m), target);
}

} // namespace orbitamp::bass
