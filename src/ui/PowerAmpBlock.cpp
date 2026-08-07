#include "PowerAmpBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

PowerAmpBlock::PowerAmpBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Power Amp", BlockFrame::Kind::dsp), state (s)
{
    addAndMakeVisible (type);

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

void PowerAmpBlock::paintContent (juce::Graphics& g)
{
    // Nothing here yet. Say so rather than leave an unexplained hole — an empty framed box reads as
    // a bug, a labelled one reads as unfinished.
    g.setColour (theme::txFaint.withAlpha (0.5f));
    theme::drawTracked (g, "No illustration yet", contentArea().toFloat(),
                        theme::displayFont (8.0f), 0.1f, juce::Justification::centred);
}

} // namespace orbitamp
