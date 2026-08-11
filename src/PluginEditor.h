#pragma once

#include "PluginProcessor.h"
#include "ui/ChainStrip.h"
#include "ui/Chrome.h"
#include "ui/FaceplateView.h"
#include "ui/DemoStrip.h"   // TEMPORARY — audition player; goes with the glyph strip
#include "ui/Footer.h"
#include "ui/GateStrip.h"
#include "ui/OutStrip.h"
#include "ui/TallyBadge.h"
#include "ui/LearnOverlay.h"
#include "ui/DragRuler.h"
#include "ui/TunerStrip.h"
#include "ui/GlyphPreview.h"   // TEMPORARY — device-glyph review strip; remove with the member below
#include "ui/SetupPanel.h"

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

    void showLimiterMenu (juce::Point<int> screenPos);

private:
    static constexpr int margin    = 18;   // the ground the device sits on
    static constexpr int chromeGap = 10;   // toolbar to faceplate

    /** Room for the host's or the standalone's own title bar, which the display's user area does not
        know about. Guessing slightly high costs nothing; guessing low costs a window that opens
        partly off the bottom of the screen. */
    static constexpr int titleBarAllowance = 60;

    static constexpr int baseWidth  = FaceplateView::designWidth + margin * 2;
    static constexpr int baseHeight = Chrome::designHeight + chromeGap
                                    + ChainStrip::designHeight + chromeGap
                                    + FaceplateView::designHeight
                                    + chromeGap + TunerStrip::designHeight
                                    + chromeGap + Footer::designHeight
                                    + DemoStrip::designHeight      // TEMPORARY
                                    + GlyphPreview::designHeight   // TEMPORARY
                                    + margin * 2;

    AmpProcessor& amp;              // the base class's `processor` is the AudioProcessor& — this is ours
    Chrome        chrome;
    ChainStrip    strip;            // the chain map, across the top of the panel
    FaceplateView faceplate;
    GateStrip     gateStrip;        // the IN sliver with the gate's story, left of the faceplate
    OutStrip      outStrip;         // the OUT sliver with the master's hand, right of it
    TallyBadge    gateBadge;        // the gate's door and tally light, left of the tuner
    TallyBadge    limitBadge;       // the limiter's tally, right of it — the other guard
    TunerStrip    tunerStrip;       // the always-on needle, between the guards
    Footer        footer;
    DemoStrip     demoStrip;       // TEMPORARY — audition player
    LearnOverlay  learnOverlay;     // the LEARN measurement, projected large over the faceplate
    DragRuler     inRuler;          // the IN trim's own ladder, summoned by the hand
    DragRuler     outRuler;         // the OUT trim's, mirrored
    GlyphPreview  glyphs;           // TEMPORARY — device-glyph review strip
    SetupPanel    setup;            // the Setup overlay — last member, so it sits on top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
