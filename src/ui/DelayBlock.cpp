#include "DelayBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

DelayBlock::DelayBlock (AmpProcessor& processor)
    : BlockFrame ("Delay", BlockFrame::Kind::dsp), amp (processor)
{
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

    applyTimeMode();
}

DelayBlock::~DelayBlock() = default;

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

    // The picture takes the whole box; everything else overlays it.
    view.setBounds (area);

    const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight() / 2 + 14));
    auto row = area.removeFromTop (side);
    mix.setBounds (row.removeFromRight (side).translated (-2, 2));
    mix.toFront (false);

    // The two loop refinements, small at the hero's left hand, on one centre line.
    const int mini = 42;
    auto miniRow = row.removeFromRight (mini * 2 + 6).withHeight (mini + 11)
                       .translated (0, (side - mini) / 2 - 4);
    repeats.setBounds (miniRow.removeFromLeft (mini));
    miniRow.removeFromLeft (6);
    dark.setBounds (miniRow);
    repeats.toFront (false);
    dark.toFront (false);

    // The bottom corners: the conducting number at the left, the stereo at the right.
    auto bottom = area.removeFromBottom (mini + 11);
    offset.setBounds (bottom.removeFromRight (mini).translated (-2, -2));
    offset.toFront (false);

    field.setBounds (bottom.removeFromLeft (78).withSizeKeepingCentre (78, 20).translated (6, 8));
    field.toFront (false);
}

} // namespace orbitamp
