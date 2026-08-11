#include "EqSection.h"

#include "Theme.h"

namespace orbitamp
{

//==============================================================================
/** The link's output meter with its fader riding it: the IN sliver's grammar — a fixed
    violet-through-magenta-to-orange scale the level reveals, a peak-hold line, and the LEVEL as
    a slide-rule frame on its own ±12 scale with a unity notch. */
class EqSection::LevelColumn final : public juce::Component,
                                     private juce::Timer
{
public:
    LevelColumn (const std::atomic<float>& outDbSource, juce::RangedAudioParameter& levelParam)
        : outDb (outDbSource), param (levelParam)
    {
        att = std::make_unique<juce::ParameterAttachment> (param, [this] (float) { repaint(); });
        startTimerHz (30);
    }

    static constexpr int designWidth = 12;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto col = meterArea();

        if (const float ly = dbToY (col, levelDb); ly < col.getBottom() - 1.0f)
        {
            juce::ColourGradient scale (theme::violet, col.getX(), col.getBottom(),
                                        theme::orange, col.getX(), col.getY(), false);
            scale.addColour (0.55, transition);
            g.setGradientFill (scale);
            g.fillRoundedRectangle (col.withTop (ly), 2.0f);
        }

        if (holdDb > floorDb + 0.5f)
        {
            g.setColour (theme::tx.withAlpha (0.9f));
            g.fillRect (col.getX(), dbToY (col, holdDb) - 0.75f, col.getWidth(), 1.5f);
        }

        // The fader frame, slide-rule style, on its own ±12 scale.
        {
            const float ty = faderY (param.convertFrom0to1 (param.getValue()));

            g.setColour (theme::orange.withAlpha (0.5f));
            g.fillRect (r.getX() + 0.5f, faderY (0.0f) - 0.5f, 3.0f, 1.0f);
            g.fillRect (r.getRight() - 3.5f, faderY (0.0f) - 0.5f, 3.0f, 1.0f);

            const auto frame = juce::Rectangle<float> (r.getX() + 0.75f, ty - 5.0f,
                                                       r.getWidth() - 1.5f, 10.0f);
            g.setColour (theme::orange);
            g.drawRoundedRectangle (frame, 2.0f, 1.6f);
            g.fillRect (frame.getX() - 0.5f,     ty - 3.5f, 2.5f, 7.0f);
            g.fillRect (frame.getRight() - 2.0f, ty - 3.5f, 2.5f, 7.0f);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        att->beginGesture();
        att->setValueAsPartOfGesture (faderFromY (e.position.y));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        att->setValueAsPartOfGesture (faderFromY (e.position.y));
    }

    void mouseUp (const juce::MouseEvent&) override { att->endGesture(); }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        // Home is one knock away. The double-click's own mouseDown opened a gesture; this write
        // rides it and the closing mouseUp seals it as one undoable move.
        att->setValueAsPartOfGesture (0.0f);
    }

private:
    void timerCallback() override
    {
        const float now = outDb.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);

        if (now >= holdDb)
        {
            holdDb  = now;
            holdAge = 0;
        }
        else if (++holdAge > holdTicks)
        {
            holdDb = juce::jmax (floorDb, holdDb - holdReleasePerTick);
        }

        repaint();
    }

    juce::Rectangle<float> meterArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f);
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float faderY (float levelDbValue) const
    {
        const auto r = meterArea();
        return r.getCentreY() - levelDbValue / params::eqLevelRangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    float faderFromY (float y) const
    {
        const auto r = meterArea();
        return juce::jlimit (-params::eqLevelRangeDb, params::eqLevelRangeDb,
                             (r.getCentreY() - y) / juce::jmax (1.0f, r.getHeight() * 0.5f - 6.0f)
                                 * params::eqLevelRangeDb);
    }

    static constexpr float floorDb            = -80.0f;
    static constexpr float releasePerTick     = 1.4f;
    static constexpr int   holdTicks          = 60;
    static constexpr float holdReleasePerTick = 0.8f;

    inline static const juce::Colour transition { 0xffc862b4 };

    const std::atomic<float>& outDb;
    juce::RangedAudioParameter& param;
    std::unique_ptr<juce::ParameterAttachment> att;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
};

//==============================================================================
EqSection::EqSection (juce::AudioProcessorValueTreeState& s, int eqLink,
                      const std::atomic<float>& outDb,
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

        g.setColour (theme::violet.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (theme::violet.withAlpha (0.30f));
        g.strokePath (peak, juce::PathStrokeType (1.0f));
    };

    level = std::make_unique<LevelColumn> (outDb, *state.getParameter (params::eqLevel (link)));

    for (auto* k : { &lo, &b1, &b2, &hi })
    {
        k->textForValue    = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };
        k->onValueChange   = [this] { refreshCurve(); };
        k->labelFontHeight = 15.0f;
        k->labelRowHeight  = 19;
    }

    loAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqLoDb (link), lo);
    b1Att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqBellDb (link, 0), b1);
    b2Att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqBellDb (link, 1), b2);
    hiAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::eqHiDb (link), hi);

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

    // The cut switches carry their own attachments; the curve refreshes off ours.
    hpfSw.accent = theme::violet;
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

    // The frequency of every node, in writing, under its own knob — and a way to just TYPE it.
    static constexpr int readoutHandle[4] = { hLo, hB1, hB2, hHi };

    for (int i = 0; i < 4; ++i)
    {
        auto& l = freqReadout[i];
        l.setFont (theme::displayFont (13.0f));
        l.setColour (juce::Label::textColourId, theme::txDim);
        l.setColour (juce::Label::backgroundWhenEditingColourId, theme::bezel);
        l.setColour (juce::Label::textWhenEditingColourId, theme::lilac);
        l.setColour (juce::TextEditor::highlightColourId, theme::violet.withAlpha (0.35f));
        l.setJustificationType (juce::Justification::centred);
        l.setEditable (true, false, false);
        l.onTextChange = [this, i] { freqEdited (readoutHandle[i], freqReadout[i]); };

        const juce::String hzId = i == 0 ? params::eqLoHz (link)
                                : i == 1 ? params::eqBellHz (link, 0)
                                : i == 2 ? params::eqBellHz (link, 1)
                                :          params::eqHiHz (link);

        l.onEditorShow = [this, i, hzId]
        {
            if (auto* ed = freqReadout[i].getCurrentTextEditor())
            {
                ed->setJustification (juce::Justification::centred);
                ed->setText (juce::String (juce::roundToInt (raw (hzId))), false);
                ed->selectAll();
            }
        };
    }

    hpfSlopeBox.getIndex = [this] { return juce::roundToInt (raw (params::eqHpfSlope (link))); };
    lpfSlopeBox.getIndex = [this] { return juce::roundToInt (raw (params::eqLpfSlope (link))); };
    hpfSlopeBox.setIndex = [this] (int i) { hpfSlopeAtt->setValueAsCompleteGesture ((float) i); };
    lpfSlopeBox.setIndex = [this] (int i) { lpfSlopeAtt->setValueAsCompleteGesture ((float) i); };

    presetBtn.onClick = [this] (juce::Point<int> screenPos) { showPresets (screenPos); };

    curve.filterMagnitudeDb  = [this] (double hz) { return display.filterMagnitudeDb (hz); };
    curve.onHandleDrag       = [this] (int i, double hz, double db) { handleDragged (i, hz, db); };
    curve.onDragActive       = [this] (int i, bool a) { handleDragActive (i, a); };
    curve.onHandleWheel      = [this] (int i, float d) { handleWheel (i, d); };
    curve.onHandleStep       = [this] (int i, int n) { stepSlope (i, n); };
    curve.onCurveDoubleClick = [this] (double hz) { curveDoubleClicked (hz); };
    curve.onHandleDoubleClick = [this] (int i) { handleDoubleClicked (i); };

    refreshCurve();
}

EqSection::~EqSection() = default;

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

void EqSection::PresetButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (theme::hair2);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (theme::txDim);
    theme::drawTracked (g, "PRESETS", r.withTrimmedRight (14.0f), theme::displayFont (12.0f), 0.14f,
                        juce::Justification::centred);

    juce::Path v;
    const float cx = r.getRight() - 13.0f, cy = r.getCentreY() - 1.0f;
    v.startNewSubPath (cx - 3.0f, cy);
    v.lineTo (cx, cy + 3.0f);
    v.lineTo (cx + 3.0f, cy);
    g.strokePath (v, juce::PathStrokeType (1.2f));
}

void EqSection::showPresets (juce::Point<int> screenPos)
{
    // The menu is stamps of starting points, named for the JOB this link does at its place in the
    // chain — eq1 preps the signal for the boost, eq2 shapes it for the preamp. Every stamp
    // starts from flat and applies its move; the moment you touch a knob the sound is yours.
    juce::PopupMenu m;
    m.addSectionHeader ("EQ " + juce::String (link + 1));
    m.addItem (1, "Flat");
    m.addSeparator();

    if (link == 0)
    {
        m.addItem (2, "Rumble cut");
        m.addItem (3, "Tight low");
        m.addItem (4, "Lead push");
    }
    else
    {
        m.addItem (2, "Tight");
        m.addItem (3, "Bright");
        m.addItem (4, "Smooth");
    }

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [this] (int r)
                     {
                         if (r == 0)
                             return;

                         resetLink();   // every preset is a move away from flat

                         if (link == 0)
                         {
                             if (r == 2)        // Rumble cut: the infra-low goes before the boost sees it
                             {
                                 setParam (params::eqHpfOn (link), 1.0f);
                                 setParam (params::eqHpfHz (link), 75.0f);
                                 setParam (params::eqHpfSlope (link), 3.0f);   // 24 dB/oct
                             }
                             else if (r == 3)   // Tight low: higher cut plus a leaner shelf
                             {
                                 setParam (params::eqHpfOn (link), 1.0f);
                                 setParam (params::eqHpfHz (link), 110.0f);
                                 setParam (params::eqHpfSlope (link), 3.0f);
                                 setParam (params::eqLoDb (link), -2.0f);
                             }
                             else if (r == 4)   // Lead push: the screamer hump
                             {
                                 setParam (params::eqBellDb (link, 0), 4.0f);
                                 setParam (params::eqBellHz (link, 0), 750.0f);
                             }
                         }
                         else
                         {
                             if (r == 2)        // Tight: the preamp gets nothing to flub
                             {
                                 setParam (params::eqHpfOn (link), 1.0f);
                                 setParam (params::eqHpfHz (link), 120.0f);
                                 setParam (params::eqHpfSlope (link), 3.0f);
                                 setParam (params::eqLoDb (link), -3.0f);
                                 setParam (params::eqLoHz (link), 150.0f);
                             }
                             else if (r == 3)   // Bright
                             {
                                 setParam (params::eqHiDb (link), 4.0f);
                                 setParam (params::eqHiHz (link), 3500.0f);
                             }
                             else if (r == 4)   // Smooth: the glass comes off before the drive
                             {
                                 setParam (params::eqLpfOn (link), 1.0f);
                                 setParam (params::eqLpfHz (link), 7000.0f);
                                 setParam (params::eqLpfSlope (link), 1.0f);   // 12 dB/oct
                             }
                         }
                     });
}

void EqSection::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = state.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void EqSection::resetLink()
{
    // Everything back to its default in one sweep — except the power: that is the frame's, and a
    // reset that also switched you off would be a reset that argues.
    const juce::StringArray ids {
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

    for (const auto& id : ids)
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
    parent.addAndMakeVisible (presetBtn);
    parent.addAndMakeVisible (hpfSw);
    parent.addAndMakeVisible (lpfSw);
    parent.addAndMakeVisible (hpfLabel);
    parent.addAndMakeVisible (lpfLabel);
    parent.addAndMakeVisible (hpfSlopeBox);
    parent.addAndMakeVisible (lpfSlopeBox);
    parent.addAndMakeVisible (*level);

    for (auto& l : freqReadout)
        parent.addAndMakeVisible (l);

    for (auto* k : { &lo, &b1, &b2, &hi })
        parent.addAndMakeVisible (*k);
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

    const double readoutHz[4] = { s.loHz, s.b1Hz, s.b2Hz, s.hiHz };

    for (int i = 0; i < 4; ++i)
        if (! freqReadout[i].isBeingEdited())
            freqReadout[i].setText (formatHz (readoutHz[i]), juce::dontSendNotification);

    hpfSlopeBox.repaint();
    lpfSlopeBox.repaint();
    curve.repaint();
}

juce::String EqSection::formatHz (double hz)
{
    if (hz < 1000.0)
        return juce::String (juce::roundToInt (hz));

    auto k = juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1)
                 .trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return k + "K";
}

void EqSection::freqEdited (int handle, juce::Label& label)
{
    // Accepts what a musician types: "820", "2.5k", "2500", "1 K" — anything else is ignored and
    // the next refresh writes the truth back.
    const auto t = label.getText().toUpperCase().retainCharacters ("0123456789.K");
    const double mul = t.containsChar ('K') ? 1000.0 : 1.0;
    const double hz  = t.upToFirstOccurrenceOf ("K", false, false).getDoubleValue() * mul;

    if (hz > 0.0)
        freqAtt[handle]->setValueAsCompleteGesture ((float) hz);

    refreshCurve();
}

void EqSection::SlopeCombo::paint (juce::Graphics& g)
{
    if (getIndex == nullptr)
        return;

    const auto r = getLocalBounds().toFloat();
    const auto text = params::eqSlopes[juce::jlimit (0, params::eqSlopes.size() - 1, getIndex())]
                          + juce::String (" DB/OCT");

    g.setColour (theme::txDim);
    theme::drawTracked (g, text, r.withTrimmedRight (12.0f), theme::displayFont (12.0f), 0.06f,
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

    h.getReference (hLo) = { s.loHz, s.loDb, H::Freedom::both, true };
    h.getReference (hB1) = { s.b1Hz, s.b1Db, H::Freedom::both, true };
    h.getReference (hB2) = { s.b2Hz, s.b2Db, H::Freedom::both, true };
    h.getReference (hB3) = { s.b3Hz, s.b3Db, H::Freedom::both, s.b3On };
    h.getReference (hHi) = { s.hiHz, s.hiDb, H::Freedom::both, true };

    // The cuts only exist while they are switched on — a handle for a filter that is not in the
    // chain would be a control over nothing.
    h.getReference (hHpf) = { s.hpfHz, 0.0, H::Freedom::freq, s.hpfOn };
    h.getReference (hLpf) = { s.lpfHz, 0.0, H::Freedom::freq, s.lpfOn };

    curve.setHandles (std::move (h));
}

void EqSection::handleDragged (int index, double hz, double db)
{
    const auto clampDb = [] (double v) { return juce::jlimit (-15.0, 15.0, v); };

    switch (index)
    {
        case hLo: lo.setValue (clampDb (db), juce::sendNotificationSync); break;
        case hB1: b1.setValue (clampDb (db), juce::sendNotificationSync); break;
        case hB2: b2.setValue (clampDb (db), juce::sendNotificationSync); break;
        case hHi: hi.setValue (clampDb (db), juce::sendNotificationSync); break;
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
    // The LEVEL column takes the right edge, full height.
    auto lvl = content.removeFromRight (LevelColumn::designWidth);
    level->setBounds (lvl.reduced (0, 2));
    content.removeFromRight (12);

    // The control row: [HPF sw][LO][B1][B2][HI][LPF sw], gain knobs only; under every knob its
    // frequency in writing, under every switch its slope combo.
    auto row = content.removeFromBottom (124).reduced (0, 4);
    content.removeFromBottom (6);

    const int cellW    = row.getWidth() / 6;
    const int readoutH = 18;

    const auto switchCell = [&] (juce::Rectangle<int> cell, ZoneSwitch& sw, juce::Label& label,
                                 SlopeCombo& combo)
    {
        combo.setBounds (cell.removeFromBottom (readoutH).withSizeKeepingCentre (98, readoutH));
        label.setBounds (cell.removeFromTop (18));
        sw.setBounds (cell.withSizeKeepingCentre (30, 16));
    };

    switchCell (row.removeFromLeft (cellW), hpfSw, hpfLabel, hpfSlopeBox);

    int i = 0;
    for (auto* k : { &lo, &b1, &b2, &hi })
    {
        auto cell = row.removeFromLeft (cellW);
        freqReadout[i++].setBounds (cell.removeFromBottom (readoutH).withSizeKeepingCentre (76, readoutH));
        const int side = juce::jmin (cell.getWidth(), cell.getHeight());
        k->setBounds (cell.withSizeKeepingCentre (side, side));
    }

    switchCell (row, lpfSw, lpfLabel, lpfSlopeBox);

    // The curve gets everything else; the reset overlays its top-right corner.
    curve.setBounds (content);
    presetBtn.setBounds (content.getRight() - 124, content.getY() + 8, 114, 24);
    presetBtn.toFront (false);
}

} // namespace orbitamp
