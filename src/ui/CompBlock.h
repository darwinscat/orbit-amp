// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "MeterRail.h"
#include "scope/EnvelopeView.h"
#include "scope/TransferView.h"
#include "../PluginProcessor.h"
#include "../core/BassComp.h"

#include <vector>

namespace orbitamp
{

/** COMP'S PICTURE: the level running right to left over the last 2.5 s in THREE traces, all before the automatic make-up
    (the owner, 31.10): BEFORE COMP, the AMP's ENVELOPE page's violet fill under its edge; AFTER COMP, the compressed path
    itself, a dashed lilac line under it by exactly the squeeze; AFTER COMP + BLEND, what goes on, the orange line — and, in a strip of its own along the top, the squeeze
    itself as a violet LINE with its number now. Across the levels, the THRESHOLD: a lilac line with an orange handle
    carrying its number.

    THE LINE AND THE SLIDER ARE ONE CONTROL: dragging the line moves COMP's parameter to the knob whose threshold the
    line is (core::BassComp::amountForThreshold), through the parameter's attachment, one gesture a drag; the slider
    moved, the line follows. A right click is COMP's menu.

    It reads the processor's taps (core::CompTap) on its own timer, and only while it is on screen — the audio thread
    writes them only then — and it freezes in silence: the tap writes no column of it. Standing by or at BLEND 0 COMP writes
    nothing either: the picture stands still, dimmed with its block (the owner, 31.10). Every number on it is 13 px. */
class CompGraph final : public juce::Component,
                        private juce::Timer
{
public:
    explicit CompGraph (AmpProcessor& processor)
        : amp (processor),
          amountP (*processor.apvts.getParameter (params::compAmount)),
          characterP (*processor.apvts.getParameter (params::compCharacter)),
          thresholdP (*processor.apvts.getParameter (params::compThreshold)),
          onP (*processor.apvts.getParameter (params::compOn)),
          cols ((size_t) columnsShown)
    {
        amountAtt    = std::make_unique<juce::ParameterAttachment> (amountP,    [this] (float) { repaint(); });
        onAtt        = std::make_unique<juce::ParameterAttachment> (onP,        [] (float) {});
        thrAtt       = std::make_unique<juce::ParameterAttachment> (thresholdP, [this] (float) { repaint(); });
        characterAtt = std::make_unique<juce::ParameterAttachment> (characterP, [this] (float) { repaint(); });
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        startTimerHz (30);   // always: it is how the picture learns an ancestor was hidden or shown (refresh)
    }

    ~CompGraph() override { amp.compWatched.store (false, std::memory_order_relaxed); }

    static constexpr double topDb = 0.0, bottomDb = -48.0;   // the levels' scale
    static constexpr double grRangeDb = 24.0;                // the squeeze's, down its strip
    static constexpr double spanSeconds = 2.5;
    static constexpr int    columnsShown = (int) (spanSeconds / core::CompTap::hopSeconds + 0.5);
    static constexpr float  textHeight = 13.0f, handleW = 64.0f, grStripH = 30.0f, stripGap = 3.0f;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }

    /** The squeeze's strip along the top, and the levels' area under it. */
    juce::Rectangle<float> grStrip() const { return plot().removeFromTop (grStripH); }
    juce::Rectangle<float> levels() const  { return plot().withTrimmedTop (grStripH + stripGap); }

    float yOfDb (double db) const
    {
        const auto r = levels();
        return r.getY() + (float) ((topDb - db) / (topDb - bottomDb)) * r.getHeight();
    }

    double dbAtY (float y) const
    {
        const auto r = levels();
        return topDb - (double) ((y - r.getY()) / r.getHeight()) * (topDb - bottomDb);
    }

    core::BassComp::Character character() const
    {
        return characterP.getValue() > 0.5f ? core::BassComp::Character::punchy : core::BassComp::Character::smooth;
    }

    /** Where the line stands: the detector's threshold at the knob as it is, dB. */
    double thresholdDb() const
    {
        return (double) thresholdP.convertFrom0to1 (thresholdP.getValue());   // the threshold, its own parameter
    }

    /** The line taken, dragged to `y`, let go: ONE gesture — COMP's knob and its switch, as the rail moves them (a drag
        on a COMP standing by switches it in) — and ONE undo step, however long the hand pauses. */
    void beginLineDrag()
    {
        amp.history.beginGesture();
        thrAtt->beginGesture();
        onAtt->beginGesture();
        dragging = true;
        repaint();
    }

    void dragLineTo (float y)
    {
        thrAtt->setValueAsPartOfGesture ((float) juce::jlimit ((double) params::compThresholdMin, (double) params::compThresholdMax, dbAtY (y)));
        onAtt->setValueAsPartOfGesture (1.0f);
    }

    void endLineDrag()
    {
        if (dragging)
        {
            thrAtt->endGesture();
            onAtt->endGesture();
            amp.history.endGesture();
        }
        dragging = false;
        repaint();
    }

    /** Where the line is drawn, and its handle. */
    float lineY() const
    {
        const auto lv = levels();
        return juce::jlimit (lv.getY() + halfGrip(), lv.getBottom() - halfGrip(), yOfDb (thresholdDb()));
    }

    juce::Rectangle<float> handleBox() const
    {
        return { levels().getX() + 3.0f, lineY() - meterrail::gripH * 0.5f, handleW, meterrail::gripH };
    }

    /** CLICK-TO-TYPE on the handle: a threshold in dB, clamped to its travel — one gesture. */
    void typeThreshold (double db)
    {
        amp.history.beginGesture();
        thrAtt->setValueAsCompleteGesture ((float) juce::jlimit ((double) params::compThresholdMin, (double) params::compThresholdMax, db));
        onAtt->setValueAsCompleteGesture (1.0f);
        amp.history.endGesture();
    }

    void openTyping()
    {
        typing.open (*this, handleBox().toNearestInt(), (float) thresholdDb(), theme::orange,
                     [this] (float db) { typeThreshold ((double) db); }, 0, textHeight);
    }

    bool isTyping() const { return typing.isOpen(); }

    /** The timer's tick, callable by a gate: the taps are wanted while the picture SHOWS — every ancestor visible, the
        window up — and read only then. */
    void refresh()
    {
        const bool on = isShowing();
        amp.compWatched.store (on, std::memory_order_relaxed);
        if (on)
            pull();
    }

    /** What the taps hold, newest last — the timer's work, and a picture's made off screen. */
    void pull()
    {
        shown = amp.compTap.read (cols.data(), (int) cols.size());
        if (const auto w = amp.compTap.written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = plot(), strip = grStrip(), lv = levels();
        g.setColour (well);
        g.fillRoundedRectangle (r, 3.0f);

        g.setColour (theme::hair);
        g.fillRect (r.getX(), strip.getBottom() + stripGap * 0.5f - 0.5f, r.getWidth(), 1.0f);   // the strip's floor
        for (double db = -12.0; db > bottomDb; db -= 12.0)
            g.fillRect (lv.getX(), yOfDb (db), lv.getWidth(), 1.0f);

        // The columns, the newest at the right edge; a pixel shows the loudest of the columns it covers, and the deepest
        // squeeze. Three traces, each a path along the pixels that have playing behind them.
        const int w = juce::jmax (1, (int) r.getWidth());
        juce::Path inEdge, midEdge, outEdge, grLine, inFill, outFill;
        bool started = false;
        float firstX = 0.0f, lastX = 0.0f, lastGr = 0.0f;
        const auto grY = [&] (float gr) { return strip.getY() + 3.0f + juce::jmin (1.0f, (float) (-gr / grRangeDb)) * (strip.getHeight() - 6.0f); };
        if (shown > 0)
            lastGr = cols[(size_t) shown - 1].gr;
        for (int x = 0; x < w && shown > 0; ++x)
        {
            const int a = x * columnsShown / w, b = juce::jmax (a + 1, (x + 1) * columnsShown / w);
            float in = 0.0f, out = 0.0f, gr = 0.0f, mid = 0.0f;
            bool  any = false;
            for (int j = a; j < b; ++j)
                if (const int k = j - (columnsShown - shown); k >= 0)
                {
                    const auto& c = cols[(size_t) k];
                    in  = juce::jmax (in, c.in);
                    out = juce::jmax (out, c.out);
                    mid = juce::jmax (mid, c.mid);
                    gr  = juce::jmin (gr, c.gr);
                    any = true;
                }
            if (! any)
                continue;

            const float px  = r.getX() + (float) x + 0.5f;
            const float yIn = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (in)));
            const float yOut = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (out)));
            const float yMid = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (mid)));
            if (! started)
            {
                started = true;
                firstX  = px;
                inEdge.startNewSubPath (px, yIn);
                midEdge.startNewSubPath (px, yMid);
                outEdge.startNewSubPath (px, yOut);
                grLine.startNewSubPath (px, grY (gr));
            }
            else
            {
                inEdge.lineTo (px, yIn);
                midEdge.lineTo (px, yMid);
                outEdge.lineTo (px, yOut);
                grLine.lineTo (px, grY (gr));
            }
            lastX = px;
        }

        if (started)
        {
            // Each level a translucent fill under its own edge: neither hides the other where they overlap.
            const auto filled = [&] (const juce::Path& edge)
            {
                juce::Path p (edge);
                p.lineTo (lastX, lv.getBottom());
                p.lineTo (firstX, lv.getBottom());
                p.closeSubPath();
                return p;
            };
            // In the AMP's ENVELOPE page's own style (scope::EnvelopeView): IN its violet fill under its edge; OUT its
            // orange — as a LINE alone: orange is never a wash on the violet panel.
            using Page = scope::EnvelopeView;
            g.setColour (Page::inColour().withAlpha (Page::inFillAlpha));
            g.fillPath (filled (inEdge));
            g.setColour (Page::inColour());
            g.strokePath (inEdge, juce::PathStrokeType (Page::edgeWidth));
            // AFTER COMP, the compressed path itself — under BEFORE by exactly the squeeze: the page's lilac, DASHED, so it reads
            // against the violet fill and beside the orange.
            {
                juce::Path dashed;
                const float dashes[] { 4.0f, 3.0f };
                juce::PathStrokeType (Page::edgeWidth).createDashedStroke (dashed, midEdge, dashes, 2);
                g.setColour (theme::lilac);
                g.fillPath (dashed);
            }
            // AFTER COMP + BLEND, what goes on down the chain (before the make-up): the orange line.
            g.setColour (Page::outColour());
            g.strokePath (outEdge, juce::PathStrokeType (Page::edgeWidth));

            g.setColour (theme::violet.brighter (0.3f));                                // the squeeze, a line in its strip
            g.strokePath (grLine, juce::PathStrokeType (1.6f));
        }

        // The squeeze now, at the strip's left, on a plate of its own so the line passes under it.
        {
            const auto text = dbText (lastGr < -0.05f ? (double) lastGr : 0.0);
            const auto box  = strip.withWidth (theme::trackedWidth (text, theme::displayFont (textHeight), 0.02f) + 12.0f).reduced (0.0f, 4.0f);
            g.setColour (well);
            g.fillRoundedRectangle (box, 2.5f);
            g.setColour (theme::violet.brighter (0.6f));
            theme::drawTracked (g, text, box, theme::displayFont (textHeight), 0.02f, juce::Justification::centred);
        }

        // THE THRESHOLD: the lilac line, the orange handle at its left end with its number.
        const float yT = lineY();
        g.setColour (theme::lilac);
        g.fillRect (lv.getX(), yT - 0.75f, lv.getWidth(), 1.5f);
        const juce::Rectangle<float> track (lv.getX() + 3.0f, lv.getY(), handleW, lv.getHeight());
        g.setColour (well);
        g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), yT - meterrail::gripH * 0.5f, handleW, meterrail::gripH), 2.5f);
        meterrail::paintGrip (g, track, yT, dbText (thresholdDb()), theme::orange, dragging, textHeight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        grab    = lineY() - (float) e.y;   // the line moves with the hand, from where it was taken
        pressed = true;
        onHandle = handleBox().contains (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed && ! dragging && e.getDistanceFromDragStart() > 3)
            beginLineDrag();
        if (dragging)
            dragLineTo ((float) e.y + grab);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            endLineDrag();
        else if (pressed && onHandle)
            openTyping();   // a click on the handle types the threshold
        pressed = false;
    }

    void visibilityChanged() override      { refresh(); }
    void parentHierarchyChanged() override { refresh(); }

private:
    static float halfGrip() { return meterrail::gripH * 0.5f; }
    static double dbOf (float peak) { return peak > 1.0e-6f ? 20.0 * std::log10 ((double) peak) : -120.0; }
    static juce::String dbText (double db) { return juce::String (juce::roundToInt (db)) + " dB"; }

    void timerCallback() override { refresh(); }

    inline static const juce::Colour well { 0xff121218 }, levelIn { 0xff8e8e9c }, levelOut { 0xffeeeef6 };

    AmpProcessor& amp;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& characterP;
    juce::RangedAudioParameter& thresholdP;
    juce::RangedAudioParameter& onP;
    std::unique_ptr<juce::ParameterAttachment> amountAtt, characterAtt, onAtt, thrAtt;
    std::vector<core::CompTap::Column> cols;
    int shown = 0;
    std::uint32_t lastWritten = 0;
    bool  dragging = false, pressed = false, onHandle = false;
    float grab = 0.0f;
    meterrail::GripEditor typing;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompGraph)
};

/** COMP'S RATIO, what both its controls share: the ratio, its own parameter, and a drag of it — COMP switched in with
    it, one gesture on `comp_ratio`, one undo step. */
struct CompRatio
{
    explicit CompRatio (AmpProcessor& processor)
        : amp (processor),
          onP (*processor.apvts.getParameter (params::compOn)),
          amountP (*processor.apvts.getParameter (params::compAmount)),
          characterP (*processor.apvts.getParameter (params::compCharacter)),
          ratioP (*processor.apvts.getParameter (params::compRatio)),
          thresholdP (*processor.apvts.getParameter (params::compThreshold)) {}

    double thresholdDb() const
    {
        return (double) thresholdP.convertFrom0to1 (thresholdP.getValue());
    }

    double ratio() const
    {
        return (double) ratioP.convertFrom0to1 (ratioP.getValue());   // the ratio, its own parameter
    }

    /** Taken: from the ratio where it stands, so the grip does not jump — and COMP switched in, as
        its line and its rail do: one gesture, and one undo step, until let go. */
    void take()
    {
        const auto now = (float) ratio();
        amp.history.beginGesture();
        onP.beginChangeGesture();
        onP.setValueNotifyingHost (1.0f);
        ratioP.beginChangeGesture();
        ratioP.setValueNotifyingHost (ratioP.convertTo0to1 (now));
        taken = true;
    }

    void moveTo (double r)
    {
        if (taken)
            ratioP.setValueNotifyingHost (ratioP.convertTo0to1 ((float) juce::jlimit ((double) params::compRatioMin, (double) params::compRatioMax, r)));
    }

    void letGo()
    {
        if (taken)
        {
            ratioP.endChangeGesture();
            onP.endChangeGesture();
            amp.history.endGesture();
        }
        taken = false;
    }

    AmpProcessor& amp;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& characterP;
    juce::RangedAudioParameter& ratioP;
    juce::RangedAudioParameter& thresholdP;
    bool taken = false;
};

/** COMP'S RATIO DETENTS (the owner, 31.10): a drag near 2, 3, 4, 5, 7 or 10 : 1 — within `detentPx` of it on the control —
    sticks to it; between them the ratio moves freely; a typed ratio is taken as typed. */
inline constexpr double compRatioDetents[] { 2.0, 3.0, 4.0, 5.0, 7.0, 10.0 };
inline constexpr float  compDetentPx = 4.0f;

template <typename PositionOf>
double snapRatio (double r, float at, PositionOf positionOf)
{
    // The NEAREST detent within reach, not the first: where two are within reach of each other (the square's upper
    // ratios, a few pixels apart) the hand lands on the one it is on.
    double best = r;
    float  apart = compDetentPx;
    for (const double d : compRatioDetents)
        if (const float gap = std::abs (positionOf (d) - at); gap <= apart)
        {
            best  = d;
            apart = gap;
        }
    return best;
}

/** COMP'S RATIO SLIDER, over the IN/OUT square: 2:1 … 10:1 along `comp_ratio`'s own travel, a lilac track and an orange
    grip — the hand's — carrying the ratio at 13 px, the ratio's own control (the detents: compRatioDetents); moved, it sets
    the ratio (one gesture a drag), as a drag in the square does. */
class CompRatioSlider final : public juce::Component
{
public:
    explicit CompRatioSlider (AmpProcessor& processor) : ratio (processor)
    {
        for (auto* p : { &ratio.amountP, &ratio.characterP, &ratio.ratioP, &ratio.thresholdP })
            atts.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); }));
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    static constexpr float gripW = 10.0f, gripH = 18.0f, textH = 18.0f;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    double shownRatio() const { return ratio.ratio(); }

    /** The track, under the row its value is written in. */
    juce::Rectangle<float> track() const { return getLocalBounds().toFloat().withTrimmedTop (textH).reduced (gripW * 0.5f, 0.0f); }

    float xOfRatio (double r) const
    {
        const auto t = track();
        return t.getX() + ratio.ratioP.convertTo0to1 ((float) r) * t.getWidth();
    }

    /** Where the ticks stand: at every detent, on the slider's own mapping. */
    std::vector<float> tickXs() const
    {
        std::vector<float> xs;
        for (const double d : compRatioDetents)
            xs.push_back (xOfRatio (d));
        return xs;
    }

    double ratioAtX (float x) const
    {
        const auto t = track();
        return (double) ratio.ratioP.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (x - t.getX()) / t.getWidth()));
    }

    /** The row the ratio is written in, over the grip. */
    juce::Rectangle<float> textBox() const
    {
        constexpr float textW = 48.0f;
        const auto b = getLocalBounds().toFloat();
        const float x = xOfRatio (shownRatio());
        return juce::Rectangle<float> (textW, textH).withCentre ({ juce::jlimit (b.getX() + textW * 0.5f, b.getRight() - textW * 0.5f, x),
                                                                   b.getY() + textH * 0.5f });
    }

    /** CLICK-TO-TYPE on the ratio: taken as typed, 2..10 — one gesture. */
    void typeRatio (double r)
    {
        ratio.take();
        ratio.moveTo (r);
        ratio.letGo();
    }

    void openTyping()
    {
        typing.open (*this, textBox().toNearestInt(), (float) shownRatio(), theme::orange,
                     [this] (float r) { typeRatio ((double) r); }, 1, 13.0f);
    }

    bool isTyping() const { return typing.isOpen(); }

    void beginDrag()          { ratio.take(); repaint(); }
    void dragTo (float x)     { ratio.moveTo (snapRatio (ratioAtX (x), x, [this] (double d) { return xOfRatio (d); })); }
    void endDrag()            { ratio.letGo(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto t = track();
        const float cy = t.getCentreY();
        g.setColour (theme::lilac.withAlpha (0.55f));
        g.fillRoundedRectangle (juce::Rectangle<float> (t.getX(), cy - 1.0f, t.getWidth(), 2.0f), 1.0f);
        for (const float x : tickXs())   // a tick at every detent — where the grip catches — no numbers: the grip says it
            g.fillRect (x - 0.5f, cy - 4.0f, 1.0f, 8.0f);

        // The grip, a solid of the hand's orange, its ratio written over it.
        const auto  r     = shownRatio();
        const float x     = xOfRatio (r);
        const auto  hand  = ratio.taken ? theme::orange.brighter (0.25f) : theme::orange;
        g.setColour (hand);
        g.fillRoundedRectangle (juce::Rectangle<float> (gripW, gripH).withCentre ({ x, cy }), 2.0f);
        theme::drawTracked (g, juce::String (r, 1) + ":1", textBox(), theme::displayFont (13.0f), 0.02f, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        typingClick = e.position.y < textH;   // a click on the number types it; anywhere else the grip moves
        grab = xOfRatio (shownRatio()) - e.position.x;   // ...with the hand, from where it was taken
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (typingClick)
            return;
        if (! ratio.taken && e.getDistanceFromDragStart() > 3)   // a click moves nothing: only a drag takes it
            beginDrag();
        if (ratio.taken)
            dragTo (e.position.x + grab);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (typingClick)
            openTyping();
        else
            endDrag();
        typingClick = false;
    }

private:
    CompRatio ratio;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    float grab = 0.0f;
    bool  typingClick = false;
    meterrail::GripEditor typing;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompRatioSlider)
};

/** COMP'S IN/OUT SQUARE, under the ratio slider (the TRANSFER painter's square, unity and stroke): the compressor's own
    curve in dB, -48..0 each way — the knee at the threshold, a lilac dot there, the upper segment's slope the ratio.
    A drag up or down sets the ratio the hand HOLDS, as the slider does, and the slider follows. A right click is
    COMP's menu. */
class CompTransfer final : public juce::Component
{
public:
    explicit CompTransfer (AmpProcessor& processor) : ratio (processor)
    {
        for (auto* p : { &ratio.amountP, &ratio.characterP, &ratio.ratioP, &ratio.thresholdP })
            atts.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); }));
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    std::function<void (juce::Point<int> screenPos)> onMenu;

    static constexpr double lowDb = -48.0;

    double shownRatio() const { return ratio.ratio(); }

    juce::Rectangle<float> box() const { return scope::TransferView::square (getLocalBounds().toFloat().reduced (1.0f)); }

    /** The upper segment's end, where the input reaches 0 dB, taken to `y`: its height says the slope. */
    void beginRatioDrag()   { ratio.take(); }
    void dragRatioTo (float y)
    {
        const auto   b   = box();
        const double t   = ratio.thresholdDb();
        const double out = juce::jlimit (t + 0.05, 0.0, (double) (lowDb * (y - b.getY()) / b.getHeight()));
        ratio.moveTo (snapRatio (-t / (out - t), y, [this] (double d) { return yOfRatio (d); }));
    }
    void endRatioDrag()     { ratio.letGo(); }

    /** Where the segment's end stands for a ratio — the inverse of dragRatioTo, for a gate. */
    float yOfRatio (double r) const
    {
        const auto b = box();
        const double t = ratio.thresholdDb();
        return yOf (t - t / r, b);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = box();
        g.setColour (juce::Colour (0xff121218));
        g.fillRoundedRectangle (b, 3.0f);
        scope::TransferView::paintUnity (g, b);

        const double t = ratio.thresholdDb(), r = ratio.ratio();
        juce::Path p;
        for (int i = 0; i <= 96; ++i)
        {
            const double in = lowDb + (double) i * (-lowDb / 96.0);
            const juce::Point<float> at { xOf (in, b), yOf (curve (in, t, r), b) };
            if (i == 0)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
        }
        scope::TransferView::strokeCurve (g, p);

        g.setColour (theme::lilac);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ xOf (t, b), yOf (t, b) }));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        grab = yOfRatio (shownRatio()) - e.position.y;   // the segment's end moves with the hand, from where it was taken
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! ratio.taken && e.getDistanceFromDragStart() > 3)   // a click moves nothing: only a drag takes it
            beginRatioDrag();
        if (ratio.taken)
            dragRatioTo (e.position.y + grab);
    }

    void mouseUp (const juce::MouseEvent&) override { endRatioDrag(); }

private:
    static float xOf (double db, juce::Rectangle<float> b) { return b.getX() + (float) ((db - lowDb) / -lowDb) * b.getWidth(); }
    static float yOf (double db, juce::Rectangle<float> b) { return b.getBottom() - (float) ((db - lowDb) / -lowDb) * b.getHeight(); }

    /** The compressor's static curve with core's soft knee (core::BassComp::kneeDb wide). */
    static double curve (double in, double t, double r)
    {
        const double w = core::BassComp::kneeDb, over = in - t;
        if (2.0 * over < -w)
            return in;
        if (2.0 * std::abs (over) <= w)
            return in + (1.0 / r - 1.0) * (over + w * 0.5) * (over + w * 0.5) / (2.0 * w);
        return t + over / r;
    }

    CompRatio ratio;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    float grab = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompTransfer)
};

/** THE BASS'S COMP, A BLOCK OF ITS OWN (the owner, 31.10): after the GATE and before OCTAVE on the panel, as in the
    strip. Inside, left to right: its BLEND (a rail, 0..100 %, parallel compression), its PICTURE with its THRESHOLD
    line, and its RATIO, a slider over the IN/OUT square — each control one job. ATTACK, RELEASE and CHARACTER are its
    settings (the gear, a right click). Our own DSP, so violet; its arrow in the strip switches it, and standing by it dims in place, still
    in reach. */
class CompBlock final : public BlockFrame
{
public:
    explicit CompBlock (AmpProcessor& processor)
        : BlockFrame ("COMP", Kind::dsp),
          state (processor.apvts),
          rail (*processor.apvts.getParameter (params::compAmount), *processor.apvts.getParameter (params::compOn), 0.0f, 1.0f, false),
          graph (processor),
          ratioSlider (processor),
          transfer (processor)
    {
        shieldsWhenOff = false;
        bass::asBassRail (rail);   // the fader's amount in percent, a field too
        rail.onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        for (auto* c : { (juce::Component*) &rail, (juce::Component*) &graph, (juce::Component*) &ratioSlider, (juce::Component*) &transfer })
            addAndMakeVisible (*c);
        const auto menu = [this] (juce::Point<int>) { bass::showCompMenu (state, this); };
        rail.onMenu = graph.onMenu = ratioSlider.onMenu = transfer.onMenu = menu;
        attachPower (*processor.apvts.getParameter (params::compOn));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            bass::showCompMenu (state, this);
    }

    CompGraph&       picture() noexcept { return graph; }
    CompTransfer&    square() noexcept  { return transfer; }
    CompRatioSlider& ratio() noexcept   { return ratioSlider; }
    GuardRail&       fader() noexcept   { return rail; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        rail.setBounds (r.removeFromLeft (GuardRail::designWidth));
        r.removeFromLeft (gap);
        auto column = r.removeFromRight (squareMax);
        r.removeFromRight (gap);
        graph.setBounds (r);
        ratioSlider.setBounds (column.removeFromTop (sliderH));
        column.removeFromTop (4);
        const int side = juce::jmin (column.getHeight(), column.getWidth());
        transfer.setBounds (column.removeFromTop (side).withSizeKeepingCentre (side, side));
    }

private:
    static constexpr int gap = 10, squareMax = 118, sliderH = 40;

    juce::AudioProcessorValueTreeState& state;
    GuardRail       rail;
    CompGraph       graph;
    CompRatioSlider ratioSlider;
    CompTransfer    transfer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompBlock)
};

} // namespace orbitamp
