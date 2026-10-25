// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "CabinetBlock.h"
#include "CapturedBlockPanel.h"
#include "DelayBlock.h"
#include "GuardBlock.h"
#include "ReverbBlock.h"

namespace orbitamp
{

class AmpProcessor;

/** The faceplate: the whole chain at once, four blocks in two rows.

    There used to be three ways to look at this — a map of thumbs across the top, an overview of the
    blocks, and a lens that threw one block across the panel. Three ways to show one signal path, and
    a map is only worth its room while something is hidden. Nothing is hidden here: the row is the
    order, and what a block is set to is written on its face.

    Row one is what makes the sound — the gate's narrow block, then the captured preamp across the
    rest of the row. Row two is what happens to it afterwards, in chain order: delay, reverb,
    cabinet in even shares, then the limiter's narrow block. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

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
    static constexpr int colGap = 14;

    BlockFrame& frame (Block b);

    CapturedBlockPanel preamp;
    DelayBlock         delay;
    ReverbBlock        reverb;
    CabinetBlock       cabinet;
    GuardBlock         gate, limit;

    /** Who stands on the panel, indexed by Block. The core three out of the box; the delay is
        reached for, not found already on. */
    std::array<bool, 4> shownFlags { true, true, true, true };

    bool fillsHeight = true;   // see setFillsHeight

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
