#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "VoicingSelector.h"
#include "ZoneSwitch.h"

#include <felitronics/analysis/SpectrumPane.h>

namespace orbitamp
{

class AmpProcessor;

/** The reverb — our own tail on top of the captured voice.

    Mix is the hero, the character is the title on the border, DECAY and PRE stand small at the
    hero's left hand. The picture is the PAIR, the cab grammar on the frequency axis: what walks
    into the room as the quiet grey ground, what the room ADDS in the block's violet over it —
    and the decay is simply how long the violet keeps glowing after the note. The tail's HPF
    lives ON the picture: a switch in the corner, and when it is on, its curve and a dashed
    vertical to drag — the consoles' cut, at home in the reverb. */
class ReverbBlock final : public BlockFrame,
                          private juce::Timer
{
public:
    explicit ReverbBlock (AmpProcessor&);
    ~ReverbBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;
    void timerCallback() override;

    /** The picture: dumb surface, callbacks up — paint from the block, the cut drag reported
        with a phase so the gesture brackets properly. */
    struct SpectraView final : public juce::Component
    {
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintBody;
        std::function<bool (juce::Point<float>)> hitCut;
        std::function<void (float x, int phase)> onCut;   // 0 = down, 1 = drag, 2 = up

        void paint (juce::Graphics& g) override
        {
            if (paintBody) paintBody (g, getLocalBounds().toFloat());
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = hitCut != nullptr && hitCut (e.position);
            if (dragging && onCut) onCut (e.position.x, 0);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (dragging && onCut) onCut (e.position.x, 1); }
        void mouseUp   (const juce::MouseEvent& e) override { if (dragging && onCut) onCut (e.position.x, 2); dragging = false; }

        bool dragging = false;
    };

    static constexpr int maxKnobSide = 84;

    AmpProcessor& amp;

    VoicingSelector character;
    Knob mix   { "Mix",   theme::violet, 0 };
    Knob decay { "Decay", theme::violet, 0 };
    Knob pre   { "Pre",   theme::violet, 0 };

    SpectraView view;
    ZoneSwitch  hpfSw;
    juce::Label hpfLabel { {}, "HPF" };

    /** The pair's readers — the door and the added tail, same liquid pane as everywhere. */
    std::array<felitronics::analysis::SpectrumPane, 2> panes;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,
                                                                          decayAttachment,
                                                                          preAttachment;
    std::unique_ptr<juce::ParameterAttachment> characterAttachment, hpfHzAtt, hpfRepaintAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverbBlock)
};

} // namespace orbitamp
