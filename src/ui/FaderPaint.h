// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <cmath>
#include <initializer_list>

/** THE FADER'S LOOK, one painter for every fader on the faces: the guitar DELAY's and REVERB's MIX (Knob::setFader) and
    the rails worn as faders (GuardRail::setFaderLook) — the bass's sliders, the guards' rails. */
namespace orbitamp::fader
{

constexpr int   columnW = 46;    // the column: "100%" at 13 px under it
constexpr float gripW   = 30.0f, gripH = 12.0f, railW = 10.0f;
constexpr float textH   = 13.0f;   // the name over a fader and the number under it: the reading floor

/** Where the grip's centre travels in `area`: half a grip in from either end, so it never stands half off. */
inline juce::Rectangle<float> travel (juce::Rectangle<float> area) { return area.reduced (0.0f, gripH * 0.5f); }

/** A rail of the bezel's black in `area`, the amount (`pos`, 0..1 of the travel) flooding it from the foot in the accent,
    a lip round it, a tick across it at every quarter, and the grip — the channel lever's, the accent drawn through it as
    one line, brighter while `handled` (under the hand). */
inline void paint (juce::Graphics& g, juce::Rectangle<float> area, float pos, juce::Colour accent, bool handled)
{
    const auto  span = travel (area);
    const float y    = span.getBottom() - juce::jlimit (0.0f, 1.0f, pos) * span.getHeight();
    const float cx   = area.getCentreX();

    // The rail: a cut in the bezel's black, the amount flooding it from the foot, a lip round it.
    const auto rail = juce::Rectangle<float> (railW, area.getHeight()).withCentre ({ cx, area.getCentreY() });
    g.setColour (theme::bezel);
    g.fillRoundedRectangle (rail, railW * 0.5f);

    g.setColour (accent.withMultipliedAlpha (0.55f));
    g.fillRoundedRectangle (rail.reduced (2.0f).withTop (y), (railW - 4.0f) * 0.5f);

    g.setColour (theme::violet.withAlpha (0.45f));
    g.drawRoundedRectangle (rail.reduced (0.5f), railW * 0.5f, 1.2f);

    // A tick across the rail at every quarter.
    g.setColour (theme::hair2);
    for (int i = 0; i <= 4; ++i)
        g.fillRect (juce::Rectangle<float> (railW + 8.0f, 1.0f)
                        .withCentre ({ cx, span.getBottom() - span.getHeight() * (float) i / 4.0f }));

    // The grip: the channel lever's, the accent through it as one line — brighter under the hand.
    const auto bar = juce::Rectangle<float> (juce::jmin (gripW, area.getWidth() - 4.0f), gripH).withCentre ({ cx, y });

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (bar.translated (0.0f, 2.0f).expanded (1.0f), gripH * 0.5f + 1.0f);

    juce::ColourGradient body (juce::Colour (0xff2c2a38), 0.0f, bar.getY(),
                               juce::Colour (0xff111018), 0.0f, bar.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (bar, gripH * 0.5f);

    g.setColour (handled ? accent.withMultipliedAlpha (0.8f) : theme::hair2);
    g.drawRoundedRectangle (bar.reduced (0.5f), gripH * 0.5f, 1.0f);

    g.setColour (handled ? accent.brighter (0.3f) : accent);
    g.fillRect (juce::Rectangle<float> (bar.getWidth() - 10.0f, 1.5f).withCentre (bar.getCentre()));
}

/** A word at 13 px, centred in `box`, its tracking eased from `preferred` toward none where the box is tight — the
    height never: nothing a player reads goes under 13 px. */
inline void drawWord (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box, float preferred = 0.04f)
{
    const auto  font = theme::displayFont (textH);
    const float bare = theme::trackedWidth (text, font, 0.0f);
    const int   gaps = juce::jmax (1, text.length() - 1);
    const float tr   = juce::jlimit (0.0f, preferred, (box.getWidth() - bare) / ((float) gaps * textH));
    theme::drawTracked (g, text, box, font, tr, juce::Justification::centred);
}

/** A fader's column wide enough for every word it writes at 13 px — its name, its widest number: the family's 46, or
    more where a word needs it (ENVELOPE's RELEASE). */
inline int columnFor (std::initializer_list<juce::String> words)
{
    float w = 0.0f;
    for (const auto& s : words)
        w = juce::jmax (w, theme::trackedWidth (s, theme::displayFont (textH), 0.0f));
    return juce::jmax (columnW, (int) std::ceil (w) + 2);
}

} // namespace orbitamp::fader
