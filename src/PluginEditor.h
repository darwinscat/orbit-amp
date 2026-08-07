#pragma once

#include "PluginProcessor.h"
#include "ui/Chrome.h"
#include "ui/FaceplateView.h"
#include "ui/Footer.h"
#include "ui/GlyphPreview.h"   // TEMPORARY — device-glyph review strip; remove with the member below

namespace orbitamp
{

/** The editor window: the toolbar, then the device. No data, no engine reach-through.

    Zoom is ONE factor, applied as a transform to each child. Nothing below this point knows the
    editor's zoom: every component lays itself out in design units and is scaled as a whole, so the
    window stays vector-crisp at any size and no layout is ever recomputed per zoom level. */
class AmpEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AmpEditor (AmpProcessor&);
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int margin    = 18;   // the ground the device sits on
    static constexpr int chromeGap = 10;   // toolbar to faceplate

    static constexpr int baseWidth  = FaceplateView::designWidth + margin * 2;
    static constexpr int baseHeight = Chrome::designHeight + chromeGap
                                    + FaceplateView::designHeight
                                    + chromeGap + Footer::designHeight
                                    + GlyphPreview::designHeight   // TEMPORARY
                                    + margin * 2;

    AmpProcessor& amp;              // the base class's `processor` is the AudioProcessor& — this is ours
    Chrome        chrome;
    FaceplateView faceplate;
    Footer        footer;
    GlyphPreview  glyphs;           // TEMPORARY — device-glyph review strip

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
