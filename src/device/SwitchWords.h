// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

namespace orbitamp::device
{

/** A BARE POSITION OF A PACK'S SWITCH: a number, or off / on — words that alone say nothing of what
    they switch (the IR-X Ch1's Bright: off, 1, 2), where the IR-X Ch2's Tight and Fat name themselves. */
inline bool isBarePosition (const juce::String& position)
{
    const auto p = position.trim();
    const auto w = p.toLowerCase();
    return w == "off" || w == "on" || (p.isNotEmpty() && p.containsOnly ("0123456789.+-"));
}

/** WHAT IS WRITTEN FOR ONE POSITION where it stands alone, as the AMP's switch writes the one it is
    at: the position's own word where it has one (TIGHT), the switch's name before it where it is bare
    (BRIGHT 1). In capitals, as every face writes them. The rovers' keys, which stand side by side,
    write the name once over them instead (Console::rightKeysOf). */
inline juce::String positionWords (const juce::String& switchName, const juce::String& position)
{
    const auto p = position.trim();
    return (isBarePosition (p) ? switchName.trim() + " " + p : p).toUpperCase();
}

} // namespace orbitamp::device
