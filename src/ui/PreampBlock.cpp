#include "PreampBlock.h"

#include "../Parameters.h"
#include "../device/VoicingLibrary.h"

namespace orbitamp
{

PreampBlock::PreampBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Preamp", BlockFrame::Kind::captured), state (s)
{
    addAndMakeVisible (type);
    addAndMakeVisible (voice);
    addAndMakeVisible (gain);

    attachPower (*state.getParameter (params::preampOn));

    type.setItems (params::typeNames, 0);

    // Choice / int parameters come through a plain ParameterAttachment because these are custom
    // views, not juce widgets — the attachment reports DENORMALISED values, i.e. the index itself.
    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampType),
        [this] (float v)
        {
            const int index = juce::roundToInt (v);
            type.setSelectedIndex (index, juce::dontSendNotification);
            refreshVoices (index);
        });

    voiceAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampVoice),
        [this] (float v) { voice.setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification); });

    type.onChange  = [this] (int i) { typeAttachment->setValueAsCompleteGesture ((float) i); };
    voice.onChange = [this] (int i) { voiceAttachment->setValueAsCompleteGesture ((float) i); };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::preampGain, gain);

    typeAttachment->sendInitialUpdate();
    voiceAttachment->sendInitialUpdate();
}

PreampBlock::~PreampBlock() = default;

void PreampBlock::refreshVoices (int typeIndex)
{
    const auto names = device::VoicingLibrary::voicesFor (typeIndex);
    const int wanted = device::VoicingLibrary::clampVoice (typeIndex, voice.getSelectedIndex());

    voice.setItems (names, wanted);

    // Switching type can leave the stored index past the end of the new type's list; write the
    // clamped value back so the parameter and the face never disagree.
    if (voiceAttachment != nullptr && wanted != voice.getSelectedIndex())
        voiceAttachment->setValueAsCompleteGesture ((float) wanted);
}

void PreampBlock::layOutContent (juce::Rectangle<int> area)
{
    auto combos = area.removeFromTop (combosHeight);
    type.setBounds (combos.removeFromLeft (typeWidth));
    combos.removeFromLeft (combosGap);
    voice.setBounds (combos);

    area.removeFromTop (knobGap);

    const int side = juce::jmin (area.getWidth(), area.getHeight());
    gain.setBounds (area.withSizeKeepingCentre (side, side));
}

} // namespace orbitamp
