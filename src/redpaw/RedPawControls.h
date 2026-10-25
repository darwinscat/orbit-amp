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

struct Skin;

/** THE GEAR LEVER in the dashboard's straight slot: the product's gears down the slot's notches, the
    first at the top — CLEAN, CRUNCH, HI-GAIN, the top ones of them as many as it has — the gear's name
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
    /** Its positions — its places — top to bottom: the gears, each place its gear's number from 0. */
    inline static const juce::StringArray names { "CLEAN", "CRUNCH", "HI-GAIN" };

    /** Where the grip's bar ends and its collar begins on the lever's render, down from its top — a
        fraction of the render's height. The bar is what the hand takes. */
    static constexpr float barBottom = 0.85f;

    /** A position's name on the nav screen. The gears are named down from the top, as the weather is
        graded (PlainsWorld::turboDust): a lever of fewer than three has the top names, so its last gear
        is always HI-GAIN — a rover of two drives CRUNCH and HI-GAIN. */
    static juce::String nameOf (int position, int gears)
    {
        return names[juce::jlimit (0, names.size() - 1, position + names.size() - gears)];
    }

    /** In the product's gears, as many as the slot's top notches; it stands in the first. */
    GearLever (const Skin&, int gears);

    int numGears() const noexcept { return numPlaces(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { lit.target = 1.0f; startTimerHz (60); }
    void mouseExit (const juce::MouseEvent&) override  { if (! isHeld()) { lit.target = 0.0f; startTimerHz (60); } }

private:
    const Skin& skin;                   // the rover's whose lever it is: its slot, its notches, its light
    std::vector<float> heights;         // each position's height in the dashboard's space: the gears' notches
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
    the gears change. On a screen too narrow for the three, and tall enough, they stand one under
    another. */
class NavHeader final : public juce::Component, private juce::Timer
{
public:
    NavHeader();

    std::function<float()> speed;       // km/h
    bool showsSpeed = true;             // ...none where the glass shows it (SpeedHud)

    void set (const juce::String& gear, const juce::String& channel);
    void paint (juce::Graphics&) override;

    /** WHAT THE TOP LINE MAKES of a gear, a channel and a speed at this width: the width the three want on
        one line, how many lines they stand on, and the channel as one line shows it, cut with an ellipsis
        where it is wider than the room left beside the gear and the speed. */
    struct Fit { float wanted = 0.0f, room = 0.0f; int lines = 1; juce::String shown; };
    Fit fit (const juce::String& gear, const juce::String& channel, int kmh) const;

private:
    juce::String gear, channel;
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
/** THE RING ROUND A KNOB: the ring round each knob's socket, made to say where the knob stands — lit
    from the start of its travel to where its pointer is, the rest dim. It breathes only when it matters:
    under the hand, or while the knob turns, it brightens softly, and about a second after the hand has
    gone it eases back. Nothing moves on it otherwise.

    It is the whole ring: the pictures had one painted in the groove round each socket, and tools/bake-art
    takes it out of them, for a painted ring is no true circle — it wandered out from under a true one in
    slivers, and in under it in steps at the lit band's edge. It watches its knob (a dumb view watching
    another) and lies in that groove, `radius` from the socket's middle and `width` wide, where the ring
    was painted; it never takes a click. */
class KnobRing final : public juce::Component, private juce::Timer
{
public:
    KnobRing (const Skin&, const face::ArtKnob&, juce::Point<float> centre, float radius, float width);

    void paint (juce::Graphics&) override;

    /** Put round its knob where the knob now stands, the ring this big: the rows are laid again with
        the channel (face::Console::layStrips). */
    void place (juce::Point<float> centre, float radius, float width);

    // What it is drawn with — said here so that the site's demo is drawn with it too (RedPawEditor::webExtras).
    static constexpr float from = -135.0f, sweep = 270.0f;      // a knob's travel, as ArtKnob turns it
    static constexpr juce::uint32 restDark = 0xff05090b;         // the rest of the ring: its light taken toward this dark...
    static constexpr float restDim = 0.62f;                      // ...this far, as the painted ring looked under the veil that dimmed it
    static juce::Colour restOf (juce::Colour light) { return light.interpolatedWith (juce::Colour (restDark), restDim); }
    static constexpr float litAlpha = 0.7f, litAlphaHand = 0.3f;         // the lit arc, and how much more under the hand
    static constexpr float glowAlpha = 0.35f, glowAlphaHand = 0.4f, glowRadius = 3.0f, glowRadiusHand = 4.0f;
    static constexpr float rimOfKnob = 0.46f, rimAlpha = 0.8f;   // under the hand, an orange rim round the cap, of the knob's width

private:
    const Skin& skin;               // the rover's whose ring it is: its light
    const face::ArtKnob& knob;
    float radius, width;
    float shown = -1.0f;            // the value the whole ring is drawn at, taken with its repaint (timerCallback)
    face::Glide breath;             // 0 resting .. 1 under the hand
    double leftAt = 0.0;            // when the hand last left, on the millisecond counter
    bool touched = false;
    void timerCallback() override;
};

} // namespace orbitamp::redpaw
