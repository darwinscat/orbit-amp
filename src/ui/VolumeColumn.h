// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "MeterRail.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** A VOLUME COLUMN — the level, and the hand on it. One of these stands at each end of the panel,
    mirrored, and they are the same object because they were always the same idea.

    They were two classes, eight hundred lines between them, and neither was really about volume:
    the IN column also carried the gate's threshold and the OUT column the limiter's ceiling, each
    as a second runner on the same rail. A drag moved whichever runner was nearer the grab. Both
    guards have their own consoles now, opened from their own arrows, and what is left is what a
    column is:

        the level on the family rail, with its peak hold and its latched clip cap,
        and the trim riding the same scale as tabby's hollow sliding frame.

    Drag the frame; double-click anywhere for unity; double-click the frame itself to type a
    number. While it is in hand, the peak-hold's FUTURE walks the scale as a dashed line — where
    the last phrase would land at the new gain, which is what puts a fader inside its own meter.

    A click on a red clip cap clears it. Nothing else here answers to a click, because there is
    nothing else here. */
class VolumeColumn final : public juce::Component,
                          private juce::Timer
{
public:
    /** Which end of the panel this one stands at. It decides two things and no more: which way
        the rail's ticks face, and what the column is called. */
    enum class Side { in, out };

    VolumeColumn (Side columnSide, const std::atomic<float>& levelSource,
                  std::atomic<bool>& clipLatch, juce::RangedAudioParameter& trimParam)
        : side (columnSide), levelSource (levelSource), clip (clipLatch), trimP (trimParam)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { repaint(); });

        setRepaintsOnMouseActivity (true);   // the runner fades in under the mouse
        startTimerHz (30);
    }

    static constexpr int designWidth = 38;

    /** WORKING or standing by. A link standing by is not applying its volume, so its column must
        not go on looking like it is: it dims, its meter FREEZES where it stood, and it stops
        answering the mouse. Freezing rather than falling to silence is the point — a needle that
        keeps twitching on a dark face is the one exception this whole rework set out to delete.

        Only matters when a standing-by link keeps its place on the panel; under the other setting
        the column is not there at all. */
    void setLive (bool nowLive)
    {
        if (live == nowLive)
            return;

        live = nowLive;

        // Going dark under a hand that is still down: the mouse stops being intercepted, so no
        // mouseUp will ever arrive and the automation gesture would stay open for ever.
        if (! live && dragging)
        {
            trim->endGesture();
            dragging = false;

            if (onTrimDrag != nullptr)
                onTrimDrag (false);
        }

        setAlpha (live ? 1.0f : theme::offAlpha);
        setInterceptsMouseClicks (live, live);

        if (live)
            startTimerHz (30);
        else
            stopTimer();      // the reading stops where it stood

        repaint();
    }

    /** The runner entered or left the hand — the editor slides the drag ruler out beside us. */
    std::function<void (bool)> onTrimDrag;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto col = columnArea();

        // The family meter rail — dB-anchored, white hold, clip cap — wearing the whole
        // thermometer: dark violet floor, corporate violet, the traffic middle, orange, red.
        meterrail::paintFill (g, col, dbToY (col, levelDb), 0.0f, true);

        if (holdDb > floorDb + 0.5f)
            meterrail::paintHold (g, col, dbToY (col, holdDb));

        meterrail::paintClipCap (g, col, clip.load());

        // The ghost: while the trim is in hand, where the last phrase's peak WOULD land at the
        // new gain, walking the scale as a dashed line. The consequence, on the grid.
        if (dragging)
        {
            const float delta = trimP.convertFrom0to1 (trimP.getValue()) - trimStartDb;

            if (holdAtGrab > floorDb + 0.5f && std::abs (delta) > 0.05f)
            {
                const float gy = dbToY (col, holdAtGrab + delta);
                const float dashes[] = { 4.0f, 3.0f };
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawDashedLine ({ col.getX(), gy, col.getRight(), gy }, dashes, 2, 1.4f);
            }
        }

        // No ticks on the window's edge — the marks live on the inner side only, which is the one
        // thing that makes these two mirror images rather than copies.
        const bool ticksLeft  = side == Side::out;
        const bool ticksRight = side == Side::in;

        meterrail::paintDbScale (g, r.reduced (0.0f, 2.0f),
                                 [&] (float db) { return dbToY (col, db); }, floorDb,
                                 ticksLeft, ticksRight, dbToY (col, levelDb));
        meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (col, 0.0f), ticksLeft, ticksRight);

        // The runner lives half-ghosted until the hand comes near: the column is a METER first,
        // and its one control surfaces when wanted.
        const float hoverA = isMouseOverOrDragging (true) ? 1.0f : 0.3f;
        const float v = trimP.convertFrom0to1 (trimP.getValue());

        meterrail::paintGrip (g, r, trimY (col, v), meterrail::trimText (v),
                              theme::orange.withMultipliedAlpha (hoverA), dragging);

        meterrail::paintName (g, col, side == Side::in ? "IN" : "OUT");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // A click anywhere while the grip editor is open COMMITS it first — the field convention.
        if (gripEd.isOpen())
        {
            gripEd.takeAndHide();
            swallow = true;
            return;
        }

        // The reset click must not turn into a fader drag.
        if (clip.load() && e.position.y <= scaleArea().getY() + 5.0f)
        {
            clip.store (false);
            swallow = true;
            repaint();
            return;
        }

        swallow  = false;
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // A click that meant something else — clearing the clip cap, closing the grip editor —
        // must not become a fader drag when the hand moves before it lifts. mouseUp swallows the
        // release, so a gesture begun here would never have been ended either.
        if (swallow)
            return;

        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;
            trim->beginGesture();

            // The ghost's anchors: the trim AND the hold as they stood when the hand closed — a
            // hold that kept decaying under the drag would melt the ghost mid-thought.
            trimStartDb = trimP.convertFrom0to1 (trimP.getValue());
            holdAtGrab  = holdDb;

            if (onTrimDrag != nullptr)
                onTrimDrag (true);
        }

        if (dragging)
            trim->setValueAsPartOfGesture (trimFromY (scaleArea(), e.position.y));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (swallow)
        {
            swallow = false;
            return;
        }

        if (dragging)
        {
            trim->endGesture();

            if (onTrimDrag != nullptr)
                onTrimDrag (false);

            dragging = false;
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // The convention: DOUBLE-click the grip to type its number; a double anywhere else sends
        // the runner home. The release must not read as another click.
        swallow = true;

        if (gripRect().contains (e.position))
        {
            gripEd.open (*this, gripRect().toNearestInt(),
                         trimP.convertFrom0to1 (trimP.getValue()), theme::orange,
                         [this] (float v) { trim->setValueAsCompleteGesture (v); });
            return;
        }

        trim->setValueAsCompleteGesture (0.0f);
    }

    juce::Rectangle<float> gripRect() const
    {
        const float y = trimY (scaleArea(), trimP.convertFrom0to1 (trimP.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
    }

private:
    void timerCallback() override
    {
        // Peak-style ballistics: jump up instantly, fall at a readable rate.
        const float now = levelSource.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);

        // The hold line: grabs every new maximum, keeps it steady for ~2 s, then lets go slowly —
        // long enough to read after the phrase, never stale enough to lie about the next one.
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

    juce::Rectangle<float> scaleArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f);
    }

    /** The COLUMN, narrower than the rail and not moved: cut hard on the WINDOW's edge and a
        little on the inner side, so the runner keeps the full rail width and sticks out toward
        the edge. Which edge is the window's is the whole of the mirror. */
    juce::Rectangle<float> columnArea() const
    {
        const auto r = scaleArea();

        return side == Side::in ? r.withTrimmedLeft (10.0f).withTrimmedRight (4.0f)
                                : r.withTrimmedRight (10.0f).withTrimmedLeft (4.0f);
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float trimY (juce::Rectangle<float> r, float trimDb) const
    {
        return r.getCentreY() - trimDb / params::inTrimRangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    float trimFromY (juce::Rectangle<float> r, float y) const
    {
        return juce::jlimit (-params::inTrimRangeDb, params::inTrimRangeDb,
                             (r.getCentreY() - y) / juce::jmax (1.0f, r.getHeight() * 0.5f - 6.0f)
                                 * params::inTrimRangeDb);
    }

    static constexpr float floorDb            = -80.0f;
    static constexpr float releasePerTick     = 1.4f;   // ~42 dB/s at 30 Hz
    static constexpr int   holdTicks          = 60;     // 2 s of steady hold...
    static constexpr float holdReleasePerTick = 0.8f;   // ...then ~24 dB/s down

    const Side side;
    const std::atomic<float>& levelSource;
    std::atomic<bool>&        clip;
    juce::RangedAudioParameter& trimP;
    std::unique_ptr<juce::ParameterAttachment> trim;

    meterrail::GripEditor gripEd;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging = false;
    bool  swallow  = false;
    float trimStartDb = 0.0f;
    float holdAtGrab  = -90.0f;
    bool  live        = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VolumeColumn)
};

} // namespace orbitamp
