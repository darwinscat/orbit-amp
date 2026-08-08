#include "BoostBlock.h"

#include "../PluginProcessor.h"

#include <felitronics/lineareq/MagnitudeCurve.h>

namespace orbitamp
{

BoostBlock::BoostBlock (AmpProcessor& processor)
    : BlockFrame ("Boost", BlockFrame::Kind::captured), amp (processor),
      scope (processor.boostScope, [this] (double hz) { return toneDb (hz); })
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

    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::boostDevice),
        [this] (float v) { pedal.setSelection (juce::roundToInt (v)); });

    pedal.onPick = [this] (int i)
    {
        deviceAttachment->setValueAsCompleteGesture ((float) i);
        amp.selectBoostDevice (i);
        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::boostGain, gain);

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

BoostBlock::~BoostBlock() = default;

void BoostBlock::deviceChanged()
{
    const auto* measured = amp.boost.measured();
    const auto  positions = amp.boost.gainPositions();

    // The list IS the combo: what a player has, greenest first. Nothing invented, nothing curated
    // into groups — the character ramp does the ordering a "type" heading used to.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : amp.devicePacks)
    {
        VoicingSelector::Entry e;
        e.name = pack.name;
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;   // the rule between shipped and added
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    pedal.setEntries (std::move (entries));

    caption = amp.boost.deviceName();
    scope.setSpec (felitronics::appkit::parseDeviceSpec (amp.boost.circuit()));

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next pack
    // says for the next one — and none at all for a lone model, which has no gain axis and so gets no
    // knob rather than a dead one.
    gain.setNotches (juce::jmax (0, positions.size()));
    gain.setVisible (! positions.isEmpty());

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

    resized();
    refreshCurve();
    repaint();
}

void BoostBlock::rebuildCurves()
{
    const auto* measured = amp.boost.measured();

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& c = curves[(size_t) i];
        c = Curve {};

        if (measured == nullptr || i >= (int) measured->size())
            continue;

        const auto& m = (*measured)[(size_t) i];
        if (m.positions.size() < 2 || m.grid.points < 2)
            continue;

        std::vector<std::vector<double>> pos;
        std::vector<double> norms;
        for (const auto& p : m.positions)
        {
            pos.push_back (p.db);
            norms.push_back (p.norm);
        }

        double t = 0.5;
        if (const auto* v = amp.apvts.getRawParameterValue (params::boostMeasured (i)))
            t = juce::jlimit (0.0, 1.0, (double) v->load());

        c.hz = felitronics::lineareq::logFreqGrid (m.grid.fLo, m.grid.fHi, m.grid.points);

        // The same band decision the sound makes — see core::MeasuredFilter. Drawing the raw curve
        // where the audio holds it flat would make the picture a promise the sound does not keep.
        const bool tested = m.trusted.levels >= 2;
        c.db = felitronics::lineareq::heldOutsideBand (
            felitronics::lineareq::curveAtPosition (pos, norms, t), c.hz,
            tested ? m.trusted.loHz : 0.0, tested ? m.trusted.hiHz : 0.0);
    }
}

double BoostBlock::toneDb (double freqHz) const
{
    // Every measured control at once. They sit in series in the pedal — SM7's two EQ knobs and its
    // Sharp/Smooth switch all act on the same signal — so the picture is their sum, not whichever one
    // happens to be first. Showing one was showing a third of the tone.
    double sum = 0.0;
    for (const auto& c : curves)
        sum += felitronics::lineareq::curveDbAt (c.db, c.hz, freqHz);

    return sum;
}

void BoostBlock::refreshCurve()
{
    rebuildCurves();
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

    // A switch sits under the knobs rather than beside them: it is not a third amount.
    for (auto& slot : slots)
        if (slot.steps != nullptr)
        {
            slot.steps->setBounds (area.removeFromBottom (switchRow));
            area.removeFromBottom (gap / 2);
        }

    // The hero on the left, the measured knobs to its right. A device with neither leaves the space
    // to the picture instead of to a gap.
    int smallCount = 0;
    for (const auto& slot : slots)
        if (slot.knob != nullptr)
            ++smallCount;

    if (gain.isVisible())
    {
        const int side = juce::jmin (area.getHeight(), area.getWidth() * (smallCount > 0 ? 1 : 2) / 2);
        auto left = area.removeFromLeft (juce::jmin (side, area.getWidth()));
        gain.setBounds (left.withSizeKeepingCentre (juce::jmin (left.getWidth(), left.getHeight()),
                                                    juce::jmin (left.getWidth(), left.getHeight())));

        if (smallCount > 0)
            area.removeFromLeft (knobGap);
    }

    if (smallCount == 0)
        return;
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
    if (caption.isEmpty())
    {
        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No device loaded", contentArea().toFloat(), theme::displayFont (8.0f),
                            0.1f, juce::Justification::centred);
    }
}

} // namespace orbitamp
