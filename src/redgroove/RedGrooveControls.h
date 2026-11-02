// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"

#include <functional>

/** THE WALKER'S OWN CONTROLS: what its dashboard has that no other face's console does. Dumb views — each is told
    what it shows and says what the hand did; none of them reaches the processor. */
namespace orbitamp::redgroove
{

/** WORDS IN THEIR OWN CASE — a reading's units, "dB", "Hz", "kHz", which the panel's names in capitals would turn into
    other words — centred in `area`, in the face's ink, its edge under them. */
void writeAsIs (juce::Graphics&, const face::Ink&, const juce::String& words, juce::Rectangle<float> area, float em,
                juce::Colour);

/** A READING UNDER A CONTROL: what the control is set to, in words in their own case (writeAsIs), centred in its box.
    Its owner says the words; it takes no click. */
class Readout final : public juce::Component
{
public:
    Readout (const face::Ink&, float em);

    void set (const juce::String& words);
    juce::Colour colour;

    void paint (juce::Graphics&) override;

private:
    const face::Ink ink;
    const float em;
    juce::String words;
};

/** COMP'S SQUEEZE: a column falling from its top, violet, as deep as COMP is pressing — its whole height `rangeDb` —
    the depth's edge an orange line. Read 30 times a second from `grDb` (0 or below); takes no click. */
class GrColumn final : public juce::Component, private juce::Timer
{
public:
    explicit GrColumn (std::function<float()> grDb);

    static constexpr float rangeDb = 24.0f;

    void paint (juce::Graphics&) override;

private:
    std::function<float()> read;
    float shown = 0.0f;
    void timerCallback() override;
};

} // namespace orbitamp::redgroove
