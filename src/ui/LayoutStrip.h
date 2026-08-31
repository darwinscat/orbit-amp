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

    The END CAPS are the side columns in miniature: a narrow vertical rectangle at each end —
    LIVE, a little meter breathing with the real IN and OUT level — and a click stands the
    column down the same way. The chain enters through the left column and leaves through the
    right, so the caps bracket the arrows the way the rails bracket the panel.

    Dumb view: rows in, `onToggle (index, on)` / `onCapToggle (side, on)` out. Who owns the
    prefs, the faceplate, and the "a hidden block must not colour the sound" law is the
    editor's business. */
class LayoutStrip final : public juce::Component,
                          private juce::Timer
{
public:
    struct Row
    {
        juce::String name;
        juce::Colour accent;
        bool on = true;
    };

    /** The strip's height, in design units — the window budgets for it like for any strip. */
    static constexpr int designHeight = 34;

    explicit LayoutStrip (std::vector<Row> blockRows) : rows (std::move (blockRows))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (20);   // the caps' breath; they only repaint when a level actually moved
    }

    std::function<void (int index, bool on)> onToggle;

    /** The end caps: side 0 is the IN column, side 1 the OUT. */
    std::function<void (int side, bool on)> onCapToggle;
    std::function<float()> capLevel[2];   // live level, 0..1 — a cap without one stays still

    void setCapOn (int side, bool on)
    {
        capOn[(size_t) side] = on;
        repaint();
    }

    /** Redress one row from outside — the editor answers a toggle through here, so the strip
        only ever shows what was actually applied. */
    void setRowOn (int index, bool on)
    {
        rows[(size_t) index].on = on;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // The ground: the panel's own colour and a hairline underneath — the strips' family.
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::hair2);
        g.fillRect (getLocalBounds().removeFromBottom (1));

        // The end caps: the side columns in miniature. The level wears the RAILS' own dB
        // gradient — the same painter, so the miniature never lies about the colour — and it
        // stays live even when its column is hidden: the meter still measures, only the frame
        // goes out to say the column has left the panel.
        for (int side = 0; side < 2; ++side)
        {
            const auto cap = capArea (side).toFloat();
            const float a  = capOn[(size_t) side] ? 1.0f : theme::offAlpha;

            g.setColour (juce::Colour (0xff101016));
            g.fillRoundedRectangle (cap, 2.5f);

            if (capLevel[side] != nullptr)
            {
                const float lvl  = juce::jlimit (0.0f, 1.0f, shownLevel[(size_t) side]);
                const auto  well = cap.reduced (2.0f);
                // heat = the whole thermometer, exactly as the big rails wear it.
                meterrail::paintFill (g, well, well.getBottom() - well.getHeight() * lvl, 0.0f, true);
            }

            g.setColour (theme::lilac.withAlpha (0.8f * a));
            g.drawRoundedRectangle (cap.reduced (0.75f), 2.5f, 1.5f);
        }

        for (size_t i = 0; i < rows.size(); ++i)
        {
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

            g.setColour (juce::Colour (0xff1b1b22));
            g.fillPath (arrow);
            g.setColour (rows[i].accent.withAlpha (0.8f * a));
            g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered,
                                                       juce::PathStrokeType::rounded));

            g.setColour ((rows[i].on ? theme::tx : theme::txDim).withAlpha (a));
            theme::drawTracked (g, rows[i].name,
                                { x0 + (float) tipW, y0, x1 - x0 - 2.0f * (float) tipW,
                                  tile.getHeight() },
                                theme::displayFont (10.0f), 0.12f, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int side = 0; side < 2; ++side)
            if (capArea (side).contains (e.getPosition()))
            {
                if (onCapToggle)
                    onCapToggle (side, ! capOn[(size_t) side]);
                return;
            }

        for (size_t i = 0; i < rows.size(); ++i)
            if (tileArea ((int) i).contains (e.getPosition()))
            {
                if (onToggle)
                    onToggle ((int) i, ! rows[i].on);
                return;
            }
    }

private:
    static constexpr int   padX   = 14;
    static constexpr int   tileH  = 22;
    static constexpr int   tipW   = 8;      // the arrow's point, and the notch it nests into
    static constexpr float airX   = 2.0f;   // breathing room in the nest, each side
    static constexpr int   capW   = 14;     // the end caps: narrow, vertical — a rail in miniature
    static constexpr int   capH   = 26;
    static constexpr int   capGap = 10;

    void timerCallback() override
    {
        for (int side = 0; side < 2; ++side)
        {
            if (capLevel[side] == nullptr)
                continue;

            const float lvl = capLevel[side]();
            if (std::abs (lvl - shownLevel[(size_t) side]) > 0.02f)
            {
                shownLevel[(size_t) side] = lvl;
                repaint (capArea (side));
            }
        }
    }

    juce::Rectangle<int> capArea (int side) const
    {
        const auto lane = getLocalBounds().reduced (padX, 0);
        const int  x    = side == 0 ? lane.getX() : lane.getRight() - capW;

        return { x, lane.getCentreY() - capH / 2, capW, capH };
    }

    /** An arrow's cell: even steps between the caps, each shape reaching one tip past its cell
        into the next block's notch. The rectangle is the HIT area and the name's home; the
        point is drawn beyond its right edge, in the neighbour's notch, where a click means the
        neighbour. */
    juce::Rectangle<int> tileArea (int index) const
    {
        const int n     = (int) rows.size();
        const auto lane = getLocalBounds().reduced (padX + capW + capGap, 0);
        const int  step = (lane.getWidth() - tipW) / n;

        return juce::Rectangle<int> (lane.getX() + index * step,
                                     lane.getCentreY() - tileH / 2, step, tileH);
    }

    std::vector<Row> rows;
    bool  capOn[2]      = { true, true };
    float shownLevel[2] = { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutStrip)
};

} // namespace orbitamp
