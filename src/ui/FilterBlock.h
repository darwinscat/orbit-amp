// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "MeterRail.h"
#include "../PluginProcessor.h"
#include "../core/FilterTap.h"

#include <vector>

namespace orbitamp
{

/** THE ENVELOPE'S PICTURE — "the curve that rides": the envelope filter's response over a log frequency axis, 40 Hz to
    8 kHz, as a violet LINE where the sweep stands now — a hump in BP, a cut in LP — jumping right on a pluck and riding
    back as the note fades, with a short fading trail of where it stood the 160 ms before, so the depth and the speed of
    the quack read at a glance. Under it, lilac, the sweep's range: the corner it starts from (a line, its number over
    it) and how far a fingered note (-18 dBFS at the key, the law's own "every fingered note opens it all the way" at
    the top) takes it at the slider as it stands — a bar along the floor between the two, the range's far end a faint
    tick.

    THE CURVE IS THE STAGE'S: the knobs are the ones the stage is told (AmpProcessor::filterKnobsNow) and the response
    the module's own settled path at the rate it runs (core::envelopeResponseDb); where the sweep stands is the
    module's read-out, through the processor's tap (core::FilterTap). The slider at 0 or the link standing by is a
    flat line: the stage passes the signal untouched there.

    THE START CORNER'S NUMBER IS A FIELD: a click on it, a number typed, Enter — the RANGE whose resting corner is
    nearest (the menu's three), one gesture; under 20 it reads as kHz ("3" is 3K). Anywhere else a click is the
    block's menu. Every number on it is 13 px; the axis's are a reading and do not look like fields.

    It reads the tap on its own timer, and only while it is on screen — the audio thread writes it only then. The timer
    asks isShowing() on every tick, so a parent hidden (which tells no child) or a window put away stops the writing
    too; a still sweep writes nothing, and the picture stands still — as it does standing by or at 0, dimmed with its block
    (the owner, 31.10). */
class FilterCurve final : public juce::Component,
                          private juce::Timer
{
public:
    explicit FilterCurve (AmpProcessor& processor)
        : amp (processor),
          rangeP (*processor.apvts.getParameter (params::filterRange)),
          cols ((size_t) trailColumns + 1)
    {
        rangeAtt = std::make_unique<juce::ParameterAttachment> (rangeP, [this] (float) { repaint(); });
        for (const auto* id : { params::filterOn, params::filterAmount, params::filterMode, params::filterDirection,
                                params::filterResonance, params::filterKeepLows, params::filterKeepHz })
            atts.push_back (std::make_unique<juce::ParameterAttachment> (*processor.apvts.getParameter (id), [this] (float) { repaint(); }));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (hiddenHz);
    }

    ~FilterCurve() override
    {
        stopTimer();
        if (watching)
            amp.filterWatched.store (false, std::memory_order_relaxed);
    }

    static constexpr double loHz = 40.0, hiHz = 8000.0;           // the axis
    static constexpr double topDb = 12.0, bottomDb = -30.0;       // the response's scale
    static constexpr double fingeredDb = -18.0;                   // the note the reach mark is drawn for, at the key
    static constexpr double trailSeconds = 0.16;
    static constexpr int    trailSteps = 8;
    static constexpr int    trailColumns = (int) (trailSeconds / core::FilterTap::hopSeconds + 0.5);
    static constexpr float  textHeight = 13.0f, numberRowH = 20.0f, axisRowH = 18.0f;
    static constexpr int    shownHz = 30, hiddenHz = 5;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    // ---- what it shows -------------------------------------------------------------------------------------------
    felitronics::envfilter::EnvelopeFilterParams knobs() const { return amp.filterKnobsNow(); }
    double restHz() const  { return core::envelopeRestHz (knobs()); }
    double reachHz() const { return core::envelopeReachHz (knobs(), fingeredDb); }

    /** Where the sweep stands now: the tap's newest column, once one has come in since the picture last came on
        screen; the module's own read-out before that. */
    double nowHz() const
    {
        if (shown > 0 && cols[(size_t) shown - 1].index + 1 > writtenAtWatch)
            return (double) cols[(size_t) shown - 1].hz;
        return (double) amp.filterShownCutoffHz();
    }

    double responseDb (double hz, double cutoffHz) const
    {
        return core::envelopeResponseDb (knobs(), cutoffHz, hz, amp.getSampleRate());
    }

    // ---- where things stand ------------------------------------------------------------------------------------
    juce::Rectangle<float> well() const { return getLocalBounds().toFloat().reduced (1.0f); }

    juce::Rectangle<float> plot() const
    {
        auto r = well().reduced (4.0f, 0.0f);
        r.removeFromTop (numberRowH);
        r.removeFromBottom (axisRowH);
        return r;
    }

    float xOfHz (double hz) const
    {
        const auto r = plot();
        return r.getX() + (float) (std::log (juce::jlimit (loHz, hiHz, hz) / loHz) / std::log (hiHz / loHz)) * r.getWidth();
    }

    float yOfDb (double db) const
    {
        const auto r = plot();
        return r.getY() + (float) ((topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb)) * r.getHeight();
    }

    /** The start corner's number, over its line, kept inside the well. */
    juce::Rectangle<float> numberBox() const
    {
        const auto  font = theme::displayFont (textHeight);
        const float w    = theme::trackedWidth (hzText (restHz()), font, 0.02f) + 8.0f;
        const auto  row  = well().reduced (3.0f, 2.0f).removeFromTop (numberRowH - 2.0f);
        const float x    = juce::jlimit (row.getX(), row.getRight() - w, xOfHz (restHz()) - w * 0.5f);
        return { x, row.getY(), w, row.getHeight() };
    }

    // ---- the field ---------------------------------------------------------------------------------------------
    void openStartField()
    {
        field.open (*this, numberBox().toNearestInt().expanded (2, 1), (float) restHz(), theme::lilac,
                    [this] (float typed) { typeStartHz (typed); }, 0, textHeight);
    }

    /** A start corner typed: the RANGE whose resting corner — its low end going up, its high end going down — is
        nearest on the log axis; one gesture. Under 20 it is kHz. */
    void typeStartHz (float typed)
    {
        if (! (typed > 0.0f) || ! std::isfinite (typed))
            return;
        const double hz   = typed < 20.0f ? typed * 1000.0 : (double) typed;
        const bool   down = knobs().direction == felitronics::envfilter::Direction::down;
        int    best  = 0;
        double apart = 1.0e9;
        for (int i = 0; i < 3; ++i)
            if (const double d = std::abs (std::log2 (hz / (double) params::filterRangeHz[i][down ? 1 : 0])); d < apart)
            {
                apart = d;
                best  = i;
            }
        amp.history.beginGesture();   // ONE undo step of its own: two typed in quick succession stay two
        rangeAtt->setValueAsCompleteGesture ((float) best);
        amp.history.endGesture();
    }

    bool fieldOpen() const { return field.isOpen(); }

    // ---- the tap -----------------------------------------------------------------------------------------------
    /** What the tap holds, newest last — the timer's work, and a picture's made off screen. */
    void pull()
    {
        shown = amp.filterTap.read (cols.data(), (int) cols.size());
        if (const auto w = amp.filterTap.written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    /** One tick of the timer: on screen or not — asked every time, not only when JUCE says this component changed,
        since a hidden parent tells no child — and the tap read while it is. */
    void tick() { tick (isShowing()); }

    /** ...the same, told whether it shows — what the timer asks isShowing(), and a gate with no screen says itself. */
    void tick (bool on)
    {
        if (on != watching)
        {
            watching = on;
            if (on)
                writtenAtWatch = amp.filterTap.written();
            amp.filterWatched.store (on, std::memory_order_relaxed);
            startTimerHz (on ? shownHz : hiddenHz);
        }
        else if (on)
        {
            amp.filterWatched.store (true, std::memory_order_relaxed);   // a second editor's picture put away cleared it
        }
        if (on)
            pull();
    }

    bool isWatching() const noexcept { return watching; }
    bool polling() const noexcept    { return isTimerRunning(); }   // hidden too: it has to notice coming back
    meterrail::GripEditor& startField() noexcept { return field; }

    // ---- drawing -----------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto w = well();
        g.setColour (wellColour);
        g.fillRoundedRectangle (w, 3.0f);

        const auto r = plot();

        // The grid: 0 dB, and the decades the axis names.
        g.setColour (theme::hair);
        for (const double hz : { 100.0, 1000.0 })
            g.fillRect (xOfHz (hz) - 0.5f, r.getY(), 1.0f, r.getHeight());
        g.setColour (theme::hair2);
        g.fillRect (r.getX(), yOfDb (0.0) - 0.5f, r.getWidth(), 1.0f);

        // The axis: a reading, dim, not a field.
        const auto font = theme::displayFont (textHeight);
        g.setColour (theme::txDim);
        for (const auto& [hz, text] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1K" } })
        {
            const float tw = theme::trackedWidth (text, font, 0.02f) + 2.0f;
            const juce::Rectangle<float> box (xOfHz (hz) - tw * 0.5f, r.getBottom() + 2.0f, tw, axisRowH - 3.0f);
            theme::drawTracked (g, text, box, font, 0.02f, juce::Justification::centred);
        }

        // THE RANGE, lilac: the far end a faint tick, the travel a bar along the floor, the start a line.
        const auto   k     = knobs();
        const bool   up    = k.direction == felitronics::envfilter::Direction::up;
        const double rest  = core::envelopeRestHz (k), reach = core::envelopeReachHz (k, fingeredDb);
        const double farHz = up ? (double) k.sweepHiHz : (double) k.sweepLoHz;
        const float  xRest = xOfHz (rest), xReach = xOfHz (reach), xFar = xOfHz (farHz);
        const float  floorY = r.getBottom() - 3.0f;

        g.setColour (theme::lilac.withAlpha (0.35f));
        g.fillRect (xFar - 0.5f, floorY - 6.0f, 1.0f, 6.0f);
        g.setColour (theme::lilac);
        g.fillRect (juce::jmin (xRest, xReach), floorY - 0.75f, std::abs (xReach - xRest), 1.5f);
        g.fillRect (xReach - 0.75f, floorY - 6.0f, 1.5f, 6.0f);
        {
            const float dash[] = { 3.0f, 3.0f };
            g.setColour (theme::lilac.withAlpha (0.6f));
            g.drawDashedLine (juce::Line<float> (xReach, r.getY(), xReach, floorY - 6.0f), dash, 2, 1.0f);
        }
        g.setColour (theme::lilac);
        g.fillRect (xRest - 0.5f, r.getY(), 1.0f, r.getHeight());

        // THE CURVE, violet lines: the trail first, fading, then where it stands now over it.
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.expanded (0.0f, 1.0f).toNearestInt());
            if (shown > 0 && cols[(size_t) shown - 1].index + 1 > writtenAtWatch)
            {
                const auto newest = cols[(size_t) shown - 1].index;
                for (int s = trailSteps; s >= 1; --s)
                {
                    const auto want = newest - (std::uint32_t) (s * trailColumns / trailSteps);
                    for (int i = 0; i < shown; ++i)
                        if (cols[(size_t) i].index == want && cols[(size_t) i].index + 1 > writtenAtWatch)
                        {
                            g.setColour (theme::violet.withAlpha (0.55f * (1.0f - (float) s / (float) (trailSteps + 1))));
                            g.strokePath (curve ((double) cols[(size_t) i].hz), juce::PathStrokeType (1.0f));
                            break;
                        }
                }
            }
            g.setColour (theme::violet.withAlpha (0.95f));
            g.strokePath (curve (nowHz()), juce::PathStrokeType (1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // The start corner's number: a field — lilac, brighter under the mouse.
        if (! field.isOpen())
        {
            const auto box = numberBox();
            g.setColour (hovered ? theme::tx : theme::lilac);
            theme::drawTracked (g, hzText (rest), box, font, 0.02f, juce::Justification::centred);
            if (hovered)
            {
                g.setColour (theme::lilac.withAlpha (0.6f));
                g.fillRect (box.getX() + 3.0f, box.getBottom() - 2.0f, box.getWidth() - 6.0f, 1.0f);
            }
        }
    }

    // ---- the hand ----------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && numberBox().contains (e.position))
        {
            openStartField();
            return;
        }
        if (onMenu != nullptr)
            onMenu (e.getScreenPosition());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = numberBox().contains (e.position);
        if (over != hovered)
        {
            hovered = over;
            setMouseCursor (over ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered)
        {
            hovered = false;
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            repaint();
        }
    }

    void visibilityChanged() override      { tick(); }
    void parentHierarchyChanged() override { tick(); }

private:
    static juce::String hzText (double hz)
    {
        if (hz < 1000.0)
            return juce::String (juce::roundToInt (hz));
        const double k = hz / 1000.0;
        return (std::abs (k - std::round (k)) < 0.05 ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + "K";
    }

    /** The response with the sweep at `cutoffHz`, a point every pixel across the plot. */
    juce::Path curve (double cutoffHz) const
    {
        const auto r = plot();
        const auto k = knobs();
        const double fs = amp.getSampleRate();
        juce::Path p;
        const int n = juce::jmax (2, (int) r.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const double hz = loHz * std::pow (hiHz / loHz, (double) i / (double) n);
            const juce::Point<float> at { r.getX() + r.getWidth() * (float) i / (float) n,
                                          yOfDb (core::envelopeResponseDb (k, cutoffHz, hz, fs)) };
            if (i == 0)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
        }
        return p;
    }

    void timerCallback() override { tick(); }

    inline static const juce::Colour wellColour { 0xff121218 };

    AmpProcessor& amp;
    juce::RangedAudioParameter& rangeP;
    std::unique_ptr<juce::ParameterAttachment> rangeAtt;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::vector<core::FilterTap::Column> cols;
    meterrail::GripEditor field;
    int  shown = 0;
    std::uint32_t lastWritten = 0, writtenAtWatch = 0;
    bool watching = false, hovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterCurve)
};

/** THE BASS'S ENVELOPE FILTER, A BLOCK OF ITS OWN (the owner, 31.10: "ENVELOPE" on the face — the parameters keep
    their filter_* ids): after OCTAVE at the head of the AMP's row, as in the strip. Its SLIDER at the left — the rail
    over its one amount, 0..1 (felitronics::envfilter's law: the mix, then the sensitivity), the bass's rail (orange
    grip, percent, a click on it types it: bass::asBassRail) — and its PICTURE right of it (FilterCurve), which shows
    it at work: no lamp. MODE, DIRECTION, RESONANCE, RANGE, KEEP LOWS, ATTACK
    and RELEASE are its menu, on a click anywhere on it but the rail's drag and the start corner's number. Our own DSP,
    so violet; its arrow in the strip switches it, and standing by it dims in place, still in reach. */
class FilterBlock final : public BlockFrame
{
public:
    explicit FilterBlock (AmpProcessor& processor)
        : BlockFrame ("ENVELOPE", Kind::dsp),
          state (processor.apvts),
          rail (*processor.apvts.getParameter (params::filterAmount), *processor.apvts.getParameter (params::filterOn), 0.0f, 1.0f, false),
          curve (processor)
    {
        shieldsWhenOff = false;
        bass::asBassRail (rail);   // the amount in percent, a field too
        rail.onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };   // one undo step a drag, as COMP's
        for (auto* c : { (juce::Component*) &rail, (juce::Component*) &curve })
            addAndMakeVisible (*c);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        const auto menu = [this] (juce::Point<int>) { bass::showFilterMenu (state, this); };
        rail.onMenu = curve.onMenu = menu;
        attachPower (*processor.apvts.getParameter (params::filterOn));
    }

    void mouseDown (const juce::MouseEvent&) override { bass::showFilterMenu (state, this); }

    FilterCurve& picture() noexcept { return curve; }
    GuardRail&   fader() noexcept   { return rail; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        rail.setBounds (r.removeFromLeft (GuardRail::designWidth));
        r.removeFromLeft (gap);
        curve.setBounds (r);
    }

private:
    static constexpr int gap = 10;

    juce::AudioProcessorValueTreeState& state;
    GuardRail   rail;
    FilterCurve curve;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterBlock)
};

} // namespace orbitamp
