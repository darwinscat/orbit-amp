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

/** THE GEAR LEVER in the dashboard's straight slot: CLEAN, CRUNCH, HI-GAIN down the slot's three
    notches, their names left of it, the one it stands in lit. It follows the hand up and down the
    slot while held, sits in a notch within reach of one, and springs into the nearest on release; a
    click on a name puts it there, and so does the wheel, a notch at a time. In a notch its stem leans
    into it.

    Its box takes in the names and the gate's plate: in the dashboard's space from `left` across. */
class GearLever final : public juce::Component, private juce::Timer
{
public:
    static constexpr int numGears = 3;
    inline static const juce::StringArray names { "CLEAN", "CRUNCH", "HI-GAIN" };
    static constexpr float left = 660.0f, top = 640.0f;

    GearLever();

    std::function<void (int)>   onGear;       // the gear it settled in (or snapped into while held), 0..2
    std::function<void()>       onRelease;    // the hand has let go

    int  getGear() const noexcept { return gear; }
    bool isHeld() const noexcept  { return held; }

    /** `notify` off moves the lever without reporting it — for following a state that moved by itself. */
    void setGear (int, bool animate, bool notify = true);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    face::Glide pos;            // where the stem stands: 0..2, a notch to the gear
    int  gear = 0;
    bool held = false;
    float grabOffset = 0.0f;

    float yOf (float position) const noexcept;      // in the lever's own space
    float positionAt (float y) const noexcept;
    int   nameAt (juce::Point<float>) const noexcept;
    void  timerCallback() override;
};

} // namespace orbitamp::redpaw
