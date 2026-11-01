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

    The value runs linearly from `lo` to `hi` over the span the hand fits in WHOLE: half a hand in
    from each end — so the hand never stands half off the rail, and the hand stops where the value
    does (Oleh, 24.10). The block's fixed scale reads the same span (yOfValue).

    A drag switches the guard on: the hand moved is the guard wanted (Oleh, 24.10). `offAtFoot`: the
    rail's END STOP is OFF — the gate's way out — with no zone of its own (the owner, 01.11): the scale
    runs the whole travel, the hand pushed onto the stop under its lowest value parks the guard there,
    reading OFF, and lifted off it the guard is on again at the lowest value.

    Drag to move; a click without a drag, either button, is the guard's menu (Oleh, 24.10). */
class GuardRail final : public juce::Component
{
public:
    GuardRail (juce::RangedAudioParameter& valueParam, juce::RangedAudioParameter& onParam,
               float footValue, float headValue, bool footIsOff)
        : GuardRail (valueParam, &onParam, footValue, headValue, footIsOff) {}

    /** A rail with no switch of its own (the bass's BLEND, the AMP's): a drag moves its value and nothing else. */
    GuardRail (juce::RangedAudioParameter& valueParam, float footValue, float headValue)
        : GuardRail (valueParam, nullptr, footValue, headValue, false) {}

private:
    GuardRail (juce::RangedAudioParameter& valueParam, juce::RangedAudioParameter* onParam,
               float footValue, float headValue, bool footIsOff)
        : valueP (valueParam), onP (onParam), lo (footValue), hi (headValue), offAtFoot (footIsOff)
    {
        valueAtt = std::make_unique<juce::ParameterAttachment> (valueP, [this] (float) { repaint(); });
        if (onP != nullptr)
            onAtt = std::make_unique<juce::ParameterAttachment> (*onP, [this] (float) { repaint(); });

        setRepaintsOnMouseActivity (true);   // the hand comes up under the mouse
    }

public:
    static constexpr int designWidth = 30;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    /** CLICK-TO-TYPE on the grip, where the rail's number is a field: the decimals it is typed with, or -1 where a click
        on the grip is the menu like a click anywhere else (the guards, the effects). */
    int typeDecimals = -1;

    /** A number typed: the value and the switch, one gesture each — what a drag does. */
    void typeValue (float v)
    {
        // A typed number is a gesture of its own — two typed in quick succession are two undo steps, not one burst.
        if (onGesture != nullptr)
            onGesture (true);
        const auto range = valueP.getNormalisableRange();
        valueAtt->setValueAsCompleteGesture (juce::jlimit (juce::jmax (range.start, lo), juce::jmin (range.end, hi), v));
        if (onAtt != nullptr)
            onAtt->setValueAsCompleteGesture (1.0f);
        if (onGesture != nullptr)
            onGesture (false);
    }

    void openTyping()
    {
        const float v = valueP.convertFrom0to1 (valueP.getValue());
        const auto  box = juce::Rectangle<float> (1.0f, yOfValue (v) - meterrail::gripH * 0.5f, (float) getWidth() - 2.0f, meterrail::gripH);
        typing.open (*this, box.toNearestInt(), v * typedScale, gripColour, [this] (float t) { typeValue (t / typedScale); }, typeDecimals, 13.0f);
    }

    bool isTyping() const { return typing.isOpen(); }

    /** For a gate: whose value the rail moves, the size its grip writes it at, and what it writes. */
    const juce::String& paramId() const noexcept { return valueP.paramID; }

    /** The rail's lowest value, as its parameter holds it: where a hand lifted off the end stop sets the guard on. */
    float lowestValue() const { return juce::jmax (valueP.getNormalisableRange().start, lo); }

    juce::String gripText() const { return gripTextOf (valueP.convertFrom0to1 (valueP.getValue())); }

    /** THE GRIP'S LOOK, the guards' by default: a lilac hand writing a trim's decibels at 11 px. A rail whose number is
        an amount (the bass's) says how it is written, at what size, in which colour — and the scale a typed number is
        in (100: typed in percent). */
    std::function<juce::String (float)> textForValue;
    float        gripTextHeight = 11.0f;
    juce::Colour gripColour     = theme::lilac;
    float        typedScale     = 1.0f;

    /** Told when a drag begins (true) and ends (false) — an owner's undo history brackets it. */
    std::function<void (bool)> onGesture;

    /** Where a value stands, in the rail's own coordinates: the hand's centre at it. */
    float yOfValue (float v) const
    {
        const auto s = valueSpan();
        return s.getBottom() - s.getHeight() * (juce::jlimit (lo, hi, v) - lo) / (hi - lo);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto s = scaleArea();
        const bool on = onP == nullptr || onP->getValue() > 0.5f;

        // The slot the hand rides in.
        g.setColour (theme::lilac.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (2.0f, s.getHeight()).withCentre (s.getCentre()));

        const float hoverA = isMouseOverOrDragging (true) ? 1.0f : 0.55f;
        const float alpha  = (on || offAtFoot ? 1.0f : theme::offAlpha) * hoverA;

        const bool  atFoot = offAtFoot && ! on;
        const float v      = valueP.convertFrom0to1 (valueP.getValue());
        const float y      = atFoot ? valueSpan().getBottom() : yOfValue (v);   // parked on the stop, under the lowest value

        meterrail::paintGrip (g, r, y, atFoot ? juce::String ("OFF") : gripTextOf (v),
                              gripColour.withMultipliedAlpha (alpha), dragging, gripTextHeight);
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
            if (onAtt != nullptr)
                onAtt->beginGesture();
            if (onGesture != nullptr)
                onGesture (true);
        }

        if (dragging)
            moveTo (e.position.y);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (swallow)
        {
            swallow = false;
            return;
        }

        // A click moves nothing, as on the columns: it is the guard's menu. Only a drag moves. Where the number is a
        // field, a click on the grip types it.
        if (! dragging)
        {
            if (typeDecimals >= 0
                && std::abs (e.position.y - yOfValue (valueP.convertFrom0to1 (valueP.getValue()))) <= meterrail::gripH * 0.5f)
            {
                openTyping();
                return;
            }

            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }

        valueAtt->endGesture();
        if (onAtt != nullptr)
            onAtt->endGesture();
        if (onGesture != nullptr)
            onGesture (false);

        dragging = false;
        repaint();
    }

private:
    juce::Rectangle<float> scaleArea() const { return getLocalBounds().toFloat().reduced (2.0f); }

    /** The span the hand's centre travels: half a hand in from the head and from the foot. */
    juce::Rectangle<float> valueSpan() const
    {
        auto s = scaleArea();
        s.removeFromTop (meterrail::gripH * 0.5f);
        s.removeFromBottom (meterrail::gripH * 0.5f);
        return s;
    }

    void moveTo (float y)
    {
        const auto span = valueSpan();

        // THE END STOP IS OFF (offAtFoot): the hand pushed onto it — at or under the lowest value's place — parks the
        // guard; the threshold stays where it was, for the way back.
        if (offAtFoot && y >= span.getBottom())
        {
            if (onAtt != nullptr)
                onAtt->setValueAsPartOfGesture (0.0f);
            return;
        }

        // Held to the RAIL's ends, not only the parameter's: the gate's rail stops at -30 while its
        // threshold could go on to -10, and the number ran on past a hand that had stopped (Oleh, 24.10).
        const auto range = valueP.getNormalisableRange();
        const float v    = lo + (span.getBottom() - y) / juce::jmax (1.0f, span.getHeight()) * (hi - lo);

        valueAtt->setValueAsPartOfGesture (juce::jlimit (juce::jmax (range.start, lo), juce::jmin (range.end, hi), v));
        if (onAtt != nullptr)
            onAtt->setValueAsPartOfGesture (1.0f);
    }

    juce::RangedAudioParameter& valueP;
    juce::RangedAudioParameter* onP;
    const float lo, hi;
    const bool  offAtFoot;

    juce::String gripTextOf (float v) const { return textForValue != nullptr ? textForValue (v) : meterrail::trimText (v); }
    std::unique_ptr<juce::ParameterAttachment> valueAtt, onAtt;

    bool dragging = false;
    bool swallow  = false;
    meterrail::GripEditor typing;

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
