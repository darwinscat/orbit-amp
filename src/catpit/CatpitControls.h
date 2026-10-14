// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitArt.h"
#include "../face/Controls.h"

/** The cockpit's own controls, beside the family's kit (face/Controls.h): the throttle, and TURBO under
    its guard. Dumb views, laid out in the render's own space (CatpitLayout.h) like the kit's. */
namespace orbitamp::catpit
{

using namespace face;

//==============================================================================
/** THE THROTTLE. Four detents — CLEAN, EDGE, CRUNCH, HI-GAIN — down the slot, and pulling it toward
    you (down) is going faster. It follows the hand while held, sticks to a detent within reach of one
    (that is the click under the palm), and springs into the nearest one on release. */
class Lever final : public juce::Component, private juce::Timer
{
public:
    static constexpr int numDetents = 4;
    inline static const juce::StringArray names { "CLEAN", "EDGE", "CRUNCH", "HI-GAIN" };

    /** The grip is wider than the frame's left margin: the lever's box starts this far left of the frame. */
    static constexpr int overhang = 34;

    Lever();

    std::function<void (int)>   onDetent;     // the detent it settled in (or snapped into while held)
    std::function<void()>       onRelease;    // the hand has let go
    std::function<void (float)> onTravel;     // continuous position, 0..numDetents-1, every movement

    int  getDetent() const noexcept { return detent; }
    bool isHeld() const noexcept    { return held; }

    /** `notify` off moves the lever without reporting it — for following a state that moved by itself. */
    void setDetent (int d, bool animate, bool notify = true);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    Glide pos;
    int   detent = 0;
    bool  held = false;
    float grabOffset = 0.0f;

    float detentY (int i) const noexcept;
    float posFromY (float y) const noexcept;
    int   labelAt (juce::Point<float>) const noexcept;
    void  travelled();
    void  timerCallback() override;
};

//==============================================================================
/** TURBO under a hinged guard. Closed → a click lifts the guard; open → a click on the button arms it,
    a click on the lifted lid closes it, and closing it throws the switch back off — the way a guarded
    switch works in a cockpit: the guard is there so the switch cannot be left on by accident. */
class GuardButton final : public juce::Component, private juce::Timer
{
public:
    GuardButton();

    std::function<void (bool)> onChange;
    std::function<void()>      onMenu;           // a right click, when set — and then the guard stays as it is

    bool isOn() const noexcept { return on; }
    void setOn (bool, bool notify);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void enablementChanged() override;

private:
    bool open = false, on = false;
    Glide lid, glow;                             // 0 closed .. 1 open; 0 off .. 1 on
    juce::Rectangle<float> artArea() const;
    void timerCallback() override;
};

} // namespace orbitamp::catpit
