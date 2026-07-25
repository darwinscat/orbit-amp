#pragma once

#include "PluginProcessor.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** The faceplate window. A dumb view: it owns no data and reaches into nothing — the block
    components get a descriptor + state and hand gestures back. Empty for now; the block row
    (boost / preamp / EQ / reverb) lands here next.

    Sizing follows the design: one scale factor drives the whole editor, 50-200%. */
class AmpEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // The faceplate's design size, at 100%. Everything scales from these.
    static constexpr int baseWidth  = 900;
    static constexpr int baseHeight = 420;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
