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
