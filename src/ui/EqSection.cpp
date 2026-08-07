#include "EqSection.h"

#include "../Parameters.h"

namespace orbitamp
{

EqSection::EqSection (juce::AudioProcessorValueTreeState& s)
    : state (s)
{
    display.prepare (displayRate, 1);

    for (auto* k : { &low, &mid, &high, &presence })
    {
        // Tone knobs read in dB, signed — the sign is the whole information here.
        k->textForValue = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };
        k->onValueChange = [this] { refreshCurve(); };
    }

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
    powerAtt = attach (params::eqOn,    [this] (float v) { on = v > 0.5f; });
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

void EqSection::addTo (juce::Component& parent)
{
    parent.addAndMakeVisible (curve);
    parent.addAndMakeVisible (hpf);
    parent.addAndMakeVisible (lpf);

    for (auto* k : { &low, &mid, &high, &presence })
        parent.addAndMakeVisible (*k);
}

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

void EqSection::layOutCurve (juce::Rectangle<int> area)
{
    curve.setBounds (area);

    auto pills = area.reduced (pillInset).removeFromTop (pillHeight);
    hpf.setBounds (pills.removeFromLeft (pillWidth));
    lpf.setBounds (pills.removeFromRight (pillWidth));
}

void EqSection::layOutKnobs (juce::Rectangle<int> area)
{
    // Presence over the trio, not beside it: it is a second high shelf laid on top of the tone
    // controls, and a row of four would call it their equal.
    const int side = juce::jmin ((area.getHeight() - presenceGap) * 5 / 9,
                                 (area.getWidth() - 2 * knobGap) / 3);

    auto top = area.removeFromTop (juce::roundToInt (side * 0.8f));
    presence.setBounds (top.withSizeKeepingCentre (juce::roundToInt (side * 0.8f),
                                                   juce::roundToInt (side * 0.8f)));
    area.removeFromTop (presenceGap);

    const int total = side * 3 + knobGap * 2;
    auto row = area.withSizeKeepingCentre (total, side);

    for (auto* k : { &low, &mid, &high })
    {
        k->setBounds (row.removeFromLeft (side));
        row.removeFromLeft (knobGap);
    }
}


} // namespace orbitamp
