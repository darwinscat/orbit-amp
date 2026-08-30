#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "VoicingSelector.h"
#include "ZoneSwitch.h"

#include <felitronics/appkit/IrWaveView.h>

#include <juce_dsp/juce_dsp.h>

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

    /** One tap's newest window into ~72 log-frequency bins, 0..1 against `ref` — the shared peak,
        so the pre and the post keep their relative size. */
    bool binsOf (int tap, std::vector<float>& out, float& ref);

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

    std::array<Switch, 4> switches;   // HPF, LPF, TRIM, Ø

    std::unique_ptr<juce::ParameterAttachment> irAtt, hpfHzAtt, lpfHzAtt, trimAtt;

    juce::dsp::FFT     fft;
    std::vector<float> frame, work, preBins, postBins;
    float              specRef = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabinetBlock)
};

} // namespace orbitamp
