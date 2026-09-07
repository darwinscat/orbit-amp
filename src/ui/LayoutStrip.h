// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MeterRail.h"
#include "Theme.h"

#include <vector>

namespace orbitamp
{

/** The layout strip — the chain laid flat between the toolbar and the device, ALWAYS there:
    one arrow per block, the process-chevron banner — notch in, point out — nesting
    point-into-notch down the line, so the shape itself is the sequence and nothing else has
    to say it. The first block's notch is where the guitar walks in; the last block's point is
    the way to the speakers.

    The arrow IS the block's one presence switch: click it and the block stands down or comes
    back, the panel re-splitting underneath in plain view. A lit arrow stands on the panel; a
    dark one has stepped out. Each wears its block's accent — orange for the captured, violet
    for ours — so the strip reads as a miniature of the panel.

    The two ENDS are rows like any other now. They used to be end caps — a narrow vertical
    meter at each edge, a different shape with a different click — and that was one grammar too
    many in a strip 34 units tall. `IN` and `OUT` are the way in and the way out, they carry a
    volume each, and they answer to the same two switches as everything else. The level they
    used to breathe with lives on the columns' own rails, which is where a level belongs.

    Dumb view: rows in, `onToggle (index, on)` out. Who owns the faceplate and what a switch
    means to the sound is the editor's business. */
class LayoutStrip final : public juce::Component,
                          private juce::Timer
{
public:
    struct Row
    {
        juce::String name;
        juce::Colour accent;
        bool on = true;

        /** Whether this link is in the rig at all. A row that is not present takes no width, no
            paint, no click and no repaint — it is ABSENT, which is a different thing from `on`
            being false: that one is still here, standing by. Rows keep their places in this
            vector whatever happens, so an index stays the same row forever and no callback ever
            has to be rebound. */
        bool present = true;

        /** The service links' lights, optional: how hard the guard presses (floods the arrow,
            solid) and the latched "worked while you were away" mark. A sound block has none. */
        std::function<float()> depth;
        std::function<bool()>  dot;

        /** Whether a right click means a menu — the editor answers through onRowMenu. */
        bool hasMenu = false;

        /** The guards' arrows are their CONSOLES: any click opens the menu (OFF lives inside),
            nothing toggles — there is no badge left to hide. */
        bool clickIsMenu = false;
    };

    /** The strip's height, in design units — the window budgets for it like for any strip. */
    static constexpr int designHeight = 34;

    explicit LayoutStrip (std::vector<Row> blockRows) : rows (std::move (blockRows))
    {
        shownDepth.assign (rows.size(), 0.0f);
        shownDot.assign (rows.size(), false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        menuButton.onClick = [this]
        {
            if (onChainMenu)
                onChainMenu (menuButton);
        };
        addAndMakeVisible (menuButton);

        startTimerHz (20);   // the lights' breath; they only repaint what actually moved
    }

    std::function<void (int index, bool on)> onToggle;

    /** A right click on a row that carries a menu — the guards' way to their settings when
        their badges are hidden. */
    std::function<void (int index, juce::Point<int> screenPos)> onRowMenu;

    /** The checklist at the strip's right end was pressed, and here is the button it was pressed
        on. What the list OFFERS — which links are in the rig at all, and what an emptied row does
        to the window — is the editor's business; the strip only hands over what it hangs off.

        The BUTTON, not a point: a menu that does not know its launcher dismisses synchronously and
        lets the click through, so pressing the button a second time closes the menu and instantly
        reopens it (juce_PopupMenu.cpp says so in as many words). It would never shut. */
    std::function<void (juce::Component& anchor)> onChainMenu;


    /** Redress one row from outside — the editor answers a toggle through here, so the strip
        only ever shows what was actually applied. */
    void setRowOn (int index, bool on)
    {
        rows[(size_t) index].on = on;
        repaint();
    }

    /** In or out of the rig. The whole strip repaints: everyone after this row moves. */
    void setRowPresent (int index, bool present)
    {
        rows[(size_t) index].present = present;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // The ground: the panel's own colour and a hairline underneath — the strips' family.
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::hair2);
        g.fillRect (getLocalBounds().removeFromBottom (1));

        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (! rows[i].present)
                continue;

            const auto tile = tileArea ((int) i).toFloat();
            const float x0 = tile.getX() + airX, x1 = tile.getRight() + (float) tipW - airX;
            const float y0 = tile.getY(), y1 = tile.getBottom(), cy = tile.getCentreY();

            juce::Path arrow;
            arrow.startNewSubPath (x0, y0);
            arrow.lineTo (x1 - (float) tipW, y0);
            arrow.lineTo (x1, cy);
            arrow.lineTo (x1 - (float) tipW, y1);
            arrow.lineTo (x0, y1);
            arrow.lineTo (x0 + (float) tipW, cy);
            arrow.closeSubPath();

            const float a = rows[i].on ? 1.0f : theme::offAlpha;

            // A guard's arrow floods with its press — SOLID, interpolated toward the violet
            // (orange is a line here, never a wash) — and the flood stays live even when the
            // row below lost its badge: the light moved here, it did not go out.
            // Only a WORKING link floods. A guard standing by is not pressing on anything, and
            // its last reading has no business burning on in a dark arrow.
            const float d = rows[i].on ? juce::jlimit (0.0f, 1.0f, shownDepth[i]) : 0.0f;
            g.setColour (juce::Colour (0xff1b1b22).interpolatedWith (theme::violet, d));
            g.fillPath (arrow);

            g.setColour (rows[i].accent.withAlpha (0.8f * a));
            g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered,
                                                       juce::PathStrokeType::rounded));

            g.setColour ((rows[i].on ? theme::tx : theme::txDim)
                             .interpolatedWith (juce::Colours::white, d).withAlpha (a));
            theme::drawTracked (g, rows[i].name,
                                { x0 + (float) tipW, y0, x1 - x0 - 2.0f * (float) tipW,
                                  tile.getHeight() },
                                theme::displayFont (10.0f), 0.12f, juce::Justification::centred);

            // The latched mark: it worked while you were away.
            if (shownDot[i])
            {
                g.setColour (theme::orange);
                g.fillEllipse (x1 - (float) tipW - 7.0f, y0 + 2.0f, 4.5f, 4.5f);
            }
        }
    }

    void resized() override
    {
        auto lane = getLocalBounds().reduced (padX, 0);
        menuButton.setBounds (lane.removeFromRight (menuW).withSizeKeepingCentre (menuW, menuW));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].present && tileArea ((int) i).contains (e.getPosition()))
            {
                if (rows[i].clickIsMenu || e.mods.isPopupMenu())
                {
                    if (rows[i].hasMenu && onRowMenu)
                        onRowMenu ((int) i, e.getScreenPosition());
                }
                else if (onToggle)
                    onToggle ((int) i, ! rows[i].on);
                return;
            }
    }

private:
    /** The strip's menu handle: a checklist — two ticked rows and an empty one — because that is
        literally what opens under it. NOT a gear: the toolbar already wears one and it means the
        Setup window; a second one would be the same mark for two different promises.

        Drawn here, in a 24x24 design box fitted to the button the way appkit's icons are, so it
        keeps its proportions at any zoom. It lives in this file until the shape settles, then it
        moves to `felitronics::appkit::IconButton` as one more Kind. */
    class ChecklistButton final : public juce::Button
    {
    public:
        ChecklistButton() : juce::Button ("chain")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat();

            if (over || down)
            {
                g.setColour (juce::Colour (over ? 0x1effffff : 0x14ffffff));
                g.fillRoundedRectangle (r.reduced (0.5f), 4.0f);
            }

            const auto fit = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                 .getTransformToFit ({ 0.0f, 0.0f, 24.0f, 24.0f }, r.reduced (2.0f));

            juce::Path p;

            for (int row = 0; row < 3; ++row)
            {
                const float y = 5.0f + (float) row * 7.0f;

                if (row < 2)   // some links are in, one is not — that is the whole picture
                {
                    p.startNewSubPath (2.0f, y);
                    p.lineTo (4.5f, y + 2.5f);
                    p.lineTo (8.5f, y - 3.0f);
                }

                p.startNewSubPath (11.5f, y);
                p.lineTo (22.0f, y);
            }

            g.setColour (over || down ? theme::tx : theme::txDim);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded), fit);
        }
    };

    static constexpr int   padX   = 14;
    static constexpr int   tileH  = 22;
    static constexpr int   tipW   = 8;      // the arrow's point, and the notch it nests into
    static constexpr float airX   = 2.0f;   // breathing room in the nest, each side
    static constexpr int   menuW  = 20;     // the checklist's square, at the right end of the lane
    static constexpr int   menuGap = 8;     // and the air the last arrow's point keeps from it

    void timerCallback() override
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (! rows[i].present || (rows[i].depth == nullptr && rows[i].dot == nullptr))
                continue;

            const float d   = rows[i].depth != nullptr ? rows[i].depth() : 0.0f;
            const bool  dot = rows[i].dot   != nullptr && rows[i].dot();

            if (std::abs (d - shownDepth[i]) > 0.02f || dot != shownDot[i])
            {
                shownDepth[i] = d;
                shownDot[i]   = (char) dot;

                // The arrow's NOSE reaches one tip past its cell, into the neighbour's notch —
                // repaint it too, or a flood lights everything but the nose and a fading one
                // leaves the nose burning.
                repaint (tileArea ((int) i).withTrimmedRight (-tipW));
            }
        }
    }

    /** How many links stand in the rig, and where this one stands among them. The width is shared
        by the PRESENT rows only, so a row leaving the rig gives its width to the rest rather than
        leaving a hole — while its index in `rows` never moves, which is what keeps every callback
        and every `setRowOn` pointing at the row it was bound to. */
    int presentCount() const
    {
        int n = 0;
        for (const auto& r : rows)
            if (r.present)
                ++n;

        return n;
    }

    int placeOf (int index) const
    {
        int place = 0;
        for (int i = 0; i < index; ++i)
            if (rows[(size_t) i].present)
                ++place;

        return place;
    }

    /** An arrow's cell: even steps between the caps, each shape reaching one tip past its cell
        into the next block's notch. The rectangle is the HIT area and the name's home; the
        point is drawn beyond its right edge, in the neighbour's notch, where a click means the
        neighbour. An absent row has no cell at all — an empty rectangle contains no click and
        paints nothing, so every walk over the rows is safe even when nobody checks the flag. */
    juce::Rectangle<int> tileArea (int index) const
    {
        const int n = presentCount();

        if (n <= 0 || ! rows[(size_t) index].present)
            return {};

        const auto lane = getLocalBounds().reduced (padX, 0)
                              .withTrimmedRight (menuW + menuGap);
        const int  step = (lane.getWidth() - tipW) / n;

        return juce::Rectangle<int> (lane.getX() + placeOf (index) * step,
                                     lane.getCentreY() - tileH / 2, step, tileH);
    }

    ChecklistButton  menuButton;

    std::vector<Row> rows;
    std::vector<float> shownDepth;
    std::vector<char>  shownDot;   // char, not bool: vector<bool> has no honest references

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutStrip)
};

} // namespace orbitamp
