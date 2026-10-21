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

    Dumb view: a word in, lit or not, a press out through `onPress`. Which parameter it moves, and what
    a press does to it, is the owner's business. */
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

    std::function<void()> onPress;

    static constexpr int keyH = 24;

    /** As wide as the word wants, with the lamp and the padding round it. */
    int idealWidth() const
    {
        return juce::roundToInt (theme::trackedWidth (text.toUpperCase(), theme::displayFont (fontH), tracking)
                                 + 2.0f * padX + lampW + 4.0f);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) keyH).reduced (0.5f);

        g.setColour (theme::panel.withAlpha (0.85f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (lit ? theme::orange : (isMouseOver() ? theme::violet.withAlpha (0.6f) : theme::hair2));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, lit ? 1.2f : 1.0f);

        auto body = r.reduced (padX, 0.0f);
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

        g.setColour (lit ? theme::orange : (isMouseOver() ? theme::tx : theme::txDim));
        theme::drawTracked (g, text.toUpperCase(), body, theme::displayFont (fontH), tracking,
                            juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && onPress != nullptr)
            onPress();
    }

private:
    juce::String text;
    bool lit = false;

    static constexpr float fontH    = 13.0f;
    static constexpr float tracking = 0.1f;
    static constexpr float padX     = 10.0f;
    static constexpr float lampW    = 12.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LatchKey)
};

} // namespace orbitamp
