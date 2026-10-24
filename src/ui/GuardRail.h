// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MeterRail.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace orbitamp
{

/** A GUARD'S RAIL — the small slider in a guard's narrow block (GuardBlock.h), with the guard's one
    hand on it: the gate's threshold, the limiter's ceiling. Lilac, the thresholds' colour, so it is
    never taken for a column's orange trim.

    The value runs linearly from `lo` at the rail's foot to `hi` at its head, over the rail less the
    columns' own inset.

    `offAtFoot`: the foot of the rail is OFF — the gate's way out. Dragged into it, the guard's
    switch goes off; dragged out of it, on. Without it the switch only dims the rail (the limiter's
    is its arrow in the strip).

    While it is dragged, the guard's own ruler is summoned beside it (onDrag), the way the columns
    summon theirs.

    Drag to move — a click alone moves nothing — double-click for the guard's home, right click for
    its menu. */
class GuardRail final : public juce::Component
{
public:
    GuardRail (juce::RangedAudioParameter& valueParam, juce::RangedAudioParameter& onParam,
               float footValue, float headValue, bool footIsOff, float homeValue)
        : valueP (valueParam), onP (onParam), lo (footValue), hi (headValue),
          offAtFoot (footIsOff), home (homeValue)
    {
        valueAtt = std::make_unique<juce::ParameterAttachment> (valueP, [this] (float) { repaint(); });
        onAtt    = std::make_unique<juce::ParameterAttachment> (onP,    [this] (float) { repaint(); });

        setRepaintsOnMouseActivity (true);   // the hand comes up under the mouse
    }

    static constexpr int designWidth = 30;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    /** Held and let go: the editor shows the guard's ruler while it is dragged. */
    std::function<void (bool)> onDrag;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto s = scaleArea();
        const bool on = onP.getValue() > 0.5f;

        // The slot the hand rides in.
        g.setColour (theme::lilac.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (2.0f, s.getHeight()).withCentre (s.getCentre()));

        if (offAtFoot)
        {
            const auto foot = s.withTop (s.getBottom() - offH);
            g.setColour (theme::lilac.withAlpha (0.10f));
            g.fillRoundedRectangle (foot, 2.0f);
        }

        const float hoverA = isMouseOverOrDragging (true) ? 1.0f : 0.55f;
        const float alpha  = (on || offAtFoot ? 1.0f : theme::offAlpha) * hoverA;

        const bool  atFoot = offAtFoot && ! on;
        const float v      = valueP.convertFrom0to1 (valueP.getValue());
        const float y      = atFoot ? s.getBottom() - offH * 0.5f : yOf (v);

        meterrail::paintGrip (g, r, y, atFoot ? juce::String ("OFF") : meterrail::trimText (v),
                              theme::lilac.withMultipliedAlpha (alpha), dragging);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        swallow = e.mods.isPopupMenu();

        if (swallow)
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }

        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (swallow)
            return;

        if (! dragging && e.getDistanceFromDragStart() > 3)
        {
            dragging = true;
            valueAtt->beginGesture();
            onAtt->beginGesture();

            if (onDrag != nullptr)
                onDrag (true);
        }

        if (dragging)
            moveTo (e.position.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (swallow)
        {
            swallow = false;
            return;
        }

        // A plain click moves nothing, as on the columns: only a drag does (Oleh, 24.10).
        if (! dragging)
            return;

        valueAtt->endGesture();
        onAtt->endGesture();

        if (onDrag != nullptr)
            onDrag (false);

        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        swallow = true;
        valueAtt->setValueAsCompleteGesture (home);
        if (offAtFoot)
            onAtt->setValueAsCompleteGesture (1.0f);
    }

private:
    static constexpr float offH = 20.0f;   // the OFF stop at the foot, where there is one

    /** The columns' own inset — and the ruler's, so a value here and on the ruler share a height. */
    juce::Rectangle<float> scaleArea() const { return getLocalBounds().toFloat().reduced (2.0f); }

    float yOf (float v) const
    {
        const auto s = scaleArea();
        const float y = s.getBottom() - s.getHeight() * (juce::jlimit (lo, hi, v) - lo) / (hi - lo);

        // Above the OFF stop, always: a threshold low enough to sit in it would read as OFF.
        return offAtFoot ? juce::jmin (y, s.getBottom() - offH - meterrail::gripH * 0.5f) : y;
    }

    void moveTo (float y)
    {
        const auto s = scaleArea();

        if (offAtFoot && y > s.getBottom() - offH)
        {
            onAtt->setValueAsPartOfGesture (0.0f);
            return;
        }

        const auto range = valueP.getNormalisableRange();
        const float v    = lo + (s.getBottom() - y) / juce::jmax (1.0f, s.getHeight()) * (hi - lo);

        valueAtt->setValueAsPartOfGesture (juce::jlimit (range.start, range.end, v));
        if (offAtFoot)
            onAtt->setValueAsPartOfGesture (1.0f);
    }

    juce::RangedAudioParameter& valueP;
    juce::RangedAudioParameter& onP;
    const float lo, hi;
    const bool  offAtFoot;
    const float home;
    std::unique_ptr<juce::ParameterAttachment> valueAtt, onAtt;

    bool dragging = false;
    bool swallow  = false;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardRail)
};

/** A GUARD'S LAMP — the plate beside its rail in a guard's block that lights while the guard
    presses. It floods solid toward the violet with the press (orange is a line, never a wash), and
    keeps the orange "worked while you were away" dot until it is looked at. A guard switched off is
    dark. A click is a look: the dot clears and the guard's menu opens. With no name it is a plain
    plate — the block over it says whose it is. */
class GuardLamp final : public juce::Component,
                        private juce::Timer
{
public:
    GuardLamp (juce::String lampName, juce::RangedAudioParameter& onParam,
               std::function<float()> pressDepth, std::atomic<bool>& workedLatch)
        : name (std::move (lampName)), onP (onParam), depth (std::move (pressDepth)), worked (workedLatch)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (20);   // the light's breath; it repaints only when something moved
    }

    static constexpr int designHeight = 24;

    std::function<void (juce::Point<int> screenPos)> onClick;

    void paint (juce::Graphics& g) override
    {
        const auto r  = getLocalBounds().toFloat().reduced (0.75f);
        const bool on = onP.getValue() > 0.5f;
        const float a = on ? 1.0f : theme::offAlpha;

        g.setColour (juce::Colour (0xff1b1b22).interpolatedWith (theme::violet, shownDepth));
        g.fillRoundedRectangle (r, 3.0f);

        g.setColour (theme::violet.withAlpha (0.8f * a));
        g.drawRoundedRectangle (r, 3.0f, 1.5f);

        if (name.isNotEmpty())
        {
            g.setColour ((on ? theme::tx : theme::txDim).interpolatedWith (juce::Colours::white, shownDepth)
                             .withAlpha (a));
            theme::drawTracked (g, name, r, theme::displayFont (10.0f), 0.12f, juce::Justification::centred);
        }

        if (shownDot)
        {
            // In the corner of a plate; at the head of a narrow one, centred.
            const float x = r.getWidth() < 30.0f ? r.getCentreX() - 2.25f : r.getRight() - 8.0f;
            g.setColour (theme::orange);
            g.fillEllipse (x, r.getY() + 3.0f, 4.5f, 4.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        worked.store (false);
        if (onClick != nullptr)
            onClick (e.getScreenPosition());
    }

private:
    void timerCallback() override
    {
        // Only a WORKING guard lights: one standing by presses on nothing. The switch is watched too —
        // a guard switched off while it pressed on nothing changes no depth, and must still go dark.
        const bool  on  = onP.getValue() > 0.5f;
        const float d   = on ? juce::jlimit (0.0f, 1.0f, depth()) : 0.0f;
        const bool  dot = worked.load();

        if (std::abs (d - shownDepth) > 0.02f || dot != shownDot || on != shownOn)
        {
            shownDepth = d;
            shownDot   = dot;
            shownOn    = on;
            repaint();
        }
    }

    const juce::String name;
    juce::RangedAudioParameter& onP;
    std::function<float()> depth;
    std::atomic<bool>& worked;

    float shownDepth = 0.0f;
    bool  shownDot   = false;
    bool  shownOn    = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardLamp)
};

} // namespace orbitamp
