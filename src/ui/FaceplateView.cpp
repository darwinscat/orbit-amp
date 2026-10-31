// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "BassControls.h"
#include "ChorusBlock.h"
#include "CompBlock.h"
#include "BassSettings.h"
#include "FilterBlock.h"
#include "OctaveScope.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : owner (processor),
      preamp (processor, processor.preamp, params::preampName, params::preampId, 0,
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

    // The bass's three effects, a rail each over its amount and its picture beside it, in the signal's order — OCTAVE
    // and ENVELOPE at the head of the AMP's row, CHORUS at the head of the cabinet's. One family with COMP: the rails the
    // bass's (bass::asBassRail), and no lamps — each picture shows its effect at work.
    if (processor.chainHolds (params::rowOctave))
    {
        auto& state = processor.apvts;
        // OCTAVE, a guard's block carrying its picture: the rail's scale ticks alone (the grip shows the value).
        octave = std::make_unique<GuardBlock> ("OCTAVE", *state.getParameter (params::octaveAmount), *state.getParameter (params::octaveOn),
                                               0.0f, 1.0f, false, [&processor] { return processor.effectLampDepth (0); }, octaveWorked);
        octave->setSwitchShown (false);
        octave->setLampShown (false);
        for (int k = 0; k <= 10; ++k)
            octave->marks.push_back ({ (float) k * 0.1f, k % 5 == 0 });
        octave->numbered = false;
        bass::asBassRail (octave->valueRail());
        octave->valueRail().onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        octave->onMenu = [&state, raw = octave.get()] (juce::Point<int>) { bass::showOctaveMenu (state, raw); };
        octave->setPicture (std::make_unique<OctaveScope> (processor));   // the note and its sub, right of the rail
        addAndMakeVisible (*octave);
    }

    if (processor.chainHolds (params::rowFilter))
    {
        filter = std::make_unique<FilterBlock> (processor);   // ENVELOPE on the face: its slider and its picture
        filter->setSwitchShown (false);
        addAndMakeVisible (*filter);
    }

    if (processor.chainHolds (params::rowChorus))
    {
        chorus = std::make_unique<ChorusBlock> (processor);   // its slider and its two voices on a circle
        chorus->setSwitchShown (false);
        addAndMakeVisible (*chorus);
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

    // ORBITBASS: every block's GEAR at the top right of its frame, and a right click on it, open its settings window.
    if (processor.product().chain == Chain::bass)
    {
        bassRows = true;   // three rows by the signal's way
        const auto gear = [this] (BlockFrame* b, const char* name) { if (b != nullptr) b->enableSettings ([this, name] { openSettings (name); }); };
        gear (&gate, "GATE");
        gear (comp.get(), "COMP");
        gear (octave.get(), "OCTAVE");
        gear (filter.get(), "ENVELOPE");
        gear (&preamp, "AMP");
        gear (chorus.get(), "CHORUS");
        gear (&cabinet, "CAB IR");
        gear (&limit, "LIMIT");
    }
}

FaceplateView::~FaceplateView() = default;

void FaceplateView::openSettings (const juce::String& block)
{
    lastSettings = block;
    juce::Component* at = block == "COMP" ? (juce::Component*) comp.get() : block == "OCTAVE" ? (juce::Component*) octave.get()
                        : block == "ENVELOPE" ? (juce::Component*) filter.get() : block == "CHORUS" ? (juce::Component*) chorus.get()
                        : block == "AMP" ? (juce::Component*) &preamp : block == "CAB IR" ? (juce::Component*) &cabinet
                        : block == "GATE" ? (juce::Component*) &gate : (juce::Component*) &limit;
    if (at == nullptr || ! at->isShowing())
        return;
    if (auto panel = bass::makeSettings (owner, block))
        juce::CallOutBox::launchAsynchronously (std::move (panel), at->getScreenBounds(), nullptr);
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

void FaceplateView::layOutBassRows()
{
    auto lane = getLocalBounds().reduced (0, lanePadY);
    auto fx   = lane.removeFromTop (bassFxRowH);
    lane.removeFromTop (rowGap);
    auto amp  = lane.removeFromTop (bassAmpRowH);
    lane.removeFromTop (rowGap);
    auto back = lane.removeFromTop (bassFxRowH);

    for (auto* b : { (juce::Component*) &gate, (juce::Component*) &limit, (juce::Component*) comp.get(), (juce::Component*) octave.get(),
                     (juce::Component*) filter.get(), (juce::Component*) chorus.get() })
        if (b != nullptr)
            b->setVisible (true);

    // Row 1, before the amp: GATE, COMP, OCTAVE, ENVELOPE. A wider window goes to the pictures: a third of it to COMP,
    // the rest to OCTAVE and ENVELOPE alike.
    gate.setBounds (fx.removeFromLeft (GuardBlock::designWidth));
    fx.removeFromLeft (colGap);
    constexpr int fxLeast = 219;   // OCTAVE's and ENVELOPE's share at the least width (880)
    const int rest  = fx.getWidth() - compWidth - 2 * colGap;
    const int extra = juce::jmax (0, rest - 2 * fxLeast);
    const int compW = compWidth + extra / 3;
    if (comp != nullptr)
        comp->setBounds (fx.removeFromLeft (compW));
    fx.removeFromLeft (colGap);
    const int half = (fx.getWidth() - colGap) / 2;
    if (octave != nullptr)
        octave->setBounds (fx.removeFromLeft (half));
    fx.removeFromLeft (colGap);
    if (filter != nullptr)
        filter->setBounds (fx);

    // Row 2: the AMP, the whole width.
    preamp.setVisible (shownFlags[(size_t) Block::preamp]);
    preamp.setBounds (amp);

    // Row 3, after the amp: CHORUS, the cabinet (what is left), LIMIT at the end.
    delay.setVisible (false);
    reverb.setVisible (false);
    limit.setBounds (back.removeFromRight (GuardBlock::designWidth));
    back.removeFromRight (colGap);
    if (chorus != nullptr)
    {
        chorus->setBounds (back.removeFromLeft (chorusWidth + juce::jmax (0, back.getWidth() - 880) / 4));
        back.removeFromLeft (colGap);
    }
    cabinet.setVisible (shownFlags[(size_t) Block::cabinet]);
    cabinet.setBounds (back);
}

void FaceplateView::resized()
{
    if (bassRows)
    {
        layOutBassRows();
        return;
    }

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
    for (auto* fx : { (BlockFrame*) octave.get(), (BlockFrame*) filter.get() })
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

        for (auto* fx : { (BlockFrame*) octave.get(), (BlockFrame*) filter.get() })
            if (fx != nullptr)
            {
                fx->setBounds (row1.removeFromLeft (fx == octave.get() ? octaveWidth : filterWidth));
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
            chorus->setBounds (row2.removeFromLeft (chorusWidth));
            row2.removeFromLeft (colGap);
        }
    }

    lay (row2, { Block::delay, Block::reverb, Block::cabinet });
}

} // namespace orbitamp
