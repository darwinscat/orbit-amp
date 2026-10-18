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

/** THE GEAR LEVER in the dashboard's straight slot: PARK at its top end, then the product's gears down
    the slot's notches — CLEAN, CRUNCH, HI-GAIN, the top ones of them as many as it has — the gear's name
    is on the nav screen. It follows the hand up and down the slot while held, sits in a position within reach of
    one, and springs into the nearest on release; a click anywhere on the gate puts it at the position
    nearest the click, and so does the wheel, a position at a time. In a notch its stem stands in it.

    Its box is the gate's plate and the room over it the grip stands up into: in the dashboard's space
    from layout::leverLeft, layout::leverTop. */
class GearLever final : public juce::Component, private juce::Timer
{
public:
    /** Its positions, top to bottom: PARK, then the gears. */
    static constexpr int park = 0;
    inline static const juce::StringArray names { "PARK", "CLEAN", "CRUNCH", "HI-GAIN" };
    static int positionOf (int gear) noexcept { return gear + 1; }

    /** A position's name on the nav screen. The gears are named down from the top, as the weather is
        graded (PlainsWorld::turboDust): a lever of fewer than three has the top names, so its last gear
        is always HI-GAIN — a rover of two drives CRUNCH and HI-GAIN. */
    static juce::String nameOf (int position, int gears)
    {
        if (position <= park)
            return names[park];
        return names[juce::jlimit (1, names.size() - 1, position + names.size() - 1 - gears)];
    }

    /** In the product's gears, as many as the slot's top notches. */
    explicit GearLever (int gears);

    int numGears() const noexcept     { return gears; }
    int numPositions() const noexcept { return gears + 1; }

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
    void mouseEnter (const juce::MouseEvent&) override { lit.target = 1.0f; startTimerHz (60); }
    void mouseExit (const juce::MouseEvent&) override  { if (! held) { lit.target = 0.0f; startTimerHz (60); } }

private:
    const int gears;
    std::vector<float> heights;         // each position's height in the dashboard's space: PARK, then the gears' notches
    face::Glide pos;            // where the stem stands: 0..gears, one to a position
    face::Glide lit;            // the light round the grip: 0 resting .. 1 under the hand
    int  position = 1;
    bool held = false;
    float grabOffset = 0.0f;

    float yOf (float position) const noexcept;      // in the lever's own space
    float positionAt (float y) const noexcept;
    juce::Rectangle<float> leverBox (float y, float x) const;     // the render's box, its collar standing on (x, y)
    void  timerCallback() override;
};

//==============================================================================
/** THE NAV SCREEN'S TOP LINE: the gear in orange and the channel that is playing beside it, and on the
    right the rover's speed in km/h — read from the windscreen's own drive, so it climbs and falls as
    the gears change, and stands at nothing when parked. Parked, a red warning triangle stands before
    PARK: the amp is out of the sound. On a screen too narrow for the three, and tall enough, they stand
    one under another. */
class NavHeader final : public juce::Component, private juce::Timer
{
public:
    NavHeader();

    std::function<float()> speed;       // km/h
    bool showsSpeed = true;             // ...none where the glass shows it (SpeedHud)

    void set (const juce::String& gear, const juce::String& channel, bool parked);
    void paint (juce::Graphics&) override;

private:
    juce::String gear, channel;
    bool parked = false;
    int  shownKmh = -1;
    void timerCallback() override;
};

//==============================================================================
/** THE SPEED AS A HOLOGRAM ON THE GLASS, where the layout has room for one (RedPawLayout.h): the number
    large and the unit under it, in a pale green that glows, between two brackets — a head-up display,
    light laid over the world outside. It never takes a click: the glass under it steers. */
class SpeedHud final : public juce::Component, private juce::Timer
{
public:
    SpeedHud();

    std::function<float()> speed;       // km/h

    void paint (juce::Graphics&) override;

private:
    int shownKmh = -1;
    void timerCallback() override;
};

//==============================================================================
/** PARKED, IN THE MIDDLE OF THE NAV SCREEN: the warning triangle the top line carries, large, over the
    picture dimmed — the amp is out of the sound. Shown only while parked; it never takes a click. */
class ParkSign final : public juce::Component
{
public:
    ParkSign() { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;
};

//==============================================================================
/** THE NAV SCREEN'S GLASS: a little murky, fine dust on it, a few light scratches — laid over everything
    the screen shows, and faint enough that every number and line under it reads. It never changes: it is
    made once, from a fixed seed, at the screen's own pixel size, and every paint is a copy of it. It
    never takes a click. */
class GlassFilm final : public juce::Component
{
public:
    GlassFilm();
    void paint (juce::Graphics&) override;

private:
    juce::Image film;
    float filmScale = 0.0f;
    juce::Image make (float scale) const;
};

//==============================================================================
/** THE RING ROUND A KNOB: the cyan ring the dashboard paints round each knob's socket, made to say where
    the knob stands — lit from the start of its travel to where its pointer is, the rest dimmed. It
    breathes only when it matters: under the hand, or while the knob turns, it brightens softly, and
    about a second after the hand has gone it eases back. Nothing moves on it otherwise.

    It watches its knob (a dumb view watching another) and lies over the picture round it, `radius`
    from the socket's middle and `width` wide, as the ring was painted; it never takes a click. */
class KnobRing final : public juce::Component, private juce::Timer
{
public:
    KnobRing (const face::ArtKnob&, juce::Point<float> centre, float radius, float width);

    void paint (juce::Graphics&) override;

private:
    const face::ArtKnob& knob;
    const float radius, width;
    float shown = -1.0f;            // the value last drawn
    face::Glide breath;             // 0 resting .. 1 under the hand
    double leftAt = 0.0;            // when the hand last left, on the millisecond counter
    bool touched = false;
    void timerCallback() override;
};

//==============================================================================
/** TURBO'S HATCH, SHUT ON PARK. The picture shows the hatch open over TURBO's hole; parked, the render of it
    shut (Art::turboLid) comes up over the open one and the button in about half a second, and goes again
    the same way when a gear is pulled — a parked TURBO is deaf anyway. A rover whose skin has no such
    render keeps the hatch open always. It lies over layout::turboLid and never takes a click. */
class TurboLid final : public juce::Component, private juce::Timer
{
public:
    TurboLid();

    /** `animate` off puts it there at once — for a face just built. */
    void setShut (bool shut, bool animate);

    void paint (juce::Graphics&) override;

private:
    face::Glide shown;              // 0 open .. 1 shut
    void timerCallback() override;
};

} // namespace orbitamp::redpaw
