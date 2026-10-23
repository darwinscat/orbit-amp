// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

namespace orbitamp
{

/** A KEY THAT STAYS DOWN: one word and a lamp, pressed in and lit, pressed again and out. The AMP's
    BOOST — a pack's own boost switch, the one selector a player reaches for mid-song, standing apart
    from the others as the button it is on the amp's front.

    The channel keys' grammar, kept for the one key that is left of them: lit is the hand's colour as
    a LINE — an orange rim, orange type and a lamp that burns solid — never a wash; out, the panel's
    hairline, the dimmed type and a ring of the faint ink. 13 px, the face's floor for anything read.

    A SPLIT KEY, when it is given a value: the same pill with a hairline across it, the word and lamp on
    the left and the value with a combo's arrow on the right — "BOOST | +3 ▾". One object, because the
    value is the key's own (how much it does), and two places to press, the hairline between them.
    The right one is as wide as the widest value it was told of, so the pill never changes size.

    Dumb view: a word in, lit or not, a value or none; a press out through `onPress`, a press on the
    value through `onValue` and a right click anywhere through `onMenu`. Which parameter it moves, what
    a press does to it and what the menus offer is the owner's business. */
class LatchKey final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit LatchKey (juce::String word) : text (std::move (word))
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    bool isLit() const noexcept { return lit; }

    /** The right part's text; empty, and the key is whole again. */
    void setValue (juce::String v)
    {
        if (value != v)
        {
            value = std::move (v);
            repaint();
        }
    }

    /** Every value the right part will show, so it is as wide as the widest of them. */
    void setValueRoom (const juce::StringArray& every)
    {
        valueTextW = 0.0f;
        for (const auto& v : every)
            valueTextW = juce::jmax (valueTextW, theme::trackedWidth (v, theme::displayFont (fontH), valueTracking));
    }

    /** The right part's own hint, over the key's while the mouse is on it. */
    void setValueTooltip (juce::String tip) { valueTip = std::move (tip); }

    std::function<void()> onPress;
    std::function<void()> onValue;    // a press on the value, when there is one
    std::function<void()> onMenu;     // a right click, when set — and then it is not a press

    static constexpr int keyH = 24;

    /** As wide as the word wants, with the lamp and the padding round it — and the value's part. */
    int idealWidth() const
    {
        return juce::roundToInt (theme::trackedWidth (text.toUpperCase(), theme::displayFont (fontH), tracking)
                                 + 2.0f * padX + lampW + 4.0f + valuePartW());
    }

    juce::String getTooltip() override
    {
        return overValue() && valueTip.isNotEmpty() ? valueTip : SettableTooltipClient::getTooltip();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) keyH).reduced (0.5f);

        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (lit ? theme::orange : (isMouseOver() ? theme::violet.withAlpha (0.6f) : theme::hair2));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, lit ? 1.2f : 1.0f);

        auto keyPart = r;
        if (hasValue())
        {
            auto part = keyPart.removeFromRight (valuePartW());
            g.setColour (lit ? theme::orange.withAlpha (0.6f) : theme::hair2);
            g.drawVerticalLine (juce::roundToInt (part.getX()), part.getY() + 5.0f, part.getBottom() - 5.0f);

            part = part.withTrimmedLeft (valuePadL).withTrimmedRight (padX);
            const auto a = part.removeFromRight (arrowW).withSizeKeepingCentre (arrowW, 4.0f);
            juce::Path arrow;
            arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
            g.setColour (lit ? theme::orange.withAlpha (0.7f) : theme::txFaint);
            g.fillPath (arrow);

            g.setColour (lit ? theme::orange : (overValue() ? theme::tx : theme::txDim));
            theme::drawTracked (g, value, part.withTrimmedRight (arrowGap), theme::displayFont (fontH), valueTracking,
                                juce::Justification::centred);
        }

        auto body = keyPart.reduced (padX, 0.0f);
        const auto lamp = body.removeFromLeft (lampW).withSizeKeepingCentre (6.0f, 6.0f);

        if (lit)
        {
            g.setColour (theme::orange);
            g.fillEllipse (lamp);
        }
        else
        {
            g.setColour (theme::txFaint);
            g.drawEllipse (lamp.reduced (0.5f), 1.0f);
        }

        g.setColour (lit ? theme::orange : (isMouseOver() && ! overValue() ? theme::tx : theme::txDim));
        theme::drawTracked (g, text.toUpperCase(), body, theme::displayFont (fontH), tracking,
                            juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent&) override
    {
        if (const bool now = overValue(); now != wasOverValue)
        {
            wasOverValue = now;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu();
        }
        else if (hasValue() && e.x >= valueX())
        {
            if (onValue != nullptr)
                onValue();
        }
        else if (onPress != nullptr)
        {
            onPress();
        }
    }

private:
    juce::String text, value, valueTip;
    bool lit = false;
    bool wasOverValue = false;
    float valueTextW = 0.0f;

    static constexpr float fontH         = 13.0f;
    static constexpr float tracking      = 0.1f;
    static constexpr float valueTracking = 0.06f;
    static constexpr float padX          = 10.0f;
    static constexpr float lampW         = 12.0f;
    static constexpr float valuePadL     = 8.0f;    // from the hairline to the value
    static constexpr float arrowW        = 7.0f;
    static constexpr float arrowGap      = 4.0f;

    bool hasValue() const noexcept { return value.isNotEmpty(); }

    float valuePartW() const noexcept
    {
        return hasValue() ? valuePadL + juce::jmax (valueTextW, 1.0f) + arrowGap + arrowW + padX : 0.0f;
    }

    int valueX() const noexcept { return getWidth() - juce::roundToInt (valuePartW()); }

    bool overValue() const
    {
        return hasValue() && isMouseOver() && getMouseXYRelative().x >= valueX();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatchKey)
};

} // namespace orbitamp
