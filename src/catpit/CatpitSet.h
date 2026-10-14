// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

/** THE COCKPIT'S OWN SET — its four packs and its cabinets — IS INSIDE THE BINARY and is laid out as
    files the first time the amp starts; device/FactorySet.h says why, and how. What is the cockpit's
    alone is what it carries and where each file lies: the packs at the top of the family's `Factory`
    folder, under their own names — a channel finds its pack by the capture it holds (CatpitChannels.h)
    — and the cabinets in `IRs` inside it. */
namespace orbitamp::catpit::set
{
/** Where the cabinets lie in a factory folder. */
juce::File cabinetsIn (const juce::File& factory);

/** THE COCKPIT'S OWN CABINETS in a factory folder: the ones its own list there names, and that are
    there. The folder is the family's, and another product's cabinets may lie beside them; CAB steps
    through the cockpit's alone. */
juce::Array<juce::File> ownCabinets (const juce::File& factory);

/** Makes `factory` hold this build's set, whatever it held before — FactorySet::layOut over what the
    cockpit carries, under the cockpit's own list. Returns how many files it wrote or removed, which on
    every start after the first is none. */
int layOut (const juce::File& factory);
} // namespace orbitamp::catpit::set
