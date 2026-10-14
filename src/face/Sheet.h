// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Controls.h"

#include <memory>

/** What lies over a face's stage: the tag that names what is under the hand, and the windows that open
    inside the face. */
namespace orbitamp::face
{

/** A dark name tag — the full name and state of whatever is under the hand, where the panel itself
    only has room for a short name or none. It lies OVER everything on the stage: the keys stand right
    under the tone knobs, and a tag drawn with the panel slid under the knob above it. Never takes a
    click. */
class NameTag final : public juce::Component
{
public:
    NameTag();
    void paint (juce::Graphics&) override;

    /** Centred on `anchor`, in the stage's own space, and kept inside it. Empty text hides it. */
    void show (juce::Point<float> anchor, const juce::String& text);

private:
    juce::String text;
};

//==============================================================================
/** A WINDOW INSIDE THE FACE: a plate in the family's own colours, centred over the face on a light
    scrim — where the build window opens, and for the same reason: it belongs to the amp, so it stays
    on the amp rather than hanging off its edge over whatever lies behind. A click that misses the
    plate closes it, and so does Escape. */
class Sheet final : public juce::Component
{
public:
    explicit Sheet (std::unique_ptr<juce::Component> content);

    std::function<void()> onDismiss;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** The content may change its own size — a list that grew a row — and the plate goes with it. */
    void childBoundsChanged (juce::Component*) override;

private:
    std::unique_ptr<juce::Component> content;
    juce::Rectangle<int> plate() const;
};

} // namespace orbitamp::face
