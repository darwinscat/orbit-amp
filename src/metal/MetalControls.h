// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"

#include <array>
#include <functional>
#include <vector>

/** THE E530 CABIN'S OWN CONTROLS AND ITS LETTERING: what its dashboard has that no other face's console does. Dumb
    views — each is told what it shows and says what the hand did; none of them reaches the processor. */
namespace orbitamp::metal
{

/** DC ROCKER, the cabin's lettering: New Rocker (SIL OFL 1.1) cut to a European Latin subset and renamed, as its licence
    asks of a modified copy (resources/fonts, tools/fonts/dc-rocker.py) — at `height`, the font's height as the family
    counts a label's size (face::labelFont): 13 px at the reading width is stage::px13. */
juce::Font rockerFont (float height);

/** A font height for a size in the mockup's em (its SVG's font-size): New Rocker is 1.229 em tall. */
inline constexpr float rockerHeightOfEm (float em) noexcept { return em * 1.229f; }

/** WORDS IN SILVER, as the cat at rest (the approved mockup, e530.html): the vertical gradient #f2f5f7, #c4cad0, #9aa2a9,
    #d8dde1 over each word's own height, a thin black outline under it. Centred on `centre`, set condensed into `room`
    about it, as tall, where wider; `alpha` dims the whole. */
void silverWords (juce::Graphics&, const juce::String& words, juce::Point<float> centre, float height,
                  float room = 1.0e6f, float alpha = 1.0f);

/** The mockup's silver, as a fill over `top`..`bottom`. */
juce::ColourGradient silverFill (float top, float bottom, float alpha = 1.0f);

/** A KEY'S OR A BUTTON'S CAP, as the mockup draws it: a dark cap, a chrome rim, its corners 5 round — the rim the
    family's orange while `lit`, sunk a hair while `down`, dim while not `live`. */
void drawCap (juce::Graphics&, juce::Rectangle<float> cap, bool down, bool lit, bool live = true);

/** UNDO'S AND REDO'S ARROW (keys.html, variant A, the owner's pick of 19.11): an arc of r 9.6 whose centre is 1.2 under
    `centre`, from -14 degrees anticlockwise over the top to 188, a head 6.4 long and 4.6 either side at its left end along
    its tangent; REDO its mirror. Silver, a 2.2 round-capped stroke over a 3.6 black one. */
void drawHistoryArrow (juce::Graphics&, juce::Point<float> centre, bool redo, float alpha = 1.0f);

//==============================================================================
/** THE LEVER: five stops in its slot, top to bottom PARK, CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO, LEAD GAIN HI — a cap
    across the slot, its orange line where it stands — and right of the stops their glyphs, no words: a bold P, one
    hollow circle, two, one filled circle, two (the owner, 19.11). The family's lever (face::DetentLever): held, it follows
    the hand and sits in a place within reach; let go, it springs into the nearest; a click on a glyph sends it there.
    The hand over it says which stop it is over (onHover), for the face to name. */
class GlyphLever final : public face::DetentLever
{
public:
    /** Its stops' heights, the slot's middle and the glyphs' left edge, in the stage's space, which the top left of its
        box there, `stageOrigin`, makes its own. */
    GlyphLever (std::vector<float> stops, float slotX, float glyphLeft, juce::Point<float> stageOrigin);

    juce::Colour handLight;
    static constexpr float capWidth = 40.0f, capHeight = 20.0f;

    /** The stop the hand is over, -1 once it leaves. */
    std::function<void (int)> onHover;

    /** Where a stop's glyph stands, in its own space. */
    juce::Point<float> glyphAt (int stop) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseExit (const juce::MouseEvent&) override;

private:
    std::vector<float> heights;     // in its own space
    float slotX, glyphX;
    int hovered = -1;
    float yOf (float travel) const noexcept override;
    float travelAt (float y) const noexcept override;
    juce::Rectangle<float> capAt (float y) const noexcept;
    int stopNearest (float y) const noexcept;
};

//==============================================================================
/** GATE'S AND LIMITER'S BUTTONS: a cap in the plate's well, its word on it in silver; a click asks for what it is set by
    (onClick), and the hand over it lights its rim. */
class WordButton final : public juce::Component
{
public:
    explicit WordButton (juce::String word);

    std::function<void()> onClick;
    juce::Colour handLight;
    float inset = 3.0f;                 // the cap inside its well

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const juce::String word;
};

//==============================================================================
/** THE CAT IN THE MAKER'S PLATE, drawn by the app as catbay's metal/cabin-logo.js draws it on the page. At rest, brushed
    silver: the family's mark inside 86 % of the plate, its own grey level shading a vertical steel gradient (#e3e8ec,
    #b8bec4, #8f979e) by 0.18 + 0.82 L^1.2, brushed (a streak a pixel row, a grain a pixel), a highlight on the top edges of
    its light shapes — made once at the screen's own pixels and kept. Under the mouse — anywhere over the seat's octagon —
    crimson, the same shading on a crimson gradient, and tongues of flame rise out of the seat round it, flickering; it
    eases in over 0.16 s and back over 0.6 s, and only then does it draw again. Takes no click: the hand is watched on
    the stage it stands on (watch), and the click is the emblem's. */
class CatLogo final : public juce::Component, private juce::Timer
{
public:
    /** `box` the flames' box on the stage, `logo` the mark's, `rim` and `inside` the seat's octagon. */
    CatLogo (juce::Rectangle<float> box, juce::Rectangle<float> logo, std::vector<juce::Point<float>> rim,
             std::vector<juce::Point<float>> inside);
    ~CatLogo() override;

    /** The stage whose mouse it follows. */
    void watch (juce::Component& stage);

    /** Hovered for good, the flames' clock held at `t` seconds where t >= 0 — for a still. */
    void force (bool hovered, double t = -1.0);

    float hover() const noexcept { return h; }

    void paint (juce::Graphics&) override;

private:
    const juce::Rectangle<float> box, logo;
    juce::Path rimPath, insidePath;
    std::vector<juce::Point<float>> rimPoints;
    juce::Component* watched = nullptr;
    bool over = false, forced = false;
    double frozenAt = -1.0, last = 0.0;
    float h = 0.0f;

    struct Brushed
    {
        juce::Image silver, crimson, mix;
        float scale = 0.0f;
    };
    mutable Brushed made;
    void make (float scale) const;
    juce::Image brushed (float scale, int palette) const;
    void flames (juce::Graphics&, double t, float amount) const;

    void aim (bool hovered);
    void timerCallback() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseDrag (const juce::MouseEvent& e) override { mouseMove (e); }
    void mouseExit (const juce::MouseEvent&) override;
};

//==============================================================================
/** A NAME ON THE TUNER'S SCREEN FOR A MOMENT: the lever's stop as the lever reaches it, in silver, for a second and a
    half, then gone in a fifth. `onShow` says when it comes and goes, for the tuner to step aside. */
class TunerNote final : public juce::Component, private juce::Timer
{
public:
    TunerNote();

    std::function<void (bool)> onShow;
    float height = 0.0f;

    void show (const juce::String& words);
    const juce::String& says() const noexcept { return words; }

    void paint (juce::Graphics&) override;

private:
    juce::String words;
    double shownAt = 0.0;
    void timerCallback() override;
};

} // namespace orbitamp::metal
