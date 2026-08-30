#include "EqSection.h"

#include "Theme.h"

namespace orbitamp
{

//==============================================================================
EqSection::EqSection (juce::AudioProcessorValueTreeState& s, int eqLink,
                      felitronics::analysis::RollingSpectrumTap& spectrumTap,
                      std::function<double()> sampleRateGetter)
    : state (s), link (eqLink), tap (spectrumTap), sampleRate (std::move (sampleRateGetter))
{
    display.prepare (displayRate, 1);

    // The spectrum behind everything: the signal is the ground the response stands on.
    curve.paintSpectrum = [this] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = 20.0;
        pm.freqMax    = 20000.0;
        pm.specTop    = 0.0;
        pm.specBottom = -90.0;

        juce::Path fill, peak;
        fill.startNewSubPath (r.getX(), r.getBottom());
        bool first = true;

        pane.buildColumns (pm, sampleRate(), 4.5, 1000.0,
                           [&] (int, float x, float yFill, float yPeak)
                           {
                               fill.lineTo (r.getX() + x, r.getY() + yFill);

                               if (first) { peak.startNewSubPath (r.getX() + x, r.getY() + yPeak); first = false; }
                               else       peak.lineTo (r.getX() + x, r.getY() + yPeak);
                           });

        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (theme::spectrum.withAlpha (0.22f),
                                                 0.0f, r.getY() + r.getHeight() * 0.30f,
                                                 theme::spectrum.withAlpha (0.03f),
                                                 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (theme::spectrum.withAlpha (0.55f));
        g.strokePath (peak, juce::PathStrokeType (1.0f));
    };

    buildBands (ourBands());

    // The gesture-bearing attachments: frequencies per handle, Qs, slopes, and B3's pair.
    freqAtt[hLo]  = attach (params::eqLoHz (link));
    freqAtt[hB1]  = attach (params::eqBellHz (link, 0));
    freqAtt[hB2]  = attach (params::eqBellHz (link, 1));
    freqAtt[hB3]  = attach (params::eqBellHz (link, 2));
    freqAtt[hHi]  = attach (params::eqHiHz (link));
    freqAtt[hHpf] = attach (params::eqHpfHz (link));
    freqAtt[hLpf] = attach (params::eqLpfHz (link));

    for (int b = 0; b < 3; ++b)
        qAtt[b] = attach (params::eqBellQ (link, b));

    hpfSlopeAtt = attach (params::eqHpfSlope (link));
    lpfSlopeAtt = attach (params::eqLpfSlope (link));
    b3OnAtt     = attach (params::eqB3On (link));
    b3DbAtt     = attach (params::eqBellDb (link, 2));

    // The cut switches carry their own attachments; the curve refreshes off ours. Each cut wears
    // its line's colour — orange HPF, violet LPF — so switch, dashed line and combo read as one.
    hpfSw.accent = theme::orange;
    lpfSw.accent = theme::violet;
    hpfSw.attach (*state.getParameter (params::eqHpfOn (link)));
    lpfSw.attach (*state.getParameter (params::eqLpfOn (link)));
    refreshAtts.push_back (attach (params::eqHpfOn (link)));
    refreshAtts.push_back (attach (params::eqLpfOn (link)));

    for (auto* l : { &hpfLabel, &lpfLabel })
    {
        l->setFont (theme::displayFont (14.0f));
        l->setColour (juce::Label::textColourId, theme::txDim);
        l->setJustificationType (juce::Justification::centred);
        l->setInterceptsMouseClicks (false, false);
    }

    hpfSlopeBox.getIndex = [this] { return juce::roundToInt (raw (params::eqHpfSlope (link))); };
    lpfSlopeBox.getIndex = [this] { return juce::roundToInt (raw (params::eqLpfSlope (link))); };
    hpfSlopeBox.setIndex = [this] (int i) { hpfSlopeAtt->setValueAsCompleteGesture ((float) i); };
    lpfSlopeBox.setIndex = [this] (int i) { lpfSlopeAtt->setValueAsCompleteGesture ((float) i); };

    modeBtn.onClick = [this] (juce::Point<int> screenPos) { showModeMenu (screenPos); };

    curve.onContextMenu = [this] (juce::Point<int> at) { showModeMenu (at); };

    curve.onHandleDrag       = [this] (int i, double hz, double db) { handleDragged (i, hz, db); };
    curve.onDragActive       = [this] (int i, bool a) { handleDragActive (i, a); };
    curve.onHandleWheel      = [this] (int i, float d) { handleWheel (i, d); };
    curve.onHandleStep       = [this] (int i, int n) { stepSlope (i, n); };
    curve.onCurveDoubleClick = [this] (double hz) { curveDoubleClicked (hz); };
    curve.onHandleDoubleClick = [this] (int i) { handleDoubleClicked (i); };

    refreshCurve();
}

EqSection::~EqSection() = default;

/** Short names on purpose: six cells across half a panel leave about sixty design units each, and
    "LO MID" at reading size ran straight into its neighbour. The colour carries the identity the
    name had to give up — the knob wears the same one as its point on the curve. */
std::vector<EqSection::Band> EqSection::ourBands() const
{
    return { { "LO",    theme::eqNode[0], params::eqLoDb (link) },
             { "L MID", theme::eqNode[1], params::eqBellDb (link, 0) },
             { "H MID", theme::eqNode[2], params::eqBellDb (link, 1) },
             { "HI",    theme::eqNode[3], params::eqHiDb (link) } };
}

/** Puts a set of bands on the row: a knob each, attached to whatever parameter the band names.

    Rebuildable on purpose. The four this console has today are ours and fixed, but the next set is
    a pack's own tone controls — one to three of whatever that device happened to have — and the one
    after that is a classical stack's three. A row that can only be built once is a row that can
    only ever hold ours. */
void EqSection::buildBands (std::vector<Band> newBands)
{
    // Attachments first, and in this order: a SliderAttachment reaches for its slider in its own
    // destructor, so a knob freed before its attachment is a knob freed from under it.
    bandAtts.clear();
    bandKnobs.clear();

    bands = std::move (newBands);

    for (const auto& b : bands)
    {
        auto k = std::make_unique<Knob> (b.label, b.colour, b.notches);

        if (b.showsDb) k->textForValue = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };
        else           k->textForValue = [] (double)   { return juce::String(); };

        k->onValueChange   = [this] { refreshCurve(); };
        k->labelFontHeight = 13.0f;
        k->labelRowHeight  = 17;

        bandAtts.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, b.param, *k));

        if (host != nullptr)
            host->addAndMakeVisible (*k);

        bandKnobs.push_back (std::move (k));
    }

    if (host != nullptr)
        host->resized();
}

void EqSection::setModes (const juce::StringArray& names, int selected, bool nativeAvailable)
{
    modeNames     = names;
    modeIndex     = juce::jlimit (0, juce::jmax (0, names.size() - 1), selected);
    nativeOffered = nativeAvailable;
    modeBtn.text  = modeNames[modeIndex].toUpperCase();
    modeBtn.repaint();
}

/** Whose bands the console wears — from the button in the curve's corner, or by right-clicking
    the curve: the same menu either way, so there is one place the answer changes.

    The device's side is offered only when the device measured something; a pack that brought no
    tone knobs leaves it grey rather than absent, so the row still says what it could have been.
    RESET closes the list: the console's own knobs back to flat, and — through the block — the
    device's slots back to where the pack starts them, whichever set is on show. */
void EqSection::showModeMenu (juce::Point<int> screenPos)
{
    juce::PopupMenu m;

    for (int i = 0; i < modeNames.size(); ++i)
        m.addItem (i + 1, modeNames[i].toUpperCase(), i != 0 || nativeOffered, i == modeIndex);

    m.addSeparator();
    m.addItem (100, "RESET");

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [safe = juce::Component::SafePointer<juce::Component> (&curve), this] (int r)
                     {
                         if (safe == nullptr)
                             return;

                         if (r == 100)
                         {
                             resetLink();
                             if (onReset != nullptr)
                                 onReset();
                         }
                         else if (r > 0 && onModePicked != nullptr)
                             onModePicked (r - 1);
                     });
}

void EqSection::setBandDb (size_t index, double db)
{
    if (index >= bandKnobs.size())
        return;

    // Through the KNOB, not the parameter: the knob is the parameter's face here, and writing past
    // it would leave the dial showing the value the curve no longer has.
    bandKnobs[index]->setValue (juce::jlimit (-(double) params::toneRangeDb,
                                              (double) params::toneRangeDb, db),
                                juce::sendNotificationSync);
}

void EqSection::setSpectrumRunning (bool shouldRun)
{
    if (shouldRun)
        startTimerHz (30);
    else
        stopTimer();
}

void EqSection::timerCallback()
{
    // Pull a frame if one is waiting; a mismatched order is discarded (another consumer switched
    // resolutions — cannot happen while everyone agrees on spectrumOrder, but the contract stands).
    int order = 0;

    if (tap.tryPull (pane.frameInput(), order) && order == spectrumOrder)
        pane.ingest (order);
    else
        pane.starve();

    curve.repaint();
}

float EqSection::raw (const juce::String& id) const
{
    // The PARAMETER, not the apvts atomic: listener lists notify newest-first, so an attachment
    // callback can run before the atomic cache updates — reading the cache there left this face
    // exactly one click behind. The parameter object itself is written before anyone is told.
    auto* p = state.getParameter (id);
    return p->convertFrom0to1 (p->getValue());
}

std::unique_ptr<juce::ParameterAttachment> EqSection::attach (const juce::String& id)
{
    return std::make_unique<juce::ParameterAttachment> (*state.getParameter (id),
                                                        [this] (float) { refreshCurve(); });
}

void EqSection::ModeButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (theme::hair2);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (theme::txDim);
    theme::drawTracked (g, text, r.withTrimmedRight (14.0f), theme::displayFont (12.0f), 0.14f,
                        juce::Justification::centred);

    juce::Path v;
    const float cx = r.getRight() - 13.0f, cy = r.getCentreY() - 1.0f;
    v.startNewSubPath (cx - 3.0f, cy);
    v.lineTo (cx, cy + 3.0f);
    v.lineTo (cx + 3.0f, cy);
    g.strokePath (v, juce::PathStrokeType (1.2f));
}

juce::StringArray EqSection::linkParamIds() const
{
    // Everything the link owns — except the power: that is the frame's.
    return {
        params::eqHpfOn (link),  params::eqHpfHz (link),  params::eqHpfSlope (link),
        params::eqLoDb (link),   params::eqLoHz (link),
        params::eqBellDb (link, 0), params::eqBellHz (link, 0), params::eqBellQ (link, 0),
        params::eqBellDb (link, 1), params::eqBellHz (link, 1), params::eqBellQ (link, 1),
        params::eqB3On (link),
        params::eqBellDb (link, 2), params::eqBellHz (link, 2), params::eqBellQ (link, 2),
        params::eqHiDb (link),   params::eqHiHz (link),
        params::eqLpfOn (link),  params::eqLpfHz (link),  params::eqLpfSlope (link),
        params::eqLevel (link),
    };
}

void EqSection::resetLink()
{
    // Back to default in one sweep — a reset that also switched you off would be a reset that
    // argues, hence no power here.
    for (const auto& id : linkParamIds())
    {
        if (auto* p = state.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }
}

void EqSection::addTo (juce::Component& parent)
{
    parent.addAndMakeVisible (curve);
    parent.addAndMakeVisible (modeBtn);
    host = &parent;

    for (auto& k : bandKnobs)
        parent.addAndMakeVisible (*k);
    parent.addAndMakeVisible (hpfSw);
    parent.addAndMakeVisible (lpfSw);
    parent.addAndMakeVisible (hpfLabel);
    parent.addAndMakeVisible (lpfLabel);
    parent.addAndMakeVisible (hpfSlopeBox);
    parent.addAndMakeVisible (lpfSlopeBox);

    for (auto& k : bandKnobs)
        parent.addAndMakeVisible (*k);
}

void EqSection::setWidgetsVisible (bool v)
{
    // The console's clock rides its visibility, and this is the ONLY place that starts it now.
    //
    // It used to be started by the block that drew the EQ back when the EQ had a block of its own;
    // that block is gone, nothing took the job over, and the spectrum behind the curve quietly
    // stopped existing. The curve kept redrawing on every parameter move, which is exactly why it
    // took a while to notice — a live line over a dead ground.
    setSpectrumRunning (v);

    curve.setVisible (v);
    modeBtn.setVisible (v);
    hpfSw.setVisible (v);
    lpfSw.setVisible (v);
    hpfLabel.setVisible (v);
    lpfLabel.setVisible (v);
    hpfSlopeBox.setVisible (v);
    lpfSlopeBox.setVisible (v);

    for (auto& k : bandKnobs)
        k->setVisible (v);
}

void EqSection::refreshCurve()
{
    const auto slope = [] (float v)
    {
        return params::eqSlopeValues[juce::jlimit (0, params::eqSlopes.size() - 1, juce::roundToInt (v))];
    };

    core::EqLink::Settings s;
    s.hpfOn    = raw (params::eqHpfOn (link)) > 0.5f;
    s.hpfHz    = (double) raw (params::eqHpfHz (link));
    s.hpfSlope = slope (raw (params::eqHpfSlope (link)));
    s.loDb     = (double) raw (params::eqLoDb (link));
    s.loHz     = (double) raw (params::eqLoHz (link));
    s.b1Db     = (double) raw (params::eqBellDb (link, 0));
    s.b1Hz     = (double) raw (params::eqBellHz (link, 0));
    s.b1Q      = (double) raw (params::eqBellQ (link, 0));
    s.b2Db     = (double) raw (params::eqBellDb (link, 1));
    s.b2Hz     = (double) raw (params::eqBellHz (link, 1));
    s.b2Q      = (double) raw (params::eqBellQ (link, 1));
    s.b3On     = raw (params::eqB3On (link)) > 0.5f;
    s.b3Db     = (double) raw (params::eqBellDb (link, 2));
    s.b3Hz     = (double) raw (params::eqBellHz (link, 2));
    s.b3Q      = (double) raw (params::eqBellQ (link, 2));
    s.hiDb     = (double) raw (params::eqHiDb (link));
    s.hiHz     = (double) raw (params::eqHiHz (link));
    s.lpfOn    = raw (params::eqLpfOn (link)) > 0.5f;
    s.lpfHz    = (double) raw (params::eqLpfHz (link));
    s.lpfSlope = slope (raw (params::eqLpfSlope (link)));

    display.setSettings (s);
    refreshHandles();

    hpfSlopeBox.repaint();
    lpfSlopeBox.repaint();
    curve.repaint();
}

void EqSection::SlopeCombo::paint (juce::Graphics& g)
{
    if (getIndex == nullptr)
        return;

    const auto r = getLocalBounds().toFloat();

    // "12 DB", not "12 DB/OCT". A cut cell inside a half-panel block is about sixty design units
    // wide, and the per-octave part was clipping its own first digit off — a slope that reads "2"
    // when it is twelve is worse than one that does not say the unit. Everyone reading a filter
    // slope knows what it is per.
    const auto text = params::eqSlopes[juce::jlimit (0, params::eqSlopes.size() - 1, getIndex())]
                          + juce::String (" DB");

    g.setColour (theme::txDim);
    theme::drawTracked (g, text, r.withTrimmedRight (11.0f), theme::displayFont (12.0f), 0.04f,
                        juce::Justification::centred);

    // The chevron that says "this opens".
    juce::Path v;
    const float cx = r.getRight() - 9.0f, cy = r.getCentreY() - 1.0f;
    v.startNewSubPath (cx - 3.0f, cy);
    v.lineTo (cx, cy + 3.0f);
    v.lineTo (cx + 3.0f, cy);
    g.strokePath (v, juce::PathStrokeType (1.2f));
}

void EqSection::SlopeCombo::mouseDown (const juce::MouseEvent& e)
{
    if (getIndex == nullptr || setIndex == nullptr)
        return;

    juce::PopupMenu m;
    for (int i = 0; i < params::eqSlopes.size(); ++i)
        m.addItem (i + 1, params::eqSlopes[i] + " dB/oct", true, i == getIndex());

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ e.getScreenPosition().x,
                                                  e.getScreenPosition().y, 1, 1 }),
                     [safe = juce::Component::SafePointer<SlopeCombo> (this)] (int r)
                     {
                         if (r > 0 && safe != nullptr)
                             safe->setIndex (r - 1);
                     });
}

void EqSection::refreshHandles()
{
    using H = EqCurve::Handle;

    juce::Array<H> h;
    h.resize (numHandles);

    const auto& s = display.getSettings();

    // OUR bands only, and only while the console is wearing them. In the device's own set our four
    // are bypassed — the parametric is out of the signal by definition — so a point that still
    // dragged would be writing to something nobody can hear. The exact bug the EQ's enable was
    // just deleted for, in a different costume.
    const bool ours = nativeDb == nullptr;

    h.getReference (hLo) = { s.loHz, s.loDb, H::Freedom::both, ours,   theme::eqNode[0] };
    h.getReference (hB1) = { s.b1Hz, s.b1Db, H::Freedom::both, ours,   theme::eqNode[1] };
    h.getReference (hB2) = { s.b2Hz, s.b2Db, H::Freedom::both, ours,   theme::eqNode[2] };
    h.getReference (hB3) = { s.b3Hz, s.b3Db, H::Freedom::both, ours && s.b3On, theme::eqNode[4] };
    h.getReference (hHi) = { s.hiHz, s.hiDb, H::Freedom::both, ours,   theme::eqNode[3] };

    // The cuts only exist while they are switched on — a handle for a filter that is not in the
    // chain would be a control over nothing. Orange HPF, violet LPF, same as their switches.
    h.getReference (hHpf) = { s.hpfHz, 0.0, H::Freedom::freq, s.hpfOn, theme::orange };
    h.getReference (hLpf) = { s.lpfHz, 0.0, H::Freedom::freq, s.lpfOn, theme::violet };

    curve.setHandles (std::move (h));
}

void EqSection::handleDragged (int index, double hz, double db)
{
    const auto clampDb = [] (double v) { return juce::jlimit (-15.0, 15.0, v); };

    switch (index)
    {
        // The handles and the row are the same four bands in the same order, so the index IS the
        // band. A curve that knew four names would be a second place to keep the list. (A device's
        // own bands have no handles yet — setBandDb is a no-op past the end of the list.)
        case hLo: setBandDb (0, db); break;
        case hB1: setBandDb (1, db); break;
        case hB2: setBandDb (2, db); break;
        case hHi: setBandDb (3, db); break;
        case hB3: b3DbAtt->setValueAsPartOfGesture ((float) clampDb (db)); break;
        default: break;
    }

    freqAtt[index]->setValueAsPartOfGesture ((float) hz);
    refreshCurve();
}

void EqSection::handleDragActive (int index, bool active)
{
    // One drag is ONE undoable move. The gain knobs bracket their own gestures; the frequencies
    // (and B3's gain, which has no knob) are bracketed here.
    if (active)
    {
        freqAtt[index]->beginGesture();
        if (index == hB3) b3DbAtt->beginGesture();
    }
    else
    {
        freqAtt[index]->endGesture();
        if (index == hB3) b3DbAtt->endGesture();
    }
}

void EqSection::handleWheel (int index, float delta)
{
    // Over a bell the wheel is Q; over a wall it is the slope ladder.
    if (index == hB1 || index == hB2 || index == hB3)
    {
        const int b = index == hB1 ? 0 : index == hB2 ? 1 : 2;
        const float q = raw (params::eqBellQ (link, b));
        qAtt[b]->setValueAsCompleteGesture (q * std::pow (1.6f, delta * 4.0f));
        return;
    }

    if (index == hHpf || index == hLpf)
        stepSlope (index, delta > 0 ? 1 : -1);
}

void EqSection::stepSlope (int index, int steps)
{
    if (index != hHpf && index != hLpf)
        return;

    auto& att = index == hHpf ? hpfSlopeAtt : lpfSlopeAtt;
    const auto id = index == hHpf ? params::eqHpfSlope (link) : params::eqLpfSlope (link);
    const int next = juce::jlimit (0, params::eqSlopes.size() - 1,
                                   juce::roundToInt (raw (id)) + steps);
    att->setValueAsCompleteGesture ((float) next);
}

void EqSection::curveDoubleClicked (double hz)
{
    // The surgical bell lands where you asked for it, switched on, flat — pull it down.
    freqAtt[hB3]->setValueAsCompleteGesture ((float) hz);
    b3OnAtt->setValueAsCompleteGesture (1.0f);
}

void EqSection::handleDoubleClicked (int index)
{
    if (index == hB3)
        b3OnAtt->setValueAsCompleteGesture (0.0f);
}

void EqSection::layOut (juce::Rectangle<int> content)
{
    // The control row across the bottom: [HPF sw][LO][B1][B2][HI][LPF sw], gain knobs only, and
    // under each switch its slope combo.
    //
    // The frequencies used to be written out under every cell and are not any more. Half a block is
    // not half a panel: keeping the numbers meant either shrinking the type below what can be read
    // at 1x or taking the room from the curve, and the curve IS the readout — a node's frequency is
    // where the node is standing. Dragging it sideways was always the way to set one.
    auto row = content.removeFromBottom (rowH);
    content.removeFromBottom (6);

    // ONE LINE OF NAMES. HPF, LO, L MID, H MID, HI, LPF all begin at the row's top edge, because
    // six labels at three different heights read as three groups rather than one row of controls.
    // A knob draws its own name in the first `labelRowHeight` of its bounds, so the knobs are set
    // flush with the top of the row rather than centred in their cells — then the cuts' labels,
    // given the same height, land on the same baseline for free.
    //
    // The cut cells are narrower than the knob cells on purpose: a switch and "12 DB" need less
    // width than a dial does, and what they give up goes into the dials, which is the only place
    // in this row where extra width becomes something you can aim at.
    // The cut cells are a fixed width and the bands share what is left, however many there are: a
    // device that measured one Tone control gets one big centred dial rather than one dial and
    // three holes where our other three used to be.
    const int cutW  = 50;
    const int n     = juce::jmax (1, (int) bandKnobs.size());
    const int knobW = (row.getWidth() - 2 * cutW) / n;

    const auto hpfCell = row.removeFromLeft (cutW);

    juce::Rectangle<int> firstCell = row;

    for (size_t i = 0; i < bandKnobs.size(); ++i)
    {
        const auto cell = row.removeFromLeft (knobW);
        if (i == 0)
            firstCell = cell;

        const int side = juce::jmin (cell.getWidth(), cell.getHeight());
        bandKnobs[i]->setBounds (cell.withWidth (side).withHeight (side)
                                     .withX (cell.getCentreX() - side / 2));
    }

    const auto lpfCell = row.removeFromRight (cutW);

    // The cuts hang off the first knob's geometry, so an empty row still has to put them somewhere.
    const auto first = bandKnobs.empty() ? firstCell : bandKnobs.front()->getBounds();
    const int labelH  = bandKnobs.empty() ? 17 : bandKnobs.front()->labelRowHeight;
    const int dialAxisY = first.getY() + labelH + (first.getHeight() - labelH) / 2;

    const auto switchCell = [&] (juce::Rectangle<int> cell, ZoneSwitch& sw, juce::Label& label,
                                 SlopeCombo& combo)
    {
        label.setBounds (cell.removeFromTop (labelH));

        sw.setBounds (juce::Rectangle<int> (cell.getX(), dialAxisY - 8, cell.getWidth(), 16)
                          .withSizeKeepingCentre (juce::jmin (cell.getWidth(), 30), 16));
        combo.setBounds (juce::Rectangle<int> (cell.getX(), sw.getBottom() + 4,
                                               cell.getWidth(), 18));
    };

    switchCell (hpfCell, hpfSw, hpfLabel, hpfSlopeBox);
    switchCell (lpfCell, lpfSw, lpfLabel, lpfSlopeBox);

    // The curve gets everything else; the two overlays share its top-right corner — whose bands,
    // then which stamp, reading left to right in the order the questions are asked.
    curve.setBounds (content);

    modeBtn.setBounds (content.getRight() - 150, content.getY() + 6, 140, 22);
    modeBtn.toFront (false);
}

} // namespace orbitamp
