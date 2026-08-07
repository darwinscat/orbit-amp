#include "PreampBlock.h"

#include "../Parameters.h"
#include "../device/VoicingLibrary.h"

namespace orbitamp
{

PreampBlock::PreampBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Preamp", BlockFrame::Kind::captured), state (s), eq (s)
{
    addAndMakeVisible (voicing);
    addAndMakeVisible (gain);
    addAndMakeVisible (eq);

    attachPower (*state.getParameter (params::preampOn));

    // The whole tree at once — every type with its voices under it. Nothing is rebuilt on a pick;
    // only the selection moves.
    juce::Array<VoicingSelector::Group> groups;
    for (int t = 0; t < params::typeNames.size(); ++t)
        groups.add ({ params::typeNames[t], device::VoicingLibrary::voicesFor (t) });

    voicing.setGroups (std::move (groups));
    voicing.onPick = [this] (int t, int v) { applyPick (t, v); };

    // Choice / int parameters come through a plain ParameterAttachment because this is a custom view,
    // not a juce widget — the attachment reports DENORMALISED values, i.e. the index itself.
    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampType),
        [this] (float v)
        {
            voicing.setSelection (juce::roundToInt (v), voicing.getItemIndex());
        });

    voiceAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampVoice),
        [this] (float v)
        {
            voicing.setSelection (voicing.getGroupIndex(), juce::roundToInt (v));
        });

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::preampGain, gain);

    typeAttachment->sendInitialUpdate();
    voiceAttachment->sendInitialUpdate();
}

PreampBlock::~PreampBlock() = default;

void PreampBlock::applyPick (int typeIndex, int voiceIndex)
{
    // Clamp on the way in: a tree pick is always in range, but the parameter is an index and a saved
    // session may hold one the type no longer offers.
    const int voice = device::VoicingLibrary::clampVoice (typeIndex, voiceIndex);

    typeAttachment->setValueAsCompleteGesture ((float) typeIndex);
    voiceAttachment->setValueAsCompleteGesture ((float) voice);
}

void PreampBlock::layOutHeader (juce::Rectangle<int> area)
{
    voicing.setBounds (area);
}

void PreampBlock::layOutContent (juce::Rectangle<int> area)
{
    // Lower half is the illustration — here, the tone stack. Upper half is the hero.
    eq.setBounds (area.removeFromBottom (EqSection::designHeight));
    area.removeFromBottom (eqGap);

    const int side = juce::jmin (area.getWidth(), area.getHeight());
    gain.setBounds (area.withSizeKeepingCentre (side, side));
}

} // namespace orbitamp
