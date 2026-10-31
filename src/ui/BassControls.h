// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "GuardRail.h"
#include "Knob.h"
#include "Theme.h"
#include "../Parameters.h"

#include <felitronics/appkit/chrome/FlatButtons.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
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

/** A menu of the choices a choice parameter has, under a heading, its current one ticked; `base` keeps the
    sections of one menu apart. */
inline void addChoices (juce::PopupMenu& m, juce::AudioProcessorValueTreeState& state, const char* id,
                        const juce::String& heading, int base, std::vector<std::function<void()>>& acts)
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id));
    if (p == nullptr)
        return;
    m.addSectionHeader (heading);
    for (int i = 0; i < p->choices.size(); ++i)
    {
        acts.resize ((size_t) juce::jmax ((int) acts.size(), base + i + 1));
        acts[(size_t) (base + i)] = [&state, id, i] { setPlain (state, id, (float) i); };
        m.addItem (base + i + 1, p->choices[i].toUpperCase(), true, p->getIndex() == i);
    }
}

/** A list of frequencies for one parameter, the nearest ticked; picking one also sets `position` to
    `place` where a position is named. */
inline void addFrequencies (juce::PopupMenu& m, juce::AudioProcessorValueTreeState& state, const char* id,
                            std::initializer_list<float> list, int base, std::vector<std::function<void()>>& acts,
                            const char* position = nullptr, int place = 0)
{
    const float now = plain (state, id);
    const bool  here = position == nullptr || juce::roundToInt (plain (state, position)) == place;
    int k = 0;
    for (const float hz : list)
    {
        acts.resize ((size_t) juce::jmax ((int) acts.size(), base + k + 1));
        acts[(size_t) (base + k)] = [&state, id, hz, position, place]
        {
            setPlain (state, id, hz);
            if (position != nullptr)
            {
                setPlain (state, position, (float) place);
                // The bass's cuts: a frequency picked under a switch switches it on.
                if (juce::String (position) == params::bassHpf)
                    setPlain (state, params::bassHpfOn, 1.0f);
                if (juce::String (position) == params::bassLpf)
                    setPlain (state, params::bassLpfOn, 1.0f);
            }
        };
        const auto word = hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        m.addItem (base + k + 1, word, true, here && std::abs (now - hz) < 0.5f);
        ++k;
    }
}

/** Shows `m` at `target`, and runs what was picked. */
inline void show (juce::PopupMenu& m, juce::Component* target, std::vector<std::function<void()>> acts)
{
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target),
                     [acts = std::move (acts)] (int r)
                     {
                         if (r > 0 && r <= (int) acts.size() && acts[(size_t) (r - 1)])
                             acts[(size_t) (r - 1)]();
                     });
}

/** COMP's menu: its attack, its release, its character — and its ratio given back to the slider's law when the hand
    holds one (the transfer square). */
inline void showCompMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    addChoices (m, state, params::compAttack,    "ATTACK",    0,  acts);
    addChoices (m, state, params::compRelease,   "RELEASE",   10, acts);
    addChoices (m, state, params::compCharacter, "CHARACTER", 20, acts);
    m.addSeparator();
    const bool held = plain (state, params::compRatioHeld) > 0.5f;
    acts.resize (31);
    acts[30] = [&state] { setPlain (state, params::compRatioHeld, 0.0f); };
    m.addItem (31, "RATIO: BY THE SLIDER", true, ! held);
    if (held)
        m.addItem (32, "RATIO: HELD AT " + juce::String (plain (state, params::compRatio), 1) + ":1", false, true);
    show (m, target, std::move (acts));
}

/** BLEND's menu: where the clean lows give way to the capture, and the clean leg's polarity. */
inline void showBlendMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("CROSSOVER");
    addFrequencies (m, state, params::ampBlendHz, { 80.0f, 100.0f, 120.0f, 150.0f, 180.0f, 200.0f, 250.0f }, 0, acts);
    m.addSeparator();
    const bool inverted = plain (state, params::ampBlendInvert) > 0.5f;
    acts.resize (21);
    acts[20] = [&state, inverted] { setPlain (state, params::ampBlendInvert, inverted ? 0.0f : 1.0f); };
    m.addItem (21, "INVERT POLARITY", true, inverted);
    show (m, target, std::move (acts));
}

/** The HPF's menu, under its switch: each string count's frequency — picking one chooses that count and switches the
    HPF on. */
inline void showHpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("5 STR");
    addFrequencies (m, state, params::bassHpfHz5, { 18.0f, 20.0f, 22.0f, 25.0f, 28.0f }, 0, acts,
                    params::bassHpf, (int) params::BassHpf::five);
    m.addSectionHeader ("4 STR");
    addFrequencies (m, state, params::bassHpfHz4, { 28.0f, 30.0f, 32.0f, 35.0f, 40.0f }, 10, acts,
                    params::bassHpf, (int) params::BassHpf::four);
    show (m, target, std::move (acts));
}

/** The LPF's menu, under its switch: each hand's frequency — picking one chooses that hand and switches the LPF on. */
inline void showLpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("FINGERS");
    addFrequencies (m, state, params::bassLpfHzFingers, { 3500.0f, 4000.0f, 4500.0f, 5000.0f, 6000.0f }, 0, acts,
                    params::bassLpf, (int) params::BassLpf::fingers);
    m.addSectionHeader ("SLAP");
    addFrequencies (m, state, params::bassLpfHzSlap, { 8000.0f, 9000.0f, 9500.0f, 10500.0f, 12000.0f }, 10, acts,
                    params::bassLpf, (int) params::BassLpf::slap);
    show (m, target, std::move (acts));
}

/** A list of values of one parameter, each worded by `word`, the nearest ticked. */
inline void addValues (juce::PopupMenu& m, juce::AudioProcessorValueTreeState& state, const char* id,
                       std::initializer_list<float> list, int base, std::vector<std::function<void()>>& acts,
                       const std::function<juce::String (float)>& word)
{
    const float now = plain (state, id);
    int k = 0;
    for (const float v : list)
    {
        acts.resize ((size_t) juce::jmax ((int) acts.size(), base + k + 1));
        acts[(size_t) (base + k)] = [&state, id, v] { setPlain (state, id, v); };
        m.addItem (base + k + 1, word (v), true, std::abs (now - v) <= 0.005f * std::max (1.0f, std::abs (v)));
        ++k;
    }
}

/** A switch of the menu's: on or off, ticked when on. */
inline void addToggle (juce::PopupMenu& m, juce::AudioProcessorValueTreeState& state, const char* id,
                       const juce::String& word, int base, std::vector<std::function<void()>>& acts)
{
    const bool on = plain (state, id) > 0.5f;
    acts.resize ((size_t) juce::jmax ((int) acts.size(), base + 1));
    acts[(size_t) base] = [&state, id, on] { setPlain (state, id, on ? 0.0f : 1.0f); };
    m.addItem (base + 1, word, true, on);
}

inline juce::String hzWord (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, v < 10.0f ? 1 : 0) + " Hz"; }
inline juce::String msWord (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
inline juce::String dbWord (float v) { return juce::String (juce::roundToInt (v)) + " dB"; }

/** OCTAVE's menu: the -2 at the slider's top, the sub's TONE and its SUB HP, the tracking GATE. */
inline void showOctaveMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    addToggle (m, state, params::octaveSub2, "-2 OCTAVE AT THE TOP", 0, acts);
    m.addSectionHeader ("TONE");
    addValues (m, state, params::octaveToneHz, { 60.0f, 90.0f, 120.0f, 200.0f, 400.0f }, 10, acts, hzWord);
    m.addSectionHeader ("SUB HP");
    addValues (m, state, params::octaveSubHpHz, { 10.0f, 15.0f, 20.0f, 25.0f, 35.0f, 50.0f, 80.0f }, 20, acts, hzWord);
    m.addSectionHeader ("GATE");
    addValues (m, state, params::octaveGateDb, { -70.0f, -60.0f, -50.0f, -40.0f, -30.0f }, 30, acts, dbWord);
    show (m, target, std::move (acts));
}

/** FILTER's menu: LP or BP, up or down, its resonance and range, the clean lows and their corner, the follower. */
inline void showFilterMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    addChoices (m, state, params::filterMode,      "MODE",      0,  acts);
    addChoices (m, state, params::filterDirection, "DIRECTION", 10, acts);
    m.addSectionHeader ("RESONANCE");
    addValues (m, state, params::filterResonance, { 1.0f, 2.0f, 4.0f, 6.0f, 8.0f, 12.0f }, 20, acts,
               [] (float v) { return "Q " + juce::String (v, 0); });
    addChoices (m, state, params::filterRange, "RANGE", 30, acts);
    m.addSectionHeader ("KEEP LOWS");
    addToggle (m, state, params::filterKeepLows, "KEEP LOWS CLEAN", 40, acts);
    addValues (m, state, params::filterKeepHz, { 80.0f, 100.0f, 120.0f, 150.0f, 200.0f, 250.0f, 300.0f }, 41, acts, hzWord);
    m.addSectionHeader ("ATTACK");
    addValues (m, state, params::filterAttackMs, { 1.0f, 3.0f, 10.0f, 30.0f }, 50, acts, msWord);
    m.addSectionHeader ("RELEASE");
    addValues (m, state, params::filterReleaseMs, { 60.0f, 120.0f, 250.0f, 500.0f }, 60, acts, msWord);
    show (m, target, std::move (acts));
}

/** CHORUS's menu: its rate, its crossover (below it the voice falls away), the centre its head swings about. */
inline void showChorusMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("RATE");
    addValues (m, state, params::chorusRateHz, { 0.3f, 0.5f, 0.7f, 1.0f, 1.5f, 2.5f }, 0, acts, hzWord);
    m.addSectionHeader ("CROSSOVER");
    addValues (m, state, params::chorusCrossoverHz, { 120.0f, 150.0f, 200.0f, 300.0f, 400.0f }, 10, acts, hzWord);
    m.addSectionHeader ("CENTRE");
    addValues (m, state, params::chorusCentreMs, { 6.0f, 8.0f, 10.0f, 14.0f, 20.0f }, 20, acts, msWord);
    show (m, target, std::move (acts));
}

/** The words the cuts' choices wear, under their switches. */
inline juce::String hpfWord (int position) { return position == (int) params::BassHpf::four ? "4 STR" : "5 STR"; }

inline juce::String lpfWord (int position) { return position == (int) params::BassLpf::slap ? "SLAP" : "FINGERS"; }

} // namespace orbitamp::bass
