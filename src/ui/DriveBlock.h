// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "VoicingSelector.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"

#include <array>

namespace orbitamp
{

/** OrbitAmp's DRIVE (params::driveOn, core::DrivePedal), after COMP and before the AMP — a modest pedal and a modest
    tile (the owner, 21.11): its TYPE on the top border beside its name, set like COMP's character, and in a row under
    it GAIN and the type's own one or two: TONE for TS and SD-1, ATTACK and BRIGHT for PRECISION, TIGHT for 33. A knob
    another type owns is not on the face, and keeps its place in the sound for when that type comes back. Every word a
    player reads is 13 px. Dumb: parameters in, nothing reached through. */
class DriveBlock final : public BlockFrame
{
public:
    explicit DriveBlock (AmpProcessor& processor)
        : BlockFrame ("DRIVE", Kind::dsp)
    {
        auto& state = processor.apvts;

        {
            juce::Array<VoicingSelector::Entry> entries;
            for (const auto& name : params::driveTypes)
                entries.add ({ name.toUpperCase(), 0, false });
            type.setEntries (std::move (entries));
        }
        type.fontHeight = 13.0f;
        type.tracking   = 0.15f;
        type.boxed      = false;
        type.tint       = theme::lilac;
        addAndMakeVisible (type);

        knobs[gain].setHint   ("GAIN:   from a gentle push to the pedal's full drive");
        knobs[tone].setHint   ("TONE:   the treble after the clip");
        knobs[attack].setHint ("ATTACK:   how much low end is cut before the clip - right is tighter");
        knobs[bright].setHint ("BRIGHT:   the treble after the clip");
        knobs[tight].setHint  ("TIGHT:   the low cut before the amp - right is tighter");

        static constexpr const char* ids[count] = { params::driveGain, params::driveTone, params::driveAttack,
                                                    params::driveBright, params::driveTight };
        static constexpr const char* names[count] = { "GAIN", "TONE", "ATTACK", "BRIGHT", "TIGHT" };
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
            addChildComponent (knob);
            attachments[(size_t) k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, ids[k], knob);
        }

        // THE TYPE on the border; one undo step a pick. The knobs on the face follow it, whoever moves it.
        typeAttachment = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::driveType),
            [this] (float v)
            {
                type.setSelection (juce::jlimit (0, params::driveTypes.size() - 1, juce::roundToInt (v)));
                showKnobs();
                resized();   // the name on the border is as wide as the name
            });
        type.onPick = [this] (int i) { typeAttachment->setValueAsCompleteGesture ((float) i); };
        typeAttachment->sendInitialUpdate();

        // TEMPORARY (the owner, 21.11): the clip law, to choose by ear — out before any release.
        {
            juce::Array<VoicingSelector::Entry> entries;
            for (const auto& name : params::driveClips)
                entries.add ({ "TEMP CLIP: " + name.toUpperCase(), 0, false });
            clipTmp.setEntries (std::move (entries));
        }
        clipTmp.fontHeight = 13.0f;
        clipTmp.tracking   = 0.1f;
        clipTmp.boxed      = false;
        clipTmp.tint       = theme::txDim;
        addAndMakeVisible (clipTmp);
        clipAttachment = std::make_unique<juce::ParameterAttachment> (
            *state.getParameter (params::driveClipTmp),
            [this] (float v) { clipTmp.setSelection (juce::jlimit (0, params::driveClips.size() - 1, juce::roundToInt (v))); resized(); });
        clipTmp.onPick = [this] (int i) { clipAttachment->setValueAsCompleteGesture ((float) i); };
        clipAttachment->sendInitialUpdate();

        attachPower (*state.getParameter (params::driveOn));
    }

    /** What the face shows, for the gate: the knobs of the type that plays, by their names. */
    juce::StringArray shownKnobs() const
    {
        juce::StringArray names;
        for (const auto& k : knobs)
            if (k.isVisible())
                names.add (k.getName());
        return names;
    }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        {
            const auto slot = borderSlotArea();
            type.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), type.idealWidth())));
            borderSlotUsed = type.getBounds();   // the frame opens the line under it
        }

        // TEMPORARY: the clip law's line at the foot.
        clipTmp.setBounds (r.removeFromBottom (20).withSizeKeepingCentre (juce::jmin (r.getWidth(), clipTmp.idealWidth()), 20));
        clipTmp.setVisible (type.getSelection() != 2);   // the 33 has no clip of its own

        // GAIN and the type's own, in even shares of one row, dials as large as the row lets them, centred in the box.
        int shown = 0;
        for (const auto& k : knobs)
            shown += k.isVisible() ? 1 : 0;
        const int side = juce::jmin (64, r.getHeight() - 16 - Knob::valueRowH, r.getWidth() / juce::jmax (1, shown) - 8);
        auto row = r.withSizeKeepingCentre (r.getWidth(), 16 + side + Knob::valueRowH);
        const int cell = row.getWidth() / juce::jmax (1, shown);
        for (auto& k : knobs)
            if (k.isVisible())
                k.setBounds (row.removeFromLeft (cell));
    }

private:
    enum { gain, tone, attack, bright, tight, count };

    void showKnobs()
    {
        const int t = type.getSelection();
        const bool toned = t == 0 || t == 3;   // TS, SD-1
        knobs[tone].setVisible (toned);
        knobs[attack].setVisible (t == 1);
        knobs[bright].setVisible (t == 1);
        knobs[tight].setVisible (t == 2);
        knobs[gain].setVisible (true);
    }

    VoicingSelector type;
    std::array<Knob, count> knobs { Knob { "Gain", theme::violet, 11 }, Knob { "Tone", theme::violet, 11 },
                                    Knob { "Attack", theme::violet, 11 }, Knob { "Bright", theme::violet, 11 },
                                    Knob { "Tight", theme::violet, 11 } };
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, count> attachments;
    std::unique_ptr<juce::ParameterAttachment> typeAttachment;
    VoicingSelector clipTmp;                                     // TEMPORARY
    std::unique_ptr<juce::ParameterAttachment> clipAttachment;   // TEMPORARY

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveBlock)
};

} // namespace orbitamp
