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

namespace orbitamp::redgroove
{

/** THE WALKER'S TWO CHANNELS — its two gears, B15 and SVT — and what each opens on (channels/Lineup.h). The
    engine they are kept by is the family's (channels/Engine.h); this file is what makes it the walker's: the
    lineup, CHANNEL among the parameters, the bass's chain under them. */
const channels::Lineup& lineup();

/** The walker's face (RedGrooveEditor.h), or the window that says its pictures are missing. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::redgroove
