// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** A PACK'S TONE SWITCH AS A CELL OF THE CONSOLE'S ROW — the SGT-DI's ULTRA LO and ULTRA HI, standing among the dials
    as the cuts' own switches do, and looking like them: its name over it on the names' line, the same oval on the line
    HPF's and LPF's stand on, and its marks either side of the oval, as on the pedal —

           ULTRA LO              ULTRA HI
         -  ( o    )  +         0  ( o    )  +

    The marks are the pack's own words for its first and last positions — but a position the pack calls off, in any
    case, or 0, is written 0, as the pedal marks it (the owner, 02.11: markFor); a three-way's middle position is the
    oval's centre and has no word. The end it stands at is lit. The oval is lit, its dot white, wherever it stands but
    at such a position — a switch at rest reads as a cut switched off does.

    A HAND'S CONTROL, one position at a time: a click moves it one position toward the side of the dot the click lands
    on — from the end it stands against, back the other way, so every click moves it — and a drag one position the way
    it goes, however far it goes. One press, one move: whatever it is attached to sees one gesture.

    Its bounds run from the names' line down to the cut switches' foot (EqSection::innerCell): the name in the top
    `nameH`, the oval and its marks in the bottom `ovalH`. */
class ToggleSwitch final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    ToggleSwitch() = default;   // the non-copyable macro declares a ctor, which suppresses this

    static constexpr int   nameH    = 14;      // the row's names' line (EqSection::labelRowH)
    static constexpr int   ovalW    = 30;      // the cuts' switch's
    static constexpr int   ovalH    = 16;
    static constexpr int   markGap  = 4;       // between the oval and a mark
    static constexpr int   dragStep = 6;       // how far a drag goes before it moves
    static constexpr float fontH    = 13.0f;   // the face's floor for anything read, the name and the marks

    juce::Colour accent = theme::orange;

    /** ONE LINE (the owner, 02.11): the name on the left, then the marks either side of the oval, all on one line —
        as the bass's tone row stands its switches, two a side of the dials. */
    bool oneLine = false;
    static constexpr int nameGap = 6;   // between the name and the first mark, on one line

    /** THE WALKER'S LOOK (the owner, 03.11: the switches exactly as RedGrooveBass has them): one line — the name, the
        end words either side of a dark slot, a round cap in it at its place, the word it stands at lit — in the face's
        12 px. A two-word name is written short, as there: ULTRA LO → U-LO. `slotAt` lines the slots of a column up
        (its left edge in the switch's bounds; -1 right after the switch's own left part). */
    bool walkerLook = false;
    float slotAt = -1.0f;
    static constexpr float walkerFontH = 12.0f;
    static constexpr int   slotW = 40, slotH = 20, walkerGap = 4, markPad = 3;

    juce::String shownName() const
    {
        if (! walkerLook)
            return name;
        const auto words = juce::StringArray::fromTokens (name.trim(), " ", "");
        return words.size() == 2 && name.length() > 5 ? words[0].substring (0, 1) + "-" + words[1] : name;
    }

    /** The walker's line: how much stands left of the slot (the name, the first word) and right of it (the last). */
    float leftPart() const
    {
        const auto n = shownName();
        return (n.isNotEmpty() ? walkerText (n) + (float) walkerGap : 0.0f) + walkerText (markFor (items[0])) + (float) markPad;
    }
    float rightPart() const { return (float) markPad + walkerText (markFor (items[items.size() - 1])); }

    /** The switch's name and its positions, the chosen one `selected`. */
    void setItems (juce::String switchName, juce::StringArray positions, int selected)
    {
        name  = std::move (switchName);
        items = std::move (positions);
        index = juce::jlimit (0, juce::jmax (0, items.size() - 1), selected);
        repaint();
    }

    int selectedIndex() const noexcept { return index; }
    int count() const noexcept { return items.size(); }

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
    std::function<void()> onMenu;       // a right click, when set — and then it does not move

    /** What a position is written as beside the oval: 0 where the pack calls it off or 0, its own word otherwise. */
    static juce::String markFor (const juce::String& position)
    {
        return isRest (position) ? juce::String ("0") : position;
    }

    /** The oval with its marks either side, or the name over it, whichever is wider. */
    int idealWidth() const
    {
        if (walkerLook && ! items.isEmpty())
            return juce::roundToInt (std::ceil (leftPart() + (float) slotW + rightPart())) + 2;
        const float line = markWidth (0) + (float) (markGap + ovalW + markGap) + markWidth (items.size() - 1);
        if (oneLine)
            return juce::roundToInt (std::ceil (textWidth (name) + (float) nameGap + line)) + 4;
        return juce::roundToInt (std::ceil (juce::jmax (line, textWidth (name)))) + 4;
    }

    void paint (juce::Graphics& g) override
    {
        if (items.isEmpty())
            return;

        if (walkerLook)
        {
            paintWalker (g);
            return;
        }

        g.setFont (theme::displayFont (fontH));

        // The name, as the cuts' are written over theirs.
        g.setColour (theme::txDim);
        if (oneLine)
            g.drawText (name, getLocalBounds().withWidth (juce::roundToInt (std::ceil (textWidth (name)))), juce::Justification::centredLeft, false);
        else
            g.drawText (name, getLocalBounds().removeFromTop (nameH), juce::Justification::centred, false);

        // The oval, as the cuts' switch draws it.
        const auto line = lineArea();
        const auto oval = ovalIn (line);
        const auto sw   = oval.reduced (0.5f);
        const float r   = sw.getHeight() * 0.5f;
        const bool lit  = ! atRest (index);

        g.setColour (lit ? accent.withAlpha (0.5f).overlaidWith (juce::Colour (0xff26262f)) : juce::Colour (0xff26262f));
        g.fillRoundedRectangle (sw, r);
        g.setColour (accent.withAlpha (0.85f));
        g.drawRoundedRectangle (sw, r, 1.2f);

        const float dot = sw.getHeight() - 4.0f;
        g.setColour (lit ? juce::Colours::white : juce::Colour (0xff8a8a96));
        g.fillEllipse (dotX (sw, index) - dot * 0.5f, sw.getCentreY() - dot * 0.5f, dot, dot);

        // The marks either side, the end it stands at lit.
        const int last = items.size() - 1;
        g.setColour (index == 0 ? accent : theme::txDim);
        g.drawText (markFor (items[0]), juce::Rectangle<float> (line.getX(), line.getY(), oval.getX() - (float) markGap - line.getX(), line.getHeight()),
                    juce::Justification::centredRight, false);
        g.setColour (index == last ? accent : theme::txDim);
        g.drawText (markFor (items[last]), juce::Rectangle<float> (oval.getRight() + (float) markGap, line.getY(),
                                                         line.getRight() - oval.getRight() - (float) markGap, line.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        menuClick = e.mods.isPopupMenu() && onMenu != nullptr;
        if (menuClick)
            onMenu();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int dx = e.getDistanceFromDragStartX();
        if (menuClick || dragged || std::abs (dx) < dragStep)
            return;

        dragged = true;
        setSelectedIndex (index + (dx > 0 ? 1 : -1));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (menuClick || dragged || items.size() < 2)
            return;

        // Toward the side of the dot the click lands on — and from the end it stands against, back.
        const float dot = walkerLook ? capCentreX() : dotX (ovalIn (lineArea()).reduced (0.5f), index);
        int step = e.position.x < dot ? -1 : 1;
        if (! juce::isPositiveAndBelow (index + step, items.size()))
            step = -step;
        setSelectedIndex (index + step);
    }

private:
    static float textWidth (const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (theme::displayFont (fontH), s);
    }

    static juce::Font walkerFont() { return theme::displayFont (walkerFontH).withExtraKerningFactor (0.04f); }
    static float walkerText (const juce::String& s) { return juce::GlyphArrangement::getStringWidth (walkerFont(), s); }

    juce::Rectangle<float> slotRect() const
    {
        const float x = slotAt >= 0.0f ? slotAt : leftPart();
        return { x, getLocalBounds().toFloat().getCentreY() - 0.5f * (float) slotH, (float) slotW, (float) slotH };
    }

    float capCentreX() const
    {
        const auto s = slotRect();
        const float d = s.getHeight() - 6.0f;
        return s.getX() + 3.0f + 0.5f * d + (s.getWidth() - 6.0f - d) * (float) index / (float) juce::jmax (1, items.size() - 1);
    }

    /** As RedGrooveBass's SlideSwitch draws its line: the name, the end words, the slot, the cap with its orange tick. */
    void paintWalker (juce::Graphics& g) const
    {
        const auto font = walkerFont();
        g.setFont (font);
        const auto s    = slotRect();
        const int  last = items.size() - 1;
        const auto dim  = theme::txDim.withMultipliedAlpha (0.7f);

        if (const auto n = shownName(); n.isNotEmpty())
        {
            g.setColour (theme::lilac);
            g.drawText (n, juce::Rectangle<float> (0.0f, 0.0f, walkerText (n) + 2.0f, (float) getHeight()), juce::Justification::centredLeft, false);
        }
        g.setColour (index == 0 ? theme::tx : dim);
        g.drawText (markFor (items[0]), juce::Rectangle<float> (0.0f, 0.0f, s.getX() - (float) markPad, (float) getHeight()),
                    juce::Justification::centredRight, false);
        g.setColour (index == last ? theme::tx : dim);
        g.drawText (markFor (items[last]), juce::Rectangle<float> (s.getRight() + (float) markPad, 0.0f,
                                                                   (float) getWidth() - s.getRight() - (float) markPad, (float) getHeight()),
                    juce::Justification::centredLeft, false);

        // The slot: dark, its upper edge in shadow and its lower one catching the light.
        const float r = 0.5f * s.getHeight();
        g.setColour (juce::Colour (0xff111117));
        g.fillRoundedRectangle (s, r);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (s.translated (0.0f, 0.75f), r, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (s, r, 1.0f);

        const float d = s.getHeight() - 6.0f;
        const auto cap = juce::Rectangle<float> (d, d).withCentre ({ capCentreX(), s.getCentreY() });
        g.setColour (juce::Colour (0xff22222e));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff8b6cff));
        g.drawEllipse (cap.reduced (0.9f), 1.8f);
        g.setColour (theme::orange);
        g.fillRoundedRectangle (cap.withSizeKeepingCentre (2.6f, d * 0.46f), 1.3f);
    }

    float markWidth (int i) const
    {
        return juce::isPositiveAndBelow (i, items.size()) ? textWidth (markFor (items[i])) : 0.0f;
    }

    /** A position the pack calls off or 0: the switch at rest. */
    static bool isRest (const juce::String& position)
    {
        const auto w = position.trim().toLowerCase();
        return w == "off" || w == "0";
    }

    bool atRest (int i) const { return isRest (items[i]); }

    juce::Rectangle<float> lineArea() const
    {
        if (oneLine)
        {
            auto b = getLocalBounds().toFloat();
            b.removeFromLeft (textWidth (name) + (float) nameGap);
            return b.withSizeKeepingCentre (b.getWidth(), (float) ovalH);
        }
        return getLocalBounds().toFloat().removeFromBottom ((float) ovalH);
    }

    /** The oval with its marks, centred as one across the cell, so the line reads "OFF ( ) +" as the pedal's does. */
    juce::Rectangle<float> ovalIn (juce::Rectangle<float> line) const
    {
        const float mL = markWidth (0), mR = markWidth (items.size() - 1);
        const float whole = mL + (float) (markGap + ovalW + markGap) + mR;
        return { line.getCentreX() - whole * 0.5f + mL + (float) markGap, line.getY(), (float) ovalW, line.getHeight() };
    }

    /** Where the dot stands for position `i`: the first at the oval's left end, the last at its right. */
    float dotX (juce::Rectangle<float> sw, int i) const
    {
        const float dot = sw.getHeight() - 4.0f;
        const float from = sw.getX() + 2.0f + dot * 0.5f, travel = sw.getWidth() - 4.0f - dot;
        return items.size() < 2 ? sw.getCentreX() : from + travel * (float) i / (float) (items.size() - 1);
    }

    juce::String name;
    juce::StringArray items;
    int index = 0;
    bool dragged = false;
    bool menuClick = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleSwitch)
};

} // namespace orbitamp
