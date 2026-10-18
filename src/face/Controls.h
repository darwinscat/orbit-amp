// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Stage.h"
#include "../core/TunerEar.h"

#include <felitronics/appkit/Brand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

/** THE FACES' CONTROLS — the kit the cockpit was drawn with, and every face after it. Dumb views: a value
    in, a value out through callbacks, nothing that knows the processor — the editor binds them. Every one
    is laid out in its face's background render's own space (Stage.h) and the whole face is scaled as
    one, so sizes here are in that space. What a control is drawn with that is a face's own — a knob's
    render, a cap's — it is handed. */
namespace orbitamp::face
{

using felitronics::appkit::brand::violet;
using felitronics::appkit::brand::lilac;
using felitronics::appkit::brand::orange;

/** The family's label face at a height in px — counted the way the rest of OrbitAmp counts it. */
juce::Font labelFont (float px);

/** Uppercase, tracked, centred in `area`; `weight` px heavier than the font draws it (0: the font as it is). */
void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, float em,
                juce::Colour, float trackingEm = 0.08f, float weight = 0.0f);

/** `text` centred in `box` in the Graphics' colour, as Graphics::drawText puts it there — and with
    `weight` > 0 each stroke that many px heavier, its outline traced round as well as filled. */
void writeWords (juce::Graphics&, const juce::String& text, const juce::Font&, juce::Rectangle<float> box, float weight);

/** THE INK A FACE IS LETTERED IN: the words printed on its panel, and the ones there that answer the
    hand (Legend). The family's unless the face says otherwise, for a dark panel — the names lilac, what
    they are set to a brighter near-white, a word that says a state orange, and nothing under them. A
    face of bright metal writes dark, with a light edge a pixel under each word, as if the word were cut
    into the metal and the cut's lower lip caught the light — and, cut deeper, a shade a pixel over it,
    the upper wall in its own shadow, and strokes heavier than the font's; and lets no word go fainter
    than it can still be read at there. */
struct Ink
{
    juce::Colour words  { 0xffc9b8f5 };     // the names
    juce::Colour values { 0xffece9f6 };     // what they are set to
    juce::Colour state  = orange;           // a word that says a state: TURBO, DELAY, the register playing
    juce::Colour edge   {};                 // under each word; none if transparent
    float leastAlpha = 0.0f;                // the faintest a word is drawn, however dim it is said to be
    juce::Colour shade  {};                 // over each word; none if transparent
    float weight = 0.0f;                    // how much heavier than the font a word's strokes are, px

    static constexpr float edgeDrop = 1.0f;

    /** `colour` at `alpha` — or as faint as this ink lets a word go. */
    juce::Colour at (juce::Colour colour, float alpha) const { return colour.withAlpha (juce::jmax (alpha, leastAlpha)); }

    /** The edge under a word or a sign written in `colour`: as faint as it is. */
    juce::Colour under (juce::Colour colour) const { return edge.withMultipliedAlpha (colour.getFloatAlpha()); }

    /** The shade over a word written in `colour`: as faint as it is. */
    juce::Colour over (juce::Colour colour) const { return shade.withMultipliedAlpha (colour.getFloatAlpha()); }

    /** A word as drawLabel writes it, at this ink's weight, between its shade and its edge. */
    void label (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, float em,
                juce::Colour, float trackingEm = 0.08f) const;
};

/** A soft glow under a path — the light the renders give their lit parts. */
void glow (juce::Graphics&, const juce::Path&, juce::Colour, float radius);

/** The controls' animations step at this. */
inline constexpr float frameDt = 1.0f / 60.0f;

/** Where a render's opaque part sits — its alpha bounding box, in image pixels. The renders carry
    uneven transparent margins, so anything that is centred or scaled goes by this, not the canvas. */
juce::Rectangle<float> opaqueBounds (const juce::Image&);

/** Draws `img`'s opaque part fitted into `area` (aspect kept, centred), at `alpha`. */
void drawFitted (juce::Graphics&, const juce::Image& img, juce::Rectangle<float> area, float alpha = 1.0f);

/** Where drawFitted puts a render's opaque part in `area`. */
juce::Rectangle<float> fittedArea (const juce::Image& img, juce::Rectangle<float> area);

/** THE HAND'S RIM: a thin line along `outline`, glowing a little either side of it — laid OVER a
    control, so that no render can cover it. */
void drawHandRim (juce::Graphics&, const juce::Path& outline, juce::Colour);

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
    std::function<void()>      onMenu;             // a right click, when set — and then it does not turn the knob

    /** A KNOB THAT CLICKS: when set, the place the knob lands for a place of the hand, both 0..1. The
        hand turns it from one such place to the next, and the pointer stands on one whatever value it
        was given — the value itself is left as it came. */
    std::function<float (float)> detent;

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.5f;

    /** The detents may have moved under a knob that stands still: draws it again if they did. */
    void settle();

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
    float hand = 0.5f;          // where the hand has it: between two detents the knob has not moved yet
    float painted = -1.0f;      // where the pointer was last drawn
    int   dragStartY = 0;
    bool  turning = false;

    float shown() const;        // where the pointer stands
    void  turn (float place);   // the hand's doing
    void  land (float v, bool notify);
};

//==============================================================================
/** A latching push button: press it and it goes down and stays down, lit, press again and it comes
    back up dark. It sinks under the finger while held — the travel is the click — and settles at its
    latched depth on release. Drawn to fill whatever square it is given.

    With `latching` off it is a plain key: the press is `onPress` and it comes straight back up; whether
    it is lit is then its owner's to say, through setOn. */
class PushButton final : public juce::Component, private juce::Timer
{
public:
    explicit PushButton (juce::String name);

    const juce::String name;

    /** How much of a key that does not answer the hand is left to see. */
    static constexpr float dimmed = 0.3f;

    bool latching = true;

    /** A FACE'S OWN KEY: when set, the key is drawn by this instead of the kit's chrome bezel — given
        its whole box, whether it is down (held, or latched on), and how lit it is, 0..1. A face whose
        keys are renders draws them here. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, bool down, float lit)> drawKey;

    std::function<void (bool)> onChange;
    std::function<void()>      onPress;          // a plain key's press
    std::function<void()>      onMenu;           // a right click, when set
    std::function<void (bool)> onHover;          // the editor shows the name + state while hovered

    bool isOn() const noexcept { return on; }
    void setOn (bool, bool notify);
    juce::String stateText() const { return on ? "ON" : "OFF"; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); if (onHover) onHover (false); }
    void enablementChanged() override;

private:
    bool on = false, held = false;
    Glide depth, light;                          // 0 up .. 1 bottomed; 0 dark .. 1 lit
    void aim();
    void timerCallback() override;
};

//==============================================================================
/** The family's mark the way an emblem plate wears it: a mask of the mark's light parts, `box`'s size,
    at `scale` physical pixels to the unit. Made once for the scale and the size it was last asked for. */
struct MarkCut
{
    juce::Image cut;
    static const MarkCut& at (float scale, juce::Rectangle<float> box);
};

/** THE MARK, ENGRAVED in `box`, at `scale` physical pixels to the unit. Its light parts are cut into the
    plate: the cut's upper wall falls into shadow, its lower lip catches the light, and its floor is
    bare metal a shade brighter than the plate — so the mark reads as worked into the hull, not printed
    on it. */
void engraveMark (juce::Graphics&, juce::Rectangle<float> box, float scale);

/** THE MARK ANSWERS THE HAND. Engraved and dark until the pointer is over the plate; then the cut
    turns a dark violet — its colour changes, nothing glows — and a click on it goes home, to the
    maker's site. It paints only that light: the plate and the engraving belong to the face's picture.
    `plate` is the plate's face, the hand's target; `mark` the box the mark is cut in — both in the
    stage's space. */
class Emblem final : public juce::Component, private juce::Timer
{
public:
    Emblem (std::vector<juce::Point<float>> plate, juce::Rectangle<float> mark);

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override { aim (1.0f); }
    void mouseExit (const juce::MouseEvent&) override  { aim (0.0f); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    const juce::Path plate;
    const juce::Rectangle<float> mark;
    Glide light;
    void aim (float);
    void timerCallback() override;
};

//==============================================================================
/** A WORD ON THE PANEL THAT ANSWERS THE HAND. A name and, after it and brighter, what it is set to. It
    lifts under the pointer and a click is `onClick`. With `onDrag` set, dragging the word up or down
    turns what it names — a control with no knob of its own — and a press that did not move still
    clicks; a double click is `onDoubleClick`, which such a control answers the way a knob does, by
    going back to where it starts. `dot` hangs the family's orange dot after it: something here wants
    a look. */
class Legend final : public juce::Component
{
public:
    explicit Legend (float px);

    void set (juce::String name, juce::String value = {});
    void setDot (bool);

    /** How wide its words are as it would draw them now — for laying several out in a row. */
    float idealWidth() const;

    /** Lights it for an instant, brighter than the pointer does — the answer to a tap. */
    void flash();

    juce::Colour nameColour { 0xffc9b8f5 }, valueColour { 0xffece9f6 };
    juce::Colour edgeColour;                     // under its words, where the ink has an edge (Ink)
    juce::Colour shadeColour;                    // over them, where it has a shade
    float weight = 0.0f;                         // its strokes this many px heavier than the font's
    bool upper = true;                           // false keeps the text's own case
    bool fromLeft = false;                       // true starts the words at the left edge instead of centring
                                                 // them: a number that changes width then moves nothing else

    std::function<void()>       onClick, onDoubleClick;
    std::function<void()>       onDragStart, onDragEnd;
    std::function<void (float)> onDrag;          // pixels since the last call, up is positive
    std::function<void (bool)>  onHover;         // the editor says what a click on it does

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); if (onHover) onHover (false); }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    const float px;
    juce::String name, value;
    bool dot = false, dragging = false;
    int  lastY = 0;
    double flashUntil = 0.0;                     // on the millisecond counter

    struct Row { juce::String name, value; juce::Font nameFont, valueFont; float nameWidth, valueWidth, gap, width; };
    Row row() const;
};

//==============================================================================
/** A horizontal fader in one of the lower plates: a glowing line up to a metal cap — `cap`, a render
    whose long side lies across the line. With `steps` set it is a selector instead — the cap clicks
    between that many marked positions. */
class PlateSlider final : public juce::Component
{
public:
    PlateSlider (const juce::Image& cap, juce::Colour lineFrom, juce::Colour lineTo, int steps = 0);

    /** How the cap's render lies: turned by `capAngle` and its opaque width drawn `capLength` long. By
        default it stands across the line, as long as the slider is tall less a little. */
    float capAngle = juce::MathConstants<float>::halfPi;
    float capLength = 0.0f;

    /** The hand's rim round the cap while the hand is over the slider or on it — none unless set. */
    juce::Colour handLight;

    std::function<void()>       onGestureStart, onGestureEnd;
    std::function<void (float)> onChange;
    std::function<void (int)>   onStep;          // stepped only: the position it clicked into
    std::function<void (bool)>  onHover;
    std::function<void()>       onMenu;          // a right click, when set — and then it does not move the cap

    int  getStep() const noexcept;
    void setStep (int, bool notify);
    void setSteps (int count);      // a selector whose list has grown or shrunk; the cap keeps its place in it

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); if (onHover && ! isMouseButtonDown()) onHover (false); }

private:
    const juce::Image& cap;
    juce::Colour from, to;
    int steps;
    float value = 0.0f, dragStartValue = 0.0f;
    bool  sliding = false;                       // false through a right click that opened the menu
    juce::Range<float> travel() const;
    void moveTo (float v);
};

//==============================================================================
/** A vertical fader in a side column's wide slot, its cap the same render a PlateSlider's is. */
class ColumnFader final : public juce::Component
{
public:
    explicit ColumnFader (const juce::Image& cap);

    /** The cap's render, its opaque width drawn this long; by default as wide as the fader less a little. */
    float capLength = 0.0f;

    /** A short fader in a smaller slot: its ends, its line and its scale drawn this much of a column's. */
    float scale = 1.0f;

    /** The hand's rim round the cap while the hand is over the fader or on it — none unless set. */
    juce::Colour handLight;

    /** The scale down its right edge: this many steps, and the one that is drawn longer — unity, by
        default; none, below zero. */
    int marks = 10, longMark = 5;

    std::function<void()>       onGestureStart, onGestureEnd;
    std::function<void (float)> onChange;
    std::function<void()>       onMenu;          // a right click, when set — and then it does not move the cap

    void  setValue (float v, bool notify);
    float getValue() const noexcept { return value; }
    float defaultValue = 0.5f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const juce::Image& cap;
    float value = 0.5f, dragStartValue = 0.0f;
    bool  sliding = false;                       // false through a right click that opened the menu
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

//==============================================================================
/** A LAMP THE RENDER PAINTED, LIT FROM HERE: the picture holds it out (Art), and this lays it back over
    as it was painted, at the strength it burns. `level` says how hard that is now, 0..1, read at 30 Hz;
    the lamp takes it at once and lets go slowly — it holds what it showed for `holdMs`, then fades over
    `fadeMs` — so that something one block long is still seen. */
class Lamp final : public juce::Component, private juce::Timer
{
public:
    Lamp (const juce::Image& lit, std::function<float()> level, double holdMs, double fadeMs);
    void paint (juce::Graphics&) override;

private:
    const juce::Image& lit;
    std::function<float()> level;
    const double holdMs, fadeMs;
    float held = 0.0f, shown = 0.0f;
    double heldAt = 0.0;
    void timerCallback() override;
};

//==============================================================================
/** The tuner's screen: the note big on the left, a cents ruler on the right with the needle on it —
    green when the string is home, warming to orange by 25 cents out. Reads the processor's ear on its
    own 30 Hz clock; the ear only listens while a face is open and the tuner is in the chain.

    Its state it writes in its corner, always — ON; OFF: it is not listening, and shows no note; MUTE: it
    listens, and the amp is silent while it does — since the screen is the switch: a click throws it.
    Over the note it writes the pitch its A is tuned to, when that is not 440: a needle that reads a
    tuned guitar flat wants a reason as well. */
class TunerFace final : public juce::Component, private juce::Timer
{
public:
    explicit TunerFace (const core::TunerEar&);
    void setState (bool listening, bool muting);
    void paint (juce::Graphics&) override;

    /** A STRIP: the tuner in one low line — its state, the note, and the ruler with its needle, no
        numbers under it — for a face whose glass has other work to do. */
    void setStrip (bool);

    /** A GAUGE: no glass of its own, but a dial the face's picture paints — a needle drawn over it,
        turning about `pivot` under the arc of ticks, `reach` long, `sweep` radians either side of straight
        up at fifty cents out, where the arc's end ticks are; the note in the little `window` under the
        pivot, the state between the two, and the pitch its A is tuned to, when that is not 440, over
        the pivot. In the stage's space; none while `reach` is nothing — and a dial it has is what it
        is, strip or not. */
    struct Dial
    {
        juce::Point<float> pivot;
        float reach = 0.0f, sweep = 0.0f;
        juce::Rectangle<float> window;
    };
    void setDial (const Dial&);

private:
    const core::TunerEar& ear;
    bool listening = true, muting = false, strip = false;
    Dial dial;
    void paintStrip (juce::Graphics&);
    void paintDial (juce::Graphics&);
    void timerCallback() override { repaint(); }
};

//==============================================================================
/** WHAT THE WINDOWS INSIDE A FACE ARE WRITTEN WITH — the MIDI window, the cabinets' — so that they
    read as one hand's: the type, its three weights of ink, and a word in a frame that does one thing
    when clicked. All in the editor's own pixels: a Sheet's content is not on the scaled stage. */
namespace panel
{
inline constexpr float textPx = 13.0f, tracking = 0.08f;

inline const juce::Colour ink   { 0xffece9f6 };
inline const juce::Colour dim   = juce::Colour (0xffc9b8f5).withAlpha (0.85f);
inline const juce::Colour faint = juce::Colour (0xffc9b8f5).withAlpha (0.45f);

juce::Font textFont (float px);

/** A line of the panel's type, in capitals. */
void write (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, float px, juce::Colour,
            juce::Justification = juce::Justification::centredLeft);

/** A word in a frame that does one thing when clicked, and says for a moment that it has. Disabled, it
    stands faint and deaf. Lit, it is a switch that is thrown: frame and word in the family's orange. */
class WordKey final : public juce::Component,
                      private juce::Timer
{
public:
    explicit WordKey (juce::String word);
    std::function<void()> onClick;
    void flash (const juce::String& said);
    void setLit (bool);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void enablementChanged() override;

private:
    const juce::String word;
    juce::String said;
    bool lit = false;
    void timerCallback() override;
};
} // namespace panel

} // namespace orbitamp::face
