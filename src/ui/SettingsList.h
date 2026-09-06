// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <functional>
#include <vector>

namespace orbitamp
{

/** A page of switches, and nothing else. Each row says what it is, says in one line what it does,
    and carries a toggle at its right edge.

    The SMALLEST thing that does the job, on purpose. There was a plan to write this move-ready for
    appkit so a sibling could share it; a settings row is thirty lines of paint, and a shared
    widget that has to please two products before either has finished changing its mind is how a
    thirty-line thing becomes a hundred-line thing with a theme struct. It moves when a sibling
    actually asks, and not before.

    Nothing here reads or writes anything: a row is handed a getter and a setter. What lives on the
    machine and what lives in the preset is the caller's business, and both kinds sit on this page
    looking the same, because to the hand they ARE the same. */
class SettingsList final : public juce::Component
{
public:
    // Declaring the deleted copy constructor below suppresses the implicit default one.
    SettingsList() = default;

    struct Row
    {
        juce::String name;
        juce::String note;                  // one line, under the name — what the switch does
        std::function<bool()> get;
        std::function<void (bool)> set;
        juce::Colour dot;                   // a link's own accent, when the row is about a link
        bool hasDot = false;
    };

    void setRows (std::vector<Row> newRows)
    {
        rows = std::move (newRows);
        setSize (getWidth(), (int) rows.size() * rowH);
        repaint();
    }

    /** Rows read their own state on every paint, so anything that moves them from elsewhere —
        an undo, a register, a preset, the strip — shows here without being told. */
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        for (size_t i = 0; i < rows.size(); ++i)
        {
            auto cell = r.removeFromTop (rowH);
            const auto& row = rows[i];
            const bool on = row.get != nullptr && row.get();
            const bool over = (int) i == hovered;

            if (over)
            {
                g.setColour (theme::panel2);
                g.fillRoundedRectangle (cell.toFloat().reduced (2.0f, 1.0f), theme::radiusSm);
            }

            auto sw = cell.removeFromRight (switchW + 14).withSizeKeepingCentre (switchW, switchH);
            paintSwitch (g, sw.toFloat(), on);

            auto text = cell.reduced (12, 4);

            if (row.hasDot)
            {
                auto dot = text.removeFromLeft (14);
                g.setColour (row.dot);
                g.fillEllipse (dot.toFloat().withSizeKeepingCentre (7.0f, 7.0f));
            }

            // A row with nothing to explain does not pretend to have two lines: the name takes
            // the cell and sits in the middle of it, where the eye looks for one line of text.
            const auto nameCell = row.note.isEmpty() ? text : text.removeFromTop (text.getHeight() / 2);

            g.setColour (theme::tx);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (row.name, nameCell, juce::Justification::centredLeft);

            if (row.note.isNotEmpty())
            {
                g.setColour (theme::txDim);
                g.setFont (juce::FontOptions (13.0f));
                g.drawText (row.note, text, juce::Justification::centredLeft);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition().y);

        if (i >= 0 && rows[(size_t) i].set != nullptr)
        {
            rows[(size_t) i].set (! (rows[(size_t) i].get != nullptr && rows[(size_t) i].get()));
            repaint();
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (const int i = rowAt (e.getPosition().y); i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0) { hovered = -1; repaint(); }
    }

    void resized() override { setSize (getWidth(), (int) rows.size() * rowH); }

    static constexpr int rowH = 40;   // two lines of 13 and air: the floor is what a player READS

private:
    int rowAt (int y) const
    {
        const int i = y / rowH;
        return i >= 0 && i < (int) rows.size() ? i : -1;
    }

    /** The house toggle: a lilac track that fills when it is on, and a knob that slides. Drawn
        rather than assembled, because a switch is a rounded rectangle and a circle. */
    static void paintSwitch (juce::Graphics& g, juce::Rectangle<float> r, bool on)
    {
        const float rad = r.getHeight() * 0.5f;

        g.setColour (on ? theme::violet.withAlpha (0.85f) : juce::Colour (0xff17171d));
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? theme::lilac : theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);

        const float d = r.getHeight() - 6.0f;
        const float x = on ? r.getRight() - d - 3.0f : r.getX() + 3.0f;
        g.setColour (on ? juce::Colours::white : theme::txDim);
        g.fillEllipse (x, r.getY() + 3.0f, d, d);
    }

    static constexpr int switchW = 34, switchH = 18;

    std::vector<Row> rows;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsList)
};

} // namespace orbitamp
