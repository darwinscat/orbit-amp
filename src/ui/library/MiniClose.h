// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Theme.h"

namespace orbitamp
{

/** A small self-painted ✕ — the row's "remove" and the panel's "close". A path, not a glyph,
    for the same reason every other mark in the toolbar is: font glyphs are the host's to break. */
class MiniClose final : public juce::Button
{
public:
    MiniClose() : juce::Button ("close")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    juce::Colour colour = theme::txDim;   // hover brightens; a destructive ✕ sets this red-ish

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.24f;
        const auto  c = r.getCentre();

        juce::Path p;
        p.startNewSubPath (c.x - s, c.y - s); p.lineTo (c.x + s, c.y + s);
        p.startNewSubPath (c.x + s, c.y - s); p.lineTo (c.x - s, c.y + s);

        g.setColour (colour.withMultipliedAlpha (over || down ? 1.0f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniClose)
};

} // namespace orbitamp
