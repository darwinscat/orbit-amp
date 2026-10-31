// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

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
                setPlain (state, position, (float) place);
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

/** COMP's menu: its attack, its release, its character. */
inline void showCompMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    addChoices (m, state, params::compAttack,    "ATTACK",    0,  acts);
    addChoices (m, state, params::compRelease,   "RELEASE",   10, acts);
    addChoices (m, state, params::compCharacter, "CHARACTER", 20, acts);
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

/** The HPF's menu: each string count's frequency, and off. */
inline void showHpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("HPF 5 STRINGS");
    addFrequencies (m, state, params::bassHpfHz5, { 18.0f, 20.0f, 22.0f, 25.0f, 28.0f }, 0, acts,
                    params::bassHpf, (int) params::BassHpf::five);
    m.addSectionHeader ("HPF 4 STRINGS");
    addFrequencies (m, state, params::bassHpfHz4, { 28.0f, 30.0f, 32.0f, 35.0f, 40.0f }, 10, acts,
                    params::bassHpf, (int) params::BassHpf::four);
    m.addSeparator();
    const bool off = juce::roundToInt (plain (state, params::bassHpf)) == (int) params::BassHpf::off;
    acts.resize (21);
    acts[20] = [&state] { setPlain (state, params::bassHpf, (float) params::BassHpf::off); };
    m.addItem (21, "HPF OFF", true, off);
    show (m, target, std::move (acts));
}

/** The LPF's menu: each hand's frequency, and off. */
inline void showLpfMenu (juce::AudioProcessorValueTreeState& state, juce::Component* target)
{
    juce::PopupMenu m;
    std::vector<std::function<void()>> acts;
    m.addSectionHeader ("LPF FINGERS");
    addFrequencies (m, state, params::bassLpfHzFingers, { 3500.0f, 4000.0f, 4500.0f, 5000.0f, 6000.0f }, 0, acts,
                    params::bassLpf, (int) params::BassLpf::fingers);
    m.addSectionHeader ("LPF SLAP");
    addFrequencies (m, state, params::bassLpfHzSlap, { 8000.0f, 9000.0f, 9500.0f, 10500.0f, 12000.0f }, 10, acts,
                    params::bassLpf, (int) params::BassLpf::slap);
    m.addSeparator();
    const bool off = juce::roundToInt (plain (state, params::bassLpf)) == (int) params::BassLpf::off;
    acts.resize (21);
    acts[20] = [&state] { setPlain (state, params::bassLpf, (float) params::BassLpf::off); };
    m.addItem (21, "LPF OFF", true, off);
    show (m, target, std::move (acts));
}

/** The words the cuts' switches wear. */
inline juce::String hpfWord (int position)
{
    return position == (int) params::BassHpf::five ? "HPF 5" : position == (int) params::BassHpf::four ? "HPF 4" : "HPF OFF";
}

inline juce::String lpfWord (int position)
{
    return position == (int) params::BassLpf::fingers ? "FINGERS" : position == (int) params::BassLpf::slap ? "SLAP" : "LPF OFF";
}

} // namespace orbitamp::bass
