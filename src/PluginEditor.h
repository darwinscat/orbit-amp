#pragma once

#include "PluginProcessor.h"
#include "ui/FaceplateView.h"

namespace orbitamp
{

/** The editor window. It holds the faceplate and nothing else — no data, no engine reach-through.

    Sizing is fixed at the design size for now; the 50-200% scale factor lands next. */
class AmpEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int margin = 18;   // the ground the device sits on

    FaceplateView faceplate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
