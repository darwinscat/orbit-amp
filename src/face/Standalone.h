// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

/** WHAT A FACE ASKS OF THE STANDALONE IT RUNS IN — JUCE's own window and device manager, reached in
    one place. Inside a host, and in any program that is not the standalone, none of it exists:
    devices() is null and the rest does nothing. */
namespace orbitamp::face::standalone
{

/** The device manager: the one thing that opens MIDI inputs and hands what they send to the engine. */
juce::AudioDeviceManager* devices();

/** The standalone writes its devices down when it quits; this writes a choice down at once. */
void rememberDevices();

/** The standalone remembers where its window stood from where it stands as it closes. A window closed
    in full screen stands at the corner of the screen; this puts down where it really belongs. */
void rememberWindowPosition (juce::Point<int> topLeft);

/** True ONCE on this machine for each `key`, and written down in the standalone's own settings as it
    says so: every later call says false, and so does every call inside a host. */
bool firstTimeHere (const char* key);

} // namespace orbitamp::face::standalone
