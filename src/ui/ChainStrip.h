#pragma once

#include "../Parameters.h"
#include "../core/ToneStack.h"
#include "ChainLink.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <optional>

namespace orbitamp
{

class AmpProcessor;

/** The chain map — every link in signal order across the top of the window, each with its own
    switch and a live preview: the EQ links their curves, the captured blocks their gain and tone,
    the cabinet its mics on the grille. It is the one place the whole signal path can be READ:
    what is in the chain, what is on, what is loaded into it.

    Clicking a thumb opens that link full width on the faceplate below — a magnifying glass, not a
    mode: the strip stays, and clicking the lit thumb again returns to the overview. */
class ChainStrip final : public juce::Component,
                         private juce::Timer
{
public:
    explicit ChainStrip (AmpProcessor&);
    ~ChainStrip() override;

    /** A thumb was clicked. The owner decides what opening means. */
    std::function<void (ChainLink)> onOpen;

    /** Which link the faceplate has open, so its thumb reads lit. */
    void setActive (std::optional<ChainLink>);

    static constexpr int designHeight = 92;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Thumb;

    void timerCallback() override;

    static constexpr int thumbGap = 10;

    AmpProcessor& amp;

    std::array<std::unique_ptr<Thumb>, (size_t) numChainLinks> thumbs;

    /** Drawing only, designed from the current parameter values on the strip's own clock — the
        playing stacks belong to the audio thread and are never read from here. */
    std::array<core::ToneStack, (size_t) params::numEqLinks> eqDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};

} // namespace orbitamp
