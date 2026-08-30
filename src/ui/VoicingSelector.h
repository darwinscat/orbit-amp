#pragma once

#include "Theme.h"

#include <optional>

namespace orbitamp
{

/** One control for the whole device choice: a flat list of everything loadable, picked from a popup.
    A combo — the name and a chevron — and nothing else: it stands on the block's top border now,
    where a pair of stepping arrows was furniture the line has no room for.

    Flat rather than nested, because the list is what a player actually has rather than a taxonomy.
    Every entry carries its own place on the character ramp — green through red as gain rises — so a
    row of devices reads as a gradient and the ordering does the work a "type" heading used to do.

    A section break draws a rule above an entry. That is all a section is here: the list stays one
    list, because auditioning should not care where a device came from.

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

    /** As wide as the NAME on show and no wider — the combo sits on a border line and the line
        should run on both sides of it, the way it runs on both sides of the block's own name. */
    int idealWidth() const
    {
        const float text = theme::trackedWidth (label(), theme::displayFont (fontHeight), tracking);
        return juce::roundToInt (text) + 2 * padX + (int) chevronW;
    }

    /** A pick out of the list. */
    std::function<void (int index)> onPick;

    float fontHeight = 8.0f;
    float tracking   = 0.04f;   // letter-spacing, as a fraction of the height — a block's name uses 0.15

    /** Boxed: a dark cell with a hairline, the control it was. Unboxed: the same dark as a pill and
        no line around it — for standing on a block's border beside the switch, a pill of the same
        family. Whoever places it there opens the line under it (the frame does, in its own layer:
        a mask painted by this control would go half-transparent with a switched-off block and let
        the line strike the name through). Hover lifts the text. */
    bool boxed = true;

    /** A list with no character ramp — a DSP block's own choices, PLATE or HALL — wears one colour,
        the block's accent, instead of a place on the green-to-red. */
    std::optional<juce::Colour> tint;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        auto cell = cellArea().toFloat();
        const auto tint = ! isValid() ? theme::txDim
                        : this->tint.has_value() ? *this->tint
                        : theme::characterColour (entries.getReference (index).character);

        if (boxed)
        {
            g.setColour (juce::Colour (0xff0d0d14));
            g.fillRoundedRectangle (cell, theme::radiusSm);
            g.setColour (hover ? tint : tint.withAlpha (0.45f));
            g.drawRoundedRectangle (cell.reduced (0.5f), theme::radiusSm, 1.0f);
            g.setColour (tint);
        }
        else
        {
            // A pill of the panel's dark — the switch beside it is one of the same family — and no
            // hairline. It masks nothing: the frame opens the line under it, in its own layer.
            g.setColour (juce::Colour (0xff0d0d14));
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            g.setColour (hover ? tint : tint.withAlpha (0.85f));
        }

        // The name fits the room it was given. A long one — "GUITAR BUTLER CLEAN" on a half-width
        // block — comes down in size to a reading floor, and past that loses its end to an
        // ellipsis rather than both ends to the edges.
        const auto textArea = cell.reduced ((float) padX, 0.0f).withTrimmedRight (chevronW);
        auto  text = label();
        float size = fontHeight;

        while (size > minFontHeight
               && theme::trackedWidth (text, theme::displayFont (size), tracking) > textArea.getWidth())
            size -= 1.0f;

        while (text.length() > 3
               && theme::trackedWidth (text, theme::displayFont (size), tracking) > textArea.getWidth())
            text = text.dropLastCharacters (text.endsWithChar ((juce::juce_wchar) 0x2026) ? 2 : 1)
                       + juce::String::charToString ((juce::juce_wchar) 0x2026);

        theme::drawTracked (g, text, textArea, theme::displayFont (size), tracking,
                            juce::Justification::centred);

        // The chevron that says "a list": down, at the right, in the same tint as the name.
        juce::Path v;
        const float cx = cell.getRight() - 12.0f, cy = cell.getCentreY() - 1.0f;
        v.startNewSubPath (cx - 3.0f, cy);
        v.lineTo (cx, cy + 3.0f);
        v.lineTo (cx + 3.0f, cy);
        g.strokePath (v, juce::PathStrokeType (1.2f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! cellArea().contains (e.getPosition()) || entries.isEmpty())
            return;

        juce::PopupMenu menu;

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& e2 = entries.getReference (i);

            if (e2.startsSection && i > 0)
                menu.addSeparator();

            juce::PopupMenu::Item item (e2.name);
            item.itemID = i + 1;
            item.colour = tint.has_value() ? *tint : theme::characterColour (e2.character);
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

    void setHover (bool h)
    {
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    juce::Rectangle<int> cellArea() const { return getLocalBounds(); }

    static constexpr float chevronW      = 14.0f;
    static constexpr float minFontHeight = 11.0f;   // the reading floor, and the size the type stops shrinking at
    static constexpr int   padX          = 10;

    juce::Array<Entry> entries;
    int  index = 0;
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicingSelector)
};

} // namespace orbitamp
