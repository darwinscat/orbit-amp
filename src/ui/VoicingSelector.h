#pragma once

#include "Theme.h"

namespace orbitamp
{

/** One control for the whole device choice: a flat list of everything loadable, picked from a popup
    or stepped through with the chevrons.

    Flat rather than nested, because the list is what a player actually has rather than a taxonomy.
    Every entry carries its own place on the character ramp — green through red as gain rises — so a
    row of devices reads as a gradient and the ordering does the work a "type" heading used to do.

    A section break draws a rule above an entry. That is all a section is here: the list stays one
    list, and stepping with the chevrons crosses the rule without stopping, because auditioning
    should not care where a device came from.

    Dumb view: it holds names, an index, and announces a pick through `onPick`. It knows nothing about
    parameters, packs, or files. */
class VoicingSelector : public juce::Component
{
public:
    VoicingSelector() = default;   // the non-copyable macro below declares a ctor, suppressing this

    struct Entry
    {
        juce::String name;
        int  character = 0;         // ramp index: 0 green (clean) .. 4 red
        bool startsSection = false; // draw a separator above this entry
    };

    void setEntries (juce::Array<Entry> newEntries)
    {
        entries = std::move (newEntries);
        index = juce::jlimit (0, juce::jmax (0, entries.size() - 1), index);
        repaint();
    }

    void setSelection (int newIndex)
    {
        index = newIndex;
        repaint();
    }

    int getSelection() const noexcept { return index; }
    int getCount() const noexcept     { return entries.size(); }

    /** A pick out of the list, or a step. */
    std::function<void (int index)> onPick;

    float fontHeight = 8.0f;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        auto cell = cellArea().toFloat();
        const auto tint = isValid() ? theme::characterColour (entries.getReference (index).character)
                                    : theme::txDim;

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (cell, theme::radiusSm);
        g.setColour (hover ? tint : tint.withAlpha (0.45f));
        g.drawRoundedRectangle (cell.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (tint);
        theme::drawTracked (g, label(), cell.reduced (5.0f, 0.0f),
                            theme::displayFont (fontHeight), 0.04f, juce::Justification::centred);

        paintChevron (g, prevArea().toFloat(), true);
        paintChevron (g, nextArea().toFloat(), false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        if (prevArea().contains (p)) { step (-1); return; }
        if (nextArea().contains (p)) { step (+1); return; }
        if (! cellArea().contains (p) || entries.isEmpty())
            return;

        juce::PopupMenu menu;

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& e2 = entries.getReference (i);

            if (e2.startsSection && i > 0)
                menu.addSeparator();

            juce::PopupMenu::Item item (e2.name);
            item.itemID = i + 1;
            item.colour = theme::characterColour (e2.character);
            item.isTicked = (i == index);
            menu.addItem (item);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int choice) { if (choice > 0) pick (choice - 1); });
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellArea().contains (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (false); }

private:
    juce::String label() const
    {
        if (! isValid())
            return juce::String::charToString ((juce::juce_wchar) 0x2014);   // em dash

        return entries.getReference (index).name.toUpperCase();
    }

    bool isValid() const { return index >= 0 && index < entries.size(); }

    void pick (int i)
    {
        setSelection (i);

        if (onPick)
            onPick (i);
    }

    void step (int delta)
    {
        if (entries.isEmpty())
            return;

        pick (juce::jlimit (0, entries.size() - 1, index + delta));
    }

    void setHover (bool h)
    {
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    juce::Rectangle<int> prevArea() const { return getLocalBounds().removeFromLeft (navWidth); }
    juce::Rectangle<int> nextArea() const { return getLocalBounds().removeFromRight (navWidth); }

    juce::Rectangle<int> cellArea() const
    {
        auto r = getLocalBounds();
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
        const float w = 2.2f, h = 3.6f;

        juce::Path p;
        p.startNewSubPath (c.x + (pointsLeft ? w : -w), c.y - h);
        p.lineTo          (c.x + (pointsLeft ? -w : w), c.y);
        p.lineTo          (c.x + (pointsLeft ? w : -w), c.y + h);

        g.setColour (theme::txDim);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    static constexpr int navWidth = 16;
    static constexpr int navGap   = 4;

    juce::Array<Entry> entries;
    int  index = 0;
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicingSelector)
};

} // namespace orbitamp
