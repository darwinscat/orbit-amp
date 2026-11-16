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

namespace orbitamp::underwater
{

/** THE BATHYSCAPHE'S THREE CHANNELS — its gears, CLEAN, DIRT and DRIVE — and what each opens on (channels/Lineup.h).
    PARK, the lever's fourth place, is no channel: it is the amp's bypass (AmpProcessor::bypassParameter). */
const channels::Lineup& lineup();

/** The cabin's face (UnderwaterEditor.h), or the window that says its renders are missing. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::underwater
