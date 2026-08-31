// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "DelayBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

DelayBlock::DelayBlock (AmpProcessor& processor)
    : BlockFrame ("Delay", BlockFrame::Kind::dsp), amp (processor)
{
    startTimerHz (envTicksPerSecond);

    addAndMakeVisible (view);
    addAndMakeVisible (timeSel);
    addAndMakeVisible (mix);
    addAndMakeVisible (repeats);
    addAndMakeVisible (dark);
    addAndMakeVisible (offset);
    addAndMakeVisible (field);

    attachPower (*amp.apvts.getParameter (params::delayOn));

    // The TIME on the border, where the captured blocks stand their combo: the sync ladder, and
    // FREE under a rule at the bottom for the player who wants milliseconds instead of a grid.
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::delayDivisions)
            entries.add ({ name, 0, false });
        entries.add ({ "FREE", 0, true });
        timeSel.setEntries (std::move (entries));
    }
    timeSel.fontHeight = 12.0f;
    timeSel.tracking   = 0.10f;
    timeSel.boxed      = false;
    timeSel.tint       = theme::lilac;

    mix.textForValue     = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    repeats.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    dark.textForValue    = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    offset.textForValue  = [] (double v)
    {
        const int ms = juce::roundToInt (v);
        return (ms > 0 ? "+" : "") + juce::String (ms);
    };
    repeats.labelFontHeight = 7.0f;
    dark.labelFontHeight    = 7.0f;
    offset.labelFontHeight  = 7.0f;

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayMix, mix);
    repeatsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayRepeats, repeats);
    darkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayDark, dark);
    offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::delayOffset, offset);

    // Sync and division both redress the face; the field follows whichever number is conducting.
    syncAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delaySync), [this] (float) { applyTimeMode(); });
    divAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayDiv), [this] (float) { applyTimeMode(); });
    bpmAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayBpm), [this] (float) { field.repaint(); });
    timeAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::delayTimeMs), [this] (float) { field.repaint(); });

    timeSel.onPick = [this] (int i)
    {
        if (i < params::delayDivisions.size())
        {
            divAtt->setValueAsCompleteGesture ((float) i);
            if (! syncOn())
                syncAtt->setValueAsCompleteGesture (1.0f);
        }
        else
            syncAtt->setValueAsCompleteGesture (0.0f);
    };

    field.text = [this]
    {
        return syncOn() ? juce::String (juce::roundToInt (plain (params::delayBpm))) + " BPM"
                        : juce::String (juce::roundToInt (plain (params::delayTimeMs))) + " MS";
    };

    field.onDrag = [this] (int phase, float dy)
    {
        auto& att = syncOn() ? *bpmAtt : *timeAtt;

        if (phase == 0)
        {
            dragStart = plain (syncOn() ? params::delayBpm : params::delayTimeMs);
            att.beginGesture();
        }
        else if (phase == 1)
            att.setValueAsPartOfGesture (dragStart + dy * (syncOn() ? 0.25f : 2.0f));
        else
            att.endGesture();
    };

    field.onTyped = [this] (const juce::String& typed)
    {
        if (const auto v = typed.getDoubleValue(); v > 0.0)
            (syncOn() ? *bpmAtt : *timeAtt).setValueAsCompleteGesture ((float) v);
    };

    view.paintBody = [this] (juce::Graphics& g, juce::Rectangle<float> r) { paintComb (g, r); };

    applyTimeMode();
}

DelayBlock::~DelayBlock() = default;

void DelayBlock::timerCallback()
{
    if (! view.isShowing())
        return;

    // The pulse's memory advances every tick, silence included — a quiet bucket is what lets
    // a note READ as a note when it arrives. A switched-off block feeds zeros: its line hears
    // nothing, so its comb goes still rather than replaying the last thing it saw.
    const float in = isBlockOn() ? amp.delayTaps().envIn.load (std::memory_order_relaxed) : 0.0f;

    // Fast attack, slow release — the eye wants the hit now and the fade at its own pace.
    envSmooth = in > envSmooth ? in : envSmooth * 0.72f;

    envPos = (envPos + 1) % envHistSize;
    envHist[(size_t) envPos] = envSmooth;

    view.repaint();
}

void DelayBlock::paintComb (juce::Graphics& g, juce::Rectangle<float> r)
{
    // The ground: the dark well every picture stands in.
    g.setColour (juce::Colour (0xff101016));
    g.fillRoundedRectangle (r, theme::radiusSm);

    const auto& taps  = amp.delayTaps();
    const float timeT = juce::jmax (1.0f, taps.shownTimeMs.load (std::memory_order_relaxed));
    const float offMs = taps.shownOffsetMs.load (std::memory_order_relaxed);
    const float reps  = plain (params::delayRepeats) * 0.01f;
    const float darkA = plain (params::delayDark) * 0.01f;
    const float mixA  = plain (params::delayMix) * 0.01f;

    // The window: fixed at two seconds so turning TIME visibly spreads the teeth, growing
    // only when a long time would push the first repeats off the glass.
    const float winMs = juce::jmax (2000.0f, 3.5f * timeT);

    const auto plot = r.reduced (6.0f, 8.0f);
    const float base = plot.getBottom();

    // The dry hit at the left edge: the note entering the line, the grey the spectra use.
    {
        const float p = std::pow (juce::jlimit (0.0f, 1.0f, envHist[(size_t) envPos]), 0.4f);
        g.setColour (theme::spectrum.withAlpha (0.25f + 0.6f * p));
        g.fillRoundedRectangle (plot.getX(), base - plot.getHeight() * (0.25f + 0.2f * p),
                                2.0f, plot.getHeight() * (0.25f + 0.2f * p), 1.0f);
    }

    // The teeth: one per repeat. Height falls by REPEATS and stands on MIX; the ink dulls
    // along the tail by DARK — the filter made visible; OFFSET stands a lilac twin beside
    // every tooth. The pulse reads the envelope n repeats ago: the note walks the comb.
    const float heightScale = 0.30f + 0.65f * mixA;
    const juce::Colour dulled = theme::violet.interpolatedWith (juce::Colour (0xff3a3350), 0.85f);

    for (int n = 1; n <= 24; ++n)
    {
        const float h01 = std::pow (juce::jmax (0.0f, reps), (float) (n - 1)) * heightScale;
        const float x   = plot.getX() + plot.getWidth() * ((float) n * timeT / winMs);

        if (h01 < 0.02f || x > plot.getRight())
            break;

        // The bucket n repeats of the current time ago; older than the memory is simply quiet.
        const int   age   = juce::roundToInt ((float) n * timeT / 1000.0f * (float) envTicksPerSecond);
        const float env   = age < envHistSize ? envHist[(size_t) ((envPos - age + envHistSize) % envHistSize)] : 0.0f;
        const float pulse = std::pow (juce::jlimit (0.0f, 1.0f, env), 0.4f)
                              * std::pow (juce::jmax (0.0f, reps), (float) (n - 1));

        const float h    = plot.getHeight() * h01 * (1.0f + 0.18f * pulse);
        const auto  ink  = theme::violet.interpolatedWith (dulled,
                               juce::jlimit (0.0f, 1.0f, (float) n * darkA * 0.30f));

        const auto tooth = [&] (float tx, juce::Colour c, float w)
        {
            g.setColour (c);
            g.fillRoundedRectangle (tx - w * 0.5f, base - h, w, h, w * 0.5f);
        };

        // The lilac twin first, so the main tooth reads on top when the offset is small.
        if (std::abs (offMs) > 0.05f)
            tooth (x + plot.getWidth() * (offMs / winMs),
                   theme::lilac.withAlpha (0.30f + 0.45f * pulse), 2.0f);

        tooth (x, ink.withAlpha (0.55f + 0.45f * pulse), 2.5f);
    }

    g.setColour (theme::hair2);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
}

bool DelayBlock::syncOn() const
{
    return plain (params::delaySync) > 0.5f;
}

float DelayBlock::plain (const char* id) const
{
    auto* p = amp.apvts.getParameter (id);
    return p->convertFrom0to1 (p->getValue());
}

void DelayBlock::applyTimeMode()
{
    timeSel.setSelection (syncOn() ? juce::jlimit (0, params::delayDivisions.size() - 1,
                                                   juce::roundToInt (plain (params::delayDiv)))
                                   : params::delayDivisions.size());
    field.repaint();
    resized();   // the pill on the border is as wide as its word
}

void DelayBlock::layOutContent (juce::Rectangle<int> area)
{
    // The time pill on the border, after the name — the captured blocks' combo slot.
    {
        const auto slot = borderSlotArea();
        timeSel.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), timeSel.idealWidth())));
        borderSlotUsed = timeSel.getBounds();
    }

    // The console's grammar: the picture above, the hands on a shelf below it.
    const int mini = 42;
    auto shelf = area.removeFromBottom (mini + 11);

    view.setBounds (area);

    // The hero stays ON the picture, top-right — the reverb's arrangement.
    const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight() / 2 + 14));
    mix.setBounds (area.removeFromTop (side).removeFromRight (side).translated (-2, 2));
    mix.toFront (false);

    // The three refinements in even thirds across the shelf, reading in the loop's own order:
    // how many, how dark, how wide.
    const int cell = shelf.getWidth() / 3;
    repeats.setBounds (shelf.removeFromLeft (cell).withSizeKeepingCentre (mini, mini + 11));
    dark.setBounds    (shelf.removeFromLeft (cell).withSizeKeepingCentre (mini, mini + 11));
    offset.setBounds  (shelf.withSizeKeepingCentre (mini, mini + 11));

    // The conducting number at the picture's top-left — the hero's opposite corner.
    field.setBounds (view.getBounds().withTrimmedLeft (6).withTrimmedTop (6)
                         .removeFromTop (20).removeFromLeft (78));
    field.toFront (false);
}

} // namespace orbitamp
