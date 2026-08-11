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

    static constexpr int designWidth = 36;

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

        g.setColour (theme::txDim);
        theme::drawTracked (g, "OUT", r.withTrimmedTop (r.getHeight() - 13.0f),
                            theme::displayFont (8.0f), 0.10f, juce::Justification::centred);
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
        return getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (13.0f);
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
        k->labelFontHeight = 11.0f;
        k->labelRowHeight  = 15;
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
        l->setFont (theme::displayFont (11.0f));
        l->setColour (juce::Label::textColourId, theme::txDim);
        l->setJustificationType (juce::Justification::centred);
        l->setInterceptsMouseClicks (false, false);
    }

    curve.filterMagnitudeDb  = [this] (double hz) { return display.filterMagnitudeDb (hz); };
    curve.onHandleDrag       = [this] (int i, double hz, double db) { handleDragged (i, hz, db); };
    curve.onDragActive       = [this] (int i, bool a) { handleDragActive (i, a); };
    curve.onHandleWheel      = [this] (int i, float d) { handleWheel (i, d); };
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
    return state.getRawParameterValue (id)->load();
}

std::unique_ptr<juce::ParameterAttachment> EqSection::attach (const juce::String& id)
{
    return std::make_unique<juce::ParameterAttachment> (*state.getParameter (id),
                                                        [this] (float) { refreshCurve(); });
}

void EqSection::addTo (juce::Component& parent)
{
    parent.addAndMakeVisible (curve);
    parent.addAndMakeVisible (hpfSw);
    parent.addAndMakeVisible (lpfSw);
    parent.addAndMakeVisible (hpfLabel);
    parent.addAndMakeVisible (lpfLabel);
    parent.addAndMakeVisible (*level);

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
    curve.repaint();
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
    {
        auto& att = index == hHpf ? hpfSlopeAtt : lpfSlopeAtt;
        const auto id = index == hHpf ? params::eqHpfSlope (link) : params::eqLpfSlope (link);
        const int step = delta > 0 ? 1 : -1;
        const int next = juce::jlimit (0, params::eqSlopes.size() - 1,
                                       juce::roundToInt (raw (id)) + step);
        att->setValueAsCompleteGesture ((float) next);
    }
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

    // The control row: [HPF sw][LO][B1][B2][HI][LPF sw], gain knobs only.
    auto row = content.removeFromBottom (96).reduced (0, 4);
    content.removeFromBottom (6);

    const int cellW = row.getWidth() / 6;

    const auto switchCell = [&] (juce::Rectangle<int> cell, ZoneSwitch& sw, juce::Label& label)
    {
        label.setBounds (cell.removeFromTop (16));
        sw.setBounds (cell.withSizeKeepingCentre (30, 16));
    };

    switchCell (row.removeFromLeft (cellW), hpfSw, hpfLabel);

    for (auto* k : { &lo, &b1, &b2, &hi })
    {
        auto cell = row.removeFromLeft (cellW);
        const int side = juce::jmin (cell.getWidth(), cell.getHeight());
        k->setBounds (cell.withSizeKeepingCentre (side, side));
    }

    switchCell (row, lpfSw, lpfLabel);

    // The curve gets everything else.
    curve.setBounds (content);
}

} // namespace orbitamp
