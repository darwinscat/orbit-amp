#pragma once

#include "../Parameters.h"
#include "../core/EqLink.h"
#include "EqCurve.h"
#include "Knob.h"
#include "ZoneSwitch.h"

#include <felitronics/analysis/RollingSpectrumTap.h>
#include <felitronics/analysis/SpectrumPane.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** One EQ link's whole face, in the console grammar:

        [        curve + filter walls  (+ spectrum, later)        ]   [LEVEL]
        [ HPF sw ][  LO  ][  B1  ][  B2  ][  HI  ][ LPF sw ]      [column]

    Knobs carry GAIN only; frequency and Q belong to the mouse — drag a node for freq+gain, wheel
    over it for Q, wheel over a wall for its slope, double-click empty curve to summon the narrow
    surgical bell (B3), double-click its node to dismiss it. The LEVEL column on the right is the
    link's output meter — the IN sliver's gradient and peak hold — with the level fader riding it
    as a slide-rule frame.

    NOT a Component: its widgets become children of the block, which places them via layOut().
    It keeps an EqLink of its own for DRAWING only, designed on the message thread — reading the
    playing link's coefficients would be a race. */
class EqSection final : private juce::Timer
{
public:
    EqSection (juce::AudioProcessorValueTreeState&, int link,
               felitronics::analysis::RollingSpectrumTap& spectrumTap,
               std::function<double()> sampleRateGetter);
    ~EqSection() override;

    /** Hands the widgets to the block that will place them. */
    void addTo (juce::Component& parent);

    /** Places everything inside the block's content area. */
    void layOut (juce::Rectangle<int> content);

    /** The spectrum only listens while the block is on screen. */
    void setSpectrumRunning (bool shouldRun);

    /** Steps the whole console aside — for when a picture is thrown open across the block's face
        and everything else has to make room. */
    void setWidgetsVisible (bool);

    /** How much of the block's height the control row under the curve wants: a dial, its name above
        it, and the slope combo that hangs a little below the dial's foot. Nothing more — the row
        used to reserve a hundred and twelve and spend the last forty on air. */
    static constexpr int rowH = 72;

private:
    void timerCallback() override;
    void refreshCurve();
    void refreshHandles();
    void handleDragged (int index, double hz, double db);
    void handleDragActive (int index, bool active);
    void handleWheel (int index, float delta);
    void stepSlope (int index, int steps);
    void curveDoubleClicked (double hz);
    void handleDoubleClicked (int index);

    float raw (const juce::String& id) const;
    std::unique_ptr<juce::ParameterAttachment> attach (const juce::String& id);

    enum Handle { hLo, hB1, hB2, hB3, hHi, hHpf, hLpf, numHandles };

    juce::AudioProcessorValueTreeState& state;
    const int link;

    static constexpr double displayRate  = 48000.0;
    static constexpr int spectrumOrder   = 11;      // must agree with every other tap consumer
    core::EqLink display;

    felitronics::analysis::RollingSpectrumTap& tap;
    felitronics::analysis::SpectrumPane        pane;
    std::function<double()>                    sampleRate;

    EqCurve curve { [this] (double hz) { return display.magnitudeDb (hz); } };

    /** The overlay in the curve's corner: the presets menu. Flat (the old reset) leads the list,
        then the link's own starting points — stamps, not state: pressed, applied, forgotten. */
    struct PresetButton final : public juce::Component
    {
        std::function<void (juce::Point<int>)> onClick;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onClick) onClick (e.getScreenPosition());
        }
    };

    void showPresets (juce::Point<int> screenPos);
    void resetLink();
    juce::StringArray linkParamIds() const;
    bool linkIsFlat() const;
    void setParam (const juce::String& id, float plainValue);

    /** The slope readout under a cut's switch: shows the ladder value, click opens the ladder as
        a menu at the mouse — a combo in behaviour, our own face in pixels. */
    struct SlopeCombo final : public juce::Component
    {
        std::function<int()> getIndex;
        std::function<void (int)> setIndex;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    };

    // Short names on purpose: six cells across half a panel leaves about sixty design units each,
    // and "LO MID" at reading size ran straight into its neighbour. The colour says which node it
    // is anyway — the knob wears the same one as its point on the curve.
    Knob lo { "LO",    theme::eqNode[0], 0 };
    Knob b1 { "L MID", theme::eqNode[1], 0 };
    Knob b2 { "H MID", theme::eqNode[2], 0 };
    Knob hi { "HI",    theme::eqNode[3], 0 };

    PresetButton presetBtn;

    ZoneSwitch  hpfSw, lpfSw;
    juce::Label hpfLabel { {}, "HPF" }, lpfLabel { {}, "LPF" };
    SlopeCombo  hpfSlopeBox, lpfSlopeBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loAtt, b1Att, b2Att, hiAtt;

    /** Frequency attachments per handle — the drag gestures write through these. */
    std::unique_ptr<juce::ParameterAttachment> freqAtt[numHandles];
    std::unique_ptr<juce::ParameterAttachment> qAtt[3];
    std::unique_ptr<juce::ParameterAttachment> hpfSlopeAtt, lpfSlopeAtt, b3OnAtt, b3DbAtt;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> refreshAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
