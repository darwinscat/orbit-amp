// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "LevelColumns.h"
#include "VoicingSelector.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"

#include <array>

namespace orbitamp
{

/** OrbitAmp's DRIVE (params::driveOn, core::DrivePedal), after COMP and before the AMP — a modest pedal and a modest
    tile (the owner, 21.11), in the Tube Screamer's green (theme::tsGreen: its frame, title, arcs and arrow — lines,
    never a wash). For now the TS alone: its TYPE on the top border beside its name, set like COMP's character, showing
    the one type — the place where the others come back — and in a row under it GAIN and TONE. On the right THE LEVEL
    BEFORE AND AFTER the pedal (the owner: the volume is critical, the level must be seen before and after): the
    house's thin double column (LevelColumns), the AMP's trained-input zone on both, and on the OUT column VOLUME's
    orange hand. Every word a player reads is 13 px; the columns' words and numbers are their hover hint. Dumb:
    parameters and the processor's level taps in. */
class DriveBlock final : public BlockFrame
{
public:
    explicit DriveBlock (AmpProcessor& processor)
        : BlockFrame ("DRIVE", Kind::dsp)
    {
        auto& state = processor.apvts;
        setAccent (theme::tsGreen);

        {
            juce::Array<VoicingSelector::Entry> entries;
            for (const auto& name : params::driveTypes)
                entries.add ({ name.toUpperCase(), 0, false });
            type.setEntries (std::move (entries));
        }
        type.fontHeight = 13.0f;
        type.tracking   = 0.15f;
        type.boxed      = false;
        type.tint       = theme::tsGreen;
        type.setSelection (0);
        addAndMakeVisible (type);

        knobs[gain].setHint ("GAIN:   from a gentle push to the pedal's full drive");
        knobs[tone].setHint ("TONE:   the treble after the clip, as his pedal's - right opens the air");

        static constexpr const char* ids[count] = { params::driveGain, params::driveTone };
        static constexpr const char* names[count] = { "GAIN", "TONE" };
        for (int k = 0; k < count; ++k)
        {
            auto& knob = knobs[(size_t) k];
            knob.setName (names[k]);
            knob.labelFontHeight = 13.0f;   // read, so at the floor (the owner: nothing a player reads under 13 px)
            knob.labelTracking   = Knob::fxLabelTracking;
            knob.labelRowHeight  = 16;
            knob.valueInDial     = false;
            knob.textForValue    = [] (double v) { return juce::String (v, 1); };
            knob.setValueRowShown (true);
            addAndMakeVisible (knob);
            attachments[(size_t) k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ids[k], knob);
        }

        // THE LEVEL BEFORE AND AFTER, the AMP's zone carried back to here — the network's door less the pack's own level
        // and TURBO's push, which still stand between this block's output and it — and VOLUME's hand on the OUT column.
        columns.takeIn  = [&processor] { return processor.driveInPeak.exchange (0.0f); };
        columns.takeOut = [&processor] { return processor.driveOutPeak.exchange (0.0f); };
        columns.zone = [&processor]
        {
            auto& block = processor.blockAt (0);
            const float back = (float) block.packInputDb() + block.turboPushDb();
            return juce::Range<float> (params::captureColdDb - back, params::captureHotDb - back);
        };
        columns.attachOutFader (*state.getParameter (params::driveVolume));
        addAndMakeVisible (columns);

        attachPower (*state.getParameter (params::driveOn));
    }

    /** What the face shows, for the gate: its knobs by their names, and the columns (VOLUME's hand). */
    juce::StringArray shownKnobs() const
    {
        juce::StringArray names;
        for (const auto& k : knobs)
            if (k.isVisible())
                names.add (k.getName());
        return names;
    }
    LevelColumns& levelColumns() noexcept { return columns; }
    juce::String typeShown() const { return params::driveTypes[type.getSelection()]; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        {
            const auto slot = borderSlotArea();
            type.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), type.idealWidth())));
            borderSlotUsed = type.getBounds();   // the frame opens the line under it
        }

        // The level columns on the right, the whole height.
        columns.setBounds (r.removeFromRight (LevelColumns::designWidth));
        r.removeFromRight (6);

        // GAIN and TONE in even shares of one row, dials as large as the row lets them, centred in the box.
        const int side = juce::jmin (64, r.getHeight() - 16 - Knob::valueRowH, r.getWidth() / count - 8);
        auto row = r.withSizeKeepingCentre (r.getWidth(), 16 + side + Knob::valueRowH);
        const int cell = row.getWidth() / count;
        for (auto& k : knobs)
            k.setBounds (row.removeFromLeft (cell));
    }

private:
    enum { gain, tone, count };

    VoicingSelector type;
    std::array<Knob, count> knobs { Knob { "Gain", theme::tsGreen, 11 }, Knob { "Tone", theme::tsGreen, 11 } };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, count> attachments;
    LevelColumns columns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveBlock)
};

} // namespace orbitamp
