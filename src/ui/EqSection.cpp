#include "EqSection.h"

#include "../Parameters.h"

namespace orbitamp
{

EqSection::EqSection (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    display.prepare (displayRate, 1);

    addAndMakeVisible (curve);
    for (auto* k : { &low, &mid, &high, &presence })
    {
        // Tone knobs read in dB, signed — the sign is the whole information here.
        k->textForValue = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };
        k->onValueChange = [this] { refreshCurve(); };
        addChildComponent (*k);          // hidden until the section is opened
    }

    addAndMakeVisible (hpf);
    addAndMakeVisible (lpf);

    lowAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqLow,      low);
    midAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqMid,      mid);
    highAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqHigh,     high);
    presAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqPresence, presence);

    auto attach = [this] (const char* id, auto&& onChanged)
    {
        return std::make_unique<juce::ParameterAttachment> (*state.getParameter (id),
                                                            std::forward<decltype (onChanged)> (onChanged));
    };

    hpfOnAtt = attach (params::eqHpfOn, [this] (float v) { hpf.setState (v > 0.5f, hpf.getHz()); refreshCurve(); });
    hpfHzAtt = attach (params::eqHpfHz, [this] (float v) { hpf.setState (hpf.isOn(), (double) v); refreshCurve(); });
    lpfOnAtt = attach (params::eqLpfOn, [this] (float v) { lpf.setState (v > 0.5f, lpf.getHz()); refreshCurve(); });
    lpfHzAtt = attach (params::eqLpfHz, [this] (float v) { lpf.setState (lpf.isOn(), (double) v); refreshCurve(); });
    powerAtt = attach (params::eqOn,    [this] (float v) { on = v > 0.5f; repaint(); });

    hpf.onToggled     = [this] (bool b)   { hpfOnAtt->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
    lpf.onToggled     = [this] (bool b)   { lpfOnAtt->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
    hpf.onFreqChanged = [this] (double f) { hpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };
    lpf.onFreqChanged = [this] (double f) { lpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };

    // A sweep is ONE undoable move in the host, not one per pixel.
    hpf.onDragActive = [this] (bool a) { a ? hpfHzAtt->beginGesture() : hpfHzAtt->endGesture(); };
    lpf.onDragActive = [this] (bool a) { a ? lpfHzAtt->beginGesture() : lpfHzAtt->endGesture(); };

    for (auto* a : { &hpfOnAtt, &hpfHzAtt, &lpfOnAtt, &lpfHzAtt, &powerAtt })
        (*a)->sendInitialUpdate();

    refreshCurve();
}

EqSection::~EqSection() = default;

void EqSection::refreshCurve()
{
    core::ToneStack::Settings s;
    s.lowDb      = low.getValue();
    s.midDb      = mid.getValue();
    s.highDb     = high.getValue();
    s.presenceDb = presence.getValue();
    s.hpfOn      = hpf.isOn();
    s.hpfHz      = hpf.getHz();
    s.lpfOn      = lpf.isOn();
    s.lpfHz      = lpf.getHz();

    display.setSettings (s);
    curve.repaint();
}

void EqSection::setExpanded (bool shouldExpand)
{
    if (expanded == shouldExpand)
        return;

    expanded = shouldExpand;

    for (auto* k : { &low, &mid, &high, &presence })
        k->setVisible (expanded);

    resized();
    repaint();
}

juce::Rectangle<int> EqSection::gripArea() const
{
    auto r = getLocalBounds();
    return expanded ? r.removeFromBottom (knobRow).removeFromTop (gripHeight)
                    : r.removeFromBottom (gripHeight);
}

void EqSection::resized()
{
    auto r = getLocalBounds();

    // The knob row is carved out of the curve, never out of the section — opening must not move the
    // block around it.
    auto knobs = expanded ? r.removeFromBottom (knobRow) : juce::Rectangle<int>();
    if (expanded)
        knobs.removeFromTop (gripHeight);   // the grip sits above the knobs once open
    else
        r.removeFromBottom (gripHeight);

    curve.setBounds (r);

    auto pills = r.reduced (pillInset).removeFromTop (pillHeight);
    hpf.setBounds (pills.removeFromLeft (pillWidth));
    lpf.setBounds (pills.removeFromRight (pillWidth));

    if (! expanded)
        return;

    const int side  = juce::jmin (knobs.getHeight(), (knobs.getWidth() - 3 * knobGap) / 4);
    const int total = side * 4 + knobGap * 3;

    auto row = knobs.withSizeKeepingCentre (total, side);
    for (auto* k : { &low, &mid, &high, &presence })
    {
        k->setBounds (row.removeFromLeft (side));
        row.removeFromLeft (knobGap);
    }
}

void EqSection::paint (juce::Graphics& g)
{
    auto grip = gripArea().toFloat();

    // The grip says "there is more here" by looking like something you can pull: a short bar of
    // dashes, brighter under the pointer, and it flips its meaning when the section is open.
    const float w = 26.0f, y = grip.getCentreY();
    const float x0 = grip.getCentreX() - w * 0.5f;

    g.setColour ((gripHover ? theme::lilac : theme::txFaint).withAlpha (on ? 1.0f : 0.5f));
    for (int i = 0; i < 3; ++i)
    {
        const float x = x0 + (float) i * (w / 3.0f);
        g.fillRect (x, y - 0.5f, w / 3.0f - 4.0f, 1.0f);
    }

    // A caret on the right end of the grip, pointing the way the curve will move.
    juce::Path caret;
    const float cx = grip.getRight() - 12.0f, ch = 3.0f;
    caret.startNewSubPath (cx - 4.0f, expanded ? y + ch : y - ch);
    caret.lineTo          (cx,        expanded ? y - ch : y + ch);
    caret.lineTo          (cx + 4.0f, expanded ? y + ch : y - ch);
    g.strokePath (caret, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EqSection::mouseDown (const juce::MouseEvent& e)
{
    if (gripArea().contains (e.getPosition()))
        setExpanded (! expanded);
}

void EqSection::mouseMove (const juce::MouseEvent& e)
{
    const bool over = gripArea().contains (e.getPosition());
    if (over != gripHover)
    {
        gripHover = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqSection::mouseExit (const juce::MouseEvent&)
{
    if (gripHover)
    {
        gripHover = false;
        repaint();
    }
}

} // namespace orbitamp
