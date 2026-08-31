// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** A slide switch — the hardware kind: a recessed slot with the lever sitting AT the chosen
    position and the position names beside it, the chosen one lit. Click a name (or the slot at
    that point) and the lever goes there.

    IT LIES DOWN TOO. Vertical is the shape of the real thing and reads best where there is height
    to spare; under a captured block's GAIN dial there is not, and a two-row stack was taking its
    height straight out of the dial's diameter. `setHorizontal` turns the slot on its side and runs
    the names along it, so a device that brought an octave switch keeps the same big dial as one
    that brought nothing.

    Positions run in item order either way. Scales to any count — a two-way OFF/OCTAVE and a
    five-way rotary both read as the same piece of hardware. */
class VSwitch final : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    VSwitch() = default;   // the non-copyable macro declares a ctor, which suppresses this

    static constexpr int rowH   = 22;
    static constexpr int slotW  = 14;
    static constexpr int lyingH = 32;   // the slot, then the names under it at reading size

    juce::Colour accent = theme::orange;

    /** Lay the slot along the width instead of down the height. */
    void setHorizontal (bool shouldLieDown)
    {
        horizontal = shouldLieDown;
        repaint();
    }

    void setItems (juce::StringArray newItems, int selectedIndex)
    {
        items = std::move (newItems);
        index = juce::jlimit (0, juce::jmax (0, items.size() - 1), selectedIndex);
        repaint();
    }

    /** Lying down WITHOUT the names: the slot and its lever alone, one line high — for a row that
        writes the name and the chosen position beside it and keeps the list for a hint. */
    void setNamesShown (bool shown)
    {
        namesShown = shown;
        repaint();
    }

    int selectedIndex() const noexcept { return index; }

    void setSelectedIndex (int newIndex, juce::NotificationType notify = juce::sendNotification)
    {
        newIndex = juce::jlimit (0, juce::jmax (0, items.size() - 1), newIndex);
        if (newIndex == index)
            return;

        index = newIndex;
        repaint();

        if (notify != juce::dontSendNotification && onChange != nullptr)
            onChange (index);
    }

    std::function<void (int)> onChange;

    int count() const noexcept { return items.size(); }

    int idealHeight() const noexcept
    {
        // Lying down it is one slot plus one line of names, not one row per position — and
        // without the names, the slot alone.
        return horizontal ? (namesShown ? lyingH : slotW + 4) : rowH * juce::jmax (1, items.size());
    }

    void paint (juce::Graphics& g) override
    {
        if (items.isEmpty())
            return;

        if (horizontal)
        {
            paintLyingDown (g);
            return;
        }

        const auto r = getLocalBounds().toFloat();
        const float rh = r.getHeight() / (float) items.size();

        // The slot: a recessed vertical channel the lever travels in.
        const juce::Rectangle<float> slot (r.getX() + 1.0f, r.getY() + 2.0f,
                                           (float) slotW - 2.0f, r.getHeight() - 4.0f);
        g.setColour (theme::bezel);
        g.fillRoundedRectangle (slot, 5.0f);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (slot.reduced (0.5f), 5.0f, 1.0f);

        // The detent marks: one tick in the slot per position.
        g.setColour (theme::hair2);
        for (int i = 0; i < items.size(); ++i)
            g.fillRect (slot.getCentreX() - 2.0f, r.getY() + rh * ((float) i + 0.5f) - 0.5f,
                        4.0f, 1.0f);

        // The lever, AT the chosen position: a solid bar filling most of the slot's width.
        {
            const float cy = r.getY() + rh * ((float) index + 0.5f);
            g.setColour (accent);
            g.fillRoundedRectangle (slot.getX() + 1.5f, cy - 6.0f, slot.getWidth() - 3.0f, 12.0f,
                                    3.5f);
        }

        // The names beside the slot, the chosen one lit.
        for (int i = 0; i < items.size(); ++i)
        {
            const juce::Rectangle<float> row (r.getX() + (float) slotW + 6.0f,
                                              r.getY() + rh * (float) i,
                                              r.getWidth() - (float) slotW - 6.0f, rh);
            g.setColour (i == index ? accent : theme::txDim);
            theme::drawTracked (g, items[i], row, theme::displayFont (12.0f), 0.08f,
                                juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (items.isEmpty())
            return;

        const float extent = horizontal ? (float) getWidth() : (float) getHeight();
        const float along  = horizontal ? e.position.x : e.position.y;

        if (extent <= 0.0f)
            return;

        setSelectedIndex (juce::jlimit (0, items.size() - 1,
                                        (int) (along / (extent / (float) items.size()))));
    }

private:
    /** The same switch on its side: the slot runs along the top and each name sits under its own
        detent, so the lever's position and the lit word are one thing rather than two columns. */
    void paintLyingDown (juce::Graphics& g)
    {
        const auto  r  = getLocalBounds().toFloat();
        const float cw = r.getWidth() / (float) items.size();

        // With names under it the slot rides the top; alone, it sits in the middle of its row.
        const float slotY = namesShown ? r.getY() + 1.0f : r.getCentreY() - ((float) slotW - 2.0f) * 0.5f;
        const juce::Rectangle<float> slot (r.getX() + 2.0f, slotY, r.getWidth() - 4.0f, (float) slotW - 2.0f);
        g.setColour (theme::bezel);
        g.fillRoundedRectangle (slot, 5.0f);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (slot.reduced (0.5f), 5.0f, 1.0f);

        g.setColour (theme::hair2);
        for (int i = 0; i < items.size(); ++i)
            g.fillRect (r.getX() + cw * ((float) i + 0.5f) - 0.5f, slot.getCentreY() - 2.0f,
                        1.0f, 4.0f);

        {
            const float cx = r.getX() + cw * ((float) index + 0.5f);
            g.setColour (accent);
            g.fillRoundedRectangle (cx - 6.0f, slot.getY() + 1.5f, 12.0f, slot.getHeight() - 3.0f,
                                    3.5f);
        }

        if (! namesShown)
            return;

        for (int i = 0; i < items.size(); ++i)
        {
            const juce::Rectangle<float> cell (r.getX() + cw * (float) i, slot.getBottom() + 1.0f,
                                               cw, r.getBottom() - slot.getBottom() - 1.0f);
            g.setColour (i == index ? accent : theme::txDim);
            theme::drawTracked (g, items[i], cell, theme::displayFont (11.0f), 0.06f,
                                juce::Justification::centred);
        }
    }

    juce::StringArray items;
    int index = 0;
    bool horizontal = false;
    bool namesShown = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSwitch)
};

} // namespace orbitamp
