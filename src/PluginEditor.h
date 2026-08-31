// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PluginProcessor.h"
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
#include "ui/LayoutStrip.h"
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
    bool keyPressed (const juce::KeyPress&) override;

    void showLimiterMenu (juce::Point<int> screenPos);

    /** The gear: Setup, and the window's own switches — the two TEMPORARY strips under the footer,
        off unless asked for (prefs::showDemo, prefs::showGlyphs). */
    void showGearMenu (juce::Point<int> screenPos);

    /** One block clicked in or out of the chain: the pref, the panel, the window's height, and
        the hidden block's power going out with it (and coming back as it was). */
    void applyLayoutToggle (int index, bool on);

private:
    static constexpr int margin    = 2;    // the device fills its window: the columns touch the sides, the brand the corner
    static constexpr int headerGap = 0;    // toolbar to faceplate — the frames' own inset already keeps the switches clear
    static constexpr int chromeGap = 10;   // faceplate to tuner to footer

    /** Room for the host's or the standalone's own title bar, which the display's user area does not
        know about. Guessing slightly high costs nothing; guessing low costs a window that opens
        partly off the bottom of the screen. */
    static constexpr int titleBarAllowance = 60;

    static constexpr int baseWidth   = FaceplateView::designWidth + margin * 2;

    /** Everything around the faceplate — the faceplate itself answers for its own height now,
        because a LAYOUT choice can collapse a whole row. */
    static constexpr int fixedHeight = Chrome::designHeight + headerGap
                                     + chromeGap + TunerStrip::designHeight
                                     + chromeGap + Footer::designHeight
                                     + margin * 2;

    /** The window is as tall as what it shows: the faceplate as its LAYOUT stands it, and the
        two strips under the footer only when this player switched them on. */
    bool showDemo   = false;
    bool showGlyphs = false;
    int  baseHeight() const noexcept
    {
        return fixedHeight + LayoutStrip::designHeight + faceplate.currentHeight()
                           + (showDemo ? DemoStrip::designHeight : 0)
                           + (showGlyphs ? GlyphPreview::designHeight : 0);
    }

    /** What each block's power was when the chooser hid it, so coming back restores it. All
        true until a hide records otherwise: a block ADDED to the chain arrives playing. */
    std::array<bool, 8> blockWasOn { true, true, true, true, true, true, true, true };

    /** After a strip is switched: the aspect the corner drag keeps, the limits, and the window
        itself, at the scale it already has. */
    void applyStripChoice();

    /** Hints. The face has started keeping its captions out of the way — a dial with no label, a
        meter with no reading, a switch with no names — and this is where they went: a word under
        the mouse, a moment after it stops. Inside the editor rather than a desktop window, so it
        goes where the window goes. */
    juce::TooltipWindow tooltips { this, 450 };

    AmpProcessor& amp;              // the base class's `processor` is the AudioProcessor& — this is ours
    Chrome        chrome;
    FaceplateView faceplate;        // the whole chain, five blocks in two rows

    /** The layout strip — the chain laid flat between the toolbar and the device, always there:
        the ONE place blocks are stood down and brought back. Built in the constructor because
        its rows come from a table the header cannot see. */
    std::unique_ptr<LayoutStrip> layoutStrip;
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
    DragRuler     ceilRuler;        // the limiter ceiling's, lilac, top-third ladder
    GlyphPreview  glyphs;           // TEMPORARY — device-glyph review strip
    SetupPanel    setup;            // the Setup overlay — last member, so it sits on top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
