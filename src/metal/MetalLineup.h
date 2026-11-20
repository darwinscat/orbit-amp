// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../channels/Lineup.h"

namespace orbitamp
{
class AmpProcessor;
}

namespace juce
{
class AudioProcessorEditor;
}

namespace orbitamp::metal
{

/** THE E530'S FOUR CHANNELS — CLEAN GAIN LO and HI, LEAD GAIN LO and HI — and what each opens on (channels/Lineup.h).
    PARK, the lever's top stop, is no channel: it is the amp's bypass (AmpProcessor::bypassParameter), as the
    bathyscaphe's. */
const channels::Lineup& lineup();

/** Where LEAD GAIN HI's DRIVE opens: LEAD GAIN LO's pack, a hotter Gain (Lineup::Channel::drive). */
inline constexpr float leadHiDrive = 7.5f;

/** The cabin's face (MetalEditor.h), or the window that says its renders are missing. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::metal
