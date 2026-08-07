#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "Selector.h"

namespace orbitamp
{

/** The power amp, first block of the lower row.

    It is the one block whose NATURE depends on what is loaded: the simulation is our own white-box
    tube stage, the NAM slots are captured hardware. So the frame recolours with the choice —
    violet for rebuilt, orange for captured — rather than claiming one of them permanently.

    The illustration below is not designed yet; the space is reserved so the row has its final
    proportions and whatever lands there does not move everything else. */
class PowerAmpBlock final : public BlockFrame
{
public:
    explicit PowerAmpBlock (juce::AudioProcessorValueTreeState&);
    ~PowerAmpBlock() override;

private:
    int  headerHeight() const override { return headerRow; }
    void layOutHeader (juce::Rectangle<int>) override;
    void layOutContent (juce::Rectangle<int>) override;

    static constexpr int headerRow = 22;

    juce::AudioProcessorValueTreeState& state;

    Selector type { theme::violet, true };
    Knob     drive { "Drive", theme::violet, 0 };
    Knob     sag   { "Sag",   theme::violet, 0 };

    static constexpr int knobGap = 12;

    std::unique_ptr<juce::ParameterAttachment> typeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, sagAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerAmpBlock)
};

} // namespace orbitamp
