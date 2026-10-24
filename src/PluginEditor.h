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
#include "ui/HumLearner.h"
#include "ui/VolumeColumn.h"
#include "ui/LearnOverlay.h"
#include "ui/DragRuler.h"
#include "ui/TunerStrip.h"
#include "ui/GuardRail.h"
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
    ~AmpEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** The limiter's whole console. */
    void showLimiterMenu (juce::Point<int> screenPos);
    void showInputMenu (juce::Point<int> screenPos);

    /** Re-read the window preferences (Setup's VIEW page) and lay the editor out to match. */
    void applyViewPrefs();

private:
    static constexpr int margin    = 2;    // the device fills its window: the columns touch the sides, the brand the corner
    static constexpr int headerGap = 0;    // toolbar to faceplate — the frames' own inset already keeps the switches clear
    static constexpr int chromeGap = 10;   // faceplate to the tuner's row, the tuner to the facts, the row to the edge

    /** Room for the host's or the standalone's own title bar, which the display's user area does not
        know about. Guessing slightly high costs nothing; guessing low costs a window that opens
        partly off the bottom of the screen. */
    static constexpr int titleBarAllowance = 60;

    /** THE WIDTH IS THE WINDOW'S CHOICE. The height sets the zoom; what the corner gives beyond the
        design's own width is laid out across — the pictures and the air widen, every knob keeps its
        size. From the design's own width up to a window of `maxAspect`. */
    static constexpr int    minDesignWidth = FaceplateView::designWidth;
    static constexpr int    minBaseWidth   = minDesignWidth + margin * 2;
    static constexpr double maxAspect      = 2.2;

    int designW = minDesignWidth;   // the faceplate's width in design units, as the window stands it
    int baseW() const noexcept { return designW + margin * 2; }
    int maxDesignWidth() const noexcept
    {
        return juce::jmax (minDesignWidth, (int) (maxAspect * (double) baseHeight()) - margin * 2);
    }

    /** The corner may set any aspect between the design's own and `maxAspect`: a corner or a side
        edge moves the width within that range, a top or bottom edge the height. The window's own
        size limits come first, the range after them. */
    struct AspectRange final : public juce::ComponentBoundsConstrainer
    {
        double lowAspect = 1.0, highAspect = 2.0;   // width / height

        void checkBounds (juce::Rectangle<int>& b, const juce::Rectangle<int>& previous,
                          const juce::Rectangle<int>& limits,
                          bool top, bool left, bool bottom, bool right) override
        {
            juce::ComponentBoundsConstrainer::checkBounds (b, previous, limits, top, left, bottom, right);

            const int h = juce::jmax (1, b.getHeight());
            const int wLo = juce::roundToInt (lowAspect * h), wHi = juce::roundToInt (highAspect * h);

            if ((top || bottom) && ! (left || right))
            {
                // A top or bottom edge: the height moves, the width stays — keep the height in range.
                const int w  = b.getWidth();
                const int nh = juce::jlimit (juce::roundToInt (w / highAspect), juce::roundToInt (w / lowAspect), h);
                b = top ? b.withTop (b.getBottom() - nh) : b.withHeight (nh);
            }
            else
            {
                const int nw = juce::jlimit (wLo, juce::jmax (wLo, wHi), b.getWidth());
                b = left ? b.withLeft (b.getRight() - nw) : b.withWidth (nw);
            }
        }
    };

    AspectRange aspect;

    /** The range and the size limits, from the height the window's rows stand at now. */
    void applySizeLimits();

    /** Everything around the faceplate — the faceplate itself answers for its own height now,
        because a LAYOUT choice can collapse a whole row. */
    /** The bottom row: the tuner in the left half, the footer's facts in the right — one row. */
    static constexpr int bottomRowH = juce::jmax (TunerStrip::designHeight, Footer::designHeight);

    static constexpr int fixedHeight = Chrome::designHeight + headerGap
                                     + chromeGap + bottomRowH + chromeGap
                                     + margin * 2;

    /** The window is as tall as what it shows: the faceplate as its LAYOUT stands it, and the
        two strips under the footer only when this player switched them on. */
    bool showDemo   = false;
    bool showGlyphs = false;
    int  baseHeight() const noexcept
    {
        // The tuner's row stays when the tuner does not: the facts stand in it too. The chain strip
        // is the header's second line, inside Chrome::designHeight.
        return fixedHeight + faceplate.currentHeight()
                           + (showDemo ? DemoStrip::designHeight : 0)
                           + (showGlyphs ? GlyphPreview::designHeight : 0);
    }

    /** One attachment per arrow in the strip — built in one loop off `params::chainLinks`.
        Wherever a switch moves — the strip, an undo, a preset, a register, automation — the arrow
        follows. */
    std::vector<std::unique_ptr<juce::ParameterAttachment>> blockRowAtts;

    /** The window's size to come back to when the full-screen button is pressed the second time;
        empty while nothing is remembered. */
    juce::Rectangle<int> sizeBeforeFull;

    /** The zoom the window stands at, from its height — kept so a strip shown or hidden under the
        footer resizes the window at the zoom and the width it already has. */
    float zoomNow = AmpProcessor::preferredScale;

    /** After a strip is switched: the range the corner keeps, the limits, and the window itself,
        at the zoom and the width it already has. */
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
        the ONE place blocks are stood by and brought back. Built in the constructor because its
        rows come from a table the header cannot see. */
    std::unique_ptr<LayoutStrip> layoutStrip;
    /** The gate's console — its menu and LEARN; its face is the rail and the lamp at IN. DECLARED BEFORE the
        overlay on purpose: the overlay draws LEARN's trace from a pointer into this object, and
        members die in reverse order, so the reader has to go first. */
    GateConsole   gateConsole;
    HumLearner    humLearner;       // the hum's own LEARN, from the IN column's menu — never with the gate's
    VolumeColumn  inColumn;         // the input's level and its hand, left of the faceplate
    VolumeColumn  outColumn;        // the output's, mirrored, right of it
    GuardRail     gateRail;         // the gate's threshold, on IN's inner side
    GuardRail     limitRail;        // the limiter's ceiling, on OUT's
    GuardLamp     gateLamp;         // the gate's light, under IN
    GuardLamp     limitLamp;        // the limiter's, under OUT
    TunerStrip    tunerStrip;       // the always-on needle, between the guards
    Footer        footer;
    DemoStrip     demoStrip;       // TEMPORARY — audition player
    LearnOverlay  learnOverlay;     // the LEARN measurement, projected large over the faceplate
    DragRuler     inRuler;          // the IN trim's own ladder, summoned by the hand
    DragRuler     outRuler;         // the OUT trim's, mirrored
    DragRuler     gateRuler;        // the gate's threshold, -80..0 like the IN column, beside its rail
    DragRuler     limitRuler;       // the limiter's ceiling, -3..-0.1, beside its rail
    GlyphPreview  glyphs;           // TEMPORARY — device-glyph review strip
    SetupPanel    setup;   // constructed with the processor: its LIBRARY page reads the packs
    DisclaimerPanel devices { amp.product().name };        // DEVICES & TRADEMARKS: the long notice and the list it is about
            // the Setup overlay — last member, so it sits on top

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpEditor)
};

} // namespace orbitamp
