// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Theme.h"

namespace orbitamp
{

/** MiniClose's sibling: a small self-painted +, the row's "add into this". Same size, same stroke,
    same hover, so the two read as one pair of marks at the end of a row. */
class MiniPlus final : public juce::Button
{
public:
    MiniPlus() : juce::Button ("add")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    juce::Colour colour = theme::txDim;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.30f;
        const auto  c = r.getCentre();

        juce::Path p;
        p.startNewSubPath (c.x - s, c.y); p.lineTo (c.x + s, c.y);
        p.startNewSubPath (c.x, c.y - s); p.lineTo (c.x, c.y + s);

        g.setColour (colour.withMultipliedAlpha (over || down ? 1.0f : 0.6f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniPlus)
};

} // namespace orbitamp
