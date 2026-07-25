#pragma once

#include "BlockFrame.h"
#include "EqBlock.h"
#include "PreampBlock.h"

namespace orbitamp
{

/** The faceplate: the device shell, its name plate, the in/out gutters and the block row.

    The row's proportions are the design's golden-ratio lane — boost and reverb take one unit each
    and the preamp takes phi, because the captured voicing is the instrument and the rest are layers
    on it. EQ spans the full row beneath them; it is the one block wide enough to need a curve. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;
    static constexpr int designHeight = 588;

private:
    void paintNamePlate (juce::Graphics&, juce::Rectangle<int>) const;
    void paintGutter (juce::Graphics&, juce::Rectangle<int>, const juce::String& label) const;

    // Lane metrics, from the visual spec's grid: 58px | 1fr | phi*1fr | 1fr | 58px.
    static constexpr int   pad        = 22;    // device shell padding
    static constexpr int   namePlateH = 64;
    static constexpr int   lanePadX   = 10;
    static constexpr int   lanePadY   = 16;
    static constexpr int   gutter     = 58;    // the in / out indicator columns
    static constexpr int   colGap     = 14;
    static constexpr int   rowGap     = 12;
    static constexpr int   row1H      = 196;
    static constexpr int   row2H      = 240;
    static constexpr float phi        = 1.62f; // the preamp column's weight — the wider anchor

    BlockFrame  boost  { "Boost",  BlockFrame::Kind::captured };
    PreampBlock preamp;
    BlockFrame  reverb { "Reverb", BlockFrame::Kind::dsp };
    EqBlock     eq;

    juce::Rectangle<int> inGutter, outGutter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
