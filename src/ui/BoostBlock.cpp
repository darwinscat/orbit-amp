#include "BoostBlock.h"

#include "../PluginProcessor.h"
#include "../device/VoicingLibrary.h"

#include <cmath>

namespace orbitamp
{

BoostBlock::BoostBlock (AmpProcessor& processor)
    : BlockFrame ("Boost", BlockFrame::Kind::captured), amp (processor),
      scope (processor.boostScope, [this] (double hz) { return measuredDb (curveSlot, hz); })
{
    addAndMakeVisible (pedal);
    addAndMakeVisible (gain);
    addAndMakeVisible (scope);
    addAndMakeVisible (scopeMode);

    // Four ways of showing the same pedal. Which one reads best is not settled, so the choice is on
    // the face rather than in the code.
    scopeMode.setItems ({ "SHAPE", "ENVELOPE", "TRANSFER", "TONE" }, 0);
    scopeMode.onChange = [this] (int i) { scope.setMode ((BoostScope::Mode) i); };

    attachPower (*amp.apvts.getParameter (params::boostOn));

    juce::Array<VoicingSelector::Group> groups;
    for (int t = 0; t < params::typeNames.size(); ++t)
        groups.add ({ params::typeNames[t], device::VoicingLibrary::pedalsFor (t) });

    pedal.setGroups (std::move (groups));

    typeAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::boostType),
        [this] (float v) { pedal.setSelection (juce::roundToInt (v), pedal.getItemIndex()); });

    voiceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::boostVoice),
        [this] (float v) { pedal.setSelection (pedal.getGroupIndex(), juce::roundToInt (v)); });

    pedal.onPick = [this] (int t, int v)
    {
        typeAttachment->setValueAsCompleteGesture ((float) t);
        voiceAttachment->setValueAsCompleteGesture ((float) v);
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::boostGain, gain);

    typeAttachment->sendInitialUpdate();
    voiceAttachment->sendInitialUpdate();

    deviceChanged();
}

BoostBlock::~BoostBlock() = default;

void BoostBlock::deviceChanged()
{
    const auto* measured = amp.boost.measured();
    const auto  positions = amp.boost.gainPositions();

    caption = amp.boost.deviceName();
    circuit = felitronics::appkit::parseDeviceSpec (amp.boost.circuit());

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next pack
    // says for the next one.
    gain.setNotches (juce::jmax (0, positions.size()));

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& slot = slots[(size_t) i];
        slot = Slot {};

        if (measured == nullptr || i >= (int) measured->size())
            continue;

        const auto& m = (*measured)[(size_t) i];
        slot.measuredIndex = i;

        const auto name = juce::String (m.name).toUpperCase();

        // Two positions with names rather than degrees is a switch, not a knob. The pack says which
        // by what it ships, so nothing here has to know that SM7's third control is called Edge.
        if (m.positions.size() == 2)
        {
            juce::StringArray labels;
            for (const auto& p : m.positions)
                labels.add (juce::String (p.label.empty() ? p.value : p.label).toUpperCase());

            slot.steps = std::make_unique<StepSwitch>();
            slot.steps->accent = theme::orange;
            slot.steps->setItems (labels, 0);
            addAndMakeVisible (*slot.steps);

            slot.stepAtt = std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (params::boostMeasured (i)),
                [this, i] (float v)
                {
                    if (auto* s = slots[(size_t) i].steps.get())
                        s->setSelectedIndex (v > 0.5f ? 1 : 0, juce::dontSendNotification);
                    refreshCurve();
                });

            slot.steps->onChange = [this, i] (int v)
            {
                slots[(size_t) i].stepAtt->setValueAsCompleteGesture (v > 0 ? 1.0f : 0.0f);
            };

            slot.stepAtt->sendInitialUpdate();
        }
        else
        {
            slot.knob = std::make_unique<Knob> (name, theme::orange, (int) m.positions.size());
            slot.knob->textForValue = [] (double) { return juce::String(); };   // no numbers here
            slot.knob->onValueChange = [this] { refreshCurve(); };
            addAndMakeVisible (*slot.knob);

            slot.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                amp.apvts, params::boostMeasured (i), *slot.knob);
        }
    }

    // Show the first sweeping control's curve; a switch has one too, but a knob is the thing you are
    // most likely to be moving.
    curveSlot = 0;
    for (int i = 0; i < params::boostNumMeasured; ++i)
        if (slots[(size_t) i].knob != nullptr) { curveSlot = i; break; }

    resized();
    refreshCurve();
    repaint();
}

double BoostBlock::measuredDb (int slot, double freqHz) const
{
    const auto* measured = amp.boost.measured();
    if (measured == nullptr || ! juce::isPositiveAndBelow (slot, (int) measured->size()))
        return 0.0;

    const auto& m = (*measured)[(size_t) slot];
    if (m.positions.size() < 2 || m.grid.points < 2)
        return 0.0;

    // Where the knob is, 0..1 across the sweep. Positions arrive ascending by norm — the pack sorts
    // them, so this can index and lerp without searching or defending.
    double t = 0.5;
    if (const auto* p = amp.apvts.getRawParameterValue (params::boostMeasured (slot)))
        t = juce::jlimit (0.0, 1.0, (double) p->load());

    const auto& pos = m.positions;
    size_t hi = 1;
    while (hi + 1 < pos.size() && pos[hi].norm < t)
        ++hi;

    const auto& a = pos[hi - 1];
    const auto& b = pos[hi];
    const double span = juce::jmax (1.0e-9, b.norm - a.norm);
    const double mix  = juce::jlimit (0.0, 1.0, (t - a.norm) / span);

    // The grid is logarithmic between fLo and fHi; find the point this frequency lands on.
    const double lo = m.grid.fLo, hiHz = m.grid.fHi;
    const double u = std::log (juce::jlimit (lo, hiHz, freqHz) / lo) / std::log (hiHz / lo);
    const int idx = juce::jlimit (0, m.grid.points - 1, (int) std::lround (u * (m.grid.points - 1)));

    if (idx >= (int) a.db.size() || idx >= (int) b.db.size())
        return 0.0;

    return a.db[(size_t) idx] * (1.0 - mix) + b.db[(size_t) idx] * mix;
}

void BoostBlock::refreshCurve()
{
    scope.repaint();
}

void BoostBlock::layOutHeader (juce::Rectangle<int> area)
{
    pedal.setBounds (area);
}

void BoostBlock::layOutContent (juce::Rectangle<int> area)
{
    scope.setBounds (area.removeFromBottom (curveHeight));
    area.removeFromBottom (gap / 2);
    scopeMode.setBounds (area.removeFromBottom (modeRow));
    area.removeFromBottom (gap);

    if (! circuit.empty())
        area.removeFromTop (glyphRow + gap / 2);   // the glyph row is painted, not a component

    // A switch sits under the knobs rather than beside them: it is not a third amount.
    for (auto& slot : slots)
        if (slot.steps != nullptr)
        {
            slot.steps->setBounds (area.removeFromBottom (switchRow));
            area.removeFromBottom (gap / 2);
        }

    // The hero on the left, the measured knobs to its right.
    int smallCount = 0;
    for (const auto& slot : slots)
        if (slot.knob != nullptr)
            ++smallCount;

    const int side = juce::jmin (area.getHeight(), area.getWidth() * (smallCount > 0 ? 1 : 2) / 2);
    auto left = area.removeFromLeft (juce::jmin (side, area.getWidth()));
    gain.setBounds (left.withSizeKeepingCentre (juce::jmin (left.getWidth(), left.getHeight()),
                                                juce::jmin (left.getWidth(), left.getHeight())));

    if (smallCount == 0)
        return;

    area.removeFromLeft (knobGap);
    const int small = juce::jmin (area.getHeight(), (area.getWidth() - (smallCount - 1) * knobGap) / smallCount);

    auto row = area.withSizeKeepingCentre (small * smallCount + knobGap * (smallCount - 1), small);
    for (auto& slot : slots)
        if (slot.knob != nullptr)
        {
            slot.knob->setBounds (row.removeFromLeft (small));
            row.removeFromLeft (knobGap);
        }
}

void BoostBlock::paintContent (juce::Graphics& g)
{
    auto area = contentArea();

    if (circuit.empty())
    {
        if (caption.isEmpty())
        {
            g.setColour (theme::txFaint.withAlpha (0.5f));
            theme::drawTracked (g, "No device loaded", area.toFloat(), theme::displayFont (8.0f), 0.1f,
                                juce::Justification::centred);
        }
        return;
    }

    // What is in the signal path, as the parts themselves. The pack ships the string; appkit draws it.
    auto row = area.removeFromTop (glyphRow).toFloat();
    felitronics::appkit::drawDeviceSpecStatic (g, row.removeFromLeft (row.getHeight() * 5.0f), circuit);

    g.setColour (theme::txFaint);
    theme::drawTracked (g, caption.toUpperCase(), row.withTrimmedLeft (6.0f), theme::displayFont (7.5f),
                        0.08f, juce::Justification::centredLeft);
}

} // namespace orbitamp
