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

    Row one is the pair that makes the sound — the captured pedal and the captured preamp, an even
    half each, because neither is a layer on the other. Row two is what happens to it afterwards,
    in chain order: delay, reverb, power amp, cabinet. The cabinet holds a half — it has a grille
    to draw and mics to place on it — until the optional blocks claim their quarters; with
    everything shown the row is four even quarters. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

    /** The loaded device changed — the captured blocks rebuild their faces from their packs. */
    void deviceChanged() { boost.deviceChanged(); preamp.deviceChanged(); power.deviceChanged(); }

    /** Whether the power amp block stands in the lower row at all. Off, the reverb and the cabinet
        split the row between them. */
    void setPowerShown (bool shown)
    {
        if (powerShown == shown)
            return;

        powerShown = shown;
        resized();
        repaint();
    }

    /** The delay, the same law: shown, it takes the row's first quarter and the row re-splits
        around it — with everything on, four even quarters. Hidden, the row closes as before. */
    void setDelayShown (bool shown)
    {
        if (delayShown == shown)
            return;

        delayShown = shown;
        resized();
        repaint();
    }

    /** Escape's errand: put a thrown-open picture back. One Escape folds one picture, so a player
        who opened two of them gets out of them one at a time rather than all at once. */
    bool foldPicture() { return boost.foldPicture() || preamp.foldPicture(); }

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

    CapturedBlockPanel boost;
    CapturedBlockPanel preamp;
    DelayBlock         delay;
    ReverbBlock        reverb;
    PowerAmpBlock      power;
    CabinetBlock       cabinet;
    bool               powerShown = false;
    bool               delayShown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
