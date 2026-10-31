// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassSettingsOpener.h"
#include "Theme.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../core/BassComp.h"

#include <felitronics/chorus/ChorusLaw.h>

#include <optional>
#include <vector>

/** ORBITBASS'S BLOCK SETTINGS (the owner, 31.10): every block's GEAR — and a right click on it — opens one window with
    everything its menu held, as controls with numbers: a choice its words, a switch its word, a number typed (click,
    type, Enter). Each change is ONE undo step (the history's bracket, as the rails do); RESET at the foot puts the
    block's settings back, one step. Where the block's slider drives a value through its law, the window shows it; where
    a parameter already holds an override (COMP's ratio) it can be typed and held, and given back. Non-modal in the
    family's way (a callout, as the load window): the sound goes on, a click outside or Esc closes it. */
namespace orbitamp::bass
{

class SettingsPanel final : public juce::Component,
                            private juce::Timer
{
public:
    struct Row
    {
        enum class Kind { number, choice, toggle, reading } kind;
        juce::String label;
        juce::String id;                          // the parameter (none for a reading)
        std::function<juce::String()> read = {};  // a reading's words
        int perLine = 0;                          // a choice's words a line, 0 all on one (a long list wraps)
    };

    /** How many lines a row takes: one, or a long choice's words wrapped `perLine` a line. */
    int linesOf (const Row& r) const
    {
        if (r.kind != Row::Kind::choice || r.perLine <= 0)
            return 1;
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (amp.apvts.getParameter (r.id));
        const int n = c != nullptr ? c->choices.size() : 0;
        return juce::jmax (1, (n + r.perLine - 1) / r.perLine);
    }

    SettingsPanel (AmpProcessor& processor, juce::String blockName, std::vector<Row> rowList)
        : amp (processor), name (std::move (blockName)), rows (std::move (rowList))
    {
        for (const auto& r : rows)
        {
            auto line = std::make_unique<Line>();
            line->row = r;
            line->count = linesOf (r);   // kept: its layout never asks the processor again
            line->label.setText (r.label.toUpperCase(), juce::dontSendNotification);
            style (line->label, theme::txDim);
            addAndMakeVisible (line->label);
            auto* p = r.id.isNotEmpty() ? amp.apvts.getParameter (r.id) : nullptr;
            if (r.kind == Row::Kind::number && p != nullptr)
            {
                style (line->value, theme::tx);
                line->value.setEditable (true, false, false);   // a click types it
                line->value.setColour (juce::Label::outlineColourId, theme::hair);
                line->value.onTextChange = [this, id = r.id, l = line.get()]
                {
                    if (const auto v = parse (l->value.getText()); v.has_value())
                        type (id, id == params::compAmount || id == params::cabTrim ? *v / 100.0f : *v);   // typed in percent
                };
                addAndMakeVisible (line->value);
            }
            else if (r.kind == Row::Kind::choice)
            {
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
                    for (int i = 0; i < c->choices.size(); ++i)
                    {
                        auto b = std::make_unique<juce::TextButton> (c->choices[i].toUpperCase());
                        styleButton (*b);
                        b->onClick = [this, id = r.id, i] { type (id, (float) i); };
                        addAndMakeVisible (*b);
                        line->buttons.push_back (std::move (b));
                    }
            }
            else if (r.kind == Row::Kind::toggle)
            {
                auto b = std::make_unique<juce::TextButton>();
                styleButton (*b);
                b->onClick = [this, id = r.id]
                {
                    if (detached)
                        return;   // a click queued before its face went: the processor may be gone
                    if (auto* q = amp.apvts.getParameter (id))
                        type (id, q->getValue() > 0.5f ? 0.0f : 1.0f);
                };
                addAndMakeVisible (*b);
                line->buttons.push_back (std::move (b));
            }
            else
            {
                style (line->value, theme::tx);
                addAndMakeVisible (line->value);
            }
            lines.push_back (std::move (line));
        }

        styleButton (resetButton);
        resetButton.setButtonText ("RESET");
        resetButton.onClick = [this] { reset(); };
        addAndMakeVisible (resetButton);

        int lineCount = 0;
        for (const auto& r : rows)
            lineCount += linesOf (r);
        setSize (width, titleH + lineCount * rowH + footH);
        refresh();
        startTimerHz (10);   // the readings and a value moved elsewhere (a host, the face) follow
    }

    static constexpr int width = 400, titleH = 34, rowH = 28, footH = 44, labelW = 150;

    const juce::String& block() const noexcept { return name; }

    /** Its face is going (the editor closing, the plug-in removed): it stops reading and writing the processor at once —
        the window it stands in goes on its own, a moment later. EVERY CALLBACK GOES WITH IT: a click or a typed value
        queued before (a keyboard activation the message loop has not delivered yet) finds nothing to call. */
    void detach() noexcept
    {
        stopTimer();
        detached = true;
        for (auto& l : lines)
        {
            l->value.onTextChange = nullptr;
            for (auto& b : l->buttons)
                b->onClick = nullptr;
        }
        resetButton.onClick = nullptr;
        setEnabled (false);
    }
    bool isDetached() const noexcept { return detached; }

    /** The parameters it sets, in its order. */
    juce::StringArray ids() const
    {
        juce::StringArray all;
        for (const auto& r : rows)
            if (r.id.isNotEmpty())
                all.add (r.id);
        return all;
    }

    /** A value set from the window — typed, a word, a switch: ONE undo step, the host told as one gesture. */
    void type (const juce::String& id, float plain)
    {
        if (detached)
            return;
        auto* p = amp.apvts.getParameter (id);
        if (p == nullptr || ! std::isfinite (plain))
            return;
        amp.history.beginGesture();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (p->getNormalisableRange().start, p->getNormalisableRange().end, plain)));
        p->endChangeGesture();
        amp.history.endGesture();
        refresh();
    }

    /** RESET: the block's settings back to their defaults, ONE undo step. */
    void reset()
    {
        if (detached)
            return;
        amp.history.beginGesture();
        for (const auto& id : ids())
            if (auto* p = amp.apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (amp.engineDefault (*p));
                p->endChangeGesture();
            }
        amp.history.endGesture();
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::lilac);
        theme::drawTracked (g, name + " SETTINGS", getLocalBounds().removeFromTop (titleH).reduced (14, 0).toFloat(),
                            theme::displayFont (15.0f), 0.12f, juce::Justification::centredLeft);
        g.setColour (theme::hair);
        g.fillRect (14, titleH - 1, getWidth() - 28, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 0);
        r.removeFromTop (titleH);
        for (auto& l : lines)
        {
            const int count = l->count;
            auto block = r.removeFromTop (rowH * count);
            auto line = block.removeFromTop (rowH).reduced (0, 3);
            l->label.setBounds (line.removeFromLeft (labelW));
            if (! l->buttons.empty())
            {
                // A long list's words wrap, `perLine` a line, every word the same width.
                const int across = count > 1 ? l->row.perLine : (int) l->buttons.size();
                const int w = line.getWidth() / juce::jmax (1, across);
                const int x0 = line.getX();
                for (size_t k = 0; k < l->buttons.size(); ++k)
                {
                    const int row = (int) k / juce::jmax (1, across), col = (int) k % juce::jmax (1, across);
                    l->buttons[k]->setBounds (juce::Rectangle<int> (x0 + col * w, line.getY() + row * rowH, w, line.getHeight()).reduced (2, 0));
                }
            }
            else
                l->value.setBounds (line.removeFromLeft (juce::jmin (150, line.getWidth())));
        }
        resetButton.setBounds (r.removeFromTop (footH).withSizeKeepingCentre (110, 26));
    }

private:
    struct Line
    {
        Row row;
        int count = 1;   // its lines (linesOf), counted once
        juce::Label label, value;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
    };

    static void style (juce::Label& l, juce::Colour c)
    {
        l.setFont (theme::displayFont (13.0f));
        l.setColour (juce::Label::textColourId, c);
        l.setColour (juce::Label::backgroundWhenEditingColourId, theme::bezel);
        l.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
        l.setJustificationType (juce::Justification::centredLeft);
    }

    static void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, theme::bezel);
        b.setColour (juce::TextButton::buttonOnColourId, theme::violet);
        b.setColour (juce::TextButton::textColourOffId, theme::txDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    }

    static std::optional<float> parse (const juce::String& text)
    {
        const auto t = text.upToFirstOccurrenceOf (":", false, false).trim();
        const auto digits = t.retainCharacters ("0123456789.+-");
        if (digits.isEmpty())
            return std::nullopt;
        float v = digits.getFloatValue();
        if (t.containsIgnoreCase ("k"))
            v *= 1000.0f;
        return v;
    }

    /** A parameter's value as the window writes it, its unit by its kind. */
    juce::String textOf (const juce::String& id) const
    {
        auto* p = amp.apvts.getParameter (id);
        if (p == nullptr)
            return {};
        const float v = p->convertFrom0to1 (p->getValue());
        if (id.endsWith ("_hz") || id.contains ("_hz_"))
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
        if (id.endsWith ("_ms") || id == params::delayOffset || id == params::reverbPredelay)
            return juce::String (v, std::abs (v) < 10.0f ? 1 : 0) + " ms";
        if (id == params::delayRepeats || id == params::delayDark)
            return juce::String (juce::roundToInt (v)) + " %";
        if (id == params::delayBpm)
            return juce::String (v, 1) + " BPM";
        if (id == params::reverbDecay)
            return juce::String (v, 2) + " x";
        if (id == params::compRatio)
            return juce::String (v, 1) + ":1";
        if (id == params::compAmount)
            return juce::String (juce::roundToInt (v * 100.0f)) + " %";
        if (id == params::cabTrim)
            return juce::String (juce::roundToInt (v * 100.0f)) + " % of the IR";   // the share of the IR kept, not a level
        if (id == params::diMix)
            return juce::String (juce::roundToInt (v)) + " %";
        if (id.containsIgnoreCase ("resonance"))
            return "Q " + juce::String (v, 1);
        return juce::String (v, 1) + " dB";
    }

    void refresh()
    {
        if (detached)
            return;
        for (auto& l : lines)
        {
            auto* p = l->row.id.isNotEmpty() ? amp.apvts.getParameter (l->row.id) : nullptr;
            switch (l->row.kind)
            {
                case Row::Kind::number:
                    if (! l->value.isBeingEdited())
                        l->value.setText (textOf (l->row.id), juce::dontSendNotification);
                    break;
                case Row::Kind::choice:
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
                        for (size_t i = 0; i < l->buttons.size(); ++i)
                            l->buttons[i]->setToggleState (c->getIndex() == (int) i, juce::dontSendNotification);
                    break;
                case Row::Kind::toggle:
                    if (p != nullptr && ! l->buttons.empty())
                    {
                        const bool on = p->getValue() > 0.5f;
                        l->buttons[0]->setButtonText (on ? "ON" : "OFF");
                        l->buttons[0]->setToggleState (on, juce::dontSendNotification);
                    }
                    break;
                case Row::Kind::reading:
                    l->value.setText (l->row.read != nullptr ? l->row.read() : juce::String(), juce::dontSendNotification);
                    break;
            }
        }
    }

    void timerCallback() override { refresh(); }

    AmpProcessor& amp;
    const juce::String name;
    std::vector<Row> rows;
    std::vector<std::unique_ptr<Line>> lines;
    juce::TextButton resetButton;
    bool detached = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};

/** EACH BLOCK'S WINDOW: its menu's settings, and the law's values its slider drives — DELAY's and REVERB's what the
    guitar's blocks offer, but MIX, which is their rail. Held back (not invented here): the
    AMP's device tone / EQ mode (its own combo on the block), the cabinet's IR (its own list on the border), GATE and
    LIMIT (no window: their own menus, LEARN included, and their full scales), ENVELOPE's reach (the picture's law). */
inline std::unique_ptr<SettingsPanel> makeSettings (AmpProcessor& amp, const juce::String& block)
{
    using K = SettingsPanel::Row::Kind;
    auto& state = amp.apvts;
    const auto plainOf = [&state] (const char* id) { auto* p = state.getParameter (id); return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; };
    std::vector<SettingsPanel::Row> rows;

    if (block == "COMP")
        rows = { { K::number, "Threshold", params::compThreshold }, { K::number, "Ratio", params::compRatio },
                 { K::number, "Blend", params::compAmount },
                 { K::choice, "Attack", params::compAttack },
                 { K::reading, "Attack now", {}, [plainOf] { return juce::String (juce::roundToInt (core::BassComp::attackMsOf ((core::BassComp::Attack) juce::roundToInt (plainOf (params::compAttack))))) + " ms"; } },
                 { K::choice, "Release", params::compRelease }, { K::choice, "Character", params::compCharacter } };
    else if (block == "OCTAVE")
        rows = { { K::toggle, "-2 at the top", params::octaveSub2 }, { K::number, "Tone", params::octaveToneHz },
                 { K::number, "Sub HP", params::octaveSubHpHz }, { K::number, "Gate", params::octaveGateDb } };
    else if (block == "ENVELOPE")
        rows = { { K::choice, "Mode", params::filterMode }, { K::choice, "Direction", params::filterDirection },
                 { K::choice, "Range", params::filterRange }, { K::number, "Resonance", params::filterResonance },
                 { K::toggle, "Keep lows", params::filterKeepLows }, { K::number, "Keep lows at", params::filterKeepHz },
                 { K::number, "Attack", params::filterAttackMs }, { K::number, "Release", params::filterReleaseMs },
                 { K::reading, "Start (rest)", {}, [plainOf]
                   {
                       const int range = juce::jlimit (0, 2, juce::roundToInt (plainOf (params::filterRange)));
                       const float hz = params::filterRangeHz[range][juce::roundToInt (plainOf (params::filterDirection)) == 1 ? 1 : 0];
                       return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
                   } } };
    else if (block == "CHORUS")
        rows = { { K::number, "Rate", params::chorusRateHz }, { K::number, "Crossover", params::chorusCrossoverHz },
                 { K::number, "Centre", params::chorusCentreMs },
                 { K::reading, "Depth (the slider)", {}, [plainOf]
                   { return juce::String (felitronics::chorus::paramsForAmount (plainOf (params::chorusAmount), {}).depthMs, 2) + " ms"; } } };
    else if (block == "DELAY")   // the guitar's DELAY block's, but MIX (the rail)
        rows = { { K::toggle, "Sync", params::delaySync },
                 { K::choice, "Division", params::delayDiv, {}, 5 },
                 { K::number, "Time (free)", params::delayTimeMs },
                 { K::toggle, "Host tempo", params::delayHostTempo },
                 { K::number, "BPM", params::delayBpm },
                 { K::number, "Repeats", params::delayRepeats }, { K::number, "Dark", params::delayDark },
                 { K::number, "Offset", params::delayOffset },
                 { K::reading, "Time now", {}, [&amp] { return juce::String (juce::roundToInt (amp.delayTaps().shownTimeMs())) + " ms"; } },
                 { K::reading, "Low cut (repeats)", {}, [] { return juce::String (juce::roundToInt (params::bassWetCutHz)) + " Hz"; } } };
    else if (block == "REVERB")   // the guitar's REVERB block's, but MIX (the rail); its low cut the bass's own
        rows = { { K::choice, "Type", params::reverbType, {}, 2 },
                 { K::number, "Decay", params::reverbDecay }, { K::number, "Pre-delay", params::reverbPredelay },
                 { K::reading, "Tail", {}, [&amp] { return juce::String (amp.reverbTailSeconds(), 1) + " s"; } },
                 { K::reading, "Low cut (tail)", {}, [] { return juce::String (juce::roundToInt (params::bassWetCutHz)) + " Hz"; } } };
    else if (block == "AMP")
        rows = { { K::number, "Blend crossover", params::ampBlendHz }, { K::toggle, "Blend invert", params::ampBlendInvert },
                 { K::toggle, "HPF", params::bassHpfOn }, { K::choice, "HPF strings", params::bassHpf },
                 { K::number, "HPF 5 STR", params::bassHpfHz5 }, { K::number, "HPF 4 STR", params::bassHpfHz4 },
                 { K::toggle, "LPF", params::bassLpfOn }, { K::choice, "LPF hand", params::bassLpf },
                 { K::number, "LPF fingers", params::bassLpfHzFingers }, { K::number, "LPF slap", params::bassLpfHzSlap } };
    else if (block == "CAB IR")
        rows = { { K::number, "DI / CAB", params::diMix }, { K::toggle, "Trim", params::cabTrimOn }, { K::number, "Trim keeps", params::cabTrim } };
    else
        return nullptr;

    return std::make_unique<SettingsPanel> (amp, block, std::move (rows));
}

} // namespace orbitamp::bass
