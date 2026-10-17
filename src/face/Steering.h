// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>

namespace orbitamp::face
{

/** STEERING BY HAND: what a face's view is steered by, all of it held — a drag left or right across the
    view while the button is down, the arrow keys while the face has the keyboard, and whatever is handed
    in from outside — summed into one value, -1 full left .. 1 full right, 0 straight ahead. Nothing
    stays where the hand left it: the drag lets go with the button, the keys with the keyboard. What the
    view does with the value — a rover's wheels, a ship's turn — is the view's own.

    The hand's side is the message thread's: the face hands its mouse and its keys on to the calls below.
    The value may be read from any thread — a GL thread's frame reads it as it draws. */
class Steering final
{
public:
    /** The button down at `x`, in the view's own units. */
    void press (float x) noexcept { startX = x; }

    /** The hand at `x`, across a view `width` wide: a quarter of the width either way is full lock. */
    void drag (float x, float width) noexcept
    {
        const float span = juce::jmax (40.0f, width * 0.25f);
        set (dragged, (x - startX) / span);
    }

    /** The button up: the drag lets go. */
    void release() noexcept { set (dragged, 0.0f); }

    /** Whether a key is one of the two that steer, ← and →. Whether they are held is read in keysChanged. */
    static bool steersBy (const juce::KeyPress& k) noexcept
    {
        return k.getKeyCode() == juce::KeyPress::leftKey || k.getKeyCode() == juce::KeyPress::rightKey;
    }

    /** A key has gone down or up: the arrows held now. True while either is. */
    bool keysChanged() noexcept
    {
        const bool left  = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::leftKey);
        const bool right = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::rightKey);
        set (keys, (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f));
        return left || right;
    }

    /** The keyboard has gone elsewhere, and the keys held with it. */
    void keysLost() noexcept { set (keys, 0.0f); }

    /** Held steering from outside, -1..1; 0 lets go. Adds to the hand's. */
    void setOutside (float amount) noexcept { set (outside, amount); }

    /** Everything let go at once — the face hidden, say. */
    void clear() noexcept
    {
        dragged.store (0.0f);
        keys.store (0.0f);
        set (outside, 0.0f);
    }

    /** The steering, -1..1. Any thread. */
    float value() const noexcept
    {
        return juce::jlimit (-1.0f, 1.0f, outside.load() + dragged.load() + keys.load());
    }

    /** Told the new value whenever any of it moves, on the thread that moved it — for a view that takes
        its steering as a value handed to it, rather than reading it here. */
    std::function<void (float)> onChange;

private:
    std::atomic<float> outside { 0.0f }, dragged { 0.0f }, keys { 0.0f };
    float startX = 0.0f;

    void set (std::atomic<float>& part, float v) noexcept
    {
        part.store (juce::jlimit (-1.0f, 1.0f, v));
        if (onChange)
            onChange (value());
    }
};

} // namespace orbitamp::face
