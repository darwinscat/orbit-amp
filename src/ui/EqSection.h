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
    EqSection (juce::AudioProcessorValueTreeState&, int link, const std::atomic<float>& outDb,
               felitronics::analysis::RollingSpectrumTap& spectrumTap,
               std::function<double()> sampleRateGetter);
    ~EqSection() override;

    /** Hands the widgets to the block that will place them. */
    void addTo (juce::Component& parent);

    /** Places everything inside the block's content area. */
    void layOut (juce::Rectangle<int> content);

    /** The spectrum only listens while the face is on screen — a hidden zoom costs nothing. */
    void setSpectrumRunning (bool shouldRun);

private:
    class LevelColumn;

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
    void setParam (const juce::String& id, float plainValue);

    static juce::String formatHz (double hz);
    void freqEdited (int handle, juce::Label&);

    /** The slope readout under a cut's switch: shows the ladder value, click opens the ladder as
        a menu at the mouse — a combo in behaviour, our own face in pixels. */
    struct SlopeCombo final : public juce::Component
    {
        std::function<int()> getIndex;
        std::function<void (int)> setIndex;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    };

    Knob lo { "LO", theme::violet, 0 };
    Knob b1 { "LO MID", theme::violet, 0 };
    Knob b2 { "HI MID", theme::violet, 0 };
    Knob hi { "HI", theme::violet, 0 };

    PresetButton presetBtn;

    ZoneSwitch  hpfSw, lpfSw;
    juce::Label hpfLabel { {}, "HPF" }, lpfLabel { {}, "LPF" };
    SlopeCombo  hpfSlopeBox, lpfSlopeBox;

    /** Editable frequency readouts under the gain knobs, in LO/B1/B2/HI order. */
    juce::Label freqReadout[4];

    std::unique_ptr<LevelColumn> level;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loAtt, b1Att, b2Att, hiAtt;

    /** Frequency attachments per handle — the drag gestures write through these. */
    std::unique_ptr<juce::ParameterAttachment> freqAtt[numHandles];
    std::unique_ptr<juce::ParameterAttachment> qAtt[3];
    std::unique_ptr<juce::ParameterAttachment> hpfSlopeAtt, lpfSlopeAtt, b3OnAtt, b3DbAtt;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> refreshAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
