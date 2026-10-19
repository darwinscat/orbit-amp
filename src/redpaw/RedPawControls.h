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
    is on the nav screen. Taken by its grip it follows the hand up and down the slot, sits in a position within
    reach of one, and springs into the nearest on release; dragged from anywhere else on the gate it goes
    where the hand is. A press that does not travel slides it into the position nearest the press — on the
    grip, the one it stands in — and so does the wheel, a position at a time. In a notch its stem stands in it.
    How it moves between its positions is every lever's (face::DetentLever); the grip, the still press and
    the light under the hand are its own.

    Its box is the gate's plate and the room over it the grip stands up into: in the dashboard's space
    from layout::leverLeft, layout::leverTop. */
class GearLever final : public face::DetentLever
{
public:
    /** Its positions — its places — top to bottom: PARK, then the gears. */
    static constexpr int park = 0;
    inline static const juce::StringArray names { "PARK", "CLEAN", "CRUNCH", "HI-GAIN" };
    static int positionOf (int gear) noexcept { return gear + 1; }

    /** Where the grip's bar ends and its collar begins on the lever's render, down from its top — a
        fraction of the render's height. The bar is what the hand takes. */
    static constexpr float barBottom = 0.85f;

    /** A position's name on the nav screen. The gears are named down from the top, as the weather is
        graded (PlainsWorld::turboDust): a lever of fewer than three has the top names, so its last gear
        is always HI-GAIN — a rover of two drives CRUNCH and HI-GAIN. */
    static juce::String nameOf (int position, int gears)
    {
        if (position <= park)
            return names[park];
        return names[juce::jlimit (1, names.size() - 1, position + names.size() - 1 - gears)];
    }

    /** In the product's gears, as many as the slot's top notches; it stands in the first. */
    explicit GearLever (int gears);

    int numGears() const noexcept { return numPlaces() - 1; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { lit.target = 1.0f; startTimerHz (60); }
    void mouseExit (const juce::MouseEvent&) override  { if (! isHeld()) { lit.target = 0.0f; startTimerHz (60); } }

private:
    std::vector<float> heights;         // each position's height in the dashboard's space: PARK, then the gears' notches
    face::Glide lit;            // the light round the grip: 0 resting .. 1 under the hand

    float yOf (float position) const noexcept override;      // in the lever's own space
    float travelAt (float y) const noexcept override;
    juce::Rectangle<float> leverBox (float y, float x) const;     // the render's box, its collar standing on (x, y)
    juce::Rectangle<float> standingBox() const;                  // the render's box where the lever stands now
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
    bool showsWarning = true;           // parked, the warning beside the gear

    void set (const juce::String& gear, const juce::String& channel, bool parked);
    void paint (juce::Graphics&) override;

    /** WHAT THE TOP LINE MAKES of a gear, a channel and a speed at this width: the width the three want on
        one line, how many lines they stand on, and the channel as one line shows it, cut with an ellipsis
        where it is wider than the room left beside the gear and the speed. */
    struct Fit { float wanted = 0.0f, room = 0.0f; int lines = 1; juce::String shown; };
    Fit fit (const juce::String& gear, const juce::String& channel, int kmh, bool parked) const;

private:
    juce::String gear, channel;
    bool parked = false;
    int  shownKmh = -1;
    void timerCallback() override;
};

//==============================================================================
/** THE SPEED AS A HOLOGRAM ON THE GLASS, where the layout has room for one (RedPawLayout.h): the number
    large and the unit under it, in a pale green that glows — a head-up display, light laid over the
    world outside. It never takes a click: the glass under it steers. */
class SpeedHud final : public juce::Component, private juce::Timer
{
public:
    SpeedHud();

    std::function<float()> speed;       // km/h

    /** HOW IT IS DRAWN, the site's demo's too (RedPawEditor::webExtras): the km/h as a whole number, at least 0,
        read this many times a second; the number and its unit as outlines of the family's label face (labelFont,
        its size its height as JUCE counts it), each centred across the box at its share of the box's height, the
        kerning JUCE's extra kerning factor; a glow of the light round both — JUCE's DropShadow, no offset, its
        radius this share of the height — and the light filled over it. */
    static constexpr int refreshHz = 10;
    static constexpr juce::uint32 light = 0xff9dffc8;     // the projector's green, pale as the haze outside
    static constexpr float lightAlpha = 0.82f, glowAlpha = 0.55f, glowOfHeight = 0.16f;
    static constexpr float numberOfHeight = 0.62f, numberKerning = 0.06f, numberCentreOfHeight = 0.40f;
    static constexpr const char* unit = "KM/H";
    static constexpr float unitOfHeight = 0.20f, unitKerning = 0.2f, unitCentreOfHeight = 0.86f;

    void paint (juce::Graphics&) override;

private:
    int shownKmh = -1;
    void timerCallback() override;
};

//==============================================================================
/** THE WARNING, wherever it stands — the top line's, small, and the parked screen's, large: a red triangle,
    its corners rounded, an exclamation mark cut dark into it. Its measures in twentieths of its height. */
struct Warning
{
    static constexpr juce::uint32 red = 0xffe8463a, cut = 0xff160606;
    static constexpr float unit = 20.0f;                  // the triangle's height in these
    static constexpr float corner = 2.5f;                 // the corners' rounding
    static constexpr float barWidth = 2.6f, barHeight = 8.0f, barRadius = 1.3f;
    static constexpr float barCentre = 0.52f;             // ...its middle this far down the triangle
    static constexpr float dot = 2.8f, dotUp = 3.6f;      // the dot's size, its middle this far over the base
};

/** PARKED, IN THE MIDDLE OF THE NAV SCREEN: the warning triangle the top line carries, large, over the
    picture dimmed — the amp is out of the sound. Shown only while parked; it never takes a click. */
class ParkSign final : public juce::Component
{
public:
    /** How it is drawn: the screen darkened under it, the triangle as tall as the screen less a margin —
        no less than a share of it, no more than a cap — and a little wider, and its red glow. */
    static constexpr juce::uint32 dim = 0xff050304;
    static constexpr float dimAlpha = 0.72f;
    static constexpr float margin = 24.0f, atLeast = 0.8f, atMost = 96.0f, widthOfHeight = 1.15f;
    static constexpr float glowAlpha = 0.55f;
    static constexpr int   glowRadius = 16;

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

    /** The glass at a scale of the stage, as paint draws it: for the site's demo. */
    juce::Image picture (float scale) const { return make (scale); }

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

    // What it is drawn with — said here so that the site's demo is drawn with it too (RedPawEditor::webExtras).
    static constexpr float from = -135.0f, sweep = 270.0f;      // a knob's travel, as ArtKnob turns it
    static constexpr juce::uint32 veil = 0xff05090b;             // the painted ring dimmed under it...
    static constexpr float veilAlpha = 0.62f, veilWider = 1.5f;  // ...this much, and this much wider than the ring
    static constexpr float litAlpha = 0.7f, litAlphaHand = 0.3f;         // the lit arc, and how much more under the hand
    static constexpr float glowAlpha = 0.35f, glowAlphaHand = 0.4f, glowRadius = 3.0f, glowRadiusHand = 4.0f;
    static constexpr float rimOfKnob = 0.46f, rimAlpha = 0.8f;   // under the hand, an orange rim round the cap, of the knob's width

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

    /** Its spring: a hatch, not a key — about half a second, with no bounce. */
    static constexpr float stiffness = 60.0f, damping = 16.0f;

private:
    face::Glide shown;              // 0 open .. 1 shut
    void timerCallback() override;
};

} // namespace orbitamp::redpaw
