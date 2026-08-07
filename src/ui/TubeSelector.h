#pragma once

#include "Theme.h"

#include <felitronics/appkit/DeviceGlyph.h>

namespace orbitamp
{

/** The output section as one choice: which bottle and how many, drawn as the bottles themselves.

        EL84  ()
        EL84  ()()
        EL34  ()
        ...

    One control rather than a list plus a count switch, because you do not pick a tube and then
    decide how many — you pick an output section, and "EL84 x2" is one thing with one sound. The
    glyphs are appkit's, the same ones the capture metadata draws elsewhere, so a tube looks like a
    tube wherever it appears. */
class TubeSelector : public juce::Component
{
public:
    TubeSelector() = default;

    /** Names in order; each gets a one-bottle and a two-bottle entry. */
    void setTubes (juce::StringArray tubeNames)
    {
        names = std::move (tubeNames);
        repaint();
    }

    void setSelection (int tubeIndex, int tubeCount)
    {
        tube  = juce::jmax (0, tubeIndex);
        count = juce::jlimit (1, 2, tubeCount);
        repaint();
    }

    int getTube() const noexcept  { return tube; }
    int getCount() const noexcept { return count; }

    /** A pick: which bottle, and how many of it. */
    std::function<void (int tubeIndex, int tubeCount)> onPick;

    void paint (juce::Graphics& g) override
    {
        auto cell = cellArea().toFloat();

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (cell, theme::radiusSm);
        g.setColour (hover ? theme::orange : theme::orange.withAlpha (0.45f));
        g.drawRoundedRectangle (cell.reduced (0.5f), theme::radiusSm, 1.0f);

        paintEntry (g, cell.reduced (6.0f, 0.0f), tube, count, theme::orange);

        paintChevron (g, prevArea().toFloat(), true);
        paintChevron (g, nextArea().toFloat(), false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();

        if (prevArea().contains (p)) { step (-1); return; }
        if (nextArea().contains (p)) { step (+1); return; }
        if (! cellArea().contains (p) || names.isEmpty())
            return;

        juce::PopupMenu menu;

        for (int i = 0; i < entryCount(); ++i)
            menu.addCustomItem (i + 1, std::make_unique<Entry> (names[i / 2], i % 2 + 1,
                                                                i == flatIndex()),
                                nullptr, names[i / 2]);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int choice) { if (choice > 0) pickFlat (choice - 1); });
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (cellArea().contains (e.getPosition())); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (false); }

private:
    /** One row of the popup: the name, then that many bottles. */
    struct Entry : juce::PopupMenu::CustomComponent
    {
        Entry (juce::String tubeName, int bottles, bool ticked)
            : juce::PopupMenu::CustomComponent (ticked), name (std::move (tubeName)), count (bottles) {}

        void getIdealSize (int& w, int& h) override { w = 132; h = 26; }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();

            if (isItemHighlighted())
            {
                g.setColour (theme::orange.withAlpha (0.18f));
                g.fillRoundedRectangle (r.reduced (2.0f), theme::radiusSm);
            }

            paintEntry (g, r.reduced (8.0f, 0.0f), name, count, theme::orange);
        }

        juce::String name;
        int count;
    };

    static void paintEntry (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                            int count, juce::Colour tint)
    {
        // Name left, bottles right — the row reads as a sentence: this tube, this many.
        const float glyphW = area.getHeight() * 0.9f;
        auto glyphs = area.removeFromRight (glyphW * (float) count + 2.0f);

        g.setColour (tint);
        theme::drawTracked (g, name.toUpperCase(), area, theme::displayFont (8.0f), 0.06f,
                            juce::Justification::centredLeft);

        const felitronics::appkit::DeviceSpec spec { { felitronics::appkit::DeviceType::tube, count } };
        felitronics::appkit::drawDeviceSpecStatic (g, glyphs, spec);
    }

    void paintEntry (juce::Graphics& g, juce::Rectangle<float> area, int tubeIndex, int bottles,
                     juce::Colour tint) const
    {
        if (juce::isPositiveAndBelow (tubeIndex, names.size()))
            paintEntry (g, area, names[tubeIndex], bottles, tint);
    }

    int entryCount() const { return names.size() * 2; }
    int flatIndex() const  { return tube * 2 + (count - 1); }

    void pickFlat (int flat)
    {
        if (! juce::isPositiveAndBelow (flat, entryCount()))
            return;

        setSelection (flat / 2, flat % 2 + 1);

        if (onPick)
            onPick (tube, count);
    }

    void step (int delta)
    {
        if (entryCount() > 0)
            pickFlat (juce::jlimit (0, entryCount() - 1, flatIndex() + delta));
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

    juce::StringArray names;
    int  tube  = 2;
    int  count = 2;
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TubeSelector)
};

} // namespace orbitamp
