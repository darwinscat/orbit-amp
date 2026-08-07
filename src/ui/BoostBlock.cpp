#include "BoostBlock.h"

#include "../Parameters.h"
#include "../device/VoicingLibrary.h"

#include <cmath>

namespace orbitamp
{

BoostBlock::BoostBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Boost", BlockFrame::Kind::captured), state (s)
{
    addAndMakeVisible (pedal);
    addAndMakeVisible (gain);
    addAndMakeVisible (tone);
    addAndMakeVisible (curve);

    // Same tree and the same character ramp as the preamp's — a pedal is picked the same way an amp
    // voicing is, so it should not be a different kind of control.
    juce::Array<VoicingSelector::Group> groups;
    for (int t = 0; t < params::typeNames.size(); ++t)
        groups.add ({ params::typeNames[t], device::VoicingLibrary::pedalsFor (t) });

    pedal.setGroups (std::move (groups));
    pedal.onPick = [this] (int t, int v) { applyPick (t, v); };

    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::boostType),
        [this] (float v) { pedal.setSelection (juce::roundToInt (v), pedal.getItemIndex()); });

    voiceAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::boostVoice),
        [this] (float v) { pedal.setSelection (pedal.getGroupIndex(), juce::roundToInt (v)); });

    curve.setInterceptsMouseClicks (false, false);   // read-only: an illustration, not a control

    attachPower (*state.getParameter (params::boostOn));

    tone.onValueChange = [this] { refreshTone(); };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::boostGain, gain);
    toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::boostTone, tone);

    typeAttachment->sendInitialUpdate();
    voiceAttachment->sendInitialUpdate();

    refreshTone();
}

BoostBlock::~BoostBlock() = default;

void BoostBlock::applyPick (int typeIndex, int voiceIndex)
{
    typeAttachment->setValueAsCompleteGesture ((float) typeIndex);
    voiceAttachment->setValueAsCompleteGesture ((float) voiceIndex);
}

void BoostBlock::layOutHeader (juce::Rectangle<int> area)
{
    pedal.setBounds (area);
}

void BoostBlock::refreshTone()
{
    // Knob 0..10 sweeps the corner logarithmically: turning it down should darken evenly by ear,
    // and frequency is heard in ratios, not in hertz.
    const double t = juce::jlimit (0.0, 1.0, tone.getValue() / 10.0);
    const double lo = params::boostToneLoHz, hi = params::boostToneHiHz;
    const double corner = lo * std::pow (hi / lo, t);

    toneCoeffs = felitronics::eq::matched::lowpass1 (corner, displayRate);
    curve.repaint();
}

double BoostBlock::toneMagnitudeDb (double freqHz) const
{
    const double w = 2.0 * juce::MathConstants<double>::pi
                       * juce::jlimit (1.0, 0.499 * displayRate, freqHz) / displayRate;
    return toneCoeffs.magnitudeDb (w);
}

void BoostBlock::layOutContent (juce::Rectangle<int> area)
{
    curve.setBounds (area.removeFromBottom (curveHeight));
    area.removeFromBottom (curveGap);

    // The gain knob is the hero here too; tone is the smaller one beside it.
    const int side  = juce::jmin (area.getHeight(), (area.getWidth() - knobGap) * 3 / 5);
    const int small = side * 2 / 3;
    const int total = side + knobGap + small;

    auto row = area.withSizeKeepingCentre (total, area.getHeight());
    gain.setBounds (row.removeFromLeft (side).withSizeKeepingCentre (side, side));
    row.removeFromLeft (knobGap);
    tone.setBounds (row.removeFromLeft (small).withSizeKeepingCentre (small, small));
}

} // namespace orbitamp
