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
               *processor.apvts.getParameter (params::blockIn (blockId)), true)
{
    // The console's widgets become children of this block, which places them. The spectrum behind
    // its curve is the block's own output tap — the same one the TONE picture reads, because after
    // the chain rework they are the same point in the signal.
    eq.addTo (*this);

    addAndMakeVisible (device);
    addAndMakeVisible (gain);
    addAndMakeVisible (inMeter);

    device.fontHeight = 16.0f;   // the device's NAME, set exactly like the block's own beside it
    device.tracking   = 0.15f;
    device.boxed      = false;   // ...and nothing behind it, the frame opens the line under it

    // The hero wears no label — the whole square is dial — and says its name under the mouse:
    // the PACK's name for it, set when the device loads (Drive, Sustain, Fuzz, Overdrive...).
    // Its arc runs cold to hot, because that is what this one dial's position is.
    gain.labelRowHeight = 0;
    gain.heat = true;

    // The IN meter stands up beside the dial instead of lying across the block above it.
    inMeter.vertical = true;

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

    // Whose bands the console wears. Written by the switch on the console and read back here, so a
    // restored session or an automating host moves the row too.
    eqModeAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockEqMode (blk)),
        [this] (float) { applyEqMode(); resized(); repaint(); });

    eq.onReset = [this]
    {
        if (eq.wearsNative())
            resetToneSlotsToPackDefaults();
    };

    eq.onModePicked = [this] (int i)
    {
        eqModeAttachment->setValueAsCompleteGesture ((float) i);

        // ...and apply it here, because the attachment will NOT call back for its own write: it
        // sets `ignoreCallbacks` while it writes, so the lambda above never runs from a pick. The
        // parameter moved, the processor's pump read it and the SOUND changed — while the row went
        // on showing the other set until something else happened to rebuild it. Which reads as a
        // switch that misses the first click. The device combo two lines up already does exactly
        // this, for exactly this reason.
        applyEqMode();
        resized();
        repaint();
    };

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

        // A PICK, and only a pick. Every slot goes back to what this pack says it should be —
        // otherwise the new device arrives wearing the last one's settings, which are not even
        // about the same controls.
        //
        // Deliberately not done on every device change: a restored session moves this parameter
        // too, and resetting there would throw away exactly the settings the session was saved to
        // keep. A host automating the device index keeps whatever it is automating, which is what
        // automation means.
        resetToPackDefaults();

        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::blockGain (blk), gain);

    // SMOOTH lit: the dial goes anywhere and the player mixes the neighbours by angle. Dark — STEP:
    // the dial itself refuses to stop between two captured positions, so what the hand feels is
    // what the player does.
    addAndMakeVisible (smoothTag);
    smoothAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockSmooth (blk)),
        [this] (float v)
        {
            smoothTag.on = v > 0.5f;
            gain.snapToNotches = ! smoothTag.on;
            smoothTag.setTooltip (smoothTag.on
                ? "SMOOTH: between two captures both play, mixed by angle (2x CPU while between)"
                : "STEP: the dial lands on the captured positions; settled, one model runs");
            smoothTag.repaint();
        });
    smoothTag.onChange = [this]
    {
        smoothAttachment->setValueAsCompleteGesture (smoothTag.on ? 1.0f : 0.0f);
    };
    smoothAttachment->sendInitialUpdate();

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

CapturedBlockPanel::~CapturedBlockPanel()
{
    closeTheater();
}

bool CapturedBlockPanel::foldPicture()
{
    if (expandedViz < 0)
        return false;

    expandedViz = -1;
    for (auto& t : expandTags)
        t.expanded = false;

    resized();
    repaint();
    return true;
}

void CapturedBlockPanel::openTheater()
{
    if (theater != nullptr)
        return;

    // Whatever is up — thrown across the face or sitting in its corner of the block. The theatre
    // used to require the expanded state first, which made it the second half of a gesture nobody
    // had a reason to start.
    const int shown = expandedViz >= 0 ? expandedViz : vizPick;

    theater = std::make_unique<ScopeTheater> (
        block, [this] (double hz) { return block.toneDb (hz); },
        (DeviceScope::Mode) shown,
        halfTag.half, amp.currentSampleRate(),
        &toneTap, AmpProcessor::eqSpectrumOrder,
        [this] { closeTheater(); });

    // The face's own copy stops while the theatre runs — the wave ribbon must have ONE
    // resolution-setting reader at a time.
    scopes[(size_t) shown]->setVisible (false);

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

    const auto tones     = block.tones();
    const auto positions = block.gainPositions();

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

    // EVERY picture carries the circuit badge, not just the first. Five views of one device that
    // only sometimes say whose device it is send you back to the combo to be sure.
    const auto spec = felitronics::appkit::parseDeviceSpec (block.circuit());

    // ...and the DEVICE view carries the paper the badge has no room for.
    //
    // THE DEVICE, by the name the pack file carries — "IR-X Ch1", "Guitar Butler Clean" — the same
    // name the combo shows; the voice's alias, when the pack has one, on the line under it.
    auto* devParam = amp.apvts.getParameter (params::blockDevice (blk));
    const int chosen = juce::jlimit (0, juce::jmax (0, block.packs.size() - 1),
                                     juce::roundToInt (devParam->convertFrom0to1 (devParam->getValue())));

    juce::StringArray paper;

    if (! block.packs.isEmpty())
    {
        const auto& pack = block.packs.getReference (chosen);
        paper.add (pack.displayName().toUpperCase());
        if (pack.alias.isNotEmpty() && pack.alias != pack.displayName())
            paper.add (pack.alias.toUpperCase());
    }

    if (const auto circuit = juce::String (block.circuit()).trim(); circuit.isNotEmpty())
        paper.add (circuit.toUpperCase().replace (",", juce::String::fromUTF8 (" \xc2\xb7 ")));

    if (const int n = block.gainPositions().size(); n > 0)
        paper.add (juce::String (n) + (n == 1 ? " CAPTURE" : " CAPTURES") + " ON THE DIAL");

    for (auto& sc : scopes)
    {
        sc->setSpec (spec);
        sc->setInfo (paper);
    }

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next
    // pack says for the next one. Detented where the pack has positions, continuous where it does
    // not — but always THERE: a knob with nothing to select drives instead, and a device without a
    // gain knob is not a device. Its name is the device's own for that dial.
    gain.setNotches (juce::jmax (0, positions.size()));
    gain.setTooltip (block.dialName().isNotEmpty() ? block.dialName().toUpperCase() : juce::String ("GAIN"));

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

        // A swept tone knob is a band of the console (nativeBands), not a knob on the face. A tone
        // SWITCH — SM7's Sharp/Smooth, a V4's bass cut — has no place on the face yet and stays at
        // its default until the console's row learns to hold one.
    }

    buildSelectors();
    applyEqMode();

    resized();
    repaint();
}

/** A device's own tone controls, as bands of the console.

    Only the SWEPT ones. A measured control whose positions carry names — SM7's Sharp/Smooth — is a
    switch rather than a dial, and a two-detent nameless knob in a row of dials would throw both its
    names away. It keeps the place it already has until the row learns to hold a switch.

    The notch count is the number of positions the pack actually swept: between them there is
    interpolation, not data, and the detents say so on the dial. */
std::vector<EqSection::Band> CapturedBlockPanel::nativeBands() const
{
    std::vector<EqSection::Band> out;

    const auto tones = block.tones();

    for (int i = 0; i < params::boostNumMeasured && i < (int) tones.size(); ++i)
    {
        const auto& m = tones[(size_t) i];

        // A SWITCH — no sweep, or two named positions — is not a band: the row holds dials.
        if (m.sweep <= 0 || (m.positions.size() == 2 && hasNamedPositions (m)))
            continue;

        // A control the pack tested and found NOT to be a filter anywhere gets no knob. namz says
        // so with a collapsed trusted band, and the player honours it by playing silence — so a
        // dial here would turn, look alive, and do nothing at all. That is worse than a gap.
        if (Block::trustFailed (m))
            continue;

        // A measured ladder clicks at the positions it was swept at; a knob shipped as bands has
        // a law for every angle and sweeps freely.
        out.push_back ({ juce::String (m.name).toUpperCase(),
                         theme::eqNode[(size_t) (out.size() % 5)],
                         params::blockMeasured (blk, i),
                         (int) m.positions.size(),
                         false });
    }

    return out;
}

void CapturedBlockPanel::applyEqMode()
{
    const auto native = nativeBands();
    // The PARAMETER, not the apvts atomic. Called from an attachment callback — a host automating
    // this, a session restoring — the atomic has not been written yet at that moment, and reading
    // it there leaves the face exactly one change behind for good.
    auto* modeParam  = amp.apvts.getParameter (params::blockEqMode (blk));
    const int  want  = juce::roundToInt (modeParam->convertFrom0to1 (modeParam->getValue()));

    // A device that measured nothing has no native set, so the choice collapses to ours on its own
    // rather than showing an empty row and asking the player to work out why.
    const bool wearsNative = native.empty() ? false
                                            : want == (int) params::EqMode::native;

    eq.setModes (params::eqModes, wearsNative ? (int) params::EqMode::native : (int) params::EqMode::ours,
                 ! native.empty());

    if (wearsNative)
    {
        eq.setBands (native);
        eq.setNativeCurve ([this] (double hz) { return block.toneDb (hz); });
    }
    else
    {
        eq.setBands (eq.ourBandSet());
        eq.setNativeCurve (nullptr);
    }
}

void CapturedBlockPanel::resetToPackDefaults()
{
    const auto write = [this] (const juce::String& id, float plain)
    {
        if (auto* p = amp.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    };

    // The measured slots. A pack names where its knob starts — and it is NOT the reference
    // position, which is usually an end of travel and therefore the wrong thing to reach for. An
    // unclaimed slot goes to the middle, which is what it means when nothing is behind it.
    resetToneSlotsToPackDefaults();

    // ...and the selecting ones, which have the same problem for the same reason: slot one is an
    // octave switch on one device and something else entirely on the next.
    const auto list = block.selectors();

    for (int i = 0; i < params::numSelectors; ++i)
        write (params::selectorId (blk, i),
               (float) (i < list.size() ? list.getReference (i).defaultIndex : 0));
}

void CapturedBlockPanel::resetToneSlotsToPackDefaults()
{
    const auto tones = block.tones();

    for (int i = 0; i < params::boostNumMeasured; ++i)
        if (auto* p = amp.apvts.getParameter (params::blockMeasured (blk, i)))
        {
            const float plain = (float) (i < (int) tones.size() ? Block::defaultNorm (tones[(size_t) i]) : 0.5);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
}

void CapturedBlockPanel::buildSelectors()
{
    const auto list = block.selectors();

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

        sel.name   = def.name.toUpperCase();
        sel.values = labels;

        sel.steps = std::make_unique<VSwitch>();
        sel.steps->accent = theme::orange;

        // Lying down under the GAIN dial, one line high: the slot and its lever alone — the name
        // and the chosen position are written beside it by the panel, and the whole list is the
        // hint. Names under the detents cost a second line, and the dial's diameter paid for it.
        sel.steps->setHorizontal (true);
        sel.steps->setNamesShown (false);
        sel.steps->setItems (labels, 0);
        sel.steps->setTooltip (labels.joinIntoString (juce::String::fromUTF8 ("   \xc2\xb7   ")));
        addAndMakeVisible (*sel.steps);

        sel.attachment = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (params::selectorId (blk, i)),
            [this, i, n = labels.size()] (float v)
            {
                if (auto* s = selectors[(size_t) i].steps.get())
                    s->setSelectedIndex (juce::jlimit (0, n - 1, juce::roundToInt (v)),
                                         juce::dontSendNotification);
                repaint();   // the chosen position's name is the panel's to write
            });

        sel.steps->onChange = [this, i] (int v)
        {
            selectors[(size_t) i].attachment->setValueAsCompleteGesture ((float) v);
        };

        sel.attachment->sendInitialUpdate();
    }
}

void CapturedBlockPanel::blockOnChanged (bool on)
{
    inMeter.live = on;
    inMeter.repaint();
}

bool CapturedBlockPanel::hasNamedPositions (const namz::rig::Tone& m)
{
    for (const auto& p : m.positions)
    {
        const auto s = juce::String (p.label.empty() ? p.value : p.label).trim();

        if (s.isNotEmpty() && ! s.containsOnly ("0123456789.+-"))
            return true;
    }

    return false;
}

void CapturedBlockPanel::layOutContent (juce::Rectangle<int> area)
{
    // The device combo stands ON the top border, between the block's name and its switch — the row
    // it used to take inside the box is the height the block gave back. Sized to the name and
    // centred in the run, so the line shows on both sides of it as it does beside the block's own.
    {
        const auto slot = borderSlotArea();
        device.setBounds (slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), device.idealWidth()),
                                                      slot.getHeight()));
        borderSlotUsed = device.getBounds();   // the frame opens the line under it
    }

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

        // The whole face is the picture, so the whole face takes the click that walks the loop.
        widgetArea = area;
        scopes[(size_t) expandedViz]->setBounds (area);

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

    // ---- the top zone is CAPPED and the console gets the remainder, not the other way round.
    //
    //      A dial and a picture stop improving once they are big enough to aim at; a curve does
    //      not — every unit of height it gets is another decibel you can tell apart. So the zone
    //      that saturates is the one with the ceiling on it. ----
    auto top = area.removeFromTop (juce::jmin (topZoneH, area.getHeight() - EqSection::rowH - 90));
    area.removeFromTop (gap);
    eq.layOut (area);

    // ---- inside it: a column of FIXED width carrying the big GAIN with the device's own
    //      selectors under it, and one picture taking everything to its right.
    //
    //      Fixed, because the picture must not move when the device changes. Fur Coat brings an
    //      octave switch and a Big Muff brings nothing, and if the column grew to fit them the
    //      whole right-hand side would shuffle sideways every time the combo was touched. Only the
    //      GAIN dial's diameter answers for what the switches take. ----
    auto column = top.removeFromLeft (columnW);
    top.removeFromLeft (gap);

    // Only the SELECTING controls stand here — the ones that pick a capture. A measured control is
    // a band of the curve and belongs in the console's row, whatever shape its own control has.
    // Each takes ONE line off the bottom — name, lever, chosen position — and GAIN gets the rest,
    // so a device with nothing to switch simply wears a bigger dial, and nothing else on the face
    // moves.
    for (auto& sel : selectors)
    {
        if (sel.steps == nullptr)
            continue;

        sel.row = column.removeFromBottom (pickRowH);
        column.removeFromBottom (gap);
        sel.steps->setBounds (sel.row.withX (sel.row.getX() + pickNameW).withWidth (pickSwitchW));
    }

    // The IN meter stands at the column's left edge, as tall as the dial beside it: what the model
    // is being fed, read against the dial that decides how hard.
    auto meterCol = column.removeFromLeft (BlockMeter::designWidth);
    column.removeFromLeft (6);

    const int gainSide = juce::jmin (maxGainSide, juce::jmin (column.getWidth(), column.getHeight()));
    gain.setBounds (column.withSizeKeepingCentre (gainSide, gainSide));
    inMeter.setBounds (meterCol.withY (gain.getY()).withHeight (gainSide));

    // The pill stands in the gap at the bottom of the dial's arc — the one place inside the dial's
    // square that neither the ring nor a notch reaches — so it reads as the dial's own caption.
    smoothTag.setBounds (juce::Rectangle<int> (66, 14).withCentre ({ gain.getBounds().getCentreX(),
                                                                     gain.getBottom() - 6 }));
    smoothTag.toFront (false);

    layWidget (top);
}

void CapturedBlockPanel::setControlsVisible (bool v)
{
    gain.setVisible (v);
    smoothTag.setVisible (v);
    inMeter.setVisible (v);
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

    // BOTH ways out, always. The whole-screen brackets used to appear only once a picture had
    // already been thrown across the face — a door you can only find from inside the room.
    int right = area.getRight() - 6;

    expandTags[(size_t) vizPick].setBounds (right - 22, area.getY() + 4, 22, 16);
    expandTags[(size_t) vizPick].setVisible (true);
    expandTags[(size_t) vizPick].toFront (false);
    right -= 26;

    screenTag.setBounds (right - 22, area.getY() + 4, 22, 16);
    screenTag.setVisible (true);
    screenTag.toFront (false);
    right -= 26;

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
    // A selecting control's line answers on its whole length: the lever goes where it is clicked,
    // and a click on the name or on the chosen position — the two words either side of it — steps
    // to the next position round the loop, so a two-way switch is a click wherever the eye lands.
    if (expandedViz < 0 && ! e.mods.isPopupMenu())
        for (auto& sel : selectors)
            if (sel.steps != nullptr && sel.row.contains (e.getPosition())
                && ! sel.steps->getBounds().contains (e.getPosition()))
            {
                sel.steps->setSelectedIndex ((sel.steps->selectedIndex() + 1) % juce::jmax (1, sel.steps->count()));
                return;
            }

    if (widgetArea.contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
            showVizMenu (e.getScreenPosition());
        else
            setViz ((vizPick + 1) % numViz);   // round the loop, one click at a time

        return;
    }

    BlockFrame::mouseDown (e);
}

/** Both ways of choosing land here, so the choice is remembered whichever was used. */
void CapturedBlockPanel::setViz (int which)
{
    vizPick = juce::jlimit (0, numViz - 1, which);

    // Thrown open, the loop walks the big picture rather than the small one behind it — otherwise
    // clicking a full-face view changes something you cannot see.
    if (expandedViz >= 0)
    {
        expandedViz = vizPick;
        for (int j = 0; j < numViz; ++j)
            expandTags[(size_t) j].expanded = expandedViz == j;
    }

    // Kept with the session rather than as a parameter: it is which picture you are looking at,
    // not something a host should be automating.
    amp.apvts.state.setProperty (vizProperty(), vizPick, nullptr);

    resized();
    repaint();
}

/** Which of the five is up. A menu rather than five checkboxes: the block carries a whole EQ
    console now and has room for one picture, and choosing is the honest verb — you look at the
    envelope INSTEAD of the transfer curve, not as well as. */
void CapturedBlockPanel::showVizMenu (juce::Point<int> screenPos)
{
    static const char* const names[numViz] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE",
                                              "DEVICE" };

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

                         safe->setViz (r - 1);
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

    // Each selecting control's line: its name to the left of the lever, the chosen position to
    // the right of it — the lever between them is the switch itself.
    if (expandedViz < 0)
        for (const auto& sel : selectors)
        {
            if (sel.steps == nullptr || sel.row.isEmpty())
                continue;

            auto row = sel.row.toFloat();
            g.setColour (theme::txDim);
            theme::drawTracked (g, sel.name, row.removeFromLeft ((float) pickNameW - 4.0f),
                                theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);

            row.removeFromLeft ((float) pickSwitchW + 8.0f);
            const int chosen = sel.steps->selectedIndex();
            g.setColour (theme::orange);
            theme::drawTracked (g, chosen < sel.values.size() ? sel.values[chosen] : juce::String(),
                                row, theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);
        }
}

} // namespace orbitamp
