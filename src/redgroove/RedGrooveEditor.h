// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Console.h"

namespace orbitamp::redgroove
{

/** The walker's dashboard itself: the picture with its dome transparent, the family's cat cut into the maker's
    plate, and every printed word on it — the panels' titles, DRIVE's name, the maker's words beside the plate,
    and the playing channel's tones' names over their knobs. Drawn over the dome, under the controls, and never
    takes a click. */
class Cabin final : public face::Picture
{
private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintStrips (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override;
    juce::Rectangle<int> historyArea() const override;
};

/** WHAT SHOWS THROUGH THE DOME: for now one frame of the walker's world, the still it ships, laid over the
    dome's whole box under the picture. Takes no click. */
class Dome final : public juce::Component
{
public:
    Dome();
    juce::Image still;
    void paint (juce::Graphics&) override;
};

//==============================================================================
/** REDGROOVEBASS'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the
    walker's dashboard, under its glass dome over the mushroom forest, its channels the bass's two amps. What
    the walker has of the console's own: DRIVE, the tones, the six keys of the history and the registers, IN
    and OUT, GATE and LIMITER, the tuner, the maker's plate; what it has not: REVERB, DELAY and CAB's plates,
    HPF and LPF (Console::Places::plates, filterKeys). */
class RedGrooveEditor final : public face::Console
{
public:
    explicit RedGrooveEditor (AmpProcessor&);

private:
    Dome dome;

    void webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject& world) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedGrooveEditor)
};

} // namespace orbitamp::redgroove
