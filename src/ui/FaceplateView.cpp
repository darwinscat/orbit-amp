// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "BassControls.h"
#include "CompBlock.h"
#include "OctaveScope.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : preamp (processor, processor.preamp, params::preampName, params::preampId, 0,
              processor.blockSpectrumTap[0], processor.blockInSpectrumTap[0]),
      delay (processor), reverb (processor), cabinet (processor),
      gate ("GATE", *processor.apvts.getParameter (params::gateThreshold), *processor.apvts.getParameter (params::gateOn),
            -80.0f, -30.0f, true,
            [&processor] { return processor.gateKeyDb.load() <= -89.5f ? 0.0f : -processor.gateMeterDb.load() / 40.0f; },
            processor.gateWorked),
      limit ("LIMIT", *processor.apvts.getParameter (params::limiterCeiling), *processor.apvts.getParameter (params::limiterOn),
             params::limiterCeilingMin, params::limiterCeilingMax, false,
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

    // A block the product's chain does not hold is not on its panel (the bass's delay and reverb): the
    // row splits among the rest.
    shownFlags[(size_t) Block::delay]  = processor.chainHolds (params::rowDelay);
    shownFlags[(size_t) Block::reverb] = processor.chainHolds (params::rowReverb);

    // The bass's COMP, a block of its own after the GATE: its slider, its picture, its ratio.
    if (processor.chainHolds (params::rowComp))
    {
        comp = std::make_unique<CompBlock> (processor);
        comp->setSwitchShown (false);
        addAndMakeVisible (*comp);
    }

    // The bass's three effects: a rail each over its amount, the rail's own scale 0..1 by tenths, its menu on a right
    // click — OCTAVE and FILTER at the head of the AMP's row, CHORUS at the head of the cabinet's, in the signal's order.
    if (processor.chainHolds (params::rowOctave))
    {
        auto& state = processor.apvts;
        // Each lamp lights while its effect works — OCTAVE while its sub plays, FILTER and CHORUS while not idle — and
        // the scale is ticks alone: the grip shows the value, and the numbers would read under 13 px.
        const auto make = [&] (const char* name, const char* amountId, const char* onId, int which, std::atomic<bool>& worked,
                               void (*menu) (juce::AudioProcessorValueTreeState&, juce::Component*))
        {
            auto g = std::make_unique<GuardBlock> (name, *state.getParameter (amountId), *state.getParameter (onId),
                                                   0.0f, 1.0f, false, [&processor, which] { return processor.effectLampDepth (which); },
                                                   worked);
            g->setSwitchShown (false);
            for (int k = 0; k <= 10; ++k)
                g->marks.push_back ({ (float) k * 0.1f, k % 5 == 0 });
            g->numbered = false;
            bass::asBassRail (g->valueRail());
            auto* raw = g.get();
            g->onMenu = [&state, raw, menu] (juce::Point<int>) { menu (state, raw); };
            addAndMakeVisible (*g);
            return g;
        };
        octave = make ("OCTAVE", params::octaveAmount, params::octaveOn, 0, effectWorked[0], bass::showOctaveMenu);
        octave->setPicture (std::make_unique<OctaveScope> (processor));   // the note and its sub, right of the rail
        filter = make ("FILTER", params::filterAmount, params::filterOn, 1, effectWorked[1], bass::showFilterMenu);
        chorus = make ("CHORUS", params::chorusAmount, params::chorusOn, 2, effectWorked[2], bass::showChorusMenu);
    }

    addAndMakeVisible (gate);
    addAndMakeVisible (limit);
    gate.setSwitchShown (false);
    limit.setSwitchShown (false);

    // The guards' fixed scales: the gate's threshold over -80..-30 — above that a gate cuts the
    // playing, not the hum, so the rail does not offer it (Oleh, 24.10) — a tick every 5 dB and a
    // number every 10; the limiter's ceiling over -3..-0.1, a numbered tick every half decibel. The
    // gate's rail stands at the window's left, the limiter's at its right, the scales toward the centre.
    for (int db = -80; db <= -30; db += 5)
        gate.marks.push_back ({ (float) db, db % 10 == 0 });

    for (int half = -6; half <= -1; ++half)
        limit.marks.push_back ({ half * 0.5f, true });

    limit.labelDecimals = 1;
    limit.railOnLeft    = false;
}

FaceplateView::~FaceplateView() = default;

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
    for (auto* fx : { octave.get(), filter.get() })
        if (fx != nullptr)
            fx->setVisible (! row1.isEmpty());
    if (comp != nullptr)
        comp->setVisible (! row1.isEmpty());
    if (chorus != nullptr)
        chorus->setVisible (! row2.isEmpty());

    if (! row1.isEmpty())
    {
        gate.setBounds (row1.removeFromLeft (GuardBlock::designWidth));
        row1.removeFromLeft (colGap);

        if (comp != nullptr)
        {
            comp->setBounds (row1.removeFromLeft (compWidth));
            row1.removeFromLeft (colGap);
        }

        for (auto* fx : { octave.get(), filter.get() })
            if (fx != nullptr)
            {
                fx->setBounds (row1.removeFromLeft (fx == octave.get() ? octaveWidth : GuardBlock::designWidth));
                row1.removeFromLeft (colGap);
            }
    }

    lay (row1, { Block::preamp });

    // Row two: the limiter's narrow block at its end, and the rest in even shares among the delay,
    // the reverb and the cabinet — the limiter's width taken from all three alike (Oleh, 24.10).
    if (! row2.isEmpty())
    {
        limit.setBounds (row2.removeFromRight (GuardBlock::designWidth));
        row2.removeFromRight (colGap);

        if (chorus != nullptr)
        {
            chorus->setBounds (row2.removeFromLeft (GuardBlock::designWidth));
            row2.removeFromLeft (colGap);
        }
    }

    lay (row2, { Block::delay, Block::reverb, Block::cabinet });
}

} // namespace orbitamp
