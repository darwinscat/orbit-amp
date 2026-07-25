#pragma once

#include "Theme.h"

namespace orbitamp
{

/** The faceplate's list control: a flat cell showing the current item, optionally flanked by step
    chevrons. Custom-drawn rather than a juce::ComboBox because the whole face is one visual system
    and a native combo drags its own look in.

    Dumb view — it holds an index and a list of names, announces changes through `onChange`, and
    knows nothing about parameters. */
class Selector : public juce::Component
{
public:
    Selector (juce::Colour cellAccent, bool showStepButtons)
        : accent (cellAccent), showNav (showStepButtons) {}

    void setItems (juce::StringArray newItems, int newIndex)
    {
        items = std::move (newItems);
        index = items.isEmpty() ? 0 : juce::jlimit (0, items.size() - 1, newIndex);
        repaint();
    }

    int getSelectedIndex() const noexcept { return index; }

    void setSelectedIndex (int newIndex, juce::NotificationType notify = juce::sendNotification)
    {
        if (items.isEmpty())
            return;

        const int clamped = juce::jlimit (0, items.size() - 1, newIndex);
        if (clamped == index)
            return;

        index = clamped;
        repaint();

        if (notify != juce::dontSendNotification && onChange != nullptr)
            onChange (index);
    }

    std::function<void (int)> onChange;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        auto cell = cellArea().toFloat();

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (cell, theme::radiusSm);
        g.setColour (hoverCell ? accent : theme::hair2);
        g.drawRoundedRectangle (cell.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (items.isEmpty() ? theme::txFaint : theme::tx);
        theme::drawTracked (g, items.isEmpty() ? "—" : items[index].toUpperCase(), cell.reduced (5.0f, 0.0f),
                            theme::displayFont (8.0f), 0.03f, juce::Justification::centred);

        if (showNav)
        {
            paintChevron (g, prevArea().toFloat(), true);
            paintChevron (g, nextArea().toFloat(), false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        if (showNav && prevArea().contains (p)) { setSelectedIndex (index - 1); return; }
        if (showNav && nextArea().contains (p)) { setSelectedIndex (index + 1); return; }

        if (! cellArea().contains (p) || items.isEmpty())
            return;

        juce::PopupMenu menu;
        for (int i = 0; i < items.size(); ++i)
            menu.addItem (i + 1, items[i], true, i == index);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int choice) { if (choice > 0) setSelectedIndex (choice - 1); });
    }

    void mouseMove (const juce::MouseEvent& e) override { updateHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override   { updateHover ({ -1, -1 }); }

private:
    void updateHover (juce::Point<int> p)
    {
        const bool over = cellArea().contains (p);
        if (over != hoverCell)
        {
            hoverCell = over;
            repaint();
        }
    }

    juce::Rectangle<int> prevArea() const { return getLocalBounds().removeFromLeft (navWidth); }
    juce::Rectangle<int> nextArea() const { return getLocalBounds().removeFromRight (navWidth); }

    juce::Rectangle<int> cellArea() const
    {
        auto r = getLocalBounds();
        if (! showNav)
            return r;

        r.removeFromLeft (navWidth + navGap);
        r.removeFromRight (navWidth + navGap);
        return r;
    }

    void paintChevron (juce::Graphics& g, juce::Rectangle<float> area, bool pointsLeft) const
    {
        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (area, theme::radiusSm);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (area.reduced (0.5f), theme::radiusSm, 1.0f);

        const auto c = area.getCentre();
        const float w = 2.5f, h = 4.0f;

        juce::Path p;
        p.startNewSubPath (c.x + (pointsLeft ? w : -w), c.y - h);
        p.lineTo          (c.x + (pointsLeft ? -w : w), c.y);
        p.lineTo          (c.x + (pointsLeft ? w : -w), c.y + h);

        g.setColour (theme::txDim);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static constexpr int navWidth = 19;
    static constexpr int navGap   = 5;

    juce::Colour accent;
    bool showNav;
    juce::StringArray items;
    int index = 0;
    bool hoverCell = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Selector)
};

} // namespace orbitamp
