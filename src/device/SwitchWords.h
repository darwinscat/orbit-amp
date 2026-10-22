// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

namespace orbitamp::device
{

/** WHAT IS WRITTEN FOR ONE POSITION OF A PACK'S SWITCH: the position's own word where it has one (the
    IR-X Ch2's TIGHT · FAT), the switch's name before it where it is bare — a number, or off / on, which
    alone say nothing of what they switch (the IR-X Ch1's BRIGHT OFF · BRIGHT 1 · BRIGHT 2). In capitals,
    as every face writes them. One reading for the faces' keys and the AMP's switches. */
inline juce::String positionWords (const juce::String& switchName, const juce::String& position)
{
    const auto p = position.trim();
    const auto w = p.toLowerCase();
    const bool bare = w == "off" || w == "on" || (p.isNotEmpty() && p.containsOnly ("0123456789.+-"));
    return (bare ? switchName.trim() + " " + p : p).toUpperCase();
}

} // namespace orbitamp::device
