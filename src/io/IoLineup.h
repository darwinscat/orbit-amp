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

/** PRESENCE, IOTIDEBASS'S OWN PARAMETER (the owner, 20.11): the knob's place, 0..1, while the playing pack has no tone named
    Presence — the sulphur snow of the Io world (IoDrive::setPresence) and no sound. Saved with the state, restored and
    automated as any parameter; declared by the product beside its CHANNEL (IoLineup.cpp, addParameters), so no other
    amp has it. It opens where the prototype's view opens, 0.2. Where a pack lists a Presence tone, the knob turns that
    tone instead, and the tone drives the snow. */
inline constexpr const char* presenceParam   = "io_presence";
inline constexpr float       presenceDefault = 0.2f;

/** The cabin's face (IoEditor.h), or the window that says its renders are missing. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::io
