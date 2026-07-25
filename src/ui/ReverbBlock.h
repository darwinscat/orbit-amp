#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "Selector.h"

namespace orbitamp
{

/** The reverb — our own tail on top of the captured voice.

    Mix is the whole control surface, per the design's simple case; size and damping follow from the
    chosen character rather than sitting on the face as loose knobs. Everything the block can do is
    reachable from what you can see. */
class ReverbBlock final : public BlockFrame
{
public:
    explicit ReverbBlock (juce::AudioProcessorValueTreeState&);
    ~ReverbBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;

    static constexpr int combosHeight = 26;
    static constexpr int knobGap      = 10;

    juce::AudioProcessorValueTreeState& state;

    Selector character { theme::violet, true };
    Knob     mix       { "Mix", theme::violet, 0 };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbBlock)
};

} // namespace orbitamp
