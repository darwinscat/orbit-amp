// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitArt.h"

#include <felitronics/appkit/Brand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

/** The cockpit's controls. Dumb views: a value in, a value out through callbacks, nothing that knows
    the processor — the editor binds them. Every one is laid out in the background render's own space
    (see CatpitLayout.h) and the whole cockpit is scaled as one, so sizes here are in that space. */
namespace orbitamp::catpit
{

using felitronics::appkit::brand::violet;
using felitronics::appkit::brand::lilac;
using felitronics::appkit::brand::orange;

/** A label face at an EM size — 13 means a 13-px em, the way the type rule counts it. */
juce::Font labelFont (float em);

/** Uppercase, tracked, centred in `area`. */
void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, float em,
                juce::Colour, float trackingEm = 0.08f);

/** A tiny animator: eases a value toward a target on the message thread, with an optional overshoot.
    Owners call `step()` from their own timer and repaint while it reports movement. */
struct Glide
{
    float value = 0.0f, target = 0.0f, velocity = 0.0f;
    float stiffness = 900.0f, damping = 42.0f;   // a spring; damping below critical overshoots a touch

    void jump (float v) noexcept { value = target = v; velocity = 0.0f; }
    bool step (float dt) noexcept
    {
        const float a = stiffness * (target - value) - damping * velocity;
        velocity += a * dt;
        value    += velocity * dt;
        if (std::abs (target - value) < 1.0e-4f && std::abs (velocity) < 1.0e-3f)
        {
            jump (target);
            return false;
        }
        return true;
    }
};

//==============================================================================
/** A knob from a pointer-less render: the render stays still — its light belongs to the room, not to
    the knob — and only the pointer turns, drawn on top. 270 degrees of sweep, drag up to raise. */
class ArtKnob final : public juce::Component
{
public:
    /** Pointer geometry as fractions of the render's opaque radius, measured off the original render. */
    struct Pointer { float inner, outer, width; };

    ArtKnob (const juce::Image& art, Pointer);

    std::function<void()>      onGestureStart, onGestureEnd;
    std::function<void (float)> onChange;          // 0..1

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.5f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const juce::Image& art;
    Pointer ptr;
    float value = 0.5f, dragStartValue = 0.0f;
    int   dragStartY = 0;
};

//==============================================================================
/** THE THROTTLE. Four detents — NORMAL, ALTERED, CRUNCH, OVERDRIVE — down the slot, and pulling it toward
    you (down) is going faster. It follows the hand while held, sticks to a detent within reach of one
    (that is the click under the palm), and springs into the nearest one on release. */
class Lever final : public juce::Component, private juce::Timer
{
public:
    static constexpr int numDetents = 4;
    inline static const juce::StringArray names { "NORMAL", "ALTERED", "CRUNCH", "OVERDRIVE" };

    Lever();

    std::function<void (int)>   onDetent;     // the detent it settled in (or snapped into while held)
    std::function<void (float)> onTravel;     // continuous position, 0..numDetents-1, every movement

    int  getDetent() const noexcept { return detent; }
    void setDetent (int d, bool animate);

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
/** A bat toggle seen from straight above, in one of the overhead holes: two positions (down OFF, up ON)
    or three (down, centre, up). A click in the upper half throws it up a notch, the lower half down;
    a two-position one just flips. It snaps with a little overshoot — the visible click. */
class BatToggle final : public juce::Component, private juce::Timer
{
public:
    BatToggle (juce::String name, int positions);

    const juce::String name;
    const int positions;

    std::function<void (int)> onChange;
    std::function<void (bool)> onHover;          // the editor shows the name + state while hovered

    int  getIndex() const noexcept { return index; }
    void setIndex (int i, bool notify);
    juce::String stateText() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override  { if (onHover) onHover (false); }
    void enablementChanged() override { repaint(); }

private:
    int   index = 0;
    Glide throw_;                                // -1 down .. 0 centre .. +1 up
    float throwOf (int i) const noexcept;
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

    bool isOn() const noexcept { return on; }
    void setOn (bool, bool notify);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    bool open = false, on = false;
    Glide lid, glow;                             // 0 closed .. 1 open; 0 off .. 1 on
    juce::Rectangle<float> artArea() const;
    void timerCallback() override;
};

//==============================================================================
/** A horizontal fader in one of the three lower plates: a glowing line up to a metal cap. */
class PlateSlider final : public juce::Component
{
public:
    explicit PlateSlider (juce::Colour lineFrom, juce::Colour lineTo);

    std::function<void()>       onGestureStart, onGestureEnd;
    std::function<void (float)> onChange;

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Colour from, to;
    float value = 0.0f, dragStartValue = 0.0f;
    juce::Range<float> travel() const;
};

//==============================================================================
/** A vertical fader in a side column's wide slot. */
class ColumnFader final : public juce::Component
{
public:
    ColumnFader();

    std::function<void()>       onGestureStart, onGestureEnd;
    std::function<void (float)> onChange;

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.5f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    float value = 0.5f, dragStartValue = 0.0f;
    juce::Range<float> travel() const;
};

//==============================================================================
/** A segment meter in a side column's narrow well, fed a dB reading at 30 Hz. */
class SegmentMeter final : public juce::Component, private juce::Timer
{
public:
    explicit SegmentMeter (std::function<float()> readDb);
    void paint (juce::Graphics&) override;

private:
    std::function<float()> readDb;
    float shown = -90.0f;
    void timerCallback() override;
};

} // namespace orbitamp::catpit
