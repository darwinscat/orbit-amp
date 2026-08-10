#include "PreampBlock.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

PreampBlock::PreampBlock (AmpProcessor& processor)
    : BlockFrame ("Preamp", BlockFrame::Kind::captured), amp (processor), state (processor.apvts),
      eq (processor.apvts)
{
    addAndMakeVisible (voicing);
    addAndMakeVisible (gain);
    eq.addTo (*this);

    attachPower (*state.getParameter (params::preampOn));

    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::preampDevice),
        [this] (float v) { voicing.setSelection (juce::roundToInt (v)); });

    voicing.onPick = [this] (int i)
    {
        deviceAttachment->setValueAsCompleteGesture ((float) i);
        amp.selectPreampDevice (i);
        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::preampGain, gain);

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

PreampBlock::~PreampBlock() = default;

void PreampBlock::deviceChanged()
{
    // The list is what is on disk, greenest first — same rule as the boost's, and the same reason:
    // the character ramp orders it and a rule marks where the shipped ones end.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : amp.preamp.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    voicing.setEntries (std::move (entries));

    // The detents ARE the captured positions, and a device with no gain axis gets no knob rather than
    // a dead one.
    const auto positions = amp.preamp.gainPositions();
    // Detented where the pack has positions, continuous where it does not — but always THERE. A
    // preamp without a gain knob is not a preamp, and a knob that drives is not a knob doing nothing.
    gain.setNotches (juce::jmax (0, positions.size()));

    resized();
    repaint();
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
