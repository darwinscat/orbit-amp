#pragma once

#include "../core/ReverbStage.h"
#include "BlockFrame.h"
#include "Knob.h"
#include "ReverbTailView.h"
#include "VoicingSelector.h"

namespace orbitamp
{

/** The reverb — our own tail on top of the captured voice.

    Mix is the whole control surface, per the design's simple case; size and damping follow from the
    chosen character rather than sitting on the face as loose knobs. The character stands on the
    block's border, the way a captured block's device does, so the box holds the dial and, beside it,
    the picture of the tail. Everything the block can do is reachable from what you can see. */
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

    static constexpr int knobGap      = 10;
    static constexpr int maxKnobSide  = 84;    // the dial stops growing before it eats the picture — the tail is the wider of the two

    static constexpr double displayRate = 48000.0;
    static constexpr double tailSeconds = 1.5;
    static constexpr int    tailBuckets = 240;

    juce::AudioProcessorValueTreeState& state;

    VoicingSelector character;
    Knob           mix       { "Mix", theme::violet, 0 };

    /** The late refinements, deliberately small beside the hero: the tail's breath and its
        holding-back. They overlay the picture like the dial does. */
    Knob decay { "Decay", theme::violet, 0 };
    Knob pre   { "Pre",   theme::violet, 0 };

    ReverbTailView tail;

    core::ReverbStage display;   // drawing only; never sees the audio thread

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,
                                                                          decayAttachment,
                                                                          preAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbBlock)
};

} // namespace orbitamp
