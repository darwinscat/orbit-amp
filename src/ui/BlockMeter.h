// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "MeterRail.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** A block's gain staging, lying down: a thin horizontal meter with the hand that moves it riding
    along the same bar.

    Two of these sit under a captured block's device combo — IN, what the model is being fed, and
    OUT, what leaves the block. They are a PAIR and that is the whole point of them being here: a
    captured device answers to what goes into it, so a block with only an output volume lets you fix
    the loudness while leaving the drive wherever it happened to land.

    TWO SCALES ON ONE BAR, which is the same trick the gate's sliver plays and works for the same
    reason: they are told apart by what they are drawn as. The FILL is the signal, on a dBFS scale
    from the floor to zero. The NAIL is a gain, on its own scale, unity notched. They meet because
    moving the nail moves the fill — drag, watch the level walk into the green, let go. That loop
    is the only reason a trim and a meter should ever share a bar.

    A NAIL AND A NUMBER, not a framed grip carrying its own reading. The frame was thirty-four units
    wide on a bar barely a hundred long: a third of the scale covered by the hand that moves it, and
    the fill it was supposed to be answering hidden underneath. The hand is one line now and the
    reading stands to the right of the bar, where it can be read at a glance and typed into with a
    double-click.

    THE GREEN ZONE is where a captured device likes to be fed. It is drawn into the track rather
    than onto the fill: a wash under the signal reads as a place, a wash over it reads as a state.
    See `params::captureColdDb` for why the numbers are a convention rather than a measurement. */
class BlockMeter final : public juce::Component,
                         public juce::SettableTooltipClient,
                         private juce::Timer
{
public:
    /** `showZone` is for the OUT meter, which has no such thing to say: what leaves a block is
        whatever the next block wants, and there is no window that is right for it. */
    BlockMeter (juce::String meterName, const std::atomic<float>& levelSource,
                juce::RangedAudioParameter& trimParam, bool showZone)
        : name (std::move (meterName)), levelDb (levelSource), param (&trimParam), zone (showZone)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam, [this] (float) { repaint(); });
        trim->sendInitialUpdate();
        setRepaintsOnMouseActivity (true);
        startTimerHz (30);
    }

    /** A meter ALONE — no hand, because there is nothing for one to hold: a block deliberately
        has no output volume, so its OUT column only answers "what leaves". Not interactive. */
    BlockMeter (juce::String meterName, const std::atomic<float>& levelSource)
        : name (std::move (meterName)), levelDb (levelSource), param (nullptr), zone (false)
    {
        startTimerHz (30);
    }

    /** The hint is a RULER, not a report: one number — what the scale says at the cursor's own
        height. Composed on demand, so it moves with the hand that hovers. */
    juce::String getTooltip() override
    {
        const auto  t = vertical ? columnArea() : trackArea();
        const auto  p = getMouseXYRelative().toFloat();
        const float u = vertical ? (t.getBottom() - p.y) / juce::jmax (1.0f, t.getHeight())
                                 : (p.x - t.getX())      / juce::jmax (1.0f, t.getWidth());
        const float db = floorDb + juce::jlimit (0.0f, 1.0f, u) * (ceilDb - floorDb);

        return juce::String (db, 1) + " dB";
    }

    /** The right-wall meter: the bar stands at the component's right edge and the dragged
        reading appears to its LEFT — the mirror of the IN wall. */
    bool valueOnLeft = false;

    /** STANDING UP: the same meter as a thin column, for the left of a dial — the fill climbs, the
        hand lies across it, the green zone runs up its edge. No name and no reading beside it:
        the column is as wide as the bar was tall, and the number rides the hint instead. */
    bool vertical = false;

    /** A switched-off block's meter indicates NOTHING — the slot and the hand stay, dimmed with
        the block, but a bar that keeps filling on a dead block claims the block is doing
        something. The owner flips this with the block's power. */
    bool live = true;
    static constexpr int designWidth = 12;

    /** Standing, the component is wider than its bar by this margin: the dB reading appears
        there while the hand drags. Empty and CLICK-THROUGH otherwise — see hitTest. */
    static constexpr int standingValueW = 48;

    bool hitTest (int x, int y) override
    {
        if (vertical && ! dragging)
            return valueOnLeft ? x >= getWidth() - designWidth - 2
                               : x <= designWidth + 2;

        return juce::Component::hitTest (x, y);
    }

    /** The grip is in the hand — the owner may want to show a ladder beside it. */
    std::function<void (bool)> onDrag;

    // Eighteen, not twenty-six. Two of these ride between the combo and the dial, and every unit
    // they keep is one the curve below does not get. A meter is a length, not a volume.
    static constexpr int designHeight = 18;

    void paint (juce::Graphics& g) override
    {
        if (vertical)
        {
            paintStanding (g);
            return;
        }

        const auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto track = trackArea();

        // ---- the lane itself. Drawn even when nothing is playing, because a bar you cannot see
        //      until it has a signal in it is a bar nobody knows they can grab. ----
        g.setColour (theme::hair2.withAlpha (0.45f));
        g.drawRoundedRectangle (track.reduced (0.5f), 2.0f, 1.0f);

        // ---- the signal ----
        if (live)
            paintFill (g, track);

        // ---- the green zone: where this block wants to be fed.
        //
        //      A band along the LANE'S EDGE rather than a wash across it, and after the fill rather
        //      than under it. Drawn as a wash it lost twice over — the signal painted across it and
        //      the grip's frame parked on top of it, which is where the trim's unity happens to
        //      sit. A mark on the rail is read the way a mark on a ruler is: it belongs to the
        //      scale, not to what is being measured against it. ----
        if (zone)
        {
            const float a = xOfLevel (track, params::captureColdDb);
            const float b = xOfLevel (track, params::captureHotDb);

            g.setColour (inRange.withAlpha (0.85f));
            g.fillRect (a, track.getBottom() - 3.0f, b - a, 2.5f);
            g.setColour (inRange.withAlpha (0.30f));
            g.fillRect (a, track.getY() + 0.5f, b - a, 1.5f);
        }

        if (live && hold > floorDb + 0.5f)
        {
            const float x = xOfLevel (track, hold);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (x - 0.75f, track.getY(), 1.5f, track.getHeight());
        }

        // ---- zero on the SIGNAL's scale: not the same place as the hand's unity, and the one
        //      line on this bar that means "full" ----
        {
            const float z = xOfLevel (track, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (z - 0.5f, track.getY() + 1.0f, 1.0f, track.getHeight() - 2.0f);
        }

        // ---- the name, inside the column at its head, like the vertical rails do ----
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        theme::drawTracked (g, name, r.withTrimmedLeft (6.0f).withWidth (nameW - 8.0f),
                            theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);

        // ---- unity, and the hand — only where there IS one ----
        if (param != nullptr)
        {
            const float unityX = xOfTrim (track, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (unityX - 1.0f, track.getY(), 2.0f, 5.0f);
            g.fillRect (unityX - 1.0f, track.getBottom() - 5.0f, 2.0f, 5.0f);

            paintNail (g, track);
            paintValue (g);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (param == nullptr)
            return;

        if (e.mods.isPopupMenu())
        {
            // The one thing a trim is always asked for: put it back.
            trim->setValueAsCompleteGesture (0.0f);
            return;
        }

        // The reading is not part of the scale — a click on it must not throw the value across the
        // bar on the way to a double-click.
        if (! vertical && valueArea().contains (e.position))
            return;

        dragging = true;
        param->beginChangeGesture();
        if (onDrag != nullptr)
            onDrag (true);

        writeFrom (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            writeFrom (e.position);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;

        dragging = false;
        param->endChangeGesture();
        if (onDrag != nullptr)
            onDrag (false);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (param == nullptr)
            return;

        // On the number: type one. Anywhere on the bar: home. The same convention the vertical
        // rails' grips already use, split across the two halves this meter has. Standing up there
        // is no number to type into, so a double-click is home.
        if (! vertical && valueArea().contains (e.position))
        {
            editor.open (*this, valueArea().toNearestInt().expanded (2, 0), trimDb(), theme::orange,
                         [this] (float v) { trim->setValueAsCompleteGesture (v); });
            return;
        }

        trim->setValueAsCompleteGesture (0.0f);
    }

private:
    void timerCallback() override
    {
        if (! live)
        {
            hold = floorDb;   // a stale peak must not greet the block when it comes back
            return;
        }

        const float now = levelDb.load();

        // The peak hold, decaying: it is here to let a phrase be READ, not to latch forever.
        if (now > hold)  hold = now;
        else             hold = juce::jmax (now, hold - 0.6f);

        repaint();
    }

    juce::Rectangle<float> trackArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f, 2.0f)
                   .withTrimmedLeft (nameW).withTrimmedRight (valueW);
    }

    juce::Rectangle<float> valueArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f, 1.0f).removeFromRight (valueW - 2.0f);
    }

    static constexpr float nameW  = 26.0f;
    static constexpr float valueW = 40.0f;

    // The scale: -72 like the big rails, now that the column runs the block's whole height —
    // there is finally enough bar for the quiet four fifths to cost nothing, and one floor
    // across every meter in the window beats a shorter, private one. A little room above zero
    // stays, so a clip is visible rather than merely full.
    static constexpr float floorDb = -72.0f;
    static constexpr float ceilDb  =   6.0f;

    static float xOfLevel (juce::Rectangle<float> t, float db)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb));
        return t.getX() + u * t.getWidth();
    }

    /** The hand's place comes from ITS parameter's range, not a baked scale: the IN wall holds
        a -24..+12 trim, the OUT wall the console's ±12 LEVEL — one meter, either hand. */
    float xOfTrim (juce::Rectangle<float> t, float db) const
    {
        const float u = param != nullptr ? param->convertTo0to1 (db) : 0.0f;
        return t.getX() + juce::jlimit (0.0f, 1.0f, u) * t.getWidth();
    }

    float trimDb() const { return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f; }

    void writeFrom (juce::Point<float> p)
    {
        const auto  t = vertical ? columnArea() : trackArea();
        const float u = vertical ? (t.getBottom() - p.y) / juce::jmax (1.0f, t.getHeight())
                                 : (p.x - t.getX())      / juce::jmax (1.0f, t.getWidth());
        trim->setValueAsPartOfGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, u)));
    }

    // ---- standing up ----

    juce::Rectangle<float> columnArea() const
    {
        // Standing, only the bar's strip is the meter — the rest of the width belongs to the
        // dragged reading, on whichever side the wall faces.
        auto r = getLocalBounds().toFloat();

        if (vertical)
            r = valueOnLeft ? r.withTrimmedLeft (r.getWidth() - (float) designWidth)
                            : r.withWidth ((float) designWidth);

        return r.reduced (2.0f, 2.0f);
    }

    static float yOfLevel (juce::Rectangle<float> t, float db)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb));
        return t.getBottom() - u * t.getHeight();
    }

    float yOfTrim (juce::Rectangle<float> t, float db) const
    {
        const float u = param != nullptr ? param->convertTo0to1 (db) : 0.0f;
        return t.getBottom() - juce::jlimit (0.0f, 1.0f, u) * t.getHeight();
    }

    /** The bar on its feet, and stripped: no bezel, no hairline, no marks on the rail. What the
        column says it says in COLOUR — the fill climbs from deep blue through the green of where a
        capture wants to be fed into yellow, orange and red, the gradient anchored to the scale so a
        given height always reads the same colour, and the level clips it. The hand lies across it;
        the peak hold is the one white line. The reading, and the advice, ride the hint. */
    void paintStanding (juce::Graphics& g) const
    {
        const auto t = columnArea();

        g.setColour (theme::bezel.withAlpha (0.75f));
        g.fillRoundedRectangle (t, 2.0f);

        if (live)
        {
            const auto u = [] (float db) { return (double) juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb)); };

            // The thermometer, zone-anchored: the same weather as the big rail — dark corporate
            // ground through the corporate violet, the zone's green, yellow, orange, red — with
            // the green pinned to where THIS block wants to be fed.
            juce::ColourGradient grad (meterrail::heatFloor, 0.0f, t.getBottom(),
                                       meterrail::heatRed,   0.0f, t.getY(), false);
            grad.addColour (u (params::captureColdDb) - 0.14, theme::violet);                 // corporate, on the way up
            grad.addColour (u (params::captureColdDb),        inRange);                       // the zone: green...
            grad.addColour (u (params::captureHotDb),         inRange);                       // ...to green
            grad.addColour (u (params::captureHotDb) + 0.05,  meterrail::heatYellow);         // then yellow
            grad.addColour (u (0.0f),                         theme::orange);                 // orange at full scale

            const float y = yOfLevel (t, levelDb.load());
            if (y < t.getBottom() - 1.0f)
            {
                const juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion ((int) std::floor (t.getX()), (int) y,
                                    (int) std::ceil (t.getWidth()) + 1, (int) std::ceil (t.getBottom() - y) + 1);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (t, 2.0f);
            }
        }

        if (live && hold > floorDb + 0.5f)
            meterrail::paintHold (g, t, yOfLevel (t, hold));

        // A meter without a hand said everything already.
        if (param == nullptr)
            return;

        // The hand, lying across the column, with a head at each end.
        const float y   = yOfTrim (t, trimDb());
        const bool  lit = dragging || isMouseOver();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (t.getX() - 1.0f, y - 1.75f, t.getWidth() + 2.0f, 3.5f);
        g.setColour (lit ? theme::orange.brighter (0.3f) : theme::orange);
        g.fillRect (t.getX() - 1.0f, y - 0.75f, t.getWidth() + 2.0f, 1.5f);
        g.fillRect (t.getX() - 1.0f,      y - 2.5f, 2.5f, 5.0f);
        g.fillRect (t.getRight() - 1.5f,  y - 2.5f, 2.5f, 5.0f);

        // The reading, in the hand's own colour, riding beside the grip while it drags — the
        // component's extra width is exactly this label's room, on the side the wall faces.
        if (dragging)
        {
            const float ly = juce::jlimit (t.getY() + 8.0f, t.getBottom() - 8.0f, y);
            const auto  label = valueOnLeft
                ? juce::Rectangle<float> (t.getX() - (float) standingValueW + 3.0f, ly - 8.0f,
                                          (float) standingValueW - 8.0f, 16.0f)
                : juce::Rectangle<float> (t.getRight() + 5.0f, ly - 8.0f,
                                          (float) standingValueW - 8.0f, 16.0f);

            g.setColour (theme::orange);
            theme::drawTracked (g, meterrail::trimText (trimDb()), label,
                                theme::displayFont (11.0f), 0.02f,
                                valueOnLeft ? juce::Justification::centredRight
                                            : juce::Justification::centredLeft);
        }
    }

    /** The family gradient, lying down: mostly violet, warming to orange only near the top, and
        the level CLIPS it — so a given length always reads the same colour. */
    void paintFill (juce::Graphics& g, juce::Rectangle<float> t) const
    {
        const float x = xOfLevel (t, levelDb.load());

        if (x <= t.getX() + 1.0f)
            return;

        juce::ColourGradient grad (theme::violet.withAlpha (0.50f), t.getX(), 0.0f,
                                   theme::orange, t.getRight(), 0.0f, false);
        grad.addColour (0.58, theme::violet.withAlpha (0.60f));

        const juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion ((int) std::floor (t.getX()), (int) std::floor (t.getY()),
                            (int) std::ceil (x - t.getX()), (int) std::ceil (t.getHeight()) + 1);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (t, 2.0f);
    }

    /** The hand: one line standing across the bar, with a head at each end so it reads as
        something to take hold of rather than as another of the meter's own marks. Orange, because
        it belongs to the player and the signal does not. */
    void paintNail (juce::Graphics& g, juce::Rectangle<float> t) const
    {
        const float x = xOfTrim (t, trimDb());
        const bool  lit = dragging || isMouseOver();

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (x - 1.75f, t.getY() - 1.0f, 3.5f, t.getHeight() + 2.0f);

        g.setColour (lit ? theme::orange.brighter (0.3f) : theme::orange);
        g.fillRect (x - 0.75f, t.getY() - 1.0f, 1.5f, t.getHeight() + 2.0f);

        // The heads, top and bottom: without them a one-pixel line is indistinguishable from the
        // peak hold that also crosses this bar.
        g.fillRect (x - 2.5f, t.getY() - 1.0f, 5.0f, 2.5f);
        g.fillRect (x - 2.5f, t.getBottom() - 1.5f, 5.0f, 2.5f);
    }

    /** The reading, beside the bar. Double-click to type one in. */
    void paintValue (juce::Graphics& g) const
    {
        g.setColour (dragging ? theme::orange : theme::txDim);
        theme::drawTracked (g, meterrail::trimText (trimDb()), valueArea(),
                            theme::displayFont (11.0f), 0.0f, juce::Justification::centredRight);
    }

    inline static const juce::Colour inRange { 0xff5fc97a };   // the character ramp's clean green

    juce::String name;
    const std::atomic<float>& levelDb;
    juce::RangedAudioParameter* param;   // null = a meter alone, no hand
    bool zone = true;

    std::unique_ptr<juce::ParameterAttachment> trim;
    meterrail::GripEditor editor;   // the in-place field the reading opens into
    float hold = -120.0f;
    bool  dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockMeter)
};

} // namespace orbitamp
