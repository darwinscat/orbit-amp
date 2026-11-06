// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "CabinetBlock.h"
#include "CapturedBlockPanel.h"
#include "DelayBlock.h"
#include "GuardBlock.h"
#include "BassMenuOpener.h"

#include <memory>
#include "ReverbBlock.h"

namespace orbitamp
{

class CompBlock;
class FilterBlock;
class ChorusBlock;

class AmpProcessor;

/** The faceplate: the whole chain at once, four blocks in two rows.

    There used to be three ways to look at this — a map of thumbs across the top, an overview of the
    blocks, and a lens that threw one block across the panel. Three ways to show one signal path, and
    a map is only worth its room while something is hidden. Nothing is hidden here: the row is the
    order, and what a block is set to is written on its face.

    Row one is what makes the sound — the gate's narrow block, then the captured preamp across the
    rest of the row. Row two is what happens to it afterwards, in chain order: delay, reverb,
    cabinet in even shares, then the limiter's narrow block. */
class FaceplateView : public juce::Component,
                      public bass::MenuOpener
{
public:
    explicit FaceplateView (AmpProcessor&);
    ~FaceplateView() override;

    /** ORBITBASS: a block's MENU (ui/BassControls.h, bass::blockMenu) at the block — its right click's, one place per
        block (the owner, 01.11: no settings windows). `lastMenu` says which was asked for last (a gate
        reads it). */
    void openMenu (const juce::String& block) override;
    juce::String lastMenu;

    /** The loaded device changed — the captured blocks rebuild their faces from their packs. */
    void deviceChanged() { preamp.deviceChanged(); }

    /** Where the console's row of hands sits, over its curve or under it. */
    void setEqRowOnTop (bool onTop) { preamp.setEqRowOnTop (onTop); }

    /** The blocks the LAYOUT popup can stand down, in chain order. */
    enum class Block { preamp, delay, reverb, cabinet };

    /** Show or hide one block. Hidden is GONE, not dimmed: the row re-splits evenly among
        whoever remains, and the CALLER puts the block's power parameter out with it — a hidden
        block must not colour the sound. */
    void setShown (Block b, bool shown)
    {
        if (shownFlags[(size_t) b] == shown)
            return;

        shownFlags[(size_t) b] = shown;
        resized();
        repaint();
    }

    /** The guards' blocks, for the menus their owner opens from them. */
    GuardBlock& gateBlock()  noexcept { return gate; }
    GuardBlock& limitBlock() noexcept { return limit; }

    /** Escape's errand: put a thrown-open picture back. One Escape folds one picture. */
    bool foldPicture() { return preamp.foldPicture(); }

    /** Whether any block of a row still stands: 0 is the preamp's, 1 the chain's tail. */
    bool rowPopulated (int row) const
    {
        const auto shown = [this] (Block b) { return shownFlags[(size_t) b]; };

        return row == 0 ? shown (Block::preamp)
                        : shown (Block::delay) || shown (Block::reverb) || shown (Block::cabinet);
    }

/** Whether an emptied row's height is GIVEN to whoever is left (the default) or leaves with it.
        The editor sets it from the player's preference; the faceplate only has to answer for its
        own height, and `resized()` needs to know nothing — a row standing alone takes the lane it
        was handed, tall or short. */
    void setFillsHeight (bool shouldFill)
    {
        if (fillsHeight == shouldFill)
            return;

        fillsHeight = shouldFill;
        resized();
        repaint();
    }

    /** The faceplate's height as laid out RIGHT NOW. Filling: always the full design height, so
        the window never moves and the row that stands grows instead. Not filling: an unpopulated
        row collapses and its space leaves with it. Either way, with NOTHING standing there is
        nobody to give the height to, and the panel closes up. */
    int currentHeight() const
    {
        if (bassRows)
            return bassDesignHeight;

        const bool r1 = rowPopulated (0), r2 = rowPopulated (1);

        if (! r1 && ! r2)
            return 2 * lanePadY;

        if (fillsHeight)
            return designHeight;

        return 2 * lanePadY + (r1 ? row1H : 0) + (r2 ? row2H : 0) + (r1 && r2 ? rowGap : 0);
    }

    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;

    /** OrbitBass's face is wider by its BLEND column beside the AMP's dial (CapturedBlockPanel::bassSideW): its least
        width is designWidth and this. */
    static constexpr int colGap = 14;

    /** OrbitBass's COMP block, after the GATE: its slider, its picture and its ratio square (ui/CompBlock.h). */
    static constexpr int compWidth = 320;

    /** OrbitBass's OCTAVE block, its rail and its picture of the note and its sub (ui/OctaveScope.h), after COMP. */
    static constexpr int octaveWidth = 170;

    /** OrbitBass's ENVELOPE block (FILTER inside), after OCTAVE: its slider and its picture (ui/FilterBlock.h). */
    static constexpr int filterWidth = 170;

    /** OCTAVE's and ENVELOPE's share at OrbitBass's least width: room for each picture to read beside its slider —
        OCTAVE's over 100 px wide, ENVELOPE's over 90. Row one (GATE, COMP and the two) at that is the face's least. */
    static constexpr int fxLeast = 244;

    /** OrbitBass's CHORUS block, at the head of the cabinet's row: its slider and its picture (ui/ChorusBlock.h). The
        row's width is the AMP's, so the cabinet gives up what it takes. */
    /** OrbitBass's CHORUS, DELAY and REVERB in the cabinet's row: each always this wide (the owner, 01.11 — the guitar's
        DELAY and REVERB stand in that), the cabinet taking the rest. */
    static constexpr int fxRowWidth = 256;

    /** ORBITBASS'S FACE, THREE ROWS by the signal's way (the owner, 31.10): GATE, COMP, OCTAVE and ENVELOPE before the
        amp; the AMP across the whole width; CHORUS, CAB IR and LIMIT after it (the cabinet takes what is left — room for
        one more block). Its least width is row one's with OCTAVE and ENVELOPE at fxLeast (02.11); a wider window widens
        the pictures, every knob keeps its size. */

    /** THE CABINET'S LEAST on the bass: never under the guitar face's cabinet at its least (226, three in a row), and wide
        enough that appkit's IrWaveView — which writes the IR's line itself ("85ms  |  48.0kHz  |  24-bit  |  mono", at
        12 px, cut mid-word where it does not fit) — has room for the WORST such line an IR the cabinet takes can give (a
        four-digit length — it takes up to five seconds — at 192 kHz, 32-bit, stereo) at 13 px in this platform's own sans.
        Measured once, on the message thread. */
    static int bassCabLeast();
    static constexpr const char* worstIrLine = "5000ms  |  192.0kHz  |  32-bit  |  stereo";

    /** THE LEAST WIDTH OF ORBITBASS'S FACEPLATE as its strip stands: row one — GATE, COMP, and OCTAVE and ENVELOPE as
        they are in it, each at fxLeast — the cabinet's row — CHORUS, DELAY, REVERB as they are in it, the cabinet at its
        least, LIMIT — or `fallback` (the guitar's least), whichever is widest. */
    static int bassLeastWidth (bool octaveIn, bool filterIn, bool chorusIn, bool delayIn, bool reverbIn, int fallback)
    {
        int first = GuardBlock::designWidth + colGap + compWidth;
        for (const bool in : { octaveIn, filterIn })
            if (in)
                first += colGap + fxLeast;
        int row = GuardBlock::designWidth + colGap + bassCabLeast();
        for (const bool in : { chorusIn, delayIn, reverbIn })
            if (in)
                row += fxRowWidth + colGap;
        return juce::jmax (fallback, first, row);
    }

    static constexpr int bassFxRowH  = 180;
    static constexpr int bassAmpRowH = 200;
    static constexpr int bassDesignHeight = 2 * 2 + bassFxRowH + 12 + bassAmpRowH + 12 + bassFxRowH;   // lanePadY, rowGap

    // Row 1 is the AMP: three sections side by side — the GAIN dial, the channel lever, the picture
    // well with the meters at its edges — each a tier over one shared strip of controls. The EQ curve
    // became one of the well's pictures and its row of knobs the third strip, so the console is no
    // longer a second storey under the rest. Row 2 is a combo and a control or two.
    static constexpr int row1H  = 200;   // 460 -> 378 (the combo onto the border, the console's row and curve trimmed);
                                         // 378 -> 200 when the EQ curve became a page of the picture well
    static constexpr int row2H  = 200;   // 220 -> 200: every block the AMP's height
    // Between the rows, the twelve that part two boxes: the AMP's bottom line from the names riding
    // the lower blocks' top lines.
    static constexpr int rowGap = 12;
    // Lane metrics. No horizontal inset: the lane's edges ARE the panel's edges, so nothing is
    // almost-aligned with anything.
    //
    // Four, not sixteen. The gap a player actually SEES above the first block is this plus the
    // toolbar's own ten plus the twelve a frame leaves above its line for the switch riding it —
    // three separate paddings that only ever get added together, and at sixteen they came to
    // thirty-eight units of nothing under the logo.
    static constexpr int lanePadY = 2;

    static constexpr int designHeight = row1H + rowGap + row2H + 2 * lanePadY;

    /** Where the blocks' drawn frames start and stop inside the faceplate. The in/out gutters
        standing either side line themselves up with THESE rather than with the component, so the
        four vertical edges across the panel are one line each. */
    static constexpr int contentTop    = lanePadY + BlockFrame::borderTopInset;
    static constexpr int contentBottom = lanePadY;

private:

    BlockFrame& frame (Block b);

    AmpProcessor&      owner;   // first: the blocks below are built after it
    CapturedBlockPanel preamp;
    DelayBlock         delay;
    ReverbBlock        reverb;
    CabinetBlock       cabinet;
    GuardBlock         gate, limit;

    /** OrbitBass's three effects, each ONE slider — a rail over its amount, 0..1, the bass's look (bass::asBassRail) —
        and its picture beside it, its menu on a click (ui/BassControls.h); none on a guitar amp. No lamps: the pictures
        show the effect at work. OCTAVE is a guard's block carrying its picture, the note and its sub (ui/OctaveScope.h);
        ENVELOPE (its FILTER, ui/FilterBlock.h) and CHORUS (ui/ChorusBlock.h) blocks of their own. */
    std::unique_ptr<GuardBlock>  octave;
    std::unique_ptr<FilterBlock> filter;
    std::unique_ptr<ChorusBlock> chorus;

    /** OrbitBass's COMP, a block of its own (ui/CompBlock.h); none on a guitar amp. */
    std::unique_ptr<CompBlock> comp;
    std::atomic<bool> octaveWorked {};   // the latch OCTAVE's guard block holds for a lamp it does not show

    /** Who stands on the panel, indexed by Block. The core three out of the box; the delay is
        reached for, not found already on. */
    std::array<bool, 4> shownFlags { true, true, true, true };

    bool fillsHeight = true;   // see setFillsHeight
    bool bassRows    = false;  // OrbitBass's three rows (bassDesignHeight)
    void layOutBassRows();

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
