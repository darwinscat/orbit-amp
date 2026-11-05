// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "CabPicker.h"

#include <functional>
#include <utility>

namespace orbitamp
{

/** THE BASS'S CABINETS' WINDOW: the cabinets drawn (CabPicker) in a call-out over whatever opened it — OrbitBass's CAB
    block, the walker's cabinet screen — its arrow at that, inside the window where it fits. What the hand sets there is
    rendered (AmpProcessor::requestCabSynth), heard while it moves and kept when it lets go; and what plays, whoever
    changed it — an undo, a channel, a preset — the window shows within a tenth of a second.

    One for every bass face: what the window does is said here, once. */
class CabWindow final : public juce::Component,
                        private juce::Timer
{
public:
    /** Opens it over `anchor`, offering `range`. `kept`, when given, is told of every cabinet kept, by the `from` it is
        kept under — the walker puts it in the slot playing by it, and brings the cabinet back into the sound. */
    using Kept = std::function<void (const juce::String& from)>;
    static void open (AmpProcessor&, juce::Component& anchor, CabPicker::Range range, Kept kept = {});

    /** A rendered cabinet as the pictures draw it: its box, -1 for none of ours, and its mics. */
    static std::pair<int, CabPicker::Setup> drawnOf (const AmpProcessor::CabSynthChoice&);

    /** ...and a box of CabPicker::cabs() with those mics asked of the engine: heard only while `live`, kept when not. */
    static void request (AmpProcessor&, int cab, const CabPicker::Setup&, bool live);

    /** A rendered cabinet as a line says it: its box as the player reads it (CabPicker::wordOf) and the mics heard —
        "4x10 · MD421", "+U87" with B heard beside A, B's alone with A off. */
    static juce::String labelOf (const AmpProcessor::CabSynthChoice&);

    void resized() override;

private:
    CabWindow (AmpProcessor&, CabPicker::Range, Kept kept);
    void timerCallback() override;

    AmpProcessor& amp;
    CabPicker picker;
    Kept kept;
    juce::String showing;   // the state's cabinet the picker was last shown
};

} // namespace orbitamp
