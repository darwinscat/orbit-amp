// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../../core/ScopeTap.h"

namespace orbitamp::scope
{

/** One window of before-and-after, handed to a view to draw.

    A plain pair of pointers rather than the tap itself: a view should not be able to decide WHEN to
    read, only what to draw with what it was given. The tap is level-matched on the way out, so
    everything here already shows what the pedal DID rather than that it got louder. */
/** Whether a picture this size gets NUMBERS on its axes. The small tile beside a dial does not:
    seven frequency labels ran into one another there and read as nothing, and a dB ladder in an
    eleven-point face was furniture. The grid stays either way, in the same places, so the eye
    that learned it on the big picture still knows which line is which. */
inline bool axesLabelled (juce::Rectangle<float> r) noexcept
{
    return r.getWidth() >= 360.0f && r.getHeight() >= 120.0f;
}

struct Frame
{
    const float* dry = nullptr;
    const float* wet = nullptr;
    int size = 0;

    bool isEmpty() const noexcept { return dry == nullptr || wet == nullptr || size < 2; }
};

} // namespace orbitamp::scope
