// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "VoicingSelector.h"
#include "ZoneSwitch.h"

#include <felitronics/analysis/PlotMap.h>
#include <felitronics/analysis/SpectrumPane.h>
#include <felitronics/appkit/IrWaveView.h>

#include <array>
#include <memory>

namespace orbitamp
{

class AmpProcessor;

/** The cabinet, filling the rest of the lower row: ONE impulse response, drawn.

    Half of OrbitCab: one IR, no mix. Its name stands on the border beside the block's; the box is
    the IR itself — the impulse with its tail, the two cuts' curve over it with a handle per cut,
    the trim's handle — and under it the four switches: HPF, LPF, TRIM, Ø. What the switches do is
    baked into the IR off the pump; the picture is the family's IrWaveView, fed the same bytes the
    engine convolves. */
class CabinetBlock final : public BlockFrame,
                           private juce::Timer
{
public:
    explicit CabinetBlock (AmpProcessor&);
    ~CabinetBlock() override;

private:
    void timerCallback() override;
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    /** The IR parameter moved: the picture decodes the shelf's bytes for that one. */
    void loadWave (int index);

    /** The picture wears what the parameters say — the cuts, the trim — so it draws the sound. */
    void pushToWave();

    static constexpr int switchRow = 20;
    static constexpr int gap       = 8;

    AmpProcessor& amp;

    VoicingSelector ir;
    felitronics::appkit::IrWaveView wave;

    struct Switch
    {
        ZoneSwitch  sw;
        juce::Label label;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    std::array<Switch, 3> switches;   // HPF, LPF, Ø — TRIM became the combo below

    /** The trim's whole story in one cell: OFF, a fixed window, or MANUAL. A combo in behaviour,
        our own face in pixels — the consoles' SlopeCombo grammar. */
    struct TrimCombo final : public juce::Component
    {
        std::function<juce::String()> getText;
        std::function<void()>         onOpen;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { if (onOpen) onOpen(); }
    };

    enum class TrimMode { off, fixed, manual };

    void showTrimMenu();
    void applyTrimPick (int itemId);

    /** Reads the mode off the parameters — for init and outside writes (automation, recall).
        An explicitly chosen MANUAL survives landing exactly on a mark: the magnet snaps there,
        and a handle that vanished under the hand would be the bug, not the feature. */
    void deriveTrimMode();

    TrimCombo trimCombo;
    TrimMode  trimMode   = TrimMode::off;
    double    trimModeMs = 0.0;

    /** The mode is HELD — nothing may re-derive it until the writes underneath have landed.

        A parameter attachment does not silence its own write: each `setValueAsCompleteGesture`
        comes straight back through `pushToWave` as it lands, so the FIRST of a pair is read while
        the second value is still yesterday's. That was enough to lose a mode. Asking for a fixed
        window from OFF turned the switch on, the mode was derived from the old fraction, missed
        every mark, landed on MANUAL — and MANUAL is sticky by design, so the value that arrived a
        line later could not take it back. It read as "sometimes it works", because sometimes the
        old fraction WAS on a mark.

        A chosen mode is not derivable in the first place — MANUAL sitting exactly on a mark is the
        same two numbers as a fixed pick — so while it is held, the mode is what it was set to, and
        derivation is left to what it is for: recall and automation. */
    bool modeIsHeld = false;

    /** Whether the trim was on at the last push. The view window has to be squared with the mode
        on the OFF->ON edge and only there — see `pushToWave`. */
    bool trimWasOn = false;

    /** MANUAL's own place, remembered in ms: a fixed pick moves the parameter, but coming back to
        MANUAL puts the handle where the hand last left it — the windows never steal its spot. */
    double manualTrimMs = 0.0;

    std::unique_ptr<juce::ParameterAttachment> irAtt, hpfHzAtt, lpfHzAtt, trimAtt, trimOnAtt,
                                               hpfSlopeAtt, lpfSlopeAtt;

    /** The same liquid analyser the consoles run — one pane for the IR's door, one for its exit. */
    std::array<felitronics::analysis::SpectrumPane, 2> panes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetBlock)
};

} // namespace orbitamp
