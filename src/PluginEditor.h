// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PluginProcessor.h"
#include "ui/Chrome.h"
#include "ui/DisclaimerPanel.h"
#include "ui/FaceplateView.h"
#include "ui/DemoStrip.h"   // TEMPORARY — audition player; goes with the glyph strip
#include "ui/Footer.h"
#include "ui/GateConsole.h"
#include "ui/GateStrip.h"
#include "ui/OutStrip.h"
#include "ui/LearnOverlay.h"
#include "ui/DragRuler.h"
#include "ui/TunerStrip.h"
#include "ui/GlyphPreview.h"   // TEMPORARY — device-glyph review strip; remove with the member below
#include "ui/LayoutStrip.h"
#include "ui/SetupPanel.h"

#include <optional>

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
    /** Not defaulted: this window hangs a callback on the processor's history, and the history
        outlives every editor that ever opens on it. Chrome clears its own for the same reason. */
    ~AmpEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** `withVolume` — the OUT trim's RESET section belongs to the column's door alone. */
    void showLimiterMenu (juce::Point<int> screenPos, bool withVolume = true);


    /** What the two switches of every row mean to the WINDOW — the tiles, the two columns and
        the tuner's row — read in one place after any of them moves. */
    void applyRowStates();

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
                           - (tunerStands ? 0 : TunerStrip::designHeight + chromeGap)
                           + (showDemo ? DemoStrip::designHeight : 0)
                           + (showGlyphs ? GlyphPreview::designHeight : 0);
    }

    /** One attachment per chain link that HAS a switch, guards included — built in one loop off
        `params::chainLinks`. Wherever a switch moves — the strip, an undo, a preset, a register,
        automation — the arrow and, for a link with a face, the panel follow. */
    std::vector<std::unique_ptr<juce::ParameterAttachment>> blockRowAtts;

    /** Whether the two columns and the tuner's row stand on the panel. Not preferences any more:
        `applyRowStates` reads them off the same two switches every other link answers to. */
    bool inColStands  = true;
    bool outColStands = true;
    bool tunerStands  = true;

    /** How a link standing by is SHOWN: removed from the panel, or left in place and dimmed. The
        eye's business, not the sound's — a machine preference. Wired to its switch in the step
        that gives it one; until then the panel does what it has always done. */
    bool dimRatherThanRemove = false;

    /** The scale to come back to when the full-screen button is pressed the second time;
        negative while nothing is remembered. */
    float scaleBeforeFull = -1.0f;

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
    /** The gate, with no face of its own — its door is the strip's arrow. DECLARED BEFORE the
        overlay on purpose: the overlay draws LEARN's trace from a pointer into this object, and
        members die in reverse order, so the reader has to go first. */
    GateConsole   gateConsole;
    GateStrip     gateStrip;        // the IN sliver, left of the faceplate
    OutStrip      outStrip;         // the OUT sliver with the master's hand, right of it
    TunerStrip    tunerStrip;       // the always-on needle, between the guards
    Footer        footer;
    DemoStrip     demoStrip;       // TEMPORARY — audition player
    LearnOverlay  learnOverlay;     // the LEARN measurement, projected large over the faceplate
    DragRuler     inRuler;          // the IN trim's own ladder, summoned by the hand
    DragRuler     outRuler;         // the OUT trim's, mirrored
    DragRuler     ceilRuler;        // the limiter ceiling's, lilac, top-third ladder
    GlyphPreview  glyphs;           // TEMPORARY — device-glyph review strip
    SetupPanel    setup;   // constructed with the processor: its EDITOR page writes parameters
    DisclaimerPanel devices;        // DEVICES & TRADEMARKS: the long notice and the list it is about
            // the Setup overlay — last member, so it sits on top

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
