// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "FaderPaint.h"
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

    Drag to move; a click without a drag, either button, is the guard's menu (Oleh, 24.10). A DOUBLE CLICK sets the
    default back (the owner, 01.11) — so a single click waits out the double click's time before its menu or field. */
class GuardRail final : public juce::Component,
                        public juce::SettableTooltipClient
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
        if (faderLook)
        {
            if (numberShown)
                openTypingIn (*this, numberRow().toNearestInt());   // a fader's number under it is its field
            return;
        }

        const float v = valueP.convertFrom0to1 (valueP.getValue());
        const auto  box = juce::Rectangle<float> (1.0f, yOfValue (v) - meterrail::gripH * 0.5f, (float) getWidth() - 2.0f, meterrail::gripH);
        openTypingIn (*this, box.toNearestInt());
    }

    /** The field opened over `box` in `host` — the rail itself, or where its number is written outside it (a guard's, in
        its lamp: GuardBlock). The same field, the same one undo step a number typed. */
    void openTypingIn (juce::Component& host, juce::Rectangle<int> box)
    {
        if (typing.inited && typing.ed.getParentComponent() != &host)
            host.addChildComponent (typing.ed);
        const float v = valueP.convertFrom0to1 (valueP.getValue());
        typing.open (host, box, v * typedScale, faderLook ? faderAccent : gripColour, [this] (float t) { typeValue (t / typedScale); },
                     typeDecimals, 13.0f);
        if (! hearsField)
        {
            typing.ed.addMouseListener (this, false);   // a drag begun on the field moves the hand (mouseDrag)
            hearsField = true;
        }
    }

    bool isTyping() const { return typing.isOpen(); }

    /** The parameter's default back, as a number typed: one undo step, the switch on. */
    void resetToDefault() { typeValue (valueP.convertFrom0to1 (valueP.getDefaultValue())); }

    /** For a gate: whose value the rail moves, the size its grip writes it at, and what it writes. */
    const juce::String& paramId() const noexcept { return valueP.paramID; }

    /** The rail's lowest value, as its parameter holds it: where a hand lifted off the end stop sets the guard on. */
    float lowestValue() const { return juce::jmax (valueP.getNormalisableRange().start, lo); }

    juce::String gripText() const { return gripTextOf (valueP.convertFrom0to1 (valueP.getValue())); }

    /** The number as the rail shows it: "OFF" on the gate's end stop, the value otherwise. */
    juce::String numberText() const { return parkedAtFoot() ? juce::String ("OFF") : gripText(); }

    /** THE FADER'S LOOK (the owner, 01.11) — the guitar DELAY's and REVERB's MIX (Knob::setFader), one painter
        (FaderPaint.h): a rail of the bezel's black, the amount flooding it from the foot in `accent`, the channel lever's
        grip with the accent drawn through it; `name` over it (none: no row) and its number under it (`numberUnder`; a
        guard writes its number in its lamp instead), both at 13 px inside the rail's own bounds, the number the
        click-to-type field. VIOLET for an amount, LILAC for a guard. Every behaviour stays the rail's, but a drag moves
        the hand BY the drag, never to the mouse — so a drag begun on the name, on the number or on its open field moves
        it from where it stands, as the MIX faders' do. */
    void setFaderLook (const juce::String& name, juce::Colour accent, bool numberUnder = true)
    {
        faderLook   = true;
        faderName   = name;
        faderAccent = accent;
        numberShown = numberUnder;
        columnWidth = name.isNotEmpty() || numberUnder ? fader::columnW : designWidth;
        repaint();
    }

    bool hasFaderLook() const noexcept { return faderLook; }

    /** The width a block lays the rail's column at: the plain rail's 30; a fader's 46 with its words — or what its widest
        word needs (fader::columnFor). */
    int columnWidth = designWidth;

    /** THE GRIP'S LOOK, the guards' by default: a lilac hand writing a trim's decibels at 11 px. A rail whose number is
        an amount (the bass's) says how it is written, at what size, in which colour — and the scale a typed number is
        in (100: typed in percent). */
    std::function<juce::String (float)> textForValue;
    float        gripTextHeight = 11.0f;
    juce::Colour gripColour     = theme::lilac;
    float        typedScale     = 1.0f;

    /** The value laid out LOGARITHMICALLY between the rail's ends — a frequency's (OCTAVE's TONE) — not linearly. */
    bool logScale = false;

    /** Told when a drag begins (true) and ends (false) — an owner's undo history brackets it. */
    std::function<void (bool)> onGesture;

    /** Where a value stands, in the rail's own coordinates: the hand's centre at it. */
    float yOfValue (float v) const
    {
        const auto s = valueSpan();
        return s.getBottom() - s.getHeight() * proportionOf (v);
    }

    void paint (juce::Graphics& g) override
    {
        if (faderLook)
        {
            paintFaderLook (g);
            return;
        }

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

    /** WHILE A NUMBER IS BEING TYPED the hand still has the rail (the owner, 01.11): a press on the rail closes the field
        unread; a drag begun on the field itself closes it and moves the hand — the field's own clicks (the caret, a
        selection) stay the field's. */
    void mouseDown (const juce::MouseEvent& raw) override
    {
        const auto e = raw.getEventRelativeTo (this);
        fromField = raw.eventComponent != this;
        swallow   = e.mods.isPopupMenu();

        if (swallow)
        {
            if (! fromField && onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }

        if (! fromField)
            typing.ed.setVisible (false);   // the hand took the rail: the field goes, what it held unread
        dragging = false;

        // A fader's hand moves BY the drag: from where it stands, wherever the drag began.
        grabOffset = faderLook ? handY() - e.position.y : 0.0f;
    }

    void mouseMove (const juce::MouseEvent& raw) override { hoverNumber (raw.getEventRelativeTo (this).position); }
    void mouseExit (const juce::MouseEvent&) override     { hoverNumber ({ -1.0f, -1.0f }); }

    void mouseDrag (const juce::MouseEvent& raw) override
    {
        const auto e = raw.getEventRelativeTo (this);
        if (swallow)
            return;

        if (! dragging && e.getDistanceFromDragStart() > 3)
        {
            typing.ed.setVisible (false);
            dragging = true;
            valueAtt->beginGesture();
            if (onAtt != nullptr)
                onAtt->beginGesture();
            if (onGesture != nullptr)
                onGesture (true);
        }

        if (dragging)
            moveTo (e.position.y + grabOffset);
    }

    void mouseUp (const juce::MouseEvent& raw) override
    {
        const auto e = raw.getEventRelativeTo (this);
        if (swallow)
        {
            swallow = false;
            return;
        }

        if (fromField && ! dragging)
            return;   // a click in the field is the field's

        // A click moves nothing, as on the columns: it is the guard's menu. Only a drag moves. Where the number is a
        // field, a click on the grip types it.
        if (! dragging)
        {
            if (e.getNumberOfClicks() > 1)
                return;   // a double click's second: mouseDoubleClick has done it

            // The field: a fader's number under it; the plain rail's grip.
            const bool onGrip = typeDecimals >= 0
                && (faderLook ? numberShown && numberRow().contains (e.position)
                              : std::abs (e.position.y - yOfValue (valueP.convertFrom0to1 (valueP.getValue()))) <= meterrail::gripH * 0.5f);
            const auto at    = e.getScreenPosition();
            const int  token = ++clickToken;
            juce::Timer::callAfterDelay (juce::jmin (300, juce::MouseEvent::getDoubleClickTimeout()),
                                         [safe = juce::Component::SafePointer<GuardRail> (this), token, onGrip, at]
                                         {
                                             if (safe == nullptr || safe->clickToken != token)
                                                 return;   // a double click came: no menu, no field
                                             if (onGrip)
                                                 safe->openTyping();
                                             else if (safe->onMenu != nullptr)
                                                 safe->onMenu (at);
                                         });
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

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.eventComponent != this)
            return;   // a double click in the field selects its word
        ++clickToken;   // the first click's menu or field never opens
        resetToDefault();
    }

private:
    juce::Rectangle<float> scaleArea() const { return getLocalBounds().toFloat().reduced (2.0f); }

    static constexpr float rowH = 16.0f;   // a fader's name or number line: 13 px type with air above and below (Knob::valueRowH)

    juce::Rectangle<float> nameRow() const   { return getLocalBounds().toFloat().removeFromTop (faderName.isNotEmpty() ? rowH : 0.0f); }
    juce::Rectangle<float> numberRow() const { return getLocalBounds().toFloat().removeFromBottom (numberShown ? rowH : 0.0f); }

    /** A fader's rail, between its name and its number (Knob::faderArea). */
    juce::Rectangle<float> faderArea() const
    {
        auto r = getLocalBounds().toFloat();
        if (faderName.isNotEmpty())
            r.removeFromTop (rowH);
        if (numberShown)
            r.removeFromBottom (rowH);
        return r.reduced (0.0f, 4.0f);
    }

    /** Where a value stands on the travel, 0 at the foot, 1 at the head. */
    float proportionOf (float v) const
    {
        const float c = juce::jlimit (lo, hi, v);
        return logScale ? std::log (c / lo) / std::log (hi / lo) : (c - lo) / (hi - lo);
    }

    bool parkedAtFoot() const { return offAtFoot && onP != nullptr && onP->getValue() <= 0.5f; }

    /** Where the hand stands now: on the end stop when parked there. */
    float handY() const { return parkedAtFoot() ? valueSpan().getBottom() : yOfValue (valueP.convertFrom0to1 (valueP.getValue())); }

    void paintFaderLook (juce::Graphics& g)
    {
        // No dimming of its own: a block standing by dims its children (BlockFrame), and a second dimming would leave its
        // 13 px words unread.
        const float v = valueP.convertFrom0to1 (valueP.getValue());

        if (faderName.isNotEmpty())
        {
            g.setColour (theme::txDim);
            fader::drawWord (g, faderName, nameRow(), 0.06f);
        }

        fader::paint (g, faderArea(), parkedAtFoot() ? 0.0f : proportionOf (v), faderAccent, dragging || isMouseOverOrDragging (true));

        if (numberShown && ! typing.isOpen())
        {
            g.setColour (overNumber ? theme::tx : theme::txDim);
            fader::drawWord (g, numberText(), numberRow());
        }
    }

    /** The number under a fader lights under the mouse, the field's cursor over it — where a click types it. */
    void hoverNumber (juce::Point<float> p)
    {
        const bool over = faderLook && numberShown && typeDecimals >= 0 && numberRow().contains (p);
        if (over == overNumber)
            return;
        overNumber = over;
        setMouseCursor (over ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    /** The span the hand's centre travels: half a hand in from the head and from the foot — a fader's, half its grip. */
    juce::Rectangle<float> valueSpan() const
    {
        if (faderLook)
            return fader::travel (faderArea());

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
        const float t    = (span.getBottom() - y) / juce::jmax (1.0f, span.getHeight());
        const float v    = logScale ? lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, t)) : lo + t * (hi - lo);

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

    bool  faderLook = false, numberShown = false, overNumber = false;
    juce::String faderName;
    juce::Colour faderAccent = theme::violet;
    float grabOffset = 0.0f;

    bool dragging = false;
    bool swallow  = false;
    int  clickToken = 0;
    bool fromField  = false, hearsField = false;
    meterrail::GripEditor typing;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardRail)
};

/** A GUARD'S LAMP — the plate beside its rail in a guard's block that lights while the guard
    presses. It floods solid toward the violet with the press (orange is a line, never a wash), and
    keeps the orange "worked while you were away" dot until it is looked at. A guard switched off is
    dark. A click is a look: the dot clears and the guard's menu opens. With no name it is a plain
    plate — the block over it says whose it is.

    THE GUARD'S NUMBER rides in it (the owner, 01.11): what its rail stands at — "-60", "-1.0", "OFF" on the gate's end
    stop — at 13 px, centred (`readout`); a click on that number types it (`onNumberClick`), a click anywhere else on
    the plate is the look and the menu. */
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

    std::function<juce::String()> readout;
    std::function<void()>         onNumberClick;

    static constexpr float numberH = 13.0f;

    /** Where the number is written — a click there types it — and where its field opens: the plate less its frame. */
    juce::Rectangle<int> numberBox() const
    {
        const int w = juce::roundToInt (theme::trackedWidth (shownText, theme::displayFont (numberH), 0.02f)) + 10;
        return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth(), w), getHeight());
    }

    juce::Rectangle<int> fieldBox() const { return getLocalBounds().reduced (6, 4); }

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

        if (readout != nullptr)
        {
            // Not dimmed by the plate's own `a`: the block dims its children already, and "OFF" must still be read.
            g.setColour ((on ? theme::tx : theme::txDim).interpolatedWith (juce::Colours::white, shownDepth));
            theme::drawTracked (g, shownText, r, theme::displayFont (numberH), 0.02f, juce::Justification::centred);
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
        if (! e.mods.isPopupMenu() && onNumberClick != nullptr && readout != nullptr && numberBox().contains (e.getPosition()))
        {
            onNumberClick();
            return;
        }
        if (onClick != nullptr)
            onClick (e.getScreenPosition());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool overNumber = onNumberClick != nullptr && readout != nullptr && numberBox().contains (e.getPosition());
        setMouseCursor (overNumber ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::PointingHandCursor);
    }

private:
    void timerCallback() override
    {
        // Only a WORKING guard lights: one standing by presses on nothing. The switch is watched too —
        // a guard switched off while it pressed on nothing changes no depth, and must still go dark.
        const bool  on  = onP.getValue() > 0.5f;
        const float d   = on ? juce::jlimit (0.0f, 1.0f, depth()) : 0.0f;
        const bool  dot = worked.load();
        const auto  text = readout != nullptr ? readout() : juce::String();

        if (std::abs (d - shownDepth) > 0.02f || dot != shownDot || on != shownOn || text != shownText)
        {
            shownDepth = d;
            shownDot   = dot;
            shownOn    = on;
            shownText  = text;
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
    juce::String shownText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardLamp)
};

} // namespace orbitamp
