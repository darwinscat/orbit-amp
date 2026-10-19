// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : preamp (processor, processor.preamp, params::preampName, params::preampId, 0,
              processor.blockSpectrumTap[0], processor.blockInSpectrumTap[0]),
      delay (processor), reverb (processor), cabinet (processor)
{
    for (auto* b : { (BlockFrame*) &preamp, (BlockFrame*) &delay, (BlockFrame*) &reverb,
                     (BlockFrame*) &cabinet })
    {
        addAndMakeVisible (*b);

        // No pill on the border: the strip's arrow IS the block's one switch now — presence
        // and power are the same parameter, and two doors to one fact is how they drift.
        b->setSwitchShown (false);
    }

    // Every block still binds its power parameter — the frames follow it for their own state.
}

BlockFrame& FaceplateView::frame (Block b)
{
    switch (b)
    {
        case Block::preamp:  return preamp;
        case Block::delay:   return delay;
        case Block::reverb:  return reverb;
        case Block::cabinet: return cabinet;
    }

    return cabinet;   // unreachable; keeps the compiler calm
}

void FaceplateView::resized()
{
    auto lane = getLocalBounds().reduced (0, lanePadY);

    // Both rows standing is the design's own proportions, and there is only one way to draw it.
    // A row standing ALONE takes the whole lane it was handed — which is the full panel when the
    // height is being given away and just its own height when it is not, so this routine needs to
    // know nothing about the choice: `currentHeight()` already made it.
    const bool r1 = rowPopulated (0), r2 = rowPopulated (1);

    juce::Rectangle<int> row1, row2;

    if (r1 && r2)
    {
        row1 = lane.removeFromTop (row1H);
        lane.removeFromTop (rowGap);
        row2 = lane.removeFromTop (row2H);
    }
    else if (r1) row1 = lane;
    else if (r2) row2 = lane;

    // ONE law for both rows: the width splits evenly among the blocks that stand in the row, in
    // chain order. The share is recomputed as the walk goes, so the rounding remainder never
    // piles up against the right edge.
    const auto lay = [this] (juce::Rectangle<int> row, std::initializer_list<Block> order)
    {
        int standing = 0;
        for (auto b : order)
            if (shownFlags[(size_t) b])
                ++standing;

        for (auto b : order)
        {
            auto& blk = frame (b);
            blk.setVisible (shownFlags[(size_t) b]);

            if (! shownFlags[(size_t) b])
                continue;

            blk.setBounds (row.removeFromLeft ((row.getWidth() - (standing - 1) * colGap) / standing));

            if (--standing > 0)
                row.removeFromLeft (colGap);
        }
    };

    lay (row1, { Block::preamp });
    lay (row2, { Block::delay, Block::reverb, Block::cabinet });
}

} // namespace orbitamp
