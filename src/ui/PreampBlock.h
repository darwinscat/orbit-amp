#pragma once

#include "BlockFrame.h"
#include "EqSection.h"
#include "Knob.h"
#include "VoicingSelector.h"

namespace orbitamp
{

class AmpProcessor;

/** The preamp — a captured device, chosen from what is on disk.

    Built from the pack the same way the boost is: the combo is the list of preamps found, the gain
    knob's detents are the positions that device was captured at, and its measured controls become
    knobs or switches according to what it shipped. What it does NOT get from the pack is the tone
    stack below — that is ours, and it stays whatever the device is.

    The voicing lives in the title row: picking one is a single decision, and it used to cost two
    lists and a whole row of the block. */
class PreampBlock final : public BlockFrame
{
public:
    explicit PreampBlock (AmpProcessor&);
    ~PreampBlock() override;

    /** Rebuilds the face from whatever device is loaded. */
    void deviceChanged();

private:
    int  headerHeight() const override { return headerRow; }
    void layOutHeader (juce::Rectangle<int>) override;
    void layOutContent (juce::Rectangle<int>) override;

    // Taller than the default title row, because it carries a control. Only this block asks for it,
    // so the other blocks keep the layout they already had.
    static constexpr int headerRow = 22;

    AmpProcessor& amp;
    juce::AudioProcessorValueTreeState& state;

    static constexpr int eqGap = 10;

    VoicingSelector voicing;
    Knob            gain { "Gain", theme::orange };
    EqSection       eq;   // the block's lower half: the tone stack, as its illustration

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampBlock)
};

} // namespace orbitamp
