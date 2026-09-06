// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "MeterRail.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace orbitamp
{

/** The IN column: the input's level, and the input's volume. One rail, ONE hand.

    It used to carry the gate as well — a lilac threshold caret beside the orange trim caret, both
    on this scale, and a drag moved whichever was nearer the grab. Two hands on one rail told apart
    by proximity, which is not something a player can aim; the class said so itself and it stayed
    true for a year. The gate has its own console now, opened from its own arrow in the strip.

    What is left is what a column is: the level on the family rail with its hold and its clip cap,
    and the trim riding it as tabby's hollow sliding frame. Drag it, double-click it for unity,
    double-click its grip to type a number. */
class GateStrip final : public juce::Component,
                        private juce::Timer
{
public:
    GateStrip (const std::atomic<float>& keyDbSource, std::atomic<bool>& clipLatch,
               juce::RangedAudioParameter& trimParam)
        : keyDb (keyDbSource), clip (clipLatch), trimP (trimParam)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { repaint(); });

        setRepaintsOnMouseActivity (true);   // the runner fades in under the mouse
        startTimerHz (30);
    }

    /** The trim runner entered/left the hand — the editor slides the drag ruler out beside us. */
    std::function<void (bool)> onTrimDrag;

    static constexpr int designWidth = 38;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto inCol = columnArea();

        // ---- IN: the family meter rail — dB-anchored, white hold, clip cap — wearing the whole
        //      thermometer (the GAIN dial's ramp on a pole): dark violet floor, corporate violet,
        //      the traffic middle, orange, red. The gate's pressure lives on the BADGE (red by
        //      depth); draining the colour here hit the eyes and retired. ----
        meterrail::paintFill (g, inCol, dbToY (inCol, levelDb), 0.0f, true);

        if (holdDb > floorDb + 0.5f)
            meterrail::paintHold (g, inCol, dbToY (inCol, holdDb));

        meterrail::paintClipCap (g, inCol, clip.load());

        // ---- the ghost: while the trim is in hand, the peak-hold's FUTURE — where the last
        //      phrase's peak will land with the new gain — walks the scale as a dashed line.
        //      This is what puts the runner IN the column's grid: you see the consequence. ----
        if (dragging)
        {
            const float delta = trimP.convertFrom0to1 (trimP.getValue()) - trimStartDb;
            if (holdAtGrab > floorDb + 0.5f && std::abs (delta) > 0.05f)
            {
                const float gy = dbToY (inCol, holdAtGrab + delta);
                const float dashes[] = { 4.0f, 3.0f };
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawDashedLine ({ inCol.getX(), gy, inCol.getRight(), gy }, dashes, 2, 1.4f);
            }
        }

        // The runner lives half-ghosted until the hand comes near: the column is a METER first,
        // and its one control surfaces when wanted.
        const float hoverA = isMouseOverOrDragging (true) ? 1.0f : 0.3f;

        // ---- the trim: tabby's hollow sliding frame with its sight, riding the whole rail ----
        {
            const auto area = scaleArea();
            // No ticks on the window's edge — the marks live on the inner side only.
            meterrail::paintDbScale (g, r.reduced (0.0f, 2.0f),
                                     [&] (float db) { return dbToY (area, db); }, floorDb,
                                     false, true, dbToY (area, levelDb));
            meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (area, 0.0f), false, true);
            const float v = trimP.convertFrom0to1 (trimP.getValue());
            meterrail::paintGrip (g, r, trimY (area, v), meterrail::trimText (v),
                                  theme::orange.withMultipliedAlpha (hoverA),
                                  dragging);
        }

        meterrail::paintName (g, inCol, "IN");
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        // A click anywhere while the grip editor is open COMMITS it first — the field convention.
        // Without this the abandoned editor sat at its old spot wearing a grip's face.
        if (gripEd.isOpen())
        {
            gripEd.takeAndHide();
            swallowUp = true;
            return;
        }

        if (clip.load() && e.position.y <= scaleArea().getY() + 5.0f)
        {
            clip.store (false);
            swallowUp = true;
            repaint();
            return;
        }

        swallowUp = false;
        dragging  = false;
        pressY    = e.position.y;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // The convention: DOUBLE-click a grip to type its number; a double anywhere else sends
        // the volume runner home. The release must not read as another click.
        swallowUp = true;

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
        const auto area = scaleArea();
        const float y   = trimY (area, trimP.convertFrom0to1 (trimP.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;

            trim->beginGesture();

            // The ghost's anchors: the trim AND the hold as they stood when the hand closed —
            // a hold that keeps decaying under the drag would melt the ghost mid-thought.
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
        if (swallowUp)
        {
            swallowUp = false;
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

private:
    void timerCallback() override
    {
        // Peak-style ballistics: jump up instantly, fall at a readable rate.
        const float now = keyDb.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);

        // The hold line: grabs every new maximum, keeps it steady for ~2 s, then lets go slowly —
        // long enough to read after the phrase, never stale enough to lie about the next one.
        if (now >= holdDb)
        {
            holdDb   = now;
            holdAge  = 0;
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

    /** The COLUMN, narrower than the rail it stands in and not moved: cut hard on the window's
        edge (left, for IN) and a little on the inner side — the grips keep the full rail width,
        so they now visibly stick out toward the edge, a handle past its slot. */
    juce::Rectangle<float> columnArea() const
    {
        return scaleArea().withTrimmedLeft (10.0f).withTrimmedRight (4.0f);
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float yToDb (juce::Rectangle<float> r, float y) const
    {
        return floorDb
             + juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / juce::jmax (1.0f, r.getHeight())) * -floorDb;
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

    const std::atomic<float>& keyDb;
    std::atomic<bool>&        clip;
    juce::RangedAudioParameter& trimP;
    std::unique_ptr<juce::ParameterAttachment> trim;

    meterrail::GripEditor gripEd;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging    = false;
    float trimStartDb = 0.0f;
    float holdAtGrab  = -90.0f;
    bool  swallowUp   = false;
    float pressY      = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateStrip)
};

} // namespace orbitamp
