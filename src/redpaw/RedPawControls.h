// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "RedPawArt.h"
#include "../face/Controls.h"

#include <functional>

/** The rover's own controls, beside the family's kit (face/Controls.h). Dumb views, laid out in the
    dashboard's own space (RedPawLayout.h). */
namespace orbitamp::redpaw
{

/** THE GEAR LEVER in the dashboard's straight slot: PARK at its top end, then CLEAN, CRUNCH, HI-GAIN
    down the slot's three notches, their names left of it, the one it stands in lit. It follows the hand
    up and down the slot while held, sits in a position within reach of one, and springs into the
    nearest on release; a click on a name puts it there, and so does the wheel, a position at a time.
    In a notch its stem leans into it.

    Its box takes in the names and the gate's plate: in the dashboard's space from `left` across. */
class GearLever final : public juce::Component, private juce::Timer
{
public:
    /** Its positions, top to bottom: PARK, then the three gears. */
    static constexpr int park = 0, numPositions = 4, numGears = 3;
    inline static const juce::StringArray names { "PARK", "CLEAN", "CRUNCH", "HI-GAIN" };
    static int positionOf (int gear) noexcept { return gear + 1; }
    static constexpr float left = 660.0f, top = 640.0f;

    GearLever();

    std::function<void (int)>   onPosition;   // the position it settled in (or snapped into while held)
    std::function<void()>       onRelease;    // the hand has let go

    int  getPosition() const noexcept { return position; }
    bool isHeld() const noexcept  { return held; }

    /** `notify` off moves the lever without reporting it — for following a state that moved by itself. */
    void setPosition (int, bool animate, bool notify = true);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    face::Glide pos;            // where the stem stands: 0..3, one to a position
    int  position = 1;
    bool held = false;
    float grabOffset = 0.0f;

    float yOf (float position) const noexcept;      // in the lever's own space
    float positionAt (float y) const noexcept;
    int   nameAt (juce::Point<float>) const noexcept;
    void  timerCallback() override;
};

} // namespace orbitamp::redpaw
