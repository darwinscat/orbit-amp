#pragma once

#include "../Parameters.h"
#include "../core/CapturedBlock.h"
#include "BlockFrame.h"
#include "ZoneSwitch.h"
#include "scope/DeviceScope.h"
#include "Knob.h"
#include "Selector.h"
#include "StepSwitch.h"
#include "VoicingSelector.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>
#include <memory>

namespace orbitamp
{

class AmpProcessor;

/** The face every captured block wears — the boost in front, the preamp behind it, and whatever
    captured block comes next. One class, because the question is always the same: which device,
    which capture of it, and what its own controls do. Which BLOCK it is arrives as a reference and
    an id prefix, and nothing else in the file knows.

    The pack says what this device HAS: how many positions its gain was captured at, which of its
    knobs were measured instead, whether one of them is a two-position switch, and what the circuit
    is. SM7 comes out as one big Gain over twenty-one detents, two EQ knobs and a Sharp/Smooth
    switch — nothing in this file names any of that.

    A control slot with nothing behind it is hidden. A knob doing nothing is worse than a gap. */
class CapturedBlockPanel final : public BlockFrame
{
public:
    using Block = core::CapturedBlock<params::boostNumMeasured>;
    static_assert (params::preampNumMeasured == params::boostNumMeasured,
                   "one face serves every captured block, so the slot counts have to agree");

    CapturedBlockPanel (AmpProcessor&, Block&, const juce::String& title, const char* blockId);
    ~CapturedBlockPanel() override;

    /** Rebuilds the face from whatever pack is loaded. Called when the device changes. */
    void deviceChanged();

private:
    int  headerHeight() const override { return headerRow; }
    void layOutHeader (juce::Rectangle<int>) override;
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    /** Whether a measured control's positions are named rather than numbered — the difference
        between a switch and a knob that happens to have been swept at two points. */
    static bool hasNamedPositions (const namz::rig::Measured&);

    /** Builds a switch for each selecting control the device has beyond its gain dial. */
    void buildSelectors();

    static constexpr int headerRow  = 22;
    static constexpr int curveHeight = 130;
    static constexpr int gap        = 10;
    static constexpr int knobGap    = 10;
    static constexpr int switchRow  = 16;
    static constexpr int modeRow    = 18;

    // Zoom caps: a block across the whole panel gets a bigger picture, not balloon knobs and a
    // switch as wide as the window.
    static constexpr int maxGainSide  = 200;
    static constexpr int maxKnobSide  = 140;
    static constexpr int maxRowWidth  = 420;

    AmpProcessor& amp;
    Block&        block;
    const char*   blk;

    /** TEMPORARY — everything of ours off, so the capture can be heard as it was taken. */
    ZoneSwitch rawSwitch;
    juce::Label rawLabel { {}, "RAW" };

    VoicingSelector device;
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

    /** A device's other selecting controls — the ones that pick a FILE rather than shape one. Fur
        Coat's octave is the first of them: two captures at every dial position, and without this
        half the pack was unreachable. Drawn as a switch because that is what it is on the pedal. */
    struct PickSlot
    {
        std::unique_ptr<StepSwitch> steps;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    std::array<PickSlot, (size_t) params::numSelectors> selectors;

    DeviceScope scope;   // constructed in the .cpp: the tap lives on the processor, incomplete here
    Selector   scopeMode { theme::orange, true };

    juce::String caption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturedBlockPanel)
};

} // namespace orbitamp
