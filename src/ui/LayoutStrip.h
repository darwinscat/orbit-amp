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

    The arrow IS the block's one switch: click it and the block goes to STANDBY or comes back,
    dimming or lighting in its place on the panel — nothing leaves, nothing re-splits. A lit arrow
    works; a dark one stands by. Each wears its block's accent — orange for the captured, violet
    for ours — so the strip reads as a miniature of the panel.

    The chain is FIXED: every row stays, no list adds or takes one away. The two ends and the
    tuner are not rows here at all — the columns and the tuner's strip are always on the face.

    Dumb view: rows in, `onToggle (index, on)` out. Who owns the faceplate and what a switch
    means to the sound is the editor's business. */
class LayoutStrip final : public juce::Component
{
public:
    struct Row
    {
        juce::String name;
        juce::Colour accent;
        bool on = true;

        /** Whether this link has an arrow here at all. A row that is not present takes no width,
            no paint and no click. Rows keep their places in this vector, so an index stays the
            same chain row forever: the editor indexes the strip by `params::ChainRow`, and the
            links that are not arrows — the two ends, the tuner — are simply not present. */
        bool present = true;
    };

    /** The strip's height, in design units — the window budgets for it like for any strip. */
    static constexpr int designHeight = 34;

    /** How far in from the strip's left edge the lane starts. The owner sets 0 when it lines the first
        arrow's tail up with something beside the strip: the tail then stands `tailAir` in. */
    int leftPad = 14;
    static constexpr int tailAir = 2;

    /** Where the owner lines the last arrow's point up with what stands above it, in this strip's
        own x. -1 keeps the strip's own. */
    int arrowsEndX = -1;

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
        // No ground of its own and no line under it: the strip is the header's second line, and the
        // arrows stand on the header's ground as the first line's controls do. The ground and the
        // hairline were for a strip across the whole window between the toolbar and the blocks;
        // beside the brand's marks they made it a box with a start and an end.
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

            // The guards' lights are not here: they burn in the lamps under the columns.
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
        if (e.mods.isPopupMenu())
            return;

        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].present && tileArea ((int) i).contains (e.getPosition()))
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
    static constexpr float airX   = (float) tailAir;   // breathing room in the nest, each side

    /** How many rows have an arrow, and where this one stands among them. */
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

        auto lane = getLocalBounds().withTrimmedLeft (leftPad).withTrimmedRight (padX);

        // The last point ends `airX` short of the lane's right, so the lane ends that much past it.
        if (arrowsEndX >= 0)
            lane = lane.withRight (arrowsEndX + tailAir);

        // Each cell from its own share of the run, so the units a whole-number step would drop are
        // spread over the cells and the last point lands exactly on the lane's end.
        const int span = lane.getWidth() - tipW;
        const int p    = placeOf (index);
        const int x0   = lane.getX() + p * span / n;
        const int x1   = lane.getX() + (p + 1) * span / n;

        return juce::Rectangle<int> (x0, lane.getCentreY() - tileH / 2, x1 - x0, tileH);
    }

    std::vector<Row> rows;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutStrip)
};

} // namespace orbitamp
