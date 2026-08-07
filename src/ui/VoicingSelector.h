#pragma once

#include "Theme.h"

namespace orbitamp
{

/** One control for the whole voicing choice: type AND voice, picked from a single tree.

    Two separate lists cost a row and made you choose twice for one decision. Here the popup nests
    voices under their type, and every entry carries its type's colour off the character ramp — green
    through red as gain rises — so the type reads without being spelled out.

    Dumb view: it holds a tree of names and a pair of indices, announces a pick through `onPick`, and
    knows nothing about parameters. */
class VoicingSelector : public juce::Component
{
public:
    VoicingSelector() = default;   // the non-copyable macro below declares a ctor, suppressing this

    struct Group
    {
        juce::String name;
        juce::StringArray items;
    };

    void setGroups (juce::Array<Group> newGroups)
    {
        groups = std::move (newGroups);
        repaint();
    }

    void setSelection (int newGroup, int newItem)
    {
        groupIndex = newGroup;
        itemIndex  = newItem;
        repaint();
    }

    int getGroupIndex() const noexcept { return groupIndex; }
    int getItemIndex()  const noexcept { return itemIndex; }

    /** A pick out of the tree, or a step. */
    std::function<void (int group, int item)> onPick;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        auto cell = cellArea().toFloat();
        const auto tint = theme::characterColour (groupIndex);

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (cell, theme::radiusSm);
        g.setColour (hover ? tint : tint.withAlpha (0.45f));
        g.drawRoundedRectangle (cell.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (tint);
        theme::drawTracked (g, label(), cell.reduced (5.0f, 0.0f),
                            theme::displayFont (8.0f), 0.04f, juce::Justification::centred);

        paintChevron (g, prevArea().toFloat(), true);
        paintChevron (g, nextArea().toFloat(), false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        if (prevArea().contains (p)) { step (-1); return; }
        if (nextArea().contains (p)) { step (+1); return; }
        if (! cellArea().contains (p) || groups.isEmpty())
            return;

        juce::PopupMenu menu;
        int id = 1;

        for (int gi = 0; gi < groups.size(); ++gi)
        {
            const auto& grp  = groups.getReference (gi);
            const auto  tint = theme::characterColour (gi);

            juce::PopupMenu sub;
            for (int ii = 0; ii < grp.items.size(); ++ii)
            {
                juce::PopupMenu::Item item (grp.items[ii]);
                item.itemID = id++;
                item.colour = tint;
                item.isTicked = (gi == groupIndex && ii == itemIndex);
                sub.addItem (item);
            }

            juce::PopupMenu::Item head (grp.name);
            head.colour  = tint;
            head.subMenu = std::make_unique<juce::PopupMenu> (sub);
            head.isEnabled = ! grp.items.isEmpty();
            menu.addItem (head);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int choice) { if (choice > 0) pickFlat (choice - 1); });
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellArea().contains (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (false); }

private:
    juce::String label() const
    {
        if (! isValid())
            return "—";

        const auto& grp = groups.getReference (groupIndex);
        return (grp.name + "  " + grp.items[itemIndex]).toUpperCase();
    }

    bool isValid() const
    {
        return groupIndex >= 0 && groupIndex < groups.size()
            && itemIndex  >= 0 && itemIndex  < groups.getReference (groupIndex).items.size();
    }

    /** Walks the tree flattened, so the chevrons step across type boundaries — the audition gesture
        should not stop at the end of a type. */
    void pickFlat (int flat)
    {
        int seen = 0;
        for (int gi = 0; gi < groups.size(); ++gi)
        {
            const int n = groups.getReference (gi).items.size();
            if (flat < seen + n)
            {
                const int ii = flat - seen;
                setSelection (gi, ii);
                if (onPick)
                    onPick (gi, ii);
                return;
            }
            seen += n;
        }
    }

    int flatCount() const
    {
        int n = 0;
        for (const auto& g : groups)
            n += g.items.size();
        return n;
    }

    int flatIndex() const
    {
        int n = 0;
        for (int gi = 0; gi < groupIndex && gi < groups.size(); ++gi)
            n += groups.getReference (gi).items.size();
        return n + itemIndex;
    }

    void step (int delta)
    {
        const int n = flatCount();
        if (n == 0)
            return;

        pickFlat (juce::jlimit (0, n - 1, flatIndex() + delta));
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

    juce::Array<Group> groups;
    int  groupIndex = 0;
    int  itemIndex  = 0;
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicingSelector)
};

} // namespace orbitamp
