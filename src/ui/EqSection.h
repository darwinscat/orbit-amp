#pragma once

#include "../Parameters.h"
#include "../core/EqLink.h"
#include "EqCurve.h"
#include "Knob.h"
#include "VSwitch.h"
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

    /** One cell of the control row between the two cuts: what it is called, what colour it wears,
        and the parameter its knob writes.

        The row used to name its four bands in four members, which is the same as saying the console
        only ever has these four. It will not: a pack's own tone controls are one to three of
        whatever that device happened to have, and the classical stacks are three more. What every
        set has in common is exactly this list — a name, a colour and something to write to. The
        colour is load-bearing rather than decoration: it is the ONLY thing tying a knob to its
        point on the curve above, now that the names had to be shortened to fit. */
    struct Band
    {
        juce::String label;
        juce::Colour colour;
        juce::String param;

        /** A measured control clicks at the positions it was actually swept at — between them
            there is interpolation, not data, and a detent says so. Ours sweep freely. */
        int notches = 0;

        /** Ours read in decibels. A device's control reads in nothing: its parameter is a
            normalised place on somebody's dial, and printing 0.63 under a knob marked TONE is
            worse than printing nothing. */
        bool showsDb = true;
    };



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

    std::function<double (double)> nativeDb;
    juce::Component* host = nullptr;   // whoever the widgets were handed to, for rebuilt rows

    EqCurve curve { [this] (double hz)
                    {
                        return display.magnitudeDb (hz) + (nativeDb != nullptr ? nativeDb (hz) : 0.0);
                    } };

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

    /** OUR set: a parametric we own, four bands wide, plus the scalpel the curve summons. Fixed
        because `core::EqLink` is fixed — a band list this one cannot vary is honest about that. */
    std::vector<Band> ourBands() const;

public:
    /** Put a set of bands on the row. Ours by default; a captured block hands it the device's own.

        The two are alternatives and the console shows one at a time, because a device wearing its
        tone stack and ours together is a decibel nobody can trace back. */
    void setBands (std::vector<Band> bands) { buildBands (std::move (bands)); }
    std::vector<Band> ourBandSet() const { return ourBands(); }

    /** What the DEVICE's own filters are doing, added under our curve. Empty for our own set —
        then the line is just the parametric and the two cuts, as before.

        It is added rather than switched to because the cuts are OURS in both modes: what the block
        actually does is the pack's curves and our two walls, so that is what the line has to be. */
    void setNativeCurve (std::function<double (double)> db) { nativeDb = std::move (db); refreshCurve(); }

    /** The one control the console owns about ITSELF: whose bands it is wearing. Hidden when the
        device brought none, because a choice with one side is a label. */
    void setModes (const juce::StringArray& names, int selected);
    std::function<void (int)> onModePicked;

private:
    void buildBands (std::vector<Band>);

    /** Writes a band's gain from the curve's own hand — the knob is the parameter's face, so the
        drag goes through it rather than round it. */
    void setBandDb (size_t index, double db);

    std::vector<Band> bands;
    std::vector<std::unique_ptr<Knob>> bandKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> bandAtts;

    PresetButton presetBtn;
    VSwitch      modeSw;

    ZoneSwitch  hpfSw, lpfSw;
    juce::Label hpfLabel { {}, "HPF" }, lpfLabel { {}, "LPF" };
    SlopeCombo  hpfSlopeBox, lpfSlopeBox;

    /** Frequency attachments per handle — the drag gestures write through these. */
    std::unique_ptr<juce::ParameterAttachment> freqAtt[numHandles];
    std::unique_ptr<juce::ParameterAttachment> qAtt[3];
    std::unique_ptr<juce::ParameterAttachment> hpfSlopeAtt, lpfSlopeAtt, b3OnAtt, b3DbAtt;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> refreshAtts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
