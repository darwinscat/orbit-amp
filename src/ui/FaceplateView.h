#pragma once

#include "BlockFrame.h"
#include "BoostBlock.h"
#include "CabinetBlock.h"
#include "PowerAmpBlock.h"
#include "PreampBlock.h"
#include "ReverbBlock.h"

namespace orbitamp
{

class AmpProcessor;

/** The faceplate: the device shell, its name plate, the in/out gutters and the block row.

    The row's proportions are the design's golden-ratio lane — boost and reverb take one unit each
    and the preamp takes phi, because the captured voicing is the instrument and the rest are layers
    on it. EQ spans the full row beneath them; it is the one block wide enough to need a curve. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

    /** The loaded device changed — the boost rebuilds its face from the pack. */
    void deviceChanged() { boost.deviceChanged(); }

    void paint (juce::Graphics&) override;
    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;

    // Row 1 is sized by the preamp: voicing row, gain knob, and the tone stack under it. Row 2 —
    // power amp and cabinet — is half of it.
    static constexpr int row1H  = 318;
    static constexpr int row2H  = row1H / 2;
    static constexpr int rowGap = 12;
    static constexpr int designHeight = row1H + rowGap + row2H + 32;   // + lanePadY top and bottom

private:
    void paintGutter (juce::Graphics&, juce::Rectangle<int>, const juce::String& label) const;

    // Lane metrics, from the visual spec's grid: 58px | 1fr | phi*1fr | 1fr | 58px. The slab's inner
    // padding went with the slab — padding only exists to hold content off the edge of a box, and
    // there is no box now. What is left is the lane's own small inset.
    static constexpr int   lanePadX   = 10;
    static constexpr int   lanePadY   = 16;
    static constexpr int   gutter     = 58;    // the in / out indicator columns
    static constexpr int   colGap     = 14;
    static constexpr float phi        = 1.62f; // the preamp column's weight — the wider anchor

    BoostBlock    boost;
    PreampBlock   preamp;
    ReverbBlock   reverb;
    PowerAmpBlock power;
    CabinetBlock  cabinet;

    juce::Rectangle<int> inGutter, outGutter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
