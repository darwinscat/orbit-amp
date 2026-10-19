// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <vector>

namespace orbitamp
{

/** THE AMP'S CHANNEL SWITCH: a key for each channel of the device that is playing, the way a real
    amp's front carries a row of them — NORMAL, ALTERED, CRUNCH, OVERDRIVE — and the one playing lit.

    Lit is the hand's colour, as a line and never a wash: an orange rim, orange type, and a lamp at the
    key's left that burns solid. The others wear the panel's hairline and the dimmed type, their lamps
    out. A press on a dark key is a pick; the lit one is already playing.

    The names read at 13 px, the face's floor for anything read; a row too long for its room keeps
    that size and loses the ends of its names to an ellipsis instead.

    Dumb view: names in, which one is lit, a pick out through `onPick`. It knows nothing of packs. */
class ChannelRow : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void setChannels (const juce::StringArray& newNames, int newLit)
    {
        names = newNames;
        lit = newLit;
        hover = -1;
        resized();
        repaint();
    }

    int count() const noexcept { return names.size(); }

    /** A key pressed, by its place in the row. */
    std::function<void (int)> onPick;

    static constexpr int rowH = 24;

    void resized() override
    {
        keys.clear();

        if (names.isEmpty())
            return;

        // Each key as wide as its name wants; when they do not all fit, each gives up the same share.
        std::vector<float> wants;
        float total = 0.0f;

        for (const auto& n : names)
        {
            wants.push_back (keyWidth (n));
            total += wants.back();
        }

        const float room  = (float) getWidth() - keyGap * (float) (names.size() - 1);
        const float scale = total > room && total > 0.0f ? juce::jmax (0.0f, room) / total : 1.0f;
        const float h = (float) juce::jmin (getHeight(), rowH);
        float x = 0.0f;

        for (const float w : wants)
        {
            keys.push_back ({ x, ((float) getHeight() - h) * 0.5f, w * scale, h });
            x += w * scale + keyGap;
        }
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < (int) keys.size() && i < names.size(); ++i)
        {
            const auto r  = keys[(size_t) i].reduced (0.5f);
            const bool on = i == lit;

            g.setColour (theme::panel.withAlpha (0.85f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? theme::orange : theme::hair2);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, on ? 1.2f : 1.0f);

            // The lamp: lit, a solid of the hand's colour; out, a ring of the faint type.
            auto body = r.reduced (padX, 0.0f);
            const auto lamp = body.removeFromLeft (lampW).withSizeKeepingCentre (6.0f, 6.0f);

            if (on)
            {
                g.setColour (theme::orange);
                g.fillEllipse (lamp);
            }
            else
            {
                g.setColour (theme::txFaint);
                g.drawEllipse (lamp.reduced (0.5f), 1.0f);
            }

            g.setColour (on ? theme::orange : (i == hover ? theme::tx : theme::txDim));
            theme::drawTracked (g, fitted (names[i].toUpperCase(), body.getWidth()), body,
                                theme::displayFont (fontH), tracking, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = keyAt (e.position);

        if (i >= 0 && i != lit && onPick != nullptr)
            onPick (i);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (keyAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

private:
    juce::StringArray names;
    int lit   = -1;
    int hover = -1;
    std::vector<juce::Rectangle<float>> keys;

    static constexpr float fontH    = 13.0f;
    static constexpr float tracking = 0.1f;
    static constexpr float padX     = 10.0f;
    static constexpr float lampW    = 12.0f;
    static constexpr float keyGap   = 6.0f;

    static float keyWidth (const juce::String& name)
    {
        return theme::trackedWidth (name.toUpperCase(), theme::displayFont (fontH), tracking) + 2.0f * padX + lampW + 4.0f;
    }

    /** The name, its end traded for an ellipsis until it fits — never a smaller type. */
    static juce::String fitted (juce::String text, float width)
    {
        const auto font = theme::displayFont (fontH);
        const auto dots = juce::String::charToString ((juce::juce_wchar) 0x2026);

        while (text.length() > 2 && theme::trackedWidth (text, font, tracking) > width)
            text = text.dropLastCharacters (text.endsWith (dots) ? 2 : 1).trimEnd() + dots;

        return text;
    }

    int keyAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) keys.size(); ++i)
            if (keys[(size_t) i].contains (p))
                return i;
        return -1;
    }

    void setHover (int i)
    {
        if (i != hover)
        {
            hover = i;
            repaint();
        }
    }
};

} // namespace orbitamp
