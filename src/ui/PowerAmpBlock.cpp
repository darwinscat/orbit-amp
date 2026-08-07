#include "PowerAmpBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

PowerAmpBlock::PowerAmpBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Power Amp", BlockFrame::Kind::dsp), state (s)
{
    addAndMakeVisible (type);
    addAndMakeVisible (drive);
    addAndMakeVisible (sag);

    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::powerDrive, drive);
    sagAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::powerSag, sag);

    attachPower (*state.getParameter (params::powerOn));

    type.setItems (params::powerTypes, 0);

    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::powerType),
        [this] (float v)
        {
            const int index = juce::roundToInt (v);
            type.setSelectedIndex (index, juce::dontSendNotification);

            // Index 0 is our simulation, the rest are captures. The frame follows.
            setKind (index == 0 ? BlockFrame::Kind::dsp : BlockFrame::Kind::captured);
        });

    type.onChange = [this] (int i) { typeAttachment->setValueAsCompleteGesture ((float) i); };

    typeAttachment->sendInitialUpdate();
}

PowerAmpBlock::~PowerAmpBlock() = default;

void PowerAmpBlock::layOutHeader (juce::Rectangle<int> area)
{
    type.setBounds (area);
}

void PowerAmpBlock::layOutContent (juce::Rectangle<int> area)
{
    // Two knobs out of the stage's ten controls: Drive is what a power amp is for, Sag is what makes
    // it feel like one. Everything else is the model, not a setting.
    const int side  = juce::jmin (area.getHeight(), (area.getWidth() - knobGap) / 2);
    const int total = side * 2 + knobGap;

    auto row = area.withSizeKeepingCentre (total, side);
    drive.setBounds (row.removeFromLeft (side));
    row.removeFromLeft (knobGap);
    sag.setBounds (row.removeFromLeft (side));
}

} // namespace orbitamp
