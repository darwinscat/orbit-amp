#include "ReverbBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

ReverbBlock::ReverbBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Reverb", BlockFrame::Kind::dsp), state (s)
{
    addAndMakeVisible (character);
    addAndMakeVisible (mix);

    attachPower (*state.getParameter (params::reverbOn));

    character.setItems (params::reverbCharacters, 0);
    mix.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };

    characterAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::reverbType),
        [this] (float v) { character.setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification); });

    character.onChange = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::reverbMix, mix);

    characterAttachment->sendInitialUpdate();
}

ReverbBlock::~ReverbBlock() = default;

void ReverbBlock::layOutContent (juce::Rectangle<int> area)
{
    character.setBounds (area.removeFromTop (combosHeight));
    area.removeFromTop (knobGap);

    const int side = juce::jmin (area.getWidth(), area.getHeight());
    mix.setBounds (area.withSizeKeepingCentre (side, side));
}

} // namespace orbitamp
