#pragma once

#include "Theme.h"

namespace orbitamp

{

/** A two- or three-position switch: every position visible at once, one of them lit.

    For settings that have a handful of NAMED positions rather than a range — a mic angle, a pedal's
    mode toggle. A slider would imply values between them; there are none, because each position is
    a capture that was or was not taken. */
class StepSwitch : public juce::Component
{
public:
    StepSwitch() = default;

    void setItems (juce::StringArray newItems, int selectedIndex)
    {
        items = std::move (newItems);
        index = items.isEmpty() ? 0 : juce::jlimit (0, items.size() - 1, selectedIndex);
        repaint();
    }

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

    int getSelectedIndex() const noexcept { return index; }

    juce::Colour accent = theme::violet;
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        if (items.isEmpty())
            return;

        const float w = r.getWidth() / (float) items.size();

        for (int i = 0; i < items.size(); ++i)
        {
            auto cell = r.withWidth (w).translated (w * (float) i, 0.0f);
            const bool lit = (i == index);

            if (lit)
            {
                g.setColour (accent.withAlpha (0.28f));
                g.fillRoundedRectangle (cell.reduced (1.5f), theme::radiusSm - 1.0f);
            }

            g.setColour (lit ? accent : (i == hovered ? theme::tx : theme::txFaint));
            theme::drawTracked (g, items[i], cell, theme::displayFont (7.5f), 0.04f,
                                juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = cellAt (e.getPosition().x);
        if (hit >= 0)
            setSelectedIndex (hit);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellAt (e.getPosition().x)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

private:
    int cellAt (int x) const
    {
        if (items.isEmpty() || getWidth() <= 0)
            return -1;

        return juce::jlimit (0, items.size() - 1, x * items.size() / getWidth());
    }

    void setHover (int h)
    {
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    juce::StringArray items;
    int index = 0;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSwitch)
};

} // namespace orbitamp
