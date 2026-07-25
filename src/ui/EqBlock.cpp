#include "EqBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

EqBlock::EqBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("EQ", BlockFrame::Kind::dsp), state (s)
{
    display.prepare (displayRate, 1);

    addAndMakeVisible (curve);
    addAndMakeVisible (low);
    addAndMakeVisible (mid);
    addAndMakeVisible (high);
    addAndMakeVisible (hpf);
    addAndMakeVisible (lpf);

    attachPower (*state.getParameter (params::eqOn));

    // Tone knobs read in dB, signed — the sign is the whole information here.
    for (auto* k : { &low, &mid, &high })
        k->textForValue = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };

    lowAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqLow,  low);
    midAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqMid,  mid);
    highAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqHigh, high);

    for (auto* k : { &low, &mid, &high })
        k->onValueChange = [this] { refreshCurve(); };

    auto attach = [this] (const char* id, auto&& onChanged)
    {
        return std::make_unique<juce::ParameterAttachment> (*state.getParameter (id),
                                                            std::forward<decltype (onChanged)> (onChanged));
    };

    hpfOnAtt = attach (params::eqHpfOn, [this] (float v) { hpf.setState (v > 0.5f, hpf.getHz()); refreshCurve(); });
    hpfHzAtt = attach (params::eqHpfHz, [this] (float v) { hpf.setState (hpf.isOn(), (double) v); refreshCurve(); });
    lpfOnAtt = attach (params::eqLpfOn, [this] (float v) { lpf.setState (v > 0.5f, lpf.getHz()); refreshCurve(); });
    lpfHzAtt = attach (params::eqLpfHz, [this] (float v) { lpf.setState (lpf.isOn(), (double) v); refreshCurve(); });

    hpf.onToggled     = [this] (bool on)  { hpfOnAtt->setValueAsCompleteGesture (on ? 1.0f : 0.0f); };
    lpf.onToggled     = [this] (bool on)  { lpfOnAtt->setValueAsCompleteGesture (on ? 1.0f : 0.0f); };
    hpf.onFreqChanged = [this] (double f) { hpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };
    lpf.onFreqChanged = [this] (double f) { lpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };

    // A sweep is ONE undoable move in the host, not one per pixel.
    hpf.onDragActive = [this] (bool active) { active ? hpfHzAtt->beginGesture() : hpfHzAtt->endGesture(); };
    lpf.onDragActive = [this] (bool active) { active ? lpfHzAtt->beginGesture() : lpfHzAtt->endGesture(); };

    for (auto* a : { &hpfOnAtt, &hpfHzAtt, &lpfOnAtt, &lpfHzAtt })
        (*a)->sendInitialUpdate();

    refreshCurve();
}

EqBlock::~EqBlock() = default;

void EqBlock::refreshCurve()
{
    core::ToneStack::Settings s;
    s.lowDb  = low.getValue();
    s.midDb  = mid.getValue();
    s.highDb = high.getValue();
    s.hpfOn  = hpf.isOn();
    s.hpfHz  = hpf.getHz();
    s.lpfOn  = lpf.isOn();
    s.lpfHz  = lpf.getHz();

    display.setSettings (s);
    curve.repaint();
}

void EqBlock::layOutContent (juce::Rectangle<int> area)
{
    auto scope = area.removeFromTop (curveHeight);
    curve.setBounds (scope);

    // The cuts ride in the scope's top corners, where they label the ends of the curve they shape.
    auto pills = scope.reduced (pillInset).removeFromTop (pillHeight);
    hpf.setBounds (pills.removeFromLeft (pillWidth));
    lpf.setBounds (pills.removeFromRight (pillWidth));

    area.removeFromTop (curveGap);

    const int side  = juce::jmin (area.getHeight(), (area.getWidth() - 2 * knobGap) / 3);
    const int total = side * 3 + knobGap * 2;

    auto row = area.withSizeKeepingCentre (total, side);
    low.setBounds (row.removeFromLeft (side));
    row.removeFromLeft (knobGap);
    mid.setBounds (row.removeFromLeft (side));
    row.removeFromLeft (knobGap);
    high.setBounds (row.removeFromLeft (side));
}

} // namespace orbitamp
