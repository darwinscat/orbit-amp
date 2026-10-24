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
      delay (processor), reverb (processor), cabinet (processor),
      gate ("GATE", *processor.apvts.getParameter (params::gateThreshold), *processor.apvts.getParameter (params::gateOn),
            -80.0f, 0.0f, true, -50.0f,
            [&processor] { return processor.gateKeyDb.load() <= -89.5f ? 0.0f : -processor.gateMeterDb.load() / 40.0f; },
            processor.gateWorked),
      limit ("LIMIT", *processor.apvts.getParameter (params::limiterCeiling), *processor.apvts.getParameter (params::limiterOn),
             params::limiterCeilingMin, params::limiterCeilingMax, false, -1.0f,
             [&processor] { return -processor.limiterGrDb.load() / 6.0f; },
             processor.limiterWorked)
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

    addAndMakeVisible (gate);
    addAndMakeVisible (limit);
    gate.setSwitchShown (false);
    limit.setSwitchShown (false);

    // The guards' own ladders, on their rails' scales: the gate's threshold over -80..0, a mark every
    // 6 dB and a number every 12; the limiter's ceiling over -3..-0.1, a mark every half decibel and
    // a number every one, with -0.5 at the top. Lilac, the thresholds' colour.
    const auto linearYOf = [] (float lo, float hi)
    {
        return [lo, hi] (juce::Rectangle<float> r, float db)
        {
            return r.getBottom() - r.getHeight() * (juce::jlimit (lo, hi, db) - lo) / (hi - lo);
        };
    };

    const auto valueOf = [&processor] (const char* id)
    {
        return [p = processor.apvts.getParameter (id)] { return p->convertFrom0to1 (p->getValue()); };
    };

    gateRuler.ticksOnLeft  = true;
    limitRuler.ticksOnLeft = false;
    gateRuler.accent  = theme::lilac;
    limitRuler.accent = theme::lilac;

    for (int db = -72; db <= 0; db += 6)
        gateRuler.marks.push_back ({ (float) db, db % 12 == 0 });

    for (int half = -6; half <= -1; ++half)
        limitRuler.marks.push_back ({ half * 0.5f, half % 2 == 0 || half == -1 });

    limitRuler.labelDecimals = 1;
    gateRuler.yOfDb      = linearYOf (-80.0f, 0.0f);
    limitRuler.yOfDb     = linearYOf (params::limiterCeilingMin, params::limiterCeilingMax);
    gateRuler.currentDb  = valueOf (params::gateThreshold);
    limitRuler.currentDb = valueOf (params::limiterCeiling);

    addChildComponent (gateRuler);
    addChildComponent (limitRuler);

    const auto summon = [] (DragRuler& ruler)
    {
        return [&ruler] (bool a) { ruler.setVisible (a); if (a) ruler.toFront (false); };
    };

    gate.slider().onDrag  = summon (gateRuler);
    limit.slider().onDrag = summon (limitRuler);
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

    // The guards first: the gate at the head of row one, the limiter at the end of row two.
    gate.setVisible (! row1.isEmpty());
    limit.setVisible (! row2.isEmpty());

    if (! row1.isEmpty())
    {
        gate.setBounds (row1.removeFromLeft (GuardBlock::designWidth));
        row1.removeFromLeft (colGap);
    }

    if (! row2.isEmpty())
    {
        limit.setBounds (row2.removeFromRight (GuardBlock::designWidth));
        row2.removeFromRight (colGap);
    }

    lay (row1, { Block::preamp });
    lay (row2, { Block::delay, Block::reverb, Block::cabinet });

    // Their ladders stand beside their rails, toward the blocks they guard, the rails' own height.
    const auto railOf = [] (GuardBlock& b) { return b.slider().getBounds() + b.getPosition(); };
    const auto g = railOf (gate), l = railOf (limit);
    gateRuler.setBounds (gate.getRight() + 2, g.getY(), 56, g.getHeight());
    limitRuler.setBounds (limit.getX() - 58, l.getY(), 56, l.getHeight());
}

} // namespace orbitamp
