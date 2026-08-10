#include "BoostBlock.h"

#include "../PluginProcessor.h"

#include <felitronics/lineareq/MagnitudeCurve.h>

namespace orbitamp
{

BoostBlock::BoostBlock (AmpProcessor& processor)
    : BlockFrame ("Boost", BlockFrame::Kind::captured), amp (processor),
      scope (processor.boost.scope, processor.boost.ribbon,
             [this] (double hz) { return toneDb (hz); })
{
    addAndMakeVisible (rawSwitch);
    addAndMakeVisible (rawLabel);
    addAndMakeVisible (pedal);
    addAndMakeVisible (gain);
    addAndMakeVisible (scope);
    addAndMakeVisible (scopeMode);

    // Four ways of showing the same pedal. Which one reads best is not settled, so the choice is on
    // the face rather than in the code.
    scopeMode.setItems ({ "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE" }, 0);
    scopeMode.onChange = [this] (int i) { scope.setMode ((DeviceScope::Mode) i); };
    scope.setSampleRate (amp.currentSampleRate());

    rawSwitch.accent = theme::violet;
    rawSwitch.attach (*amp.apvts.getParameter (params::rawId (params::boostId)));

    rawLabel.setFont (theme::displayFont (7.0f));
    rawLabel.setColour (juce::Label::textColourId, theme::txFaint);
    rawLabel.setJustificationType (juce::Justification::centredRight);
    rawLabel.setInterceptsMouseClicks (false, false);

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
    scope.setSampleRate (amp.currentSampleRate());

    const auto* measured = amp.boost.measured();
    const auto  positions = amp.boost.gainPositions();

    // The list IS the combo: what a player has, greenest first. Nothing invented, nothing curated
    // into groups — the character ramp does the ordering a "type" heading used to.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : amp.boost.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
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
    // Detented where the pack has positions, continuous where it does not — but always THERE. A
    // preamp without a gain knob is not a preamp, and a knob that drives is not a knob doing nothing.
    gain.setNotches (juce::jmax (0, positions.size()));

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& slot = slots[(size_t) i];

        // ORDER MATTERS, and `slot = Slot {}` had it backwards. Move-assignment walks the members in
        // declaration order, so the knob was destroyed first and its attachment — which reaches for
        // the slider in its own destructor — went next, into freed memory. Changing a device crashed
        // the plugin, and it only showed once a second pack made the combo worth using.
        slot.knobAtt.reset();
        slot.stepAtt.reset();
        slot.knob.reset();
        slot.steps.reset();
        slot.measuredIndex = -1;

        if (measured == nullptr || i >= (int) measured->size())
            continue;

        const auto& m = (*measured)[(size_t) i];
        slot.measuredIndex = i;

        const auto name = juce::String (m.name).toUpperCase();

        // A switch is a control whose positions have NAMES. Not one with two of them: a measured
        // control's positions are the points it was measured AT, and the player interpolates between
        // them — two is a perfectly ordinary number for a knob that was swept at each end. Fur Coat's
        // EQ says "0" and "300", which are degrees, and counting them turned a tone knob into a
        // two-position switch stuck at one end.
        if (m.positions.size() == 2 && hasNamedPositions (m))
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
                refreshCurve();
            };

            slot.stepAtt->sendInitialUpdate();

            // A switch has two positions and the parameter defaults to the middle of its range, which
            // for a knob is sensible and for a switch is a place the hardware cannot be. It played as
            // half of Smooth while the face lit Sharp, and clicking the lit half writes nothing — so
            // it could sit there forever. Land it on a real position as the device loads.
            // The SAME rule the switch lights by, below — off by half a step and the parameter says
            // Smooth while the face says Sharp, which is exactly the inversion this introduced.
            if (const float v = amp.apvts.getRawParameterValue (params::boostMeasured (i))->load();
                v > 0.0f && v < 1.0f)
                slot.stepAtt->setValueAsCompleteGesture (v > 0.5f ? 1.0f : 0.0f);
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

    buildSelectors();

    resized();
    handleAsyncUpdate();   // the face is being built; the curve has to be right by the first paint
    repaint();
}

void BoostBlock::buildSelectors()
{
    const auto list = amp.boost.stage.selectors();

    for (int i = 0; i < params::numSelectors; ++i)
    {
        auto& sel = selectors[(size_t) i];

        sel.attachment.reset();
        sel.steps.reset();

        if (i >= list.size())
            continue;

        const auto& def = list.getReference (i);

        juce::StringArray labels;
        for (const auto& v : def.values)
            labels.add (v.toUpperCase());

        sel.steps = std::make_unique<StepSwitch>();
        sel.steps->accent = theme::orange;
        sel.steps->setItems (labels, 0);
        addAndMakeVisible (*sel.steps);

        sel.attachment = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (params::selectorId (params::boostId, i)),
            [this, i, n = labels.size()] (float v)
            {
                if (auto* s = selectors[(size_t) i].steps.get())
                    s->setSelectedIndex (juce::jlimit (0, n - 1, juce::roundToInt (v)),
                                         juce::dontSendNotification);
            });

        sel.steps->onChange = [this, i] (int v)
        {
            selectors[(size_t) i].attachment->setValueAsCompleteGesture ((float) v);
        };

        sel.attachment->sendInitialUpdate();
    }
}

bool BoostBlock::hasNamedPositions (const namz::rig::Measured& m)
{
    for (const auto& p : m.positions)
    {
        const auto s = juce::String (p.label.empty() ? p.value : p.label).trim();

        if (s.isNotEmpty() && ! s.containsOnly ("0123456789.+-"))
            return true;
    }

    return false;
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

        // The same band decision the sound makes, from the same function — a picture that drew what
        // the audio does not play would be a promise the sound never keeps.
        const auto band = core::MeasuredFilter::bandFor (m);
        c.db = felitronics::lineareq::heldOutsideBand (
            felitronics::lineareq::curveAtPosition (pos, norms, t), c.hz, band.lo, band.hi);
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
    triggerAsyncUpdate();
}

void BoostBlock::handleAsyncUpdate()
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

    // Switches sit under the knobs rather than beside them: neither kind is a third amount. The
    // device's own selecting controls go lowest — turning one loads a different capture, which is a
    // bigger move than shaping the one you have.
    for (auto& sel : selectors)
        if (sel.steps != nullptr)
        {
            sel.steps->setBounds (area.removeFromBottom (switchRow));
            area.removeFromBottom (gap / 2);
        }

    for (auto& slot : slots)
        if (slot.steps != nullptr)
        {
            slot.steps->setBounds (area.removeFromBottom (switchRow));
            area.removeFromBottom (gap / 2);
        }

    // TEMPORARY, top right: the raw switch and its label.
    {
        auto row = area.removeFromTop (ZoneSwitch::designHeight);
        rawSwitch.setBounds (row.removeFromRight (ZoneSwitch::designWidth));
        row.removeFromRight (4);
        rawLabel.setBounds (row.removeFromRight (28));
        area.removeFromTop (gap / 2);
    }

    // The hero on the left, the measured knobs to its right. A device with neither leaves the space
    // to the picture instead of to a gap.
    int smallCount = 0;
    for (const auto& slot : slots)
        if (slot.knob != nullptr)
            ++smallCount;

    const int side = juce::jmin (area.getHeight(), area.getWidth() * (smallCount > 0 ? 1 : 2) / 2);
    auto left = area.removeFromLeft (juce::jmin (side, area.getWidth()));
    gain.setBounds (left.withSizeKeepingCentre (juce::jmin (left.getWidth(), left.getHeight()),
                                                juce::jmin (left.getWidth(), left.getHeight())));

    if (smallCount > 0)
        area.removeFromLeft (knobGap);

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
