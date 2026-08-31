// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "CabinetBlock.h"
#include "CapturedBlockPanel.h"
#include "DelayBlock.h"
#include "PowerAmpBlock.h"
#include "ReverbBlock.h"

namespace orbitamp
{

class AmpProcessor;

/** The faceplate: the whole chain at once, five blocks in two rows.

    There used to be three ways to look at this — a map of thumbs across the top, an overview of the
    blocks, and a lens that threw one block across the panel. Three ways to show one signal path, and
    a map is only worth its room while something is hidden. Nothing is hidden here: the row is the
    order, and what a block is set to is written on its face.

    Row one is the pair that makes the sound — the captured pedal and the captured preamp. Row two
    is what happens to it afterwards, in chain order: delay, reverb, power amp, cabinet. ONE law
    for both rows: the width splits evenly among the blocks that STAND in the row — halves or the
    whole of row one, quarters up to the whole of row two — because which blocks a player keeps on
    the panel is the player's own business, chosen in the LAYOUT popup. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

    /** The loaded device changed — the captured blocks rebuild their faces from their packs. */
    void deviceChanged() { boost.deviceChanged(); preamp.deviceChanged(); power.deviceChanged(); }

    /** The blocks the LAYOUT popup can stand down, in chain order. */
    enum class Block { boost, preamp, delay, reverb, power, cabinet };

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

    /** Escape's errand: put a thrown-open picture back. One Escape folds one picture, so a player
        who opened two of them gets out of them one at a time rather than all at once. */
    bool foldPicture() { return boost.foldPicture() || preamp.foldPicture(); }

    /** Whether any block of a row still stands: 0 is the pair, 1 the chain's tail. */
    bool rowPopulated (int row) const
    {
        return row == 0 ? shownFlags[0] || shownFlags[1]
                        : shownFlags[2] || shownFlags[3] || shownFlags[4] || shownFlags[5];
    }

    /** The faceplate's height as laid out RIGHT NOW: an unpopulated row COLLAPSES — its space
        leaves with it and the window follows. The editor sizes from this, never from
        designHeight, which stays as the both-rows constant it always was. */
    int currentHeight() const
    {
        const bool r1 = rowPopulated (0), r2 = rowPopulated (1);
        return 2 * lanePadY + (r1 ? row1H : 0) + (r2 ? row2H : 0) + (r1 && r2 ? rowGap : 0);
    }

    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;

    // Row 1 carries a device combo, the in/out meters, the gain column with the picture beside it,
    // and the whole EQ console under them. The console is the part that will not compress: knobs at
    // reading size plus a curve big enough to aim at. Row 2 is a combo and a control or two.
    static constexpr int row1H  = 378;   // the device combo left the box for its border (-46), the console's row shed a combo's
                                         // height (-10) and the curve a sixth of its own (-26): 460 -> 378
    static constexpr int row2H  = 220;
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

    CapturedBlockPanel boost;
    CapturedBlockPanel preamp;
    DelayBlock         delay;
    ReverbBlock        reverb;
    PowerAmpBlock      power;
    CabinetBlock       cabinet;

    /** Who stands on the panel, indexed by Block. The core four out of the box; the delay and
        the power amp are reached for, not found already on. */
    std::array<bool, 6> shownFlags { true, true, false, true, false, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
