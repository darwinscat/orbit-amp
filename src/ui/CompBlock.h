// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "MeterRail.h"
#include "OnFirst.h"
#include "VoicingSelector.h"
#include "scope/EnvelopeView.h"
#include "scope/TransferView.h"
#include "../PluginProcessor.h"
#include "../core/BassComp.h"

#include <vector>

namespace orbitamp
{

/** COMP'S PICTURE: the level running right to left over the last 2.5 s in THREE traces, all before the automatic make-up
    (the owner, 31.10): BEFORE COMP, the AMP's ENVELOPE page's violet fill under its edge; AFTER COMP, the compressed path
    itself, a SOLID GREY line under it by exactly the squeeze (the owner, 01.11); AFTER COMP + BLEND, what goes on, the
    orange line — and, in a strip of its own along the top, the squeeze itself as a LINE coloured by its depth, light
    violet at a little, toward fully orange at -12 dB and deeper (grColour), with its number now sitting on the strip's
    border. Across the levels, the THRESHOLD: a lilac line alone — its number is on the threshold rail's grip.

    THE LINE AND THE THRESHOLD RAIL ARE ONE CONTROL: the line dragged sets `comp_threshold` through its attachment, one
    gesture and one undo step a drag; the rail moved, the line follows — and the rail's hand travels exactly the levels'
    scale, so the hand stands at the line's height (CompBlock::layOutContent). A right click is COMP's menu.

    It reads the processor's taps (core::CompTap) on its own timer, and only while it is on screen — the audio thread
    writes them only then — and it freezes in silence: the tap writes no column of it. Standing by or at BLEND 0 COMP writes
    nothing either: the picture stands still, dimmed with its block (the owner, 31.10). Every number on it is 13 px. */
/** COMP's CHARACTER as its parameter stands. */
inline core::BassComp::Character compCharacterOf (const juce::RangedAudioParameter& p)
{
    return static_cast<core::BassComp::Character> (juce::jlimit (0, core::BassComp::characterCount - 1,
                                                                 juce::roundToInt (p.convertFrom0to1 (p.getValue()))));
}

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
          cols ((size_t) core::CompTap::columns)
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
    /** ONE PIXEL, A FIXED GROUP OF COLUMNS (the owner, 01.11: the picture flickered as it moved): a whole number of columns a
        pixel, chosen from the plot's width so the span stays about 2.5 s, the groups counted from the tap's first column —
        a pixel stays the same sound while it scrolls and the picture moves by whole pixels; only the newest, still filling
        at the right edge, grows. A resize may regroup. */
    static constexpr int spanColumns = (int) (2.5 / core::CompTap::hopSeconds + 0.5);
    static int columnsPerPixel (int w) noexcept { return juce::jmax (1, juce::roundToInt ((double) spanColumns / (double) juce::jmax (1, w))); }
    static constexpr float  textHeight = 13.0f, grStripH = 30.0f, stripGap = 3.0f;

    /** How far a GuardRail's hand centre stands in from the rail's ends (its box less 2 px, then half a hand): the levels
        end as far above the picture's foot, less the plot's own 1 px, so a rail of the picture's height under the
        GR strip carries its hand at the line's height, -48 dB to 0 dB, end to end. */
    static constexpr float railInset = 2.0f + meterrail::gripH * 0.5f, levelsFoot = railInset - 1.0f;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }

    /** The squeeze's strip along the top, and the levels' area under it. */
    juce::Rectangle<float> grStrip() const { return plot().removeFromTop (grStripH); }
    juce::Rectangle<float> levels() const  { return plot().withTrimmedTop (grStripH + stripGap).withTrimmedBottom (levelsFoot); }

    /** The line between the squeeze's strip and the levels — where the squeeze's number sits. */
    float stripBorderY() const { return grStrip().getBottom() + stripGap * 0.5f; }

    /** THE SQUEEZE'S COLOUR at `grDb` (the owner, 01.11): light violet at none, toward fully orange at -12 dB, orange
        deeper — every point of the line by its own depth. */
    static juce::Colour grColour (float grDb)
    {
        return theme::violet.brighter (0.3f).interpolatedWith (theme::orange, juce::jlimit (0.0f, 1.0f, -grDb / 12.0f));
    }

    /** The squeeze now, the newest column's — and the plate its number stands on, at the strip's left, centred on the
        strip's border. */
    float grNow() const { return shown > 0 ? cols[(size_t) shown - 1].gr : 0.0f; }

    /** The deepest squeeze over the newest `n` columns — what a meter read at 30 Hz shows without missing a hop. */
    float grRecent (int n) const
    {
        float gr = 0.0f;
        for (int k = juce::jmax (0, shown - n); k < shown; ++k)
            gr = juce::jmin (gr, cols[(size_t) k].gr);
        return gr;
    }

    juce::String grText() const { return dbText (grNow() < -0.05f ? (double) grNow() : 0.0); }

    juce::Rectangle<float> grNumberBox() const
    {
        const float w = theme::trackedWidth (grText(), theme::displayFont (textHeight), 0.02f) + 12.0f;
        return juce::Rectangle<float> (w, textHeight + 5.0f).withPosition (grStrip().getX(), stripBorderY() - (textHeight + 5.0f) * 0.5f);
    }

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
        return compCharacterOf (characterP);
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

    /** Where the line is drawn: at its level, -48 dB at the levels' foot, 0 dB at their head. */
    float lineY() const
    {
        const auto lv = levels();
        return juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (thresholdDb()));
    }

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
        shown = amp.compTap.read (cols.data(), (int) cols.size(), &end);
        if (const auto w = amp.compTap.written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    /** A pixel's columns: the loudest of each level, the deepest squeeze, and whether a break runs through them. */
    struct Pixel { float in = 0.0f, out = 0.0f, mid = 0.0f, gr = 0.0f; bool any = false, broken = false; };

    /** The plot's `w` pixels, the newest group at the right. */
    std::vector<Pixel> bins (int w) const
    {
        std::vector<Pixel> px ((size_t) juce::jmax (0, w));
        const auto take = [this] (Pixel& p, int k)
        {
            const auto& c = cols[(size_t) k];
            p.in  = juce::jmax (p.in, c.in);
            p.out = juce::jmax (p.out, c.out);
            p.mid = juce::jmax (p.mid, c.mid);
            p.gr  = juce::jmin (p.gr, c.gr);
            p.broken = p.broken || ! c.joined;
            p.any = true;
        };
        if (shown <= 0)
            return px;
        // Columns by their number since the prepare: cols[0] is column end - shown, a pixel's group its number / per.
        const std::int64_t per = columnsPerPixel (w);
        const auto last = (std::int64_t) end, first = last - shown, newest = (last - 1) / per;
        for (int x = 0; x < w; ++x)
        {
            const auto group = newest - (w - 1 - x);
            for (auto j = juce::jmax (first, group * per); j < juce::jmin (last, (group + 1) * per); ++j)
                take (px[(size_t) x], (int) (j - first));
        }
        return px;
    }

    /** The pixel (from the plot's left) a column `age` columns older than the newest stands at. */
    int pixelOfAge (int age, int w) const
    {
        const auto newest = (std::int64_t) (end - 1u), per = (std::int64_t) columnsPerPixel (w);
        return w - 1 - (int) (newest / per - (newest - age) / per);
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
        juce::Path inEdge, midEdge, outEdge, grLine, inFill, inSegment;
        bool started = false;
        float segmentX = 0.0f, lastX = 0.0f;

        // A BROKEN TRACE (the tap not written a while — COMP standing by, at BLEND 0, the picture away) starts afresh: each
        // run of joined columns its own edges and its own fill under IN.
        const auto closeSegment = [&]
        {
            if (inSegment.isEmpty())
                return;
            inSegment.lineTo (lastX, lv.getBottom());
            inSegment.lineTo (segmentX, lv.getBottom());
            inSegment.closeSubPath();
            inFill.addPath (inSegment);
            inSegment.clear();
        };
        const auto grY = [&] (float gr) { return strip.getY() + 3.0f + juce::jmin (1.0f, (float) (-gr / grRangeDb)) * (strip.getHeight() - 6.0f); };
        const auto pixels = bins (w);
        for (int x = 0; x < w; ++x)
        {
            const auto& p = pixels[(size_t) x];
            if (! p.any)
                continue;
            const float in = p.in, out = p.out, mid = p.mid, gr = p.gr;
            const bool  broken = p.broken;

            const float px  = r.getX() + (float) x + 0.5f;
            const float yIn = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (in)));
            const float yOut = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (out)));
            const float yMid = juce::jlimit (lv.getY(), lv.getBottom(), yOfDb (dbOf (mid)));
            if (! started || broken)
            {
                closeSegment();
                started  = true;
                segmentX = px;
                inSegment.startNewSubPath (px, yIn);
                inEdge.startNewSubPath (px, yIn);
                midEdge.startNewSubPath (px, yMid);
                outEdge.startNewSubPath (px, yOut);
                grLine.startNewSubPath (px, grY (gr));
            }
            else
            {
                inSegment.lineTo (px, yIn);
                inEdge.lineTo (px, yIn);
                midEdge.lineTo (px, yMid);
                outEdge.lineTo (px, yOut);
                grLine.lineTo (px, grY (gr));
            }
            lastX = px;
        }

        closeSegment();
        if (started)
        {
            // In the AMP's ENVELOPE page's own style (scope::EnvelopeView): IN its violet fill under its edge; OUT its
            // orange — as a LINE alone: orange is never a wash on the violet panel.
            using Page = scope::EnvelopeView;
            g.setColour (Page::inColour().withAlpha (Page::inFillAlpha));
            g.fillPath (inFill);   // under its own edge, a run at a time
            g.setColour (Page::inColour());
            g.strokePath (inEdge, juce::PathStrokeType (Page::edgeWidth));
            // AFTER COMP, the compressed path itself — under BEFORE by exactly the squeeze: a solid grey line (the owner,
            // 01.11), apart from the violet fill and the orange.
            g.setColour (afterGrey);
            g.strokePath (midEdge, juce::PathStrokeType (Page::edgeWidth));
            // AFTER COMP + BLEND, what goes on down the chain (before the make-up): the orange line.
            g.setColour (Page::outColour());
            g.strokePath (outEdge, juce::PathStrokeType (Page::edgeWidth));

            // The squeeze, a LINE in its strip, never a fill: each point coloured by its own depth — its height in the
            // strip is its depth, so a gradient down the strip from none to -12 dB colours it so (grColour).
            g.setGradientFill (juce::ColourGradient (grColour (0.0f), 0.0f, grY (0.0f), grColour (-12.0f), 0.0f, grY (-12.0f), false));
            g.strokePath (grLine, juce::PathStrokeType (1.6f));
        }

        // THE THRESHOLD: the lilac line alone; its number is on the rail's grip.
        g.setColour (theme::lilac);
        g.fillRect (lv.getX(), lineY() - 0.75f, lv.getWidth(), 1.5f);

        // The squeeze now, at the strip's left on its border (the owner, 01.11), on a plate of its own so the lines pass
        // under it.
        {
            const auto box = grNumberBox();
            g.setColour (well);
            g.fillRoundedRectangle (box, 2.5f);
            g.setColour (grColour (grNow()).brighter (0.3f));
            theme::drawTracked (g, grText(), box, theme::displayFont (textHeight), 0.02f, juce::Justification::centred);
        }
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
        pressed = false;
    }

    /** A DOUBLE CLICK sets the threshold's default back, as on its rail (the owner, 01.11): one undo step, COMP on. */
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        amp.history.beginGesture();
        thrAtt->setValueAsCompleteGesture (thresholdP.convertFrom0to1 (thresholdP.getDefaultValue()));
        onAtt->setValueAsCompleteGesture (1.0f);
        amp.history.endGesture();
    }

    void visibilityChanged() override      { refresh(); }
    void parentHierarchyChanged() override { refresh(); }

private:
    static double dbOf (float peak) { return peak > 1.0e-6f ? 20.0 * std::log10 ((double) peak) : -120.0; }
    static juce::String dbText (double db) { return juce::String (juce::roundToInt (db)) + " dB"; }

    void timerCallback() override { refresh(); }

    inline static const juce::Colour well { 0xff121218 }, afterGrey { 0xff8e8e9c };

    AmpProcessor& amp;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& characterP;
    juce::RangedAudioParameter& thresholdP;
    juce::RangedAudioParameter& onP;
    std::unique_ptr<juce::ParameterAttachment> amountAtt, characterAtt, onAtt, thrAtt;
    std::vector<core::CompTap::Column> cols;
    int shown = 0;
    std::uint32_t end = 0;   // the count `cols` ends at: cols[shown - 1] is column end - 1
    std::uint32_t lastWritten = 0;
    bool  dragging = false, pressed = false;
    float grab = 0.0f;

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

    core::BassComp::Character character() const { return compCharacterOf (characterP); }

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

/** COMP'S RATIOS (the owner, 01.11): the list its combo offers, gentle to hard. */
inline constexpr double compRatioItems[] { 1.5, 2.0, 3.0, 4.0, 5.0, 7.0, 10.0 };

/** COMP'S RATIO, A COMBO over the IN/OUT square (the owner, 01.11), in the family's boxed cell (VoicingSelector's): the
    ratio as it stands, "4.0:1", 13 px in the hand's orange, and a chevron. The chevron LISTS compRatioItems, the current
    one ticked — a value list, not a block's menu, so no ON over it (onfirst::showList). ONLY THE LIST'S RATIOS, nothing
    typed (the owner, 01.11): a click anywhere on the cell lists them. A pick is one gesture, COMP switched in with it,
    one undo step; a drag in the square steps through the same list. A right click is COMP's menu. */
class CompRatioCombo final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit CompRatioCombo (AmpProcessor& processor) : ratio (processor)
    {
        att = std::make_unique<juce::ParameterAttachment> (ratio.ratioP, [this] (float) { repaint(); });
    }

    static constexpr float textHeight = 13.0f, chevronW = 16.0f;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    double shownRatio() const { return ratio.ratio(); }

    /** The ratio as the cell shows it, as the list names it: "4:1", "1.5:1" (the owner, 01.11). */
    juce::String text() const { return itemText (shownRatio()); }

    /** An item as the list names it: "1.5:1", "4:1". */
    static juce::String itemText (double r)
    {
        // A whole ratio as the parameter holds it may be 3.9999998: whole within a hundredth.
        return (std::abs (r - std::round (r)) < 0.01 ? juce::String (juce::roundToInt (r)) : juce::String (r, 1)) + ":1";
    }

    /** The item the ratio stands at, or -1 off the list. */
    int tickedItem() const
    {
        for (int i = 0; i < (int) std::size (compRatioItems); ++i)
            if (std::abs (shownRatio() - compRatioItems[i]) < 0.005)
                return i;
        return -1;
    }

    /** The list, the current item ticked; item `i` is id i + 1. */
    juce::PopupMenu list() const
    {
        juce::PopupMenu m;
        const int ticked = tickedItem();
        for (int i = 0; i < (int) std::size (compRatioItems); ++i)
        {
            juce::PopupMenu::Item item (itemText (compRatioItems[i]));
            item.itemID   = i + 1;
            item.isTicked = i == ticked;
            m.addItem (item);
        }
        return m;
    }

    /** A ratio set by the hand — picked: clamped to the range, one gesture, COMP switched in, one undo step. */
    void setRatio (double r)
    {
        ratio.take();
        ratio.moveTo (r);
        ratio.letGo();
    }

    void pick (int item)
    {
        if (item >= 0 && item < (int) std::size (compRatioItems))
            setRatio (compRatioItems[item]);
    }

    void openList()
    {
        onfirst::showList (list(), juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                           [safe = juce::Component::SafePointer<CompRatioCombo> (this)] (int choice)
                           {
                               if (safe != nullptr && choice > 0)
                                   safe->pick (choice - 1);
                           });
    }

    /** The cell, and in it the number's part and the chevron's. */
    juce::Rectangle<float> cell() const { return getLocalBounds().toFloat().reduced (0.0f, 1.0f); }
    juce::Rectangle<int>   chevronArea() const { return getLocalBounds().removeFromRight ((int) chevronW); }
    juce::Rectangle<int>   numberArea() const { return getLocalBounds().withTrimmedRight ((int) chevronW); }

    void paint (juce::Graphics& g) override
    {
        const auto c = cell();
        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (c, theme::radiusSm);
        g.setColour (hover ? theme::orange : theme::orange.withAlpha (0.45f));
        g.drawRoundedRectangle (c.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (theme::orange);
        theme::drawTracked (g, text(), numberArea().toFloat(), theme::displayFont (textHeight), 0.02f, juce::Justification::centred);

        // The chevron that says "a list", as VoicingSelector draws it.
        juce::Path v;
        const float cx = (float) chevronArea().getCentreX(), cy = c.getCentreY() - 1.0f;
        v.startNewSubPath (cx - 3.0f, cy);
        v.lineTo (cx, cy + 3.0f);
        v.lineTo (cx + 3.0f, cy);
        g.strokePath (v, juce::PathStrokeType (1.2f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        openList();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        juce::ignoreUnused (e);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setHover (true);
    }
    void mouseExit (const juce::MouseEvent&) override { setHover (false); }

private:
    void setHover (bool h)
    {
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    CompRatio ratio;
    std::unique_ptr<juce::ParameterAttachment> att;
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompRatioCombo)
};

/** COMP'S IN/OUT SQUARE, under the ratio's combo (the TRANSFER painter's square, unity and stroke): the compressor's own
    curve in dB, -48..0 each way — the soft knee at the threshold, bare, the upper segment's slope the ratio.
    A drag up or down steps through the combo's list (compRatioItems) — the ratio whose segment end stands nearest the
    hand; nothing between them (the owner, 01.11) — and the combo follows. A right click is
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

    /** Not a square (the owner, 01.11): the output's -48..0 dB stands on the picture's levels exactly — the curve starts
        at the levels' foot and its knee at the threshold line's height — the input's across the column. */
    juce::Rectangle<float> box() const { return getLocalBounds().toFloat().reduced (1.0f); }

    /** The upper segment's end, where the input reaches 0 dB, taken to `y`: its height says the slope. */
    void beginRatioDrag()   { ratio.take(); }
    void dragRatioTo (float y)
    {
        if (ratio.thresholdDb() > -0.5)
            return;   // a threshold at 0 dB leaves no segment to slope
        double best = compRatioItems[0];
        for (const double r : compRatioItems)
            if (std::abs (yOfRatio (r) - y) < std::abs (yOfRatio (best) - y))
                best = r;
        ratio.moveTo (best);
    }
    void endRatioDrag()     { ratio.letGo(); }

    /** Where the segment's end stands for a ratio — the inverse of dragRatioTo, for a gate. */
    float yOfRatio (double r) const
    {
        return yOf (curve (0.0, ratio.thresholdDb(), r), box());
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = box();
        g.setColour (juce::Colour (0xff121218));
        g.fillRoundedRectangle (b, 3.0f);
        scope::TransferView::paintUnity (g, b);

        const double t = ratio.thresholdDb(), r = ratio.ratio();
        juce::Path p;   // the character's own curve: fed back (OPTO, SQUASH), its knee is the wider
        for (int i = 0; i <= 96; ++i)
        {
            const double in = lowDb + (double) i * (-lowDb / 96.0);
            const juce::Point<float> at { xOf (in, b), yOf (curve (in, t, r), b) };
            if (i == 0)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
        }
        scope::TransferView::strokeCurve (g, p);   // no dot at the threshold: it hid the knee (the owner, 01.11)
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

    /** The compressor's static curve, as the CHARACTER's detector works on it (core::BassComp::staticDeltaDb: core's
        soft knee, kneeDb wide, or the fed-back one). */
    double curve (double in, double t, double r) const
    {
        return in + core::BassComp::staticDeltaDb (ratio.character(), in, t, r);
    }

    CompRatio ratio;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    float grab = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompTransfer)
};

/** THE SQUEEZE AS A COLUMN (the owner, 01.11), between the picture and the ratio: it falls from the picture's very top
    to its foot over the squeeze's own 24 dB (CompGraph::grRangeDb), a tick every 6 dB — halfway down is -12 dB, where it
    turns fully orange; coloured as the squeeze's line —
    light violet at none, fully orange from -12 dB (CompGraph::grColour), a solid, never a wash. It reads the picture's
    columns, so it stands still when the picture does. A right click passes through to the block: COMP's menu. */
class CompGrColumn final : public juce::Component,
                           private juce::Timer
{
public:
    explicit CompGrColumn (CompGraph& picture) : graph (picture)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    static constexpr int designWidth = 8;

    float shownDb() const noexcept { return shown; }

    /** Shows `db` of squeeze (<= 0): what its timer does with the picture's newest columns while it is on screen — and a
        gate's way in, off screen. */
    void showDb (float db)
    {
        if (std::abs (db - shown) > 0.05f)
        {
            shown = db;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        const float scale = (float) CompGraph::grRangeDb;
        const auto yOf = [&] (double db) { return r.getY() + (float) (-db) / scale * r.getHeight(); };

        g.setColour (juce::Colour (0xff121218));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (theme::hair);
        for (double db = -6.0; db > -CompGraph::grRangeDb; db -= 6.0)
            g.fillRect (r.getX(), yOf (db), r.getWidth(), 1.0f);

        if (const float depth = juce::jlimit (0.0f, scale, -shown); depth > 0.05f)
        {
            g.setGradientFill (juce::ColourGradient (CompGraph::grColour (0.0f), 0.0f, yOf (0.0), CompGraph::grColour (-12.0f), 0.0f, yOf (-12.0), false));
            g.fillRoundedRectangle (r.withBottom (yOf (-depth)), 2.0f);
        }
    }

private:
    void timerCallback() override
    {
        if (graph.isShowing())
            showDb (graph.grRecent (7));   // ~33 ms of columns a frame
    }

    CompGraph& graph;
    float shown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompGrColumn)
};

/** THE BASS'S COMP, A BLOCK OF ITS OWN (the owner, 31.10): after the GATE and before OCTAVE on the panel, as in the
    strip. Inside, left to right (the owner, 01.11): its THRESHOLD (a rail in dB, "dB" over it, its number on the grip —
    one control with the line on the picture), its PICTURE, its RATIO (a combo over the IN/OUT square) and its BLEND (a
    rail, 0..100 %, parallel compression) — each control one job. Its CHARACTER a combo on the top border, centred in the
    run after its name, as the AMP's device stands (the owner, 02.11); ATTACK and RELEASE are its menu: a right click
    anywhere on it. Our own DSP, so violet; its arrow in the strip switches it, and standing by it dims in place, still
    in reach. */
class CompBlock final : public BlockFrame
{
public:
    explicit CompBlock (AmpProcessor& processor)
        : BlockFrame ("COMP", Kind::dsp),
          state (processor.apvts),
          thresholdRail (*processor.apvts.getParameter (params::compThreshold), *processor.apvts.getParameter (params::compOn),
                         params::compThresholdMin, params::compThresholdMax, false),
          rail (*processor.apvts.getParameter (params::compAmount), *processor.apvts.getParameter (params::compOn), 0.0f, 1.0f, false),
          graph (processor),
          grColumn (graph),
          ratioCombo (processor),
          transfer (processor)
    {
        shieldsWhenOff = false;
        bass::asBassAmount (rail, "BLEND");   // a fader, its amount in percent under it, a field too
        bass::asBassRail (thresholdRail);      // one control with the graph's line: the plain rail's look
        bass::hint (thresholdRail, "THRESHOLD");
        bass::hint (ratioCombo,    "RATIO");
        bass::hint (rail,          "BLEND: compressed with dry");
        thresholdRail.textForValue = [] (float db) { return juce::String (juce::roundToInt (db)); };   // dB, as "dB" over it says
        thresholdRail.typedScale   = 1.0f;
        const auto undoStep = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        rail.onGesture = thresholdRail.onGesture = undoStep;
        unit.setText ("dB", juce::dontSendNotification);
        unit.setFont (theme::displayFont (CompGraph::textHeight));
        unit.setColour (juce::Label::textColourId, theme::txDim);
        unit.setJustificationType (juce::Justification::centred);
        unit.setBorderSize ({});
        unit.setInterceptsMouseClicks (false, false);   // a right click there is the block's
        for (auto* c : { (juce::Component*) &unit, (juce::Component*) &thresholdRail, (juce::Component*) &graph,
                         (juce::Component*) &grColumn, (juce::Component*) &ratioCombo, (juce::Component*) &transfer, (juce::Component*) &rail,
                         (juce::Component*) &character })
            addAndMakeVisible (*c);

        // THE CHARACTER on the border, set like the block's name beside it (the AMP's device's look); one undo step a pick.
        {
            juce::Array<VoicingSelector::Entry> entries;
            for (const auto& name : params::compCharacters)
                entries.add ({ name.toUpperCase(), 0, false });
            character.setEntries (std::move (entries));
        }
        character.fontHeight = 16.0f;
        character.tracking   = 0.15f;
        character.boxed      = false;
        character.tint       = theme::lilac;
        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *processor.apvts.getParameter (params::compCharacter),
            [this] (float v)
            {
                character.setSelection (juce::jlimit (0, params::compCharacters.size() - 1, juce::roundToInt (v)));
                resized();   // the name on the border is as wide as the name
            });
        character.onPick = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };
        characterAttachment->sendInitialUpdate();
        const auto menu = [this] (juce::Point<int>) { bass::showCompMenu (state, this); };
        thresholdRail.onMenu = rail.onMenu = graph.onMenu = ratioCombo.onMenu = transfer.onMenu = menu;
        attachPower (*processor.apvts.getParameter (params::compOn));
        dimWhenIdle (graph, *processor.apvts.getParameter (params::compAmount));   // at BLEND 0 the picture stands still, dimmed
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            bass::showCompMenu (state, this);
    }

    CompGraph&       picture() noexcept   { return graph; }
    CompGrColumn&    grBar() noexcept     { return grColumn; }
    CompTransfer&    square() noexcept    { return transfer; }
    CompRatioCombo&  ratio() noexcept     { return ratioCombo; }
    GuardRail&       fader() noexcept     { return rail; }            // BLEND
    GuardRail&       threshold() noexcept { return thresholdRail; }
    juce::Label&     thresholdUnit() noexcept { return unit; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        // The character on the top border, sized to its name and centred in the run, the line showing both sides of it.
        {
            const auto slot = borderSlotArea();
            character.setBounds (slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), character.idealWidth()), slot.getHeight()));
            borderSlotUsed = character.getBounds();   // the frame opens the line under it
        }

        auto left = r.removeFromLeft (GuardRail::designWidth);
        r.removeFromLeft (gap);
        rail.setBounds (r.removeFromRight (rail.columnWidth));
        r.removeFromRight (gap);
        auto column = r.removeFromRight (squareMax);
        r.removeFromRight (gap);
        auto bar = r.removeFromRight (CompGrColumn::designWidth);
        r.removeFromRight (barGap);
        graph.setBounds (r);

        // The squeeze's column beside the picture, from the picture's top to its levels' foot.
        const auto pl = graph.plot();
        bar.setY (graph.getY() + juce::roundToInt (pl.getY()));
        bar.setHeight (juce::roundToInt (graph.levels().getBottom() - pl.getY()));
        grColumn.setBounds (bar);

        // The threshold rail, its hand's travel the picture's levels exactly (CompGraph::railInset): it starts where the
        // levels do less the inset, and ends at the picture's foot; "dB" over it, beside the GR strip.
        const int top = juce::roundToInt (graph.levels().getY() - CompGraph::railInset);
        unit.setBounds (left.removeFromTop (top));
        thresholdRail.setBounds (left);

        // The combo over the GR strip's height; the IN/OUT box under it on the levels' own height (CompTransfer::box).
        const int levelsTop = graph.getY() + juce::roundToInt (graph.levels().getY());
        const int levelsFoot = graph.getY() + juce::roundToInt (graph.levels().getBottom());
        ratioCombo.setBounds (column.withHeight (juce::jmin (comboH, levelsTop - column.getY() - 4)));
        transfer.setBounds (column.getX(), levelsTop - 1, column.getWidth(), levelsFoot - levelsTop + 2);
    }

private:
    static constexpr int gap = 10, barGap = 6, squareMax = 70, comboH = 26;

    juce::AudioProcessorValueTreeState& state;
    juce::Label     unit;
    GuardRail       thresholdRail;
    GuardRail       rail;
    CompGraph       graph;
    CompGrColumn    grColumn;
    CompRatioCombo  ratioCombo;
    CompTransfer    transfer;
    VoicingSelector character;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompBlock)
};

} // namespace orbitamp
