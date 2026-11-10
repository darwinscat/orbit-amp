// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <functional>

namespace orbitamp
{

/** THE BASS'S DOZEN ON A SLIDER, in OrbitAmp's own hand (the owner, 06.11): a track across with a notch a cabinet and a
    cap at the one that plays — none where a cabinet of the player's own plays. The cap dragged is heard at the notch
    nearest the hand (onMove) and kept at the one it settles in when the hand lets go (onLetGo); a click on the track
    goes there at once. RedGrooveBass's slider is its own face's (redgroove::CabinetScreen); this is OrbitBass's. */
class CabinetSlots final : public juce::Component
{
public:
    int notches = 12;
    std::function<int()> notch;                 // the one that plays, -1 for none of them
    std::function<void (int)> onMove, onLetGo;

    /** Asks for the notch again (the block's timer). */
    void refresh()
    {
        const int now = notch != nullptr ? notch() : -1;
        if (now != shown && held < 0)
        {
            shown = now;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto t = track();
        g.setColour (theme::hair2);
        g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
        for (int i = 0; i < notches; ++i)   // a notch a cabinet
        {
            g.setColour (i == at() ? theme::orange : theme::txFaint.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (1.0f, i == at() ? 9.0f : 6.0f).withCentre ({ xOf (i), t.getCentreY() }));
        }
        if (at() >= 0)
        {
            const auto cap = juce::Rectangle<float> (8.0f, 14.0f).withCentre ({ xOf (at()), t.getCentreY() });
            g.setColour (theme::panel2);
            g.fillRoundedRectangle (cap, 2.0f);
            g.setColour (isMouseOverOrDragging() ? theme::orange.brighter (0.25f) : theme::orange);
            g.drawRoundedRectangle (cap, 2.0f, 1.4f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { move (e); }
    void mouseDrag (const juce::MouseEvent& e) override { move (e); }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (held < 0)
            return;
        const int kept = held;
        held = -1;
        shown = kept;
        repaint();
        if (onLetGo != nullptr)
            onLetGo (kept);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    int shown = -1, held = -1;   // held: the notch under the hand while it has the cap

    int at() const noexcept { return held >= 0 ? held : shown; }

    juce::Rectangle<float> track() const { return getLocalBounds().toFloat().reduced (6.0f, 0.0f); }

    float xOf (int i) const
    {
        const auto t = track();
        return notches > 1 ? t.getX() + t.getWidth() * (float) i / (float) (notches - 1) : t.getCentreX();
    }

    void move (const juce::MouseEvent& e)
    {
        const auto t = track();
        const int i = juce::jlimit (0, notches - 1, juce::roundToInt ((e.position.x - t.getX()) / t.getWidth() * (float) (notches - 1)));
        if (i == held)
            return;
        held = i;
        repaint();
        if (onMove != nullptr)
            onMove (i);
    }
};

} // namespace orbitamp
