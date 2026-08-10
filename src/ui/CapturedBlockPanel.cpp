#include "CapturedBlockPanel.h"

#include "../PluginProcessor.h"

namespace orbitamp
{

CapturedBlockPanel::CapturedBlockPanel (AmpProcessor& processor, Block& b,
                                        const juce::String& title, const char* blockId)
    : BlockFrame (title, BlockFrame::Kind::captured), amp (processor), block (b), blk (blockId),
      // The tone curve comes from the block itself — the same data its filters were designed from,
      // resolved on the processor's pump. The scope repaints on its own clock, so it picks a move
      // up within a frame or two.
      scope (b.scope, b.ribbon, [this] (double hz) { return block.toneDb (hz); })
{
    addAndMakeVisible (rawSwitch);
    addAndMakeVisible (rawLabel);
    addAndMakeVisible (device);
    addAndMakeVisible (gain);
    addAndMakeVisible (scope);
    addAndMakeVisible (scopeMode);

    // Five ways of showing the same device. Which one reads best is not settled, so the choice is
    // on the face rather than in the code.
    scopeMode.setItems ({ "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE" }, 0);
    scopeMode.onChange = [this] (int i) { scope.setMode ((DeviceScope::Mode) i); };
    scope.setSampleRate (amp.currentSampleRate());

    rawSwitch.accent = theme::violet;
    rawSwitch.attach (*amp.apvts.getParameter (params::rawId (blk)));

    rawLabel.setFont (theme::displayFont (7.0f));
    rawLabel.setColour (juce::Label::textColourId, theme::txFaint);
    rawLabel.setJustificationType (juce::Justification::centredRight);
    rawLabel.setInterceptsMouseClicks (false, false);

    attachPower (*amp.apvts.getParameter (params::blockOn (blk)));

    // The attachment hears everyone BUT this panel — a restored session, a host automating the
    // parameter. Load the device it names before rebuilding the face, or the face is rebuilt from
    // the pack that is leaving; loading twice costs nothing because selectIfMoved is a no-op when
    // the pump already did it.
    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockDevice (blk)),
        [this] (float v)
        {
            const int i = juce::roundToInt (v);
            device.setSelection (i);
            block.selectIfMoved (i);
            deviceChanged();
        });

    device.onPick = [this] (int i)
    {
        deviceAttachment->setValueAsCompleteGesture ((float) i);
        block.select (i);   // message thread — it reads files
        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::blockGain (blk), gain);

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

CapturedBlockPanel::~CapturedBlockPanel() = default;

void CapturedBlockPanel::deviceChanged()
{
    scope.setSampleRate (amp.currentSampleRate());

    const auto* measured = block.measured();
    const auto  positions = block.gainPositions();

    // The list IS the combo: what a player has, greenest first. Nothing invented, nothing curated
    // into groups — the character ramp does the ordering a "type" heading used to.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : block.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;   // the rule between shipped and added
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    device.setEntries (std::move (entries));

    caption = block.deviceName();
    scope.setSpec (felitronics::appkit::parseDeviceSpec (block.circuit()));

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next
    // pack says for the next one. Detented where the pack has positions, continuous where it does
    // not — but always THERE: a knob with nothing to select drives instead, and a device without a
    // gain knob is not a device.
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
                *amp.apvts.getParameter (params::blockMeasured (blk, i)),
                [this, i] (float v)
                {
                    if (auto* s = slots[(size_t) i].steps.get())
                        s->setSelectedIndex (v > 0.5f ? 1 : 0, juce::dontSendNotification);
                });

            slot.steps->onChange = [this, i] (int v)
            {
                slots[(size_t) i].stepAtt->setValueAsCompleteGesture (v > 0 ? 1.0f : 0.0f);
            };

            slot.stepAtt->sendInitialUpdate();

            // A switch has two positions and the parameter defaults to the middle of its range, which
            // for a knob is sensible and for a switch is a place the hardware cannot be. It played as
            // half of Smooth while the face lit Sharp, and clicking the lit half writes nothing — so
            // it could sit there forever. Land it on a real position as the device loads.
            // The SAME rule the switch lights by, below — off by half a step and the parameter says
            // Smooth while the face says Sharp, which is exactly the inversion this introduced.
            if (const float v = amp.apvts.getRawParameterValue (params::blockMeasured (blk, i))->load();
                v > 0.0f && v < 1.0f)
                slot.stepAtt->setValueAsCompleteGesture (v > 0.5f ? 1.0f : 0.0f);
        }
        else
        {
            slot.knob = std::make_unique<Knob> (name, theme::orange, (int) m.positions.size());
            slot.knob->textForValue = [] (double) { return juce::String(); };   // no numbers here
            addAndMakeVisible (*slot.knob);

            slot.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                amp.apvts, params::blockMeasured (blk, i), *slot.knob);
        }
    }

    buildSelectors();

    resized();
    repaint();
}

void CapturedBlockPanel::buildSelectors()
{
    const auto list = block.stage.selectors();

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
            *amp.apvts.getParameter (params::selectorId (blk, i)),
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

bool CapturedBlockPanel::hasNamedPositions (const namz::rig::Measured& m)
{
    for (const auto& p : m.positions)
    {
        const auto s = juce::String (p.label.empty() ? p.value : p.label).trim();

        if (s.isNotEmpty() && ! s.containsOnly ("0123456789.+-"))
            return true;
    }

    return false;
}

void CapturedBlockPanel::layOutHeader (juce::Rectangle<int> area)
{
    device.setBounds (area);
}

void CapturedBlockPanel::layOutContent (juce::Rectangle<int> area)
{
    // In a column the picture gets its fixed strip; zoomed across the whole panel it gets a third
    // of the height instead — the zoom exists to look at things, not to inflate knobs.
    scope.setBounds (area.removeFromBottom (juce::jmax (curveHeight, area.getHeight() / 3)));
    area.removeFromBottom (gap / 2);
    scopeMode.setBounds (area.removeFromBottom (modeRow).withSizeKeepingCentre (
        juce::jmin (area.getWidth(), maxRowWidth), modeRow));
    area.removeFromBottom (gap);

    // Switches sit under the knobs rather than beside them: neither kind is a third amount. The
    // device's own selecting controls go lowest — turning one loads a different capture, which is a
    // bigger move than shaping the one you have.
    const auto switchBounds = [&area]
    {
        auto row = area.removeFromBottom (switchRow);
        return row.withSizeKeepingCentre (juce::jmin (row.getWidth(), maxRowWidth), switchRow);
    };

    for (auto& sel : selectors)
        if (sel.steps != nullptr)
        {
            sel.steps->setBounds (switchBounds());
            area.removeFromBottom (gap / 2);
        }

    for (auto& slot : slots)
        if (slot.steps != nullptr)
        {
            slot.steps->setBounds (switchBounds());
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
    // to the picture instead of to a gap. Both are capped: past a size a knob stops being easier to
    // grab and starts being a balloon.
    int smallCount = 0;
    for (const auto& slot : slots)
        if (slot.knob != nullptr)
            ++smallCount;

    const int side = juce::jmin (maxGainSide, juce::jmin (area.getHeight(),
                                                          area.getWidth() * (smallCount > 0 ? 1 : 2) / 2));
    auto left = area.removeFromLeft (juce::jmin (side, area.getWidth()));
    const int gainSide = juce::jmin (maxGainSide, juce::jmin (left.getWidth(), left.getHeight()));
    gain.setBounds (left.withSizeKeepingCentre (gainSide, gainSide));

    if (smallCount > 0)
        area.removeFromLeft (knobGap);

    if (smallCount == 0)
        return;
    const int small = juce::jmin (maxKnobSide,
                                  juce::jmin (area.getHeight(),
                                              (area.getWidth() - (smallCount - 1) * knobGap) / smallCount));

    auto row = area.withSizeKeepingCentre (small * smallCount + knobGap * (smallCount - 1), small);
    for (auto& slot : slots)
        if (slot.knob != nullptr)
        {
            slot.knob->setBounds (row.removeFromLeft (small));
            row.removeFromLeft (knobGap);
        }
}

void CapturedBlockPanel::paintContent (juce::Graphics& g)
{
    if (caption.isEmpty())
    {
        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No device loaded", contentArea().toFloat(), theme::displayFont (8.0f),
                            0.1f, juce::Justification::centred);
    }
}

} // namespace orbitamp
