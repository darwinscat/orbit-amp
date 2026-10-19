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
/** THE THROTTLE. Four detents — CLEAN, EDGE, CRUNCH, HI-GAIN — down the slot, its places
    (face::DetentLever), and pulling it toward you (down) is going faster. It follows the hand while held,
    sticks to a detent within reach of one (that is the click under the palm), and springs into the
    nearest one on release. Pressed anywhere on the slot it comes to the hand at once — near the grip,
    the grip goes with the hand from where it was taken — and a press on a name sends it to that name. */
class Lever final : public DetentLever
{
public:
    static constexpr int numDetents = 4;
    inline static const juce::StringArray names { "CLEAN", "EDGE", "CRUNCH", "HI-GAIN" };

    /** The grip is wider than the frame's left margin: the lever's box starts this far left of the frame. */
    static constexpr int overhang = 34;

    // The lever's own space: the 261 x 217 AMP MODE frame plus `overhang` to its left, where the grip
    // reaches past the frame. The slot on the left, the four names on the right, a lamp beside each.
    static constexpr float slotX      = (float) overhang + 42.0f;
    static constexpr float firstY     = 50.0f;
    static constexpr float detentStep = 43.0f;
    static constexpr float lampX      = (float) overhang + 92.0f;
    static constexpr float namesX     = (float) overhang + 102.0f;
    static constexpr float gripLength = 92.0f;      // end to end; the stem is not in its middle (layout::gripAxis)
    static constexpr float gripGrowth = 0.05f;      // pulled toward you, it comes a little closer: this much longer a detent
    static constexpr float stiffness  = 260.0f, damping = 22.0f;     // heavier than a toggle: it overshoots a hair and comes back

    Lever();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    float yOf (float travel) const noexcept override;
    float travelAt (float y) const noexcept override;
    int   labelAt (juce::Point<float>) const noexcept;
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
