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
    midHzAtt = attach (params::eqMidHz, [this] (float v) { midHz = (double) v; refreshCurve(); });

    curve.onHandleDrag = [this] (int i, double hz, double db) { handleDragged (i, hz, db); };
    curve.onDragActive = [this] (int i, bool a)               { handleDragActive (i, a); };

    hpf.onToggled     = [this] (bool b)   { hpfOnAtt->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
    lpf.onToggled     = [this] (bool b)   { lpfOnAtt->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
    hpf.onFreqChanged = [this] (double f) { hpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };
    lpf.onFreqChanged = [this] (double f) { lpfHzAtt->setValueAsPartOfGesture ((float) f); refreshCurve(); };

    // A sweep is ONE undoable move in the host, not one per pixel.
    hpf.onDragActive = [this] (bool a) { a ? hpfHzAtt->beginGesture() : hpfHzAtt->endGesture(); };
    lpf.onDragActive = [this] (bool a) { a ? lpfHzAtt->beginGesture() : lpfHzAtt->endGesture(); };

    for (auto* a : { &hpfOnAtt, &hpfHzAtt, &lpfOnAtt, &lpfHzAtt, &powerAtt, &midHzAtt })
        (*a)->sendInitialUpdate();

    refreshCurve();
}

EqSection::~EqSection() = default;

void EqSection::refreshCurve()
{
    core::ToneStack::Settings s;
    s.lowDb      = low.getValue();
    s.midDb      = mid.getValue();
    s.midHz      = midHz;
    s.highDb     = high.getValue();
    s.presenceDb = presence.getValue();
    s.hpfOn      = hpf.isOn();
    s.hpfHz      = hpf.getHz();
    s.lpfOn      = lpf.isOn();
    s.lpfHz      = lpf.getHz();

    display.setSettings (s);
    refreshHandles();
    curve.repaint();
    repaint();          // the mini knobs on the grip follow the values too
}

void EqSection::refreshHandles()
{
    using H = EqCurve::Handle;

    juce::Array<H> h;
    h.resize (numHandles);

    h.getReference (hLow)      = { core::ToneStack::lowHz,      low.getValue(),      H::Freedom::gain, true };
    h.getReference (hMid)      = { midHz,                       mid.getValue(),      H::Freedom::both, true };
    h.getReference (hHigh)     = { core::ToneStack::highHz,     high.getValue(),     H::Freedom::gain, true };
    h.getReference (hPresence) = { core::ToneStack::presenceHz, presence.getValue(), H::Freedom::gain, true };

    // The cuts only exist while they are switched on — a handle for a filter that is not in the
    // chain would be a control over nothing.
    h.getReference (hHpf) = { hpf.getHz(), 0.0, H::Freedom::freq, hpf.isOn() };
    h.getReference (hLpf) = { lpf.getHz(), 0.0, H::Freedom::freq, lpf.isOn() };

    curve.setHandles (std::move (h));
}

void EqSection::handleDragged (int index, double hz, double db)
{
    const auto clampDb = [] (double v) { return juce::jlimit (-15.0, 15.0, v); };

    switch (index)
    {
        case hLow:      low     .setValue (clampDb (db), juce::sendNotificationSync); break;
        case hHigh:     high    .setValue (clampDb (db), juce::sendNotificationSync); break;
        case hPresence: presence.setValue (clampDb (db), juce::sendNotificationSync); break;

        case hMid:
            mid.setValue (clampDb (db), juce::sendNotificationSync);
            midHzAtt->setValueAsPartOfGesture ((float) juce::jlimit (200.0, 2000.0, hz));
            midHz = juce::jlimit (200.0, 2000.0, hz);
            break;

        case hHpf: hpfHzAtt->setValueAsPartOfGesture ((float) juce::jlimit (20.0, 500.0, hz)); break;
        case hLpf: lpfHzAtt->setValueAsPartOfGesture ((float) juce::jlimit (2000.0, 20000.0, hz)); break;

        default: return;
    }

    refreshCurve();
}

void EqSection::handleDragActive (int index, bool active)
{
    // One drag is ONE undoable move in the host. The gain knobs are juce::Sliders, which bracket
    // their own gestures; only the frequencies need it done by hand.
    auto* att = index == hMid ? midHzAtt.get()
              : index == hHpf ? hpfHzAtt.get()
              : index == hLpf ? lpfHzAtt.get()
              : nullptr;

    if (att == nullptr)
        return;

    active ? att->beginGesture() : att->endGesture();
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
    const float y = grip.getCentreY();

    // Shut, the grip wears miniature copies of the knobs it hides — it says WHAT is behind it, not
    // merely that something is. Open, they would be duplicates of the real ones, so it goes back to
    // a plain pull-bar.
    if (! expanded)
    {
        paintMiniKnobs (g, grip);
    }
    else
    {
        const float w = 26.0f, x0 = grip.getCentreX() - w * 0.5f;
        g.setColour ((gripHover ? theme::lilac : theme::txFaint).withAlpha (on ? 1.0f : 0.5f));
        for (int i = 0; i < 3; ++i)
            g.fillRect (x0 + (float) i * (w / 3.0f), y - 0.5f, w / 3.0f - 4.0f, 1.0f);
    }

    g.setColour ((gripHover ? theme::lilac : theme::txFaint).withAlpha (on ? 1.0f : 0.5f));

    // A caret on the right end of the grip, pointing the way the curve will move.
    juce::Path caret;
    const float cx = grip.getRight() - 12.0f, ch = 3.0f;
    caret.startNewSubPath (cx - 4.0f, expanded ? y + ch : y - ch);
    caret.lineTo          (cx,        expanded ? y - ch : y + ch);
    caret.lineTo          (cx + 4.0f, expanded ? y + ch : y - ch);
    g.strokePath (caret, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EqSection::paintMiniKnobs (juce::Graphics& g, juce::Rectangle<float> grip) const
{
    // Non-const because juce::Slider::valueToProportionOfLength is not const; nothing here mutates.
    Knob* const knobs[] = { const_cast<Knob*> (&low),  const_cast<Knob*> (&mid),
                            const_cast<Knob*> (&high), const_cast<Knob*> (&presence) };
    constexpr int n = 4;

    const float rad  = juce::jmin (4.0f, grip.getHeight() * 0.42f);
    const float step = rad * 3.4f;
    const float y    = grip.getCentreY();
    float x = grip.getCentreX() - step * (n - 1) * 0.5f;

    const float alpha = on ? (gripHover ? 1.0f : 0.75f) : 0.4f;

    for (auto* k : knobs)
    {
        // 3/4 sweep, same geometry as the real knob — a dot whose pointer already sits where the
        // full-size one does.
        const double t = juce::jlimit (0.0, 1.0, k->valueToProportionOfLength (k->getValue()));
        const float  a = juce::MathConstants<float>::pi * (1.25f + 1.5f * (float) t);

        g.setColour (theme::hair2.withMultipliedAlpha (alpha));
        g.drawEllipse (x - rad, y - rad, rad * 2.0f, rad * 2.0f, 1.0f);

        g.setColour ((gripHover ? theme::lilac : theme::violet).withMultipliedAlpha (alpha));
        g.drawLine (x, y, x + std::sin (a) * rad * 0.9f, y - std::cos (a) * rad * 0.9f, 1.2f);

        x += step;
    }
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
