#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "Selector.h"
#include "StepSwitch.h"

#include <felitronics/appkit/MicGrid.h>

#include <array>

namespace orbitamp
{

/** The cabinet, filling the rest of the lower row.

    Two mic slots, each with a switch and a pick from the same full list, both standing on one
    grille. The speaker face anchors the grid's rows, so a position is a place on a real driver
    rather than a word from a list — and it is the same picture the capture tool used, which is what
    makes a position mean one thing at both ends of the pipeline.

    Distance and position are two halves of one gesture, so a click sets them together. */
class CabinetBlock final : public BlockFrame
{
public:
    explicit CabinetBlock (juce::AudioProcessorValueTreeState&);
    ~CabinetBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Pushes both mics into the grid as its dots — colour per slot, the active one ringed. */
    void refreshDots();

    /** A cell was clicked or a dot dragged: position and distance move together, as one edit. */
    void placeMic (int slot, const juce::String& position, double distanceCm);

    juce::Rectangle<int> switchArea (int slot) const;

    static constexpr int micColumn  = 132;   // the two mic rows, left of the speaker
    static constexpr int micRow     = 24;
    static constexpr int angleRow   = 16;
    static constexpr int micSwitchW = 22;
    static constexpr int micSwitchH = 11;
    static constexpr int gap        = 8;

    struct Slot
    {
        std::unique_ptr<Selector>   pick;
        std::unique_ptr<StepSwitch> angle;
        std::unique_ptr<juce::ParameterAttachment> onAtt, typeAtt, posAtt, distAtt, angleAtt;
        juce::RangedAudioParameter* onParam = nullptr;   // the truth its switch toggles by
        bool on = false;
    };

    juce::AudioProcessorValueTreeState& state;

    std::array<Slot, (size_t) params::cabNumMics> slots;
    felitronics::appkit::MicGrid grid;

    int activeSlot = 0;   // which mic a click on an empty cell moves

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetBlock)
};

} // namespace orbitamp
