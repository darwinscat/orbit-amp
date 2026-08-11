#pragma once

#include "../Parameters.h"
#include "../core/CapturedBlock.h"
#include "BlockFrame.h"
#include "scope/DeviceScope.h"
#include "Knob.h"
#include "VSwitch.h"
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
    void layOutCompact (juce::Rectangle<int>);
    void layOutViz (juce::Rectangle<int>);
    void paintContent (juce::Graphics&) override;

    /** Whether a measured control's positions are named rather than numbered — the difference
        between a switch and a knob that happens to have been swept at two points. */
    static bool hasNamedPositions (const namz::rig::Measured&);

    /** Builds a switch for each selecting control the device has beyond its gain dial. */
    void buildSelectors();

    static constexpr int headerRow  = 34;
    static constexpr int gap        = 10;
    static constexpr int knobGap    = 10;

    // Zoom caps: a block across the whole panel gets a bigger picture, not balloon knobs.
    static constexpr int maxGainSide  = 200;
    static constexpr int maxKnobSide  = 150;
    static constexpr int switchW      = 150;   // a VSwitch column's width, labels included

    static constexpr int numViz = 5;

    AmpProcessor& amp;
    Block&        block;
    const char*   blk;

    VoicingSelector device;
    Knob gain { "Gain", theme::orange, 0 };

    struct Slot
    {
        std::unique_ptr<Knob>    knob;        // a sweeping measured control
        std::unique_ptr<VSwitch> steps;       // ...or a switch, when the pack lists two positions
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
        std::unique_ptr<VSwitch> steps;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    std::array<PickSlot, (size_t) params::numSelectors> selectors;

    /** The visualizations: one scope per way of looking, shown TOGETHER — whichever checkboxes
        are down tile the picture zone, one row up to three of them, two rows past that. */
    std::array<std::unique_ptr<DeviceScope>, (size_t) numViz> scopes;

    /** A checkbox in the column: a small square, a tick, a name. */
    struct VizCheck final : public juce::Component
    {
        juce::String label;
        bool on = false;
        std::function<void()> onToggle;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const auto box = juce::Rectangle<float> (r.getX(), r.getCentreY() - 7.0f, 14.0f, 14.0f);

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (on ? theme::orange : theme::hair2);
            g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.2f);

            if (on)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY() + 0.5f);
                tick.lineTo (box.getCentreX() - 0.5f, box.getBottom() - 3.5f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
                g.setColour (theme::orange);
                g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            g.setColour (on ? theme::tx : theme::txDim);
            theme::drawTracked (g, label, r.withTrimmedLeft (20.0f), theme::displayFont (12.0f),
                                0.08f, juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            on = ! on;
            repaint();
            if (onToggle != nullptr)
                onToggle();
        }
    };

    std::array<VizCheck, (size_t) numViz> vizChecks;

    juce::String caption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturedBlockPanel)
};

} // namespace orbitamp
