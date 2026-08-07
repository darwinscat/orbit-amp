#include "BoostBlock.h"

#include "../Parameters.h"

#include <cmath>

namespace orbitamp
{

BoostBlock::BoostBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Boost", BlockFrame::Kind::captured), state (s)
{
    addAndMakeVisible (gain);
    addAndMakeVisible (tone);
    addAndMakeVisible (curve);

    curve.setInterceptsMouseClicks (false, false);   // read-only: an illustration, not a control

    attachPower (*state.getParameter (params::boostOn));

    tone.onValueChange = [this] { refreshTone(); };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::boostGain, gain);
    toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::boostTone, tone);

    refreshTone();
}

BoostBlock::~BoostBlock() = default;

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
