// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "OrbitAmpSet.h"

#include "PluginProcessor.h"
#include "device/DeviceLibrary.h"

namespace orbitamp
{

/** OrbitAmp's set is in its binary (OrbitAmpSet.h). Once in a process, however many of the amp it
    holds: the first one built lays it out, and the folder is the same for all of them. Apart from
    the face, in a file of its own, so that a gate built on the face does not lay the set out in the
    machine's real folder. */
void layOutFactorySet()
{
    static const int laid = device::FactorySet::layOut (device::DeviceLibrary::factoryDirectory(), set::carried());
    juce::ignoreUnused (laid);
}

} // namespace orbitamp
