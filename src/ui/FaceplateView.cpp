// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : boost (processor, processor.boost, "Boost", params::boostId, 0,
             processor.blockSpectrumTap[0], processor.blockInSpectrumTap[0]),
      preamp (processor, processor.preamp, "Preamp", params::preampId, 1,
              processor.blockSpectrumTap[1], processor.blockInSpectrumTap[1]),
      delay (processor), reverb (processor), power (processor, processor.poweramp),
      cabinet (processor)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &delay,
                     (BlockFrame*) &reverb, (BlockFrame*) &power, (BlockFrame*) &cabinet })
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
        case Block::boost:   return boost;
        case Block::preamp:  return preamp;
        case Block::delay:   return delay;
        case Block::reverb:  return reverb;
        case Block::power:   return power;
        case Block::cabinet: return cabinet;
    }

    return cabinet;   // unreachable; keeps the compiler calm
}

void FaceplateView::resized()
{
    auto lane = getLocalBounds().reduced (0, lanePadY);

    // An unpopulated row takes no space at all — the component is only as tall as
    // currentHeight() says, so what remains stacks from the top.
    auto row1 = rowPopulated (0) ? lane.removeFromTop (row1H) : juce::Rectangle<int>();
    if (rowPopulated (0) && rowPopulated (1))
        lane.removeFromTop (rowGap);
    auto row2 = rowPopulated (1) ? lane.removeFromTop (row2H) : juce::Rectangle<int>();

    // ONE law for both rows: the width splits evenly among the blocks that stand in the row, in
    // chain order. The share is recomputed as the walk goes, so the rounding remainder never
    // piles up against the right edge — and the arithmetic keeps the panel's ONE middle line
    // whenever the counts are even: a half is (W-g)/2, and the first two of four quarters spend
    // exactly a half plus its gap, so the third quarter starts where the preamp does. An odd
    // count (thirds) has no middle to keep, which is the price of an odd count.
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

    lay (row1, { Block::boost, Block::preamp });
    lay (row2, { Block::delay, Block::reverb, Block::power, Block::cabinet });
}

} // namespace orbitamp
