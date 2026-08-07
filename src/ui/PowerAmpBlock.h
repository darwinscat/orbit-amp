#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "Selector.h"
#include "StepSwitch.h"

#include <felitronics/appkit/DeviceGlyph.h>

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
    void paintContent (juce::Graphics&) override;

    static constexpr int headerRow = 22;

    juce::AudioProcessorValueTreeState& state;

    Selector   type  { theme::violet, true };
    Selector   tube  { theme::orange, true };   // the bottle: captured hardware's colour, not ours
    StepSwitch count;
    Knob     drive { "Drive", theme::violet, 0 };
    Knob     sag   { "Sag",   theme::violet, 0 };

    static constexpr int knobGap   = 12;
    static constexpr int tubeRow   = 20;
    static constexpr int countCol  = 46;
    static constexpr int glyphRow  = 26;
    static constexpr int rowGap    = 8;

    juce::Rectangle<int> glyphArea;

    std::unique_ptr<juce::ParameterAttachment> typeAttachment, tubeAttachment, countAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment, sagAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerAmpBlock)
};

} // namespace orbitamp
