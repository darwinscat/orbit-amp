#pragma once

#include "../core/ToneStack.h"
#include "BlockFrame.h"
#include "EqCurve.h"
#include "FilterPill.h"
#include "Knob.h"

namespace orbitamp
{

/** The tone block: Low shelf, Mid bell, High shelf, and the optional HPF/LPF cuts, over a live
    response curve.

    It keeps a ToneStack of its own for DRAWING only — designed on the message thread from the
    current parameter values, never touched by audio. That is deliberate: reading the playing
    stack's coefficients would be a race, and would leave the curve frozen whenever transport is
    stopped. Two designs of the same five filters is a cheap price for a curve that is always
    right. */
class EqBlock final : public BlockFrame
{
public:
    explicit EqBlock (juce::AudioProcessorValueTreeState&);
    ~EqBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;

    /** Re-designs the drawing stack from the current parameters and repaints the curve. */
    void refreshCurve();

    static constexpr int curveHeight = 108;
    static constexpr int curveGap    = 10;
    static constexpr int knobGap     = 52;
    static constexpr int pillWidth   = 62;
    static constexpr int pillHeight  = 18;
    static constexpr int pillInset   = 8;

    juce::AudioProcessorValueTreeState& state;

    // The drawing-only stack. Fixed at a nominal rate: across host rates the matched biquads differ
    // by far less than a pixel of this curve, and pinning it keeps the drawing free of the audio
    // thread's sample rate entirely.
    static constexpr double displayRate = 48000.0;
    core::ToneStack display;

    EqCurve curve { [this] (double hz) { return display.magnitudeDb (hz); } };

    Knob low  { "Low",  theme::violet, 0 };   // no notches — a tone control is a continuous cut/boost
    Knob mid  { "Mid",  theme::violet, 0 };
    Knob high { "High", theme::violet, 0 };

    FilterPill hpf { "HPF", { 20.0, 500.0 } };
    FilterPill lpf { "LPF", { 2000.0, 20000.0 } };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowAtt, midAtt, highAtt;
    std::unique_ptr<juce::ParameterAttachment> hpfOnAtt, hpfHzAtt, lpfOnAtt, lpfHzAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqBlock)
};

} // namespace orbitamp
