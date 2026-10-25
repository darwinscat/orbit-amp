// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

/** A PRODUCT'S OWN SET — its channels' packs and the family's cabinets — lies in the family's factory
    folder, laid out there by the installer (device/FactorySet.h): the packs at its top, under their own
    names — a channel finds its pack by the capture it holds (Channels.h) — and the cabinets in the shelf's
    folders in `IRs` inside it. Which of them are the product's is what its own list there names. */
namespace orbitamp::channels::set
{
/** Where the cabinets lie in a factory folder. */
juce::File cabinetsIn (const juce::File& factory);

/** THE PRODUCT'S OWN CABINETS in a factory folder: the ones its own list there names, in whichever of
    the shelf's folders, and that are there. The folder is the family's, and another product's cabinets
    may lie beside them; CAB steps through the product's alone. `product` is the product's name. */
juce::Array<juce::File> ownCabinets (const juce::File& factory, const juce::String& product);


} // namespace orbitamp::channels::set
