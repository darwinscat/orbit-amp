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
    eq.addTo (*this);

    attachPower (*state.getParameter (params::preampOn));

    // Every voicing in one flat list, greenest first, with a rule where the type changes. The type
    // is not a heading any more — it is the colour, and the order.
    //
    // STILL THE PLACEHOLDER SET. The boost's list comes from what is on disk; this one cannot, until
    // there are preamp captures to scan. The shape is now the same, so that swap is a scan and not a
    // rewrite.
    juce::Array<VoicingSelector::Entry> entries;
    flat.clear();

    for (int ti = 0; ti < params::typeNames.size(); ++ti)
    {
        const auto voices = device::VoicingLibrary::voicesFor (ti);

        for (int vi = 0; vi < voices.size(); ++vi)
        {
            entries.add ({ voices[vi], ti, vi == 0 && ti > 0 });
            flat.add ({ ti, vi });
        }
    }

    voicing.setEntries (std::move (entries));
    voicing.onPick = [this] (int i)
    {
        if (juce::isPositiveAndBelow (i, flat.size()))
            applyPick (flat.getReference (i).type, flat.getReference (i).voice);
    };

    // Choice / int parameters come through a plain ParameterAttachment because this is a custom view,
    // not a juce widget — the attachment reports DENORMALISED values, i.e. the index itself.
    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampType),
        [this] (float) { syncSelection(); });

    voiceAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampVoice),
        [this] (float) { syncSelection(); });

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::preampGain, gain);

    typeAttachment->sendInitialUpdate();
    voiceAttachment->sendInitialUpdate();
}

PreampBlock::~PreampBlock() = default;

void PreampBlock::syncSelection()
{
    const int type  = juce::roundToInt (state.getParameter (params::preampType)->getValue()
                                        * (float) (params::typeNames.size() - 1));
    const int voice = juce::roundToInt (
        state.getParameter (params::preampVoice)->convertFrom0to1 (
            state.getParameter (params::preampVoice)->getValue()));

    for (int i = 0; i < flat.size(); ++i)
        if (flat.getReference (i).type == type && flat.getReference (i).voice == voice)
        {
            voicing.setSelection (i);
            return;
        }
}

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
    // Lower half is the illustration — here, the tone stack's response, full width.
    eq.layOutCurve (area.removeFromBottom (EqSection::designHeight));
    area.removeFromBottom (eqGap);

    // Upper half: the hero on the left, the tone cluster on the right.
    auto left = area.removeFromLeft (area.getWidth() * 2 / 5);
    area.removeFromLeft (eqGap);

    const int side = juce::jmin (left.getWidth(), left.getHeight());
    gain.setBounds (left.withSizeKeepingCentre (side, side));

    eq.layOutKnobs (area);
}

} // namespace orbitamp
