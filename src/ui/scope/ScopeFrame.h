#pragma once

#include "../../core/ScopeTap.h"

namespace orbitamp::scope
{

/** One window of before-and-after, handed to a view to draw.

    A plain pair of pointers rather than the tap itself: a view should not be able to decide WHEN to
    read, only what to draw with what it was given. The tap is level-matched on the way out, so
    everything here already shows what the pedal DID rather than that it got louder. */
struct Frame
{
    const float* dry = nullptr;
    const float* wet = nullptr;
    int size = 0;

    bool isEmpty() const noexcept { return dry == nullptr || wet == nullptr || size < 2; }
};

} // namespace orbitamp::scope
