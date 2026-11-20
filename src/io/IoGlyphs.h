// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <functional>

/** THE IO CABIN'S PICTOGRAMS AND ITS FEW WORDS (the owner, 20.11): catbay's set (io/cabin/drafts/face/glyphs_io.html,
    io.html), drawn as vectors in Io's paint — one rounded line, round caps and joins, or a solid — and the words in B612
    (a European Latin subset, embedded: resources/io/fonts). Each glyph is drawn in its own units about its centre,
    `scale` stage px to the unit, its line as thick as the mockup draws it there. Dumb: a kind, a place and a colour in,
    paint out — so the set can move to a shared library as a file.

    THE NIGHT (paintLabels): by day the paint is #d6dad2, the words with a dark shadow under them; on the dark plate the
    labels light from within, warm white (#f6f1e2) with a soft halo round them (a 1.3 px blur) — the mockup's glow. */
namespace orbitamp::io::glyphs
{

enum class Kind
{
    drive,                      // a filled rising wedge
    presence,                   // a four-point star
    blend,                      // a circle, its right half filled
    bass, middle, treble,       // sines of 1, 2 and 4 cycles
    octave,                     // two stacked sines, the lower one twice as long
    chorus,                     // two sines, one offset
    reverb,                     // a damped wave
    out,                        // a speaker with three arcs
    comp,                       // two arrows pointing in at a bar
    input,                      // an arrow going into a box
    slow, fast,                 // one play triangle; two
    undo, redo                  // semicircle arrows, REDO the mirror of UNDO
};

/** The paint by day, the lit paint by night, and the day's shadow under the words. */
inline const juce::Colour dayInk   { 0xffd6dad2 };
inline const juce::Colour nightInk { 0xfff6f1e2 };
inline const juce::Colour shadow   { 0xb3000000 };      // rgba(0,0,0,0.7)

/** The paint at `dark` (0 the day, 1 the dark plate). */
juce::Colour inkAt (float dark);

/** One pictogram, centred on `at`. */
void draw (juce::Graphics&, Kind, juce::Point<float> at, float scale, juce::Colour);

/** The box round it, as drawn there — for the hand's spot over it. */
juce::Rectangle<float> boxOf (Kind, juce::Point<float> at, float scale);

/** Where a word is anchored: its middle, its start or its end at x. */
enum class Anchor { middle, start, end };

/** A WORD in B612 — Bold where said — `em` the font's size in stage px (the mockup's SVG font-size), its baseline at
    `baseline`, a hair of tracking (0.02 em) as the mockup sets it. */
void word (juce::Graphics&, const juce::String&, float x, float baseline, float em, juce::Colour, Anchor = Anchor::middle,
           bool bold = false);

/** Its width as set. */
float wordWidth (const juce::String&, float em, bool bold = false);

/** B612 itself, made once each. */
juce::Typeface::Ptr b612();
juce::Typeface::Ptr b612Bold();

/** THE LABELS AS IO PAINTS THEM: `labels` draws them all in the colour it is handed — `shadowPass` says the pass is the
    day's shadow, which falls under the words alone (the mockup's). By day the shadow, then the paint; on the dark plate
    (`dark` up to 1) the shadow fades, a halo of the lit paint blurred 1.3 stage px round every label comes up under them
    in `area`, and the paint turns warm white. */
void paintLabels (juce::Graphics&, juce::Rectangle<float> area, float dark,
                  const std::function<void (juce::Graphics&, juce::Colour, bool shadowPass)>& labels);

/** A BUTTON'S OR A KEY'S CAP in its socket `hole`, the mockup's: dark, a grey edge, a faint light along its top —
    pressed, a shade darker; lit (the register playing), its edge in `lit` where it is not transparent. */
void drawCap (juce::Graphics&, juce::Rectangle<float> hole, bool held = false, juce::Colour lit = {});

} // namespace orbitamp::io::glyphs
