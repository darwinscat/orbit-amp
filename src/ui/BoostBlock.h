#pragma once

#include "BlockFrame.h"
#include "EqCurve.h"
#include "Knob.h"

#include <felitronics/eq/MatchedBiquad.h>

namespace orbitamp
{

/** The boost — a captured pedal in front of the preamp.

    Upper half is the pedal's own controls: a stepped gain knob, because gain SELECTS a capture and
    between the captured positions there is nothing, and a continuous tone knob, because tone is
    measured rather than captured and interpolating a measured curve is honest.

    Lower half is the block's illustration: the tone control's response, read-only. A pedal usually
    has one or two knobs and what matters is knowing what they actually do to the signal — so the
    block shows it instead of naming it. */
class BoostBlock final : public BlockFrame
{
public:
    explicit BoostBlock (juce::AudioProcessorValueTreeState&);
    ~BoostBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;

    /** The measured tone, as the device descriptor models it: a first-order low pass whose corner
        follows the knob. Drawing only — the captured half of this pedal does not exist yet. */
    double toneMagnitudeDb (double freqHz) const;
    void   refreshTone();

    static constexpr int curveHeight = 150;   // matches the preamp's EQ, so the row reads level
    static constexpr int curveGap    = 10;
    static constexpr int knobGap     = 14;

    juce::AudioProcessorValueTreeState& state;

    Knob gain { "Gain", theme::orange, 21 };   // 21 notches = the 0.5 steps of the parameter
    Knob tone { "Tone", theme::orange, 0 };

    EqCurve curve { [this] (double hz) { return toneMagnitudeDb (hz); } };

    felitronics::eq::BiquadCoeffs toneCoeffs;
    static constexpr double displayRate = 48000.0;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, toneAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoostBlock)
};

} // namespace orbitamp
