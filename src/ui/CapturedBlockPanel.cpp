#include "CapturedBlockPanel.h"

#include "../PluginProcessor.h"

namespace orbitamp
{

/** The whole monitor, one picture: a kiosk component of its own with a fresh DeviceScope inside,
    black to the edges. A click anywhere or Escape hands the screen back. */
class CapturedBlockPanel::ScopeTheater final : public juce::Component
{
public:
    ScopeTheater (Block& b, std::function<double (double)> toneDb, DeviceScope::Mode mode,
                  bool waveHalf, double sampleRate,
                  felitronics::analysis::RollingSpectrumTap* tap, int tapOrder,
                  std::function<void()> dismiss)
        : scope (b.scope, b.ribbon, std::move (toneDb)), onDismiss (std::move (dismiss))
    {
        scope.setMode (mode);
        scope.waveHalf = waveHalf;
        scope.setSampleRate (sampleRate);
        if (mode == DeviceScope::Mode::tone && tap != nullptr)
            scope.setSpectrumTap (tap, tapOrder);
        addAndMakeVisible (scope);
        setOpaque (true);
        setWantsKeyboardFocus (true);
    }

    void resized() override { scope.setBounds (getLocalBounds().reduced (24)); }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff060609)); }

    void mouseDown (const juce::MouseEvent&) override { if (onDismiss) onDismiss(); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && onDismiss)
        {
            onDismiss();
            return true;
        }
        return false;
    }

private:
    DeviceScope scope;
    std::function<void()> onDismiss;
};

CapturedBlockPanel::CapturedBlockPanel (AmpProcessor& processor, Block& b,
                                        const juce::String& title, const char* blockId,
                                        int eqLink,
                                        felitronics::analysis::RollingSpectrumTap& toneSpectrumTap)
    : BlockFrame (title, BlockFrame::Kind::captured), amp (processor), block (b), blk (blockId),
      toneTap (toneSpectrumTap),
      eq (processor.apvts, eqLink, toneSpectrumTap,
          [&processor] { return processor.currentSampleRate(); }),
      inMeter ("IN", processor.blockInDb[(size_t) eqLink],
               *processor.apvts.getParameter (params::blockIn (blockId)), true),
      outMeter ("OUT", eqLink == 0 ? processor.boostOutDb : processor.preampOutDb,
                *processor.apvts.getParameter (params::blockLevel (blockId)), false)
{
    // The console's widgets become children of this block, which places them. The spectrum behind
    // its curve is the block's own output tap — the same one the TONE picture reads, because after
    // the chain rework they are the same point in the signal.
    eq.addTo (*this);

    addAndMakeVisible (device);
    addAndMakeVisible (gain);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    device.fontHeight = 16.0f;   // the device's NAME is the face's headline, sized like one

    // The hero's name above the hero: bigger than the rank and file.
    gain.labelFontHeight = 16.0f;
    gain.labelRowHeight  = 20;

    // Five ways of showing the same device, one at a time. The tone curve comes from the block
    // itself — the same data its filters were designed from, resolved on the processor's pump.
    for (int i = 0; i < numViz; ++i)
    {
        scopes[(size_t) i] = std::make_unique<DeviceScope> (
            b.scope, b.ribbon, [this] (double hz) { return block.toneDb (hz); });
        scopes[(size_t) i]->setMode ((DeviceScope::Mode) i);
        scopes[(size_t) i]->setSampleRate (amp.currentSampleRate());
        if ((DeviceScope::Mode) i == DeviceScope::Mode::tone)
            scopes[(size_t) i]->setSpectrumTap (&toneTap, AmpProcessor::eqSpectrumOrder);

        // The picture takes no clicks of its own, so the right-click that changes it reaches this
        // block. Its corner glyphs are separate children and keep theirs.
        scopes[(size_t) i]->setInterceptsMouseClicks (false, false);
        addChildComponent (*scopes[(size_t) i]);
    }

    // What was up when the session was saved.
    vizPick = juce::jlimit (0, numViz - 1,
                            (int) amp.apvts.state.getProperty (vizProperty(), 0));

    for (int i = 0; i < numViz; ++i)
    {
        expandTags[(size_t) i].onClick = [this, i]
        {
            expandedViz = expandedViz == i ? -1 : i;
            for (int j = 0; j < numViz; ++j)
                expandTags[(size_t) j].expanded = expandedViz == j;
            resized();
            repaint();
        };
        addChildComponent (expandTags[(size_t) i]);
    }

    halfTag.onChange = [this]
    {
        scopes[(size_t) DeviceScope::Mode::wave]->waveHalf = halfTag.half;
        repaint();
    };

    screenTag.onClick = [this] { openTheater(); };
    addChildComponent (screenTag);
    scopes[(size_t) DeviceScope::Mode::wave]->waveHalf = halfTag.half;   // half is the default
    addChildComponent (halfTag);

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

CapturedBlockPanel::~CapturedBlockPanel()
{
    closeTheater();
}

void CapturedBlockPanel::openTheater()
{
    if (expandedViz < 0 || theater != nullptr)
        return;

    theater = std::make_unique<ScopeTheater> (
        block, [this] (double hz) { return block.toneDb (hz); },
        (DeviceScope::Mode) expandedViz,
        halfTag.half, amp.currentSampleRate(),
        &toneTap, AmpProcessor::eqSpectrumOrder,
        [this] { closeTheater(); });

    // The face's own copy stops while the theatre runs — the wave ribbon must have ONE
    // resolution-setting reader at a time.
    scopes[(size_t) expandedViz]->setVisible (false);

    theater->setBounds (juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay()->totalArea);
    theater->addToDesktop (juce::ComponentPeer::windowHasDropShadow);
    theater->setVisible (true);
    juce::Desktop::getInstance().setKioskModeComponent (theater.get(), false);
    theater->grabKeyboardFocus();
}

void CapturedBlockPanel::closeTheater()
{
    if (theater == nullptr)
        return;

    juce::Desktop::getInstance().setKioskModeComponent (nullptr);
    theater.reset();
    resized();   // the face's copy comes back
}

void CapturedBlockPanel::deviceChanged()
{
    for (auto& sc : scopes)
        sc->setSampleRate (amp.currentSampleRate());

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

    // The circuit glyphs ride the FIRST picture only — five copies of the same badge is noise.
    scopes[0]->setSpec (felitronics::appkit::parseDeviceSpec (block.circuit()));

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

        // Measured EQ is ignored wholesale (see the pump) — a knob that drives a bypassed
        // filter is worse than no knob, so none are built.
        if (true || measured == nullptr || i >= (int) measured->size())
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

            slot.steps = std::make_unique<VSwitch>();
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
            slot.knob->labelFontHeight = 12.0f;   // the checkboxes' size — the reading floor
            slot.knob->labelRowHeight  = 16;
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

        sel.steps = std::make_unique<VSwitch>();
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
    device.setBounds (area.withTrimmedTop (6));   // the headline sits a touch lower
}

void CapturedBlockPanel::layOutContent (juce::Rectangle<int> area)
{
    // A thrown-open tile owns the WHOLE face: every control steps aside until the fold glyph
    // brings the room back.
    if (expandedViz >= 0)
    {
        setControlsVisible (false);
        eq.setWidgetsVisible (false);

        for (int i = 0; i < numViz; ++i)
        {
            scopes[(size_t) i]->setVisible (i == expandedViz);
            expandTags[(size_t) i].setVisible (i == expandedViz);
        }

        auto* big = scopes[(size_t) expandedViz].get();
        big->setBounds (area);

        auto corner = area.removeFromTop (24).removeFromRight (96);
        expandTags[(size_t) expandedViz].setBounds (corner.removeFromRight (28).reduced (2, 4)
                                                        .translated (-6, 4));

        screenTag.setBounds (corner.removeFromRight (28).reduced (2, 4).translated (-6, 4));
        screenTag.setVisible (true);
        screenTag.toFront (false);

        halfTag.setVisible (expandedViz == (int) DeviceScope::Mode::wave);
        if (halfTag.isVisible())
        {
            halfTag.setBounds (corner.removeFromRight (30).reduced (1, 4).translated (-6, 4));
            halfTag.toFront (false);
        }

        expandTags[(size_t) expandedViz].toFront (false);
        return;
    }

    setControlsVisible (true);
    eq.setWidgetsVisible (true);

    // ---- the pair of meters, one line under the combo, half the width each. They lead because
    //      they are the question you ask first about a captured block: is it being fed right. ----
    {
        auto line = area.removeFromTop (BlockMeter::designHeight);
        area.removeFromTop (gap);

        const int half = (line.getWidth() - gap) / 2;
        inMeter.setBounds (line.removeFromLeft (half));
        line.removeFromLeft (gap);
        outMeter.setBounds (line);
    }

    // ---- the EQ console takes the bottom of the face. It is asked for what it needs rather than
    //      given a fraction: the control row is knobs at reading size and does not compress, and
    //      the curve under half a block is still a curve — squeeze either and the console stops
    //      being usable before it stops fitting. ----
    eq.layOut (area.removeFromBottom (juce::jmin (area.getHeight() - 120, EqSection::rowH + 120)));
    area.removeFromBottom (gap);

    // ---- the top zone: a column of FIXED width carrying the big GAIN with the device's own
    //      selectors under it, and one picture taking everything to its right.
    //
    //      Fixed, because the picture must not move when the device changes. Fur Coat brings an
    //      octave switch and a Big Muff brings nothing, and if the column grew to fit them the
    //      whole right-hand side would shuffle sideways every time the combo was touched. Only the
    //      GAIN dial's diameter answers for what the switches take. ----
    auto column = area.removeFromLeft (columnW);
    area.removeFromLeft (gap);

    // Only the SELECTING controls stand here — the ones that pick a capture. A measured control is
    // a band of the curve and belongs in the console's row, whatever shape its own control has.
    std::vector<VSwitch*> picks;
    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            picks.push_back (sel.steps.get());

    int pickH = 0;
    for (auto* sw : picks)
        pickH = juce::jmax (pickH, sw->idealHeight());

    // The switches take what they need off the bottom and GAIN gets the rest — so a device with
    // nothing to switch simply wears a bigger dial, and nothing else on the face moves.
    if (pickH > 0)
    {
        auto swRow = column.removeFromBottom (pickH);
        column.removeFromBottom (gap);

        const int each = juce::jmax (1, (swRow.getWidth() - ((int) picks.size() - 1) * knobGap)
                                            / (int) picks.size());

        for (auto* sw : picks)
        {
            sw->setBounds (swRow.removeFromLeft (each));
            swRow.removeFromLeft (knobGap);
        }
    }

    const int gainSide = juce::jmin (maxGainSide, juce::jmin (column.getWidth(), column.getHeight()));
    gain.setBounds (column.withSizeKeepingCentre (gainSide, gainSide));

    layWidget (area);
}

void CapturedBlockPanel::setControlsVisible (bool v)
{
    gain.setVisible (v);
    inMeter.setVisible (v);
    outMeter.setVisible (v);
    device.setEnabled (v);

    for (auto& slot : slots)
    {
        if (slot.knob != nullptr)  slot.knob->setVisible (v);
        if (slot.steps != nullptr) slot.steps->setVisible (v);
    }

    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            sel.steps->setVisible (v);

}

/** One picture, filling what is left of the top zone, wearing its own corner glyphs. */
void CapturedBlockPanel::layWidget (juce::Rectangle<int> area)
{
    for (int i = 0; i < numViz; ++i)
        scopes[(size_t) i]->setVisible (i == vizPick);

    widgetArea = area;
    scopes[(size_t) vizPick]->setBounds (area);

    for (auto& t : expandTags)
        t.setVisible (false);

    int right = area.getRight() - 6;

    expandTags[(size_t) vizPick].setBounds (right - 22, area.getY() + 4, 22, 16);
    expandTags[(size_t) vizPick].setVisible (true);
    expandTags[(size_t) vizPick].toFront (false);
    right -= 26;

    screenTag.setVisible (false);

    const bool isWave = vizPick == (int) DeviceScope::Mode::wave;
    halfTag.setVisible (isWave);
    if (isWave)
    {
        halfTag.setBounds (right - 28, area.getY() + 4, 28, 16);
        halfTag.toFront (false);
    }
}

/** A right-click on the PICTURE picks which picture; anywhere else on the block it still means the
    power menu, which is the one edit that must never need aiming for. */
void CapturedBlockPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && widgetArea.contains (e.getPosition()))
    {
        showVizMenu (e.getScreenPosition());
        return;
    }

    BlockFrame::mouseDown (e);
}

/** Which of the five is up. A menu rather than five checkboxes: the block carries a whole EQ
    console now and has room for one picture, and choosing is the honest verb — you look at the
    envelope INSTEAD of the transfer curve, not as well as. */
void CapturedBlockPanel::showVizMenu (juce::Point<int> screenPos)
{
    static const char* const names[numViz] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE" };

    juce::PopupMenu m;
    m.addSectionHeader ("PICTURE");
    for (int i = 0; i < numViz; ++i)
        m.addItem (i + 1, names[i], true, i == vizPick);

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe = juce::Component::SafePointer<CapturedBlockPanel> (this)] (int r)
                     {
                         if (r == 0 || safe == nullptr)
                             return;

                         safe->vizPick = r - 1;

                         // Kept with the session rather than as a parameter: it is which picture you
                         // are looking at, not something a host should be automating.
                         safe->amp.apvts.state.setProperty (safe->vizProperty(), r - 1, nullptr);

                         safe->resized();
                         safe->repaint();
                     });
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
