// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

/** THE COCKPIT'S OWN SET — its four packs and its cabinets — IS INSIDE THE BINARY, every format of it
    on every system, and is laid out as files the first time the amp starts. A bundle to carry them in
    is the Mac's alone: a Windows standalone and a CLAP are single files, and a set that rode beside
    the binary would need a layout per format and an installer to put it there. Inside the binary there
    is one way, and it is the same one everywhere.

    Laid out rather than played from memory because the whole library reads files: a pack is a path to
    a zip, a cabinet is a file, and what the face offers to save is that file.

    THE FOLDER IS THE AMP'S, NOT THE PLAYER'S: `Factory` beside the player's own `Devices` and `IRs`,
    packs at its top — where the device library already looks when a bundle carries none — and the
    cabinets in `IRs` inside it. */
namespace orbitamp::catpit::set
{
/** Where the cabinets lie in a factory folder. */
juce::File cabinetsIn (const juce::File& factory);

/** Makes `factory` hold this build's set, whatever it held before: a file that is missing or differs
    is written, and one an EARLIER build laid here that this one no longer carries is removed — a pack
    or a cabinet left behind would go on being offered. What is removed is only what the folder's own
    list says a build put there: nothing else in it is the build's to touch. Returns how many files it
    wrote or removed, which on every start after the first is none. */
int layOut (const juce::File& factory);
} // namespace orbitamp::catpit::set
