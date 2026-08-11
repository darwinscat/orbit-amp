#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** A vertical slide switch — the hardware kind: a narrow slot on the left, the lever sitting AT
    the chosen position, the position names stacked beside the slot, the chosen one lit. Click a
    row (or the slot at its height) and the lever goes there.

    Positions run top to bottom in item order. Scales to any count — a two-way OFF/OCTAVE and a
    five-way rotary both read as the same piece of hardware. */
class VSwitch final : public juce::Component
{
public:
    VSwitch() = default;   // the non-copyable macro declares a ctor, which suppresses this

    static constexpr int rowH   = 22;
    static constexpr int slotW  = 14;

    juce::Colour accent = theme::orange;

    void setItems (juce::StringArray newItems, int selectedIndex)
    {
        items = std::move (newItems);
        index = juce::jlimit (0, juce::jmax (0, items.size() - 1), selectedIndex);
        repaint();
    }

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

    int count() const noexcept       { return items.size(); }
    int idealHeight() const noexcept { return rowH * juce::jmax (1, items.size()); }

    void paint (juce::Graphics& g) override
    {
        if (items.isEmpty())
            return;

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
        if (items.isEmpty() || getHeight() <= 0)
            return;

        setSelectedIndex (juce::jlimit (0, items.size() - 1,
                                        (int) (e.position.y / ((float) getHeight()
                                                               / (float) items.size()))));
    }

private:
    juce::StringArray items;
    int index = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VSwitch)
};

} // namespace orbitamp
