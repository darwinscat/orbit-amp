// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

/** A PRODUCT'S OWN SET — its channels' packs and the family's cabinets — IS INSIDE THE BINARY and is laid
    out as files the first time the amp starts; device/FactorySet.h says why, and how. Which packs the
    product carries is its build's to say (the set data its target links, ProductSetData); the cabinets
    are the family's shelf, the same in every product (device/FactoryShelf.h). Where each file lies is
    the same for all of them: the packs at the top of the family's `Factory` folder, under their own
    names — a channel finds its pack by the capture it holds (Channels.h) — and the cabinets in the
    shelf's folders in `IRs` inside it. */
namespace orbitamp::channels::set
{
/** Where the cabinets lie in a factory folder. */
juce::File cabinetsIn (const juce::File& factory);

/** THE PRODUCT'S OWN CABINETS in a factory folder: the ones its own list there names, in whichever of
    the shelf's folders, and that are there. The folder is the family's, and another product's cabinets
    may lie beside them; CAB steps through the product's alone. */
juce::Array<juce::File> ownCabinets (const juce::File& factory);

/** Makes `factory` hold this build's set, whatever it held before — FactorySet::layOut over what the
    product carries, under the product's own list and `build`, the build it is. Returns how many files it
    wrote or removed, which on every start after the first is none. */
int layOut (const juce::File& factory, juce::int64 build);
} // namespace orbitamp::channels::set
