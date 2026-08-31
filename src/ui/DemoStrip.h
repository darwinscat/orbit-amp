// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Selector.h"
#include "Theme.h"

namespace orbitamp
{

class AmpProcessor;

/** TEMPORARY — play a loop through the chain, so the blocks and the pictures can be judged without
    an instrument plugged in.

    It goes when the device packs are built and there is something real to play. It lives beside the
    glyph review strip and is marked the same way, so both leave together. */
class DemoStrip final : public juce::Component
{
public:
    explicit DemoStrip (AmpProcessor&);
    ~DemoStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int designHeight = 26;

private:
    juce::Rectangle<int> buttonArea() const;

    static constexpr int buttonWidth = 46;
    static constexpr int gap         = 10;
    static constexpr int loopWidth   = 190;

    AmpProcessor& amp;
    Selector loop { theme::lilac, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemoStrip)
};

} // namespace orbitamp
