// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <functional>
#include <vector>

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

    /** OrbitAmp's (the owner, 10.11: "на ползунке как-то отдели заводские от остальных"): where the player's own begin,
        the TOP 12 before them — the track in two halves with a gap between, the twelve on the left half, the player's
        IRs, however many, on the right. 0 is one list across the whole track, as OrbitBass's dozen. */
    int split = 0;

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
        g.setColour (theme::hair2);
        for (const auto& half : halves())
            g.fillRoundedRectangle (half.withSizeKeepingCentre (half.getWidth(), 2.0f), 1.0f);
        const auto t = track();
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

    static constexpr float splitGap = 12.0f;
    bool isSplit() const noexcept { return split > 0 && split < notches; }

    /** The track, or its two halves with the gap between them. */
    std::vector<juce::Rectangle<float>> halves() const
    {
        auto t = track();
        if (! isSplit())
            return { t };
        const float w = (t.getWidth() - splitGap) * 0.5f;
        const auto left = t.removeFromLeft (w);
        return { left, t.withTrimmedLeft (splitGap) };
    }

    /** Notch `i` of `count` spread over `r`, edge to edge; one alone in its middle. */
    static float spread (int i, int count, juce::Rectangle<float> r)
    {
        return count > 1 ? r.getX() + r.getWidth() * (float) i / (float) (count - 1) : r.getCentreX();
    }

    float xOf (int i) const
    {
        const auto h = halves();
        if (! isSplit())
            return spread (i, notches, h[0]);
        return i < split ? spread (i, split, h[0]) : spread (i - split, notches - split, h[1]);
    }

    /** The notch nearest `x`: in the half it is over, the gap's middle between them. */
    int notchAt (float x) const
    {
        const auto h = halves();
        const auto nearest = [x] (int count, juce::Rectangle<float> r)
        {
            return count > 1 ? juce::jlimit (0, count - 1, juce::roundToInt ((x - r.getX()) / r.getWidth() * (float) (count - 1))) : 0;
        };
        if (! isSplit())
            return nearest (notches, h[0]);
        return x < h[0].getRight() + splitGap * 0.5f ? nearest (split, h[0]) : split + nearest (notches - split, h[1]);
    }

    void move (const juce::MouseEvent& e)
    {
        const int i = notchAt (e.position.x);
        if (i == held)
            return;
        held = i;
        repaint();
        if (onMove != nullptr)
            onMove (i);
    }
};

} // namespace orbitamp
