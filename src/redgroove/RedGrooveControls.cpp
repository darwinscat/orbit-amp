// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveControls.h"

namespace orbitamp::redgroove
{

using face::orange;

//==============================================================================
void writeAsIs (juce::Graphics& g, const face::Ink& ink, const juce::String& words, juce::Rectangle<float> area, float em,
                juce::Colour colour)
{
    const auto font = face::labelFont (em).withExtraKerningFactor (0.04f);
    if (! ink.edge.isTransparent())
    {
        g.setColour (ink.under (colour));
        face::writeWords (g, words, font, area.translated (0.0f, face::Ink::edgeDrop), ink.weight);
    }
    g.setColour (colour);
    face::writeWords (g, words, font, area, ink.weight);
}

//==============================================================================
Readout::Readout (const face::Ink& i, float size) : colour (i.values), ink (i), em (size)
{
    setInterceptsMouseClicks (false, false);
}

void Readout::set (const juce::String& w)
{
    if (w != words)
    {
        words = w;
        repaint();
    }
}

void Readout::paint (juce::Graphics& g)
{
    writeAsIs (g, ink, words, getLocalBounds().toFloat(), em, colour);
}

//==============================================================================
GrColumn::GrColumn (std::function<float()> grDb) : read (std::move (grDb))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void GrColumn::timerCallback()
{
    const float now = juce::jlimit (-rangeDb, 0.0f, read != nullptr ? read() : 0.0f);
    if (std::abs (now - shown) > 0.05f)
    {
        shown = now;
        repaint();
    }
}

void GrColumn::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float depth = r.getHeight() * juce::jlimit (0.0f, 1.0f, -shown / rangeDb);
    if (depth < 0.5f)
        return;
    g.setColour (juce::Colour (0xff8b6cff));
    g.fillRoundedRectangle (r.withHeight (depth), 2.0f);
    g.setColour (orange);
    g.fillRect (r.getX() - 1.0f, r.getY() + depth - 1.0f, r.getWidth() + 2.0f, 2.0f);
}

} // namespace orbitamp::redgroove
