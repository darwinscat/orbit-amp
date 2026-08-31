// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../core/CapturedBlock.h"
#include "BlockFrame.h"
#include "Knob.h"
#include "VoicingSelector.h"

#include <felitronics/appkit/DeviceGlyph.h>
#include <felitronics/appkit/DeviceSpec.h>

namespace orbitamp
{

class AmpProcessor;

/** The power amp, first block of the lower row: a captured one. A pack in the poweramp slot, played
    by the same block the boost and the preamp are, after the space.

    The pack's name IS the block's name — on the border where POWER would stand, a quarter-width
    block having no room for both — and the box says what was captured: the circuit's glyphs, tube
    or transistor, and the gear's full name. A dial, when the pack has an axis to turn. */
class PowerAmpBlock final : public BlockFrame
{
public:
    PowerAmpBlock (AmpProcessor&, core::CapturedBlock&);
    ~PowerAmpBlock() override;

    /** Rebuilds the face from whatever pack is loaded. Called when the device changes. */
    void deviceChanged();

private:
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    static constexpr int gap         = 8;
    static constexpr int maxKnobSide = 84;

    AmpProcessor&        amp;
    core::CapturedBlock& block;

    VoicingSelector device;
    Knob            gain { "", theme::orange, 0 };

    felitronics::appkit::DeviceSpec spec;
    juce::String name, alias;
    juce::Rectangle<int> glyphArea, textArea;

    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerAmpBlock)
};

} // namespace orbitamp
