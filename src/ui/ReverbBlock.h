#pragma once

#include "../core/ReverbStage.h"
#include "BlockFrame.h"
#include "Knob.h"
#include "ReverbTailView.h"
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

    /** Runs an impulse through a display-only reverb and hands the tail's envelope to the view.
        Measuring beats drawing a picture of a room: the shape shown is the shape you hear. */
    void refreshTail();

    static constexpr int combosHeight = 26;
    static constexpr int knobGap      = 10;
    static constexpr int maxKnobSide  = 96;    // the dial stops growing before it eats the picture

    static constexpr double displayRate = 48000.0;
    static constexpr double tailSeconds = 1.5;
    static constexpr int    tailBuckets = 240;

    juce::AudioProcessorValueTreeState& state;

    Selector       character { theme::violet, true };
    Knob           mix       { "Mix", theme::violet, 0 };
    ReverbTailView tail;

    core::ReverbStage display;   // drawing only; never sees the audio thread

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbBlock)
};

} // namespace orbitamp
