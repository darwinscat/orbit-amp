#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "VoicingSelector.h"

namespace orbitamp
{

/** The preamp — the captured voicing.

    Type and voice are ONE control, in the title row: picking a voicing is one decision, and it used
    to cost two lists and a whole row of the block. With that row gone the gain knob takes the entire
    body, which is what the block is for. */
class PreampBlock final : public BlockFrame
{
public:
    explicit PreampBlock (juce::AudioProcessorValueTreeState&);
    ~PreampBlock() override;

private:
    int  headerHeight() const override { return headerRow; }
    void layOutHeader (juce::Rectangle<int>) override;
    void layOutContent (juce::Rectangle<int>) override;

    /** Writes both parameters for one pick. They land in the same message-loop turn, so the history's
        settle timer folds them into a single undo step. */
    void applyPick (int typeIndex, int voiceIndex);

    // Taller than the default title row, because it carries a control. Only this block asks for it,
    // so the other blocks keep the layout they already had.
    static constexpr int headerRow = 22;

    juce::AudioProcessorValueTreeState& state;

    VoicingSelector voicing;
    Knob            gain { "Gain", theme::orange };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> typeAttachment, voiceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampBlock)
};

} // namespace orbitamp
