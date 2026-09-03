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

    /** `withVolume` — the OUT trim's RESET section belongs to the column's door alone. */
    void showLimiterMenu (juce::Point<int> screenPos, bool withVolume = true);

    /** The gear: Setup, and the window's own switches — the two TEMPORARY strips under the footer,
        off unless asked for (prefs::showDemo, prefs::showGlyphs). */
    void showGearMenu (juce::Point<int> screenPos);


    /** A side column clicked in or out (side 0 = IN, 1 = OUT). The instruments are the point:
        the gate and the limiter keep working as set, but the hidden column's TRIM returns to
        unity — a hand nobody can see must not keep pressing. */
    void applyColumnToggle (int side, bool on);

    /** The tuner's needle clicked in or out — it is the whole row now, and the window follows. */
    void applyTunerToggle (bool on);

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
        // The tuner IS the row now: hidden, it collapses whole, gap included.
        return fixedHeight + LayoutStrip::designHeight + faceplate.currentHeight()
                           - (tunerShown ? 0 : TunerStrip::designHeight + chromeGap)
                           + (showDemo ? DemoStrip::designHeight : 0)
                           + (showGlyphs ? GlyphPreview::designHeight : 0);
    }

    /** One attachment per chain block: its `*_on` parameter IS its presence, and wherever the
        power moves — the strip, an undo, a preset, a register, automation — the panel follows. */
    std::vector<std::unique_ptr<juce::ParameterAttachment>> blockRowAtts;

    /** Whether the side columns stand on the panel — the strip's end caps. */
    bool inColShown  = true;
    bool outColShown = true;

    /** Whether the tuner's needle stands under the panel — the strip's TUNER arrow. */
    bool tunerShown = true;

    /** The scale to come back to when the full-screen button is pressed the second time;
        negative while nothing is remembered. */
    float scaleBeforeFull = -1.0f;

    /** The guards' arrows follow their ON parameters — a switched-off guard dims in the strip
        the way a hidden block does. */
    std::unique_ptr<juce::ParameterAttachment> gateRowAtt, limitRowAtt;

    /** After a strip is switched: the aspect the corner drag keeps, the limits, and the window
        itself, at the scale it already has. */
    void applyStripChoice();

    /** Hints. The face has started keeping its captions out of the way — a dial with no label, a
        meter with no reading, a switch with no names — and this is where they went: a word under
        the mouse, a moment after it stops. Inside the editor rather than a desktop window, so it
        goes where the window goes. */
    /** ON THE DESKTOP, not inside the editor. A tooltip parented here is a child of the editor, and
        appkit's popovers are children of the TOP-LEVEL window — so every tip raised over one of them
        (the version stamp's hashes, the update note) drew UNDERNEATH it, showing as a sliver poking
        out from behind the panel. On the desktop it floats above both. One per app, as JUCE asks. */
    juce::TooltipWindow tooltips { nullptr, 450 };

    AmpProcessor& amp;              // the base class's `processor` is the AudioProcessor& — this is ours
    Chrome        chrome;
    FaceplateView faceplate;        // the whole chain, five blocks in two rows

    /** The layout strip — the chain laid flat between the toolbar and the device, always there:
        the ONE place blocks are stood down and brought back. Built in the constructor because
        its rows come from a table the header cannot see. */
    std::unique_ptr<LayoutStrip> layoutStrip;
    GateStrip     gateStrip;        // the IN sliver with the gate's story, left of the faceplate
    OutStrip      outStrip;         // the OUT sliver with the master's hand, right of it
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
