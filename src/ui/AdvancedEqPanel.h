#pragma once

#include "../Parameters.h"
#include "Knob.h"
#include "StepSwitch.h"

#include <memory>

namespace orbitamp
{

/** The ADVANCED half of a captured block's EQ: our filters, and where they stand.

    Identical in every block on purpose. A player learns three knobs and one switch once, and then
    knows the boost, the preamp and the power amp — the device side differs from pack to pack, and
    this side never does.

    PRE / POST is the important control here, not a detail of it, so it sits with the knobs rather
    than in a menu. It decides whether the filters colour what came out or change what goes in, and
    at any real amount of gain those are not the same sound at all. */
class AdvancedEqPanel : public juce::Component
{
public:
    AdvancedEqPanel (juce::AudioProcessorValueTreeState& state, const char* blockId)
    {
        addAndMakeVisible (placement);
        addAndMakeVisible (hpf);
        addAndMakeVisible (lpf);
        addAndMakeVisible (tilt);

        placement.accent = theme::violet;
        placement.setItems ({ "PRE", "POST" }, 1);

        placementAtt = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::advPre (blockId)),
            [this] (float v)
            {
                placement.setSelectedIndex (v > 0.5f ? 0 : 1, juce::dontSendNotification);
            });

        placement.onChange = [this] (int i)
        {
            placementAtt->setValueAsCompleteGesture (i == 0 ? 1.0f : 0.0f);
        };

        hpfAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, params::advHpf (blockId), hpf);
        lpfAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, params::advLpf (blockId), lpf);
        tiltAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, params::advTilt (blockId), tilt);

        hpf.textForValue  = asHz;
        lpf.textForValue  = asHz;
        tilt.textForValue = [] (double v) { return juce::String (v, 1); };

        placementAtt->sendInitialUpdate();
    }

    void resized() override
    {
        auto area = getLocalBounds();

        placement.setBounds (area.removeFromBottom (switchRow));
        area.removeFromBottom (gap);

        const int side = juce::jmin (area.getHeight(), (area.getWidth() - 2 * gap) / 3);
        auto row = area.withSizeKeepingCentre (side * 3 + gap * 2, side);

        hpf.setBounds (row.removeFromLeft (side));
        row.removeFromLeft (gap);
        tilt.setBounds (row.removeFromLeft (side));
        row.removeFromLeft (gap);
        lpf.setBounds (row.removeFromLeft (side));
    }

private:
    static constexpr int switchRow = 16;
    static constexpr int gap = 10;

    // Hertz reads better than a decimal here: "80" and "6.5k" are the numbers a player thinks in.
    static juce::String asHz (double v)
    {
        return v >= 1000.0 ? juce::String (v / 1000.0, 1) + "k" : juce::String ((int) v);
    }

    Knob hpf  { "Low Cut",  theme::violet };
    Knob tilt { "Tilt",     theme::violet };
    Knob lpf  { "High Cut", theme::violet };
    StepSwitch placement;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfAtt, lpfAtt, tiltAtt;
    std::unique_ptr<juce::ParameterAttachment> placementAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedEqPanel)
};

} // namespace orbitamp
