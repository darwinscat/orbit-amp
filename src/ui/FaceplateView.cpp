// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "BassControls.h"
#include "ChorusBlock.h"
#include "CompBlock.h"
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
        bass::asBassAmount (octave->valueRail(), "MIX");
        bass::hint (octave->valueRail(), "MIX: WET/DRY");
        octave->valueRail().onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        octave->onMenu = [&state, raw = octave.get()] (juce::Point<int>) { bass::showOctaveMenu (state, raw); };
        octave->setPicture (std::make_unique<OctaveScope> (processor));   // the note and its sub, right of the rail
        // TONE, the sub's low pass, a fader of its own at the block's right (the owner, 01.11), 50..500 Hz laid out as a
        // frequency is heard, its number under it in Hz.
        auto& tone = octave->addSideRail (*state.getParameter (params::octaveToneHz), 50.0f, 500.0f);
        bass::asBassFader (tone, "TONE", [] (float hz) { return juce::String (juce::roundToInt (hz)) + " Hz"; });
        tone.logScale = true;
        bass::hint (tone, "TONE");
        tone.onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        octave->dimWhenIdle (*octave->pictureShown(), *state.getParameter (params::octaveAmount));   // at 0, still and dimmed
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

    // The bass's DELAY and REVERB are the guitar's own blocks (the owner, 01.11), in the cabinet's row (layOutBassRows).

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

    // Their numbers in their lamps, typed there (the owner, 01.11): the gate's in whole decibels, the ceiling's to a tenth.
    gate.setNumberDecimals (0);
    limit.setNumberDecimals (1);

    // ORBITBASS: a right click anywhere on a block opens its MENU — one place per block, no settings windows, no gear on
    // any frame (the owner, 01.11) — all but the guards: GATE and LIMIT keep their full scales and their own menus (the
    // owner, 31.10).
    if (processor.product().chain == Chain::bass)
    {
        bassRows = true;   // three rows by the signal's way
        bass::hint (gate.valueRail(),  "GATE: threshold");
        bass::hint (limit.valueRail(), "LIMIT: ceiling");
        const auto menu = [this] (BlockFrame* b, const char* name) { if (b != nullptr) b->setBlockMenu ([this, name] { openMenu (name); }); };
        menu (comp.get(), "COMP");
        menu (octave.get(), "OCTAVE");
        menu (filter.get(), "ENVELOPE");
        menu (&preamp, "AMP");
        menu (chorus.get(), "CHORUS");
        menu (&delay, "DELAY");
        menu (&reverb, "REVERB");
        menu (&cabinet, "CAB IR");
        cabinet.pictureMenuIsBlockMenu();   // its waveform takes its own clicks: its right click is the block's menu
    }
    // OrbitAmp's COMP (Product::guitarComp): the bass's block, its menu the bass's too.
    else if (comp != nullptr)
        comp->setBlockMenu ([this] { openMenu ("COMP"); });
}

int FaceplateView::bassCabLeast()
{
    static const int least = []
    {
        const float line = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (13.0f)),
                                                                   worstIrLine);
        return juce::jmax (226, CabinetBlock::bassWidthForWave ((int) std::ceil (line) + 16));   // the line's 8 px each side
    }();
    return least;
}

FaceplateView::~FaceplateView() = default;

void FaceplateView::openMenu (const juce::String& block)
{
    lastMenu = block;
    juce::Component* at = block == "COMP" ? (juce::Component*) comp.get() : block == "OCTAVE" ? (juce::Component*) octave.get()
                        : block == "ENVELOPE" ? (juce::Component*) filter.get() : block == "CHORUS" ? (juce::Component*) chorus.get()
                        : block == "DELAY" ? (juce::Component*) &delay : block == "REVERB" ? (juce::Component*) &reverb
                        : block == "AMP" ? (juce::Component*) &preamp : block == "CAB IR" ? (juce::Component*) &cabinet : nullptr;
    if (at != nullptr && at->isShowing())
        bass::show (bass::blockMenu (owner.apvts, block), at);
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

    // WHO IS IN THE STRIP (the checklist): a block out of it is not on the face, and leaves no hole.
    const auto in = [this] (params::ChainRow row) { return owner.linkShown (row); };
    BlockFrame* const octaveB = in (params::rowOctave) ? (BlockFrame*) octave.get() : nullptr;
    BlockFrame* const filterB = in (params::rowFilter) ? (BlockFrame*) filter.get() : nullptr;
    BlockFrame* const chorusB = in (params::rowChorus) ? (BlockFrame*) chorus.get() : nullptr;
    BlockFrame* const delayB  = in (params::rowDelay)  ? (BlockFrame*) &delay : nullptr;
    BlockFrame* const reverbB = in (params::rowReverb) ? (BlockFrame*) &reverb : nullptr;

    for (auto* b : { (juce::Component*) &gate, (juce::Component*) &limit, (juce::Component*) comp.get() })
        if (b != nullptr)
            b->setVisible (true);
    if (octave != nullptr)      octave->setVisible (octaveB != nullptr);
    if (filter != nullptr)      filter->setVisible (filterB != nullptr);
    if (chorus != nullptr)      chorus->setVisible (chorusB != nullptr);
    delay.setVisible (delayB != nullptr);
    reverb.setVisible (reverbB != nullptr);

    // Row 1, before the amp: GATE, COMP, OCTAVE, ENVELOPE. A wider window goes to the pictures: a third of it to COMP,
    // the rest to OCTAVE and ENVELOPE alike. With one of them out of the strip the other takes their share; with both
    // out, COMP takes the row.
    gate.setBounds (fx.removeFromLeft (GuardBlock::designWidth));
    fx.removeFromLeft (colGap);
    const int effects = (octaveB != nullptr ? 1 : 0) + (filterB != nullptr ? 1 : 0);
    if (effects == 2)
    {
        const int rest  = fx.getWidth() - compWidth - 2 * colGap;
        const int extra = juce::jmax (0, rest - 2 * fxLeast);
        const int compW = compWidth + extra / 3;
        if (comp != nullptr)
            comp->setBounds (fx.removeFromLeft (compW));
        fx.removeFromLeft (colGap);
        const int half = (fx.getWidth() - colGap) / 2;
        octaveB->setBounds (fx.removeFromLeft (half));
        fx.removeFromLeft (colGap);
        filterB->setBounds (fx);
    }
    else if (effects == 1)
    {
        const int rest  = fx.getWidth() - compWidth - colGap;
        const int extra = juce::jmax (0, rest - fxLeast);
        if (comp != nullptr)
            comp->setBounds (fx.removeFromLeft (compWidth + extra / 3));
        fx.removeFromLeft (colGap);
        (octaveB != nullptr ? octaveB : filterB)->setBounds (fx);
    }
    else if (comp != nullptr)
    {
        comp->setBounds (fx);
    }

    // Row 2: the AMP, the whole width.
    preamp.setVisible (shownFlags[(size_t) Block::preamp]);
    preamp.setBounds (amp);

    // Row 3, after the amp, in the signal's order: CHORUS, DELAY, REVERB, the cabinet (what is left), LIMIT at the end.
    // Every effect there is ALWAYS fxRowWidth wide — the guitar's DELAY and REVERB fit that — and the cabinet takes the
    // rest, so adding or taking one away moves nothing but the cabinet (the owner, 01.11).
    limit.setBounds (back.removeFromRight (GuardBlock::designWidth));
    back.removeFromRight (colGap);
    for (auto* b : { chorusB, delayB, reverbB })
        if (b != nullptr)
        {
            b->setBounds (back.removeFromLeft (fxRowWidth));
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
    // OrbitAmp's COMP, after the GATE, while it stands in the strip (its checklist): out, it leaves no hole.
    const bool compIn = comp != nullptr && owner.linkShown (params::rowComp);
    if (comp != nullptr)
        comp->setVisible (compIn && ! row1.isEmpty());
    if (chorus != nullptr)
        chorus->setVisible (! row2.isEmpty());

    if (! row1.isEmpty())
    {
        gate.setBounds (row1.removeFromLeft (GuardBlock::designWidth));
        row1.removeFromLeft (colGap);

        if (compIn)
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
            chorus->setBounds (row2.removeFromLeft (fxRowWidth));
            row2.removeFromLeft (colGap);
        }
    }

    lay (row2, { Block::delay, Block::reverb, Block::cabinet });
}

} // namespace orbitamp
