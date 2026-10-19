// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Stage.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace orbitamp::face
{

/** A FACE'S PICTURE: its background render, the plates painted over it and every printed word on it.
    Drawn under the controls, and never takes a click. Each face paints its own (the cockpit's Cockpit,
    the rover's Dashboard); what they share is how it is painted, and the two things the console tells
    it.

    The render and the printed words never change, but what shows through a cut-out window may repaint
    sixty times a second, and every one of those repaints the picture over it. So the picture is
    painted ONCE, at the screen's own pixel size (paintStatic), and each frame is a straight copy of the
    part that changed — with what does change, the signs by the history's keys and the names under the
    keys, drawn live over it (paintLive). */
class Picture : public juce::Component
{
public:
    Picture();

    void paint (juce::Graphics&) override;

    /** What the signs by the history's keys say: an arrow with nothing to step back or forward to
        stands dim, and the register that is playing has its letter and its lamp lit. */
    void setHistory (bool canUndo, bool canRedo, int activeRegister);

    /** Which of the six keys under the tone knobs answer the hand, and what each is called — the name
        under one that does not is dimmed with it. The names are the playing pack's where its switches
        are on the keys (Console::showChannel), so they change with the channel. */
    void setKeys (const std::array<bool, 6>& live, const std::array<juce::String, 6>& names);

    /** THE PICTURE THAT NEVER CHANGES, on its own: the stage at `scale` pixels to its unit, what the
        picture leaves bare — its cut-out window — transparent. */
    juce::Image staticImage (float scale) const;

    /** Where the history's signs and lamps are drawn (historyArea), for whoever must know the place. */
    juce::Rectangle<int> historyBox() const { return historyArea(); }

protected:
    bool canUndo = false, canRedo = false;
    int  activeRegister = 0;
    std::array<bool, 6> keysLive { true, true, true, true, true, true };
    std::array<juce::String, 6> keyNames;

    /** The picture that never changes, at `scale` physical pixels to the stage's unit. */
    virtual void paintStatic (juce::Graphics&, float scale) const = 0;

    /** What changes, over it: drawn on every paint. */
    virtual void paintLive (juce::Graphics&) const {}

    /** Where the history's signs are, so that a step of it repaints no more than that. */
    virtual juce::Rectangle<int> historyArea() const { return getLocalBounds(); }

private:
    juce::Image layer;
    float layerScale = 0.0f;
};

} // namespace orbitamp::face
