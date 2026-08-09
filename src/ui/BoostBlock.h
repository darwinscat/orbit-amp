#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "BoostScope.h"
#include "Knob.h"
#include "Selector.h"
#include "StepSwitch.h"
#include "VoicingSelector.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>
#include <memory>
#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** The boost — a captured pedal in front of the preamp, laid out from the pack rather than from a
    guess.

    The pack says what this device HAS: how many positions its gain was captured at, which of its
    knobs were measured instead, whether one of them is a two-position switch, and what the circuit
    is. SM7 comes out as one big Gain over twenty-one detents, two EQ knobs and a Sharp/Smooth
    switch — nothing in this file names any of that.

    A control slot with nothing behind it is hidden. A knob doing nothing is worse than a gap. */
class BoostBlock final : public BlockFrame,
                         private juce::AsyncUpdater
{
public:
    explicit BoostBlock (AmpProcessor&);
    ~BoostBlock() override;

    /** Rebuilds the face from whatever pack is loaded. Called when the device changes. */
    void deviceChanged();

private:
    int  headerHeight() const override { return headerRow; }
    void layOutHeader (juce::Rectangle<int>) override;
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    /** Every measured control summed — what the pedal's tone section is doing as a whole. */
    double toneDb (double freqHz) const;

    /** Resolves each control's curve at wherever its knob sits. Cheap enough to do on every knob
        move, and far cheaper than doing it once per pixel. */
    void rebuildCurves();

    /** Asks for the curve to be rebuilt — on the next message, not now.

        Everything that moves a measured control writes a parameter, and the writes and the redraw
        used to race. juce::ParameterAttachment deliberately does NOT call its own callback when it
        writes the value itself (that would be a loop), and the redraw lived in that callback — so
        clicking the switch changed the parameter and left the curve showing the position BEFORE the
        click. On screen that reads as the switch being wired backwards. Deferring puts the rebuild
        after every write, whoever made it. */
    void refreshCurve();

    void handleAsyncUpdate() override;

    static constexpr int headerRow  = 22;
    static constexpr int curveHeight = 130;
    static constexpr int gap        = 10;
    static constexpr int knobGap    = 10;
    static constexpr int switchRow  = 16;
    static constexpr int modeRow    = 18;

    AmpProcessor& amp;

    VoicingSelector pedal;
    Knob gain { "Gain", theme::orange, 0 };

    struct Slot
    {
        std::unique_ptr<Knob>       knob;     // a sweeping measured control
        std::unique_ptr<StepSwitch> steps;    // ...or a switch, when the pack lists two positions
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt;
        std::unique_ptr<juce::ParameterAttachment> stepAtt;
        int measuredIndex = -1;
    };

    std::array<Slot, (size_t) params::boostNumMeasured> slots;

    struct Curve { std::vector<double> db, hz; };
    std::array<Curve, (size_t) params::boostNumMeasured> curves;

    BoostScope scope;   // constructed in the .cpp: the tap lives on the processor, incomplete here
    Selector   scopeMode { theme::orange, true };

    juce::String caption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoostBlock)
};

} // namespace orbitamp
