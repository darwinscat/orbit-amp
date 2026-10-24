// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

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
               felitronics::analysis::RollingSpectrumTap& inSpectrumTap,
               std::function<double()> sampleRateGetter);
    ~EqSection() override;

    /** Hands the widgets to the block that will place them. */
    void addTo (juce::Component& parent);

    /** THE CONSOLE IS TWO PIECES NOW, placed apart. The row of hands is a strip of the block's own —
        under the picture well, where the block's other controls have their strips — and the curve is
        one of the pictures that well can show, with the button that says whose tone it wears in its
        corner. So the block places each where its own layout puts it, rather than handing the console
        one rectangle to split.

        `cornerReserve` is the width the block keeps free at the curve's top-right for its own glyph
        (the one that throws the picture open): the mode button stands left of it. */
    void layOutRow (juce::Rectangle<int> row);
    void layOutCurve (juce::Rectangle<int> area, int cornerReserve);

    /** Which side of the curve the row of hands sits on — a preference of the old layout, where the
        row and the curve shared one box. In the strip they no longer share one, so it is kept only
        so the editor's preference has somewhere to land; nothing is laid out by it. */
    void setRowOnTop (bool onTop) { rowOnTop = onTop; }

    /** The spectrum only listens while the curve is on screen. */
    void setSpectrumRunning (bool shouldRun);

    /** The row of hands, shown or stepped aside — for when a picture is thrown open across the
        block's face and everything else has to make room. */
    void setRowVisible (bool);

    /** The curve — and the mode button riding it — shown or not: it is one page of the block's
        picture well, and up only while that page is. Its spectrum listens only while it is up, so
        the TONE picture, which reads the same tap, never has a hidden twin stealing its frames. */
    void setCurveVisible (bool);

    /** How much of the block's height the control row wants: a dial, its name above it at reading
        size, and the slope combo standing on the dial's bottom line. Nothing more — the row used to
        reserve a hundred and twelve and spend the last forty on air. */
    static constexpr int labelRowH = 14;   // 13 px type, the floor, with a unit either side
    static constexpr int rowH = labelRowH + Knob::smallSide    // 64 -> 56: the dial 48 -> the face's small one, the name row 16 -> 14 —
                              + Knob::valueRowH;               // the eight go to the curve; the cuts' stack fits the same height.
                                                               // 56 -> 72: each band's number under its dial (Oleh, 24.10)

    /** A cut's cell — its name, its switch, its slope combo: wide enough for "24 DB" and the
        chevron at 13 px. */
    static constexpr int cutW = 64;

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

        /** A device knob's point on the curve: the frequency where the knob acts hardest,
            computed once per device from its measured curves, so the dot never wanders. It is a
            MARKER — it says where the knob works, and the knob says how much. 0 = no dot (ours
            place their own handles). */
        double anchorHz = 0.0;
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

    felitronics::analysis::RollingSpectrumTap& tap;      // after the console — its own voice
    felitronics::analysis::RollingSpectrumTap& inTap;    // before it — the quiet ground, like the cab's
    felitronics::analysis::SpectrumPane        pane;
    felitronics::analysis::SpectrumPane        inPane;
    std::function<double()>                    sampleRate;

    std::function<double (double)> nativeDb;
    juce::Component* host = nullptr;   // whoever the widgets were handed to, for rebuilt rows
    bool rowOnTop = false;             // see setRowOnTop

    EqCurve curve { [this] (double hz) { return drawnDb (hz); } };

    /** The overlay in the curve's corner: WHOSE tone controls the console wears, written on the
        button itself so the answer is read without opening anything, and the menu under it —
        the two sets, and RESET. The stamps that used to live here went with the PRESETS name:
        a console that can be the device's own or ours has one question, not a drawer. */
    struct ModeButton final : public juce::Component
    {
        juce::String text;
        std::function<void (juce::Point<int>)> onClick;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onClick) onClick (e.getScreenPosition());
        }
    };

    void resetLink();
    juce::StringArray linkParamIds() const;
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

    /** WHAT THE BLOCK ACTUALLY DOES TO THE BALANCE — the one function every picture of this
        console's effect should call, so the curve here and the curve in the block's tile can never
        describe different halves of the same signal.

        The two tone stacks are alternatives: wearing the device's, ours is parked in the DSP, so
        only our cut FILTERS remain in the path — the walls belong to the block whichever tone is
        playing, and so does the level, which is not in this sum at all because it is not a balance.

        `EqLink` was already split along this seam (filterMagnitudeDb / toneMagnitudeDb); nothing
        needs re-designing to ask it the question. */
    double drawnDb (double hz) const
    {
        return nativeDb != nullptr ? display.filterMagnitudeDb (hz) + nativeDb (hz)
                                   : display.magnitudeDb (hz);
    }

    /** The one question the console asks about ITSELF: whose bands it is wearing. `names` is the
        whole pair, in the parameter's order; `nativeAvailable` false greys the device's side, for
        a device that measured nothing. The button in the curve's corner wears the answer, and its
        menu — also the curve's right-click — offers the other side and RESET. */
    void setModes (const juce::StringArray& names, int selected, bool nativeAvailable);
    bool wearsNative() const noexcept { return modeIndex == 0; }
    std::function<void (int)> onModePicked;

    /** RESET, after the console's own parameters went back to default: the block puts whatever
        else it has on show — the device's tone slots — back where the pack starts them. */
    std::function<void()> onReset;

    /** The product's switch for its analysers (prefs::spectraCache), read where the spectra are about to
        be drawn. None: shown. */
    const std::atomic<bool>* spectraShown = nullptr;

private:
    void buildBands (std::vector<Band>);

    /** Writes a band's gain from the curve's own hand — the knob is the parameter's face, so the
        drag goes through it rather than round it. */
    void setBandDb (size_t index, double db);

    std::vector<Band> bands;
    std::vector<std::unique_ptr<Knob>> bandKnobs;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> bandAtts;

    ModeButton modeBtn;
    static constexpr int modeBtnW = 150;   // DEVICE TONE and its chevron at 13 px

    void showModeMenu (juce::Point<int> screenPos);
    juce::StringArray modeNames;
    int               modeIndex = 0;
    bool              nativeOffered = false;

    ZoneSwitch  hpfSw, lpfSw;
    juce::Label hpfLabel { {}, "HPF" }, lpfLabel { {}, "LPF" };
    SlopeCombo  hpfSlopeBox, lpfSlopeBox;

    /** Frequency attachments per handle — the drag gestures write through these. */
    std::unique_ptr<juce::ParameterAttachment> freqAtt[numHandles];
    std::unique_ptr<juce::ParameterAttachment> qAtt[3];
    std::unique_ptr<juce::ParameterAttachment> hpfSlopeAtt, lpfSlopeAtt, b3OnAtt, b3DbAtt;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> refreshAtts;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
