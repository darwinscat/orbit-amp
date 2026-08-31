// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "ZoneSwitch.h"

#include <vector>

namespace orbitamp
{

/** The LAYOUT popup: which blocks stand on the panel at all. One row per block in chain order,
    the two panel rows separated by a hairline, each switch wearing its block's own accent —
    orange for the captured, violet for ours — so the list reads as a miniature of the panel.

    Dumb view: rows in, `onToggle (index, on)` out. Who owns the prefs, the faceplate, and the
    "a hidden block must not colour the sound" consequence is the editor's business. */
class LayoutPanel final : public juce::Component
{
public:
    struct Row
    {
        juce::String name;
        juce::Colour accent;
        bool on          = true;
        bool startsGroup = false;   // draw the hairline above this row: the panel's row seam
    };

    explicit LayoutPanel (std::vector<Row> blockRows) : rows (std::move (blockRows))
    {
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto sw = std::make_unique<ZoneSwitch>();
            sw->accent = rows[(size_t) i].accent;
            sw->setOn (rows[(size_t) i].on, false);
            sw->onChange = [this, i] (bool on)
            {
                if (onToggle)
                    onToggle (i, on);
            };

            addAndMakeVisible (*sw);
            switches.push_back (std::move (sw));
        }

        int h = padY + headerH;
        for (const auto& r : rows)
            h += (r.startsGroup ? seamH : 0) + rowH;
        setSize (width, h + padY);
    }

    std::function<void (int index, bool on)> onToggle;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (padX, padY);

        g.setColour (theme::tx);
        theme::drawTracked (g, "LAYOUT", r.removeFromTop (headerH).toFloat(),
                            theme::displayFont (12.0f), 0.15f, juce::Justification::centredLeft);

        for (const auto& row : rows)
        {
            if (row.startsGroup)
            {
                g.setColour (theme::hair2);
                g.fillRect (r.removeFromTop (seamH).withSizeKeepingCentre (r.getWidth(), 1));
            }

            g.setColour (row.on ? theme::tx : theme::txDim);
            theme::drawTracked (g, row.name, r.removeFromTop (rowH).toFloat(),
                                theme::displayFont (11.0f), 0.12f, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padX, padY);
        r.removeFromTop (headerH);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].startsGroup)
                r.removeFromTop (seamH);

            switches[i]->setBounds (r.removeFromTop (rowH)
                                        .removeFromRight (34).withSizeKeepingCentre (34, 18));
        }
    }

    /** The popup repaints a row's name when its switch answers — the editor calls back after
        applying, so a refused change (none today) would also read truthfully. */
    void setRowOn (int index, bool on)
    {
        rows[(size_t) index].on = on;
        repaint();
    }

private:
    static constexpr int width = 190, rowH = 26, headerH = 22, seamH = 9, padX = 14, padY = 8;

    std::vector<Row> rows;
    std::vector<std::unique_ptr<ZoneSwitch>> switches;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutPanel)
};

} // namespace orbitamp
