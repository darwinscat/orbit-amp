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

namespace orbitamp::io
{

/** THE IO ROVER'S ONE SOUND under its two speeds, SLOW and FAST — a pair over one memory (channels/Lineup.h): the
    SansAmp RBI has no channels, and both speeds play the same. PARK, the lever's top place, is no channel: it is the
    amp's bypass (AmpProcessor::bypassParameter). */
const channels::Lineup& lineup();

/** The cabin's face (IoEditor.h), or the window that says its renders are missing. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::io
