// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <cstdint>

namespace orbitamp::core
{

/** HOW MUCH MEMORY THIS PROCESS IS HOLDING, in bytes — asked of the system, since nothing in the
    engine counts it. Each system is asked for the figure its own monitor shows:

      macOS    the physical footprint — Activity Monitor's "Memory": what the process is charged
               for, compressed pages included. Not the resident size, which leaves those out and
               reads far too low on a machine under pressure;
      Windows  the working set — what it holds in RAM now, the pages it shares with others included;
      Linux    the resident set, from /proc.

    It is the PROCESS's figure. Standing alone that is the amp; inside a host it is the host and
    everything else the host has loaded, which says nothing about the amp — a face that shows this
    number should not show it there.

    0 when the system would not say. Cheap enough to ask a few times a second; not for the audio
    thread. */
std::uint64_t processMemoryBytes() noexcept;

} // namespace orbitamp::core
