// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

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

    Dumb view: rows in, `onToggle (index, on)` out. Who owns the prefs, the faceplate, and the
    "a hidden block must not colour the sound" law is the editor's business. */
class LayoutStrip final : public juce::Component
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
    }

    std::function<void (int index, bool on)> onToggle;

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
        for (size_t i = 0; i < rows.size(); ++i)
            if (tileArea ((int) i).contains (e.getPosition()))
            {
                if (onToggle)
                    onToggle ((int) i, ! rows[i].on);
                return;
            }
    }

private:
    static constexpr int   padX  = 14;
    static constexpr int   tileH = 22;
    static constexpr int   tipW  = 8;      // the arrow's point, and the notch it nests into
    static constexpr float airX  = 2.0f;   // breathing room in the nest, each side

    /** An arrow's cell: even steps, each shape reaching one tip past its cell into the next
        block's notch. The rectangle is the HIT area and the name's home; the point is drawn
        beyond its right edge, in the neighbour's notch, where a click means the neighbour. */
    juce::Rectangle<int> tileArea (int index) const
    {
        const int n     = (int) rows.size();
        const auto lane = getLocalBounds().reduced (padX, 0);
        const int  step = (lane.getWidth() - tipW) / n;

        return juce::Rectangle<int> (lane.getX() + index * step,
                                     lane.getCentreY() - tileH / 2, step, tileH);
    }

    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutStrip)
};

} // namespace orbitamp
