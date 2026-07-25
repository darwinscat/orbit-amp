#pragma once

#include "PluginProcessor.h"
#include "ui/FaceplateView.h"

namespace orbitamp
{

/** The editor window. It holds the faceplate and nothing else — no data, no engine reach-through.

    Zoom is ONE factor, applied once as a transform on the faceplate. Nothing below this point knows
    the editor's zoom: every component lays itself out in design units and is scaled as a whole, so
    the faceplate stays vector-crisp at any size and no layout is ever recomputed per zoom level. */
class AmpEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int margin = 18;   // the ground the device sits on

    static constexpr int baseWidth  = FaceplateView::designWidth  + margin * 2;
    static constexpr int baseHeight = FaceplateView::designHeight + margin * 2;

    AmpProcessor& amp;              // the base class's `processor` is the AudioProcessor& — this is ours
    FaceplateView faceplate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
