// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

/** THE VAN'S PICTOGRAMS (the owner, 20.11: no words on the panel, only our own signs): catbay's set
    (van-cabin/drafts/glyphs/van.html), drawn as vectors — one thick rounded line, round caps and joins, dark brown on
    the cream panels. Each is drawn in its own units about its centre, `scale` stage px to the unit, the line as thick as
    the mockup draws it there (2.6 units, so 2.6 px at a scale of 1). Dumb: a kind, a place and a colour in, paint out —
    so the set can move to a shared library as a file. */
namespace orbitamp::california::glyphs
{

enum class Kind
{
    bass, middle, treble,       // sines of 1, 2 and 4 cycles
    gain,                       // a filled rising wedge
    volume, out,                // a speaker with one arc; with three
    reverb,                     // a damped wave
    delay, phaser,              // a five-petal flower with one dot; with two
    comp,                       // two arrows pointing in at a bar
    gate,                       // a wave stopping at a bar, then flat
    limiter,                    // a wave flattened under a ceiling
    input,                      // a jack plug
    fork,                       // a tuning fork
    clean, dirty,               // a smooth wave; a clipped one
    undo, redo                  // semicircle arrows
};

/** The ink they are drawn in on the cream panels, the cream the fork is drawn in on the dark glass, and the keys' caps. */
inline const juce::Colour ink      { 0xff3b2314 };
inline const juce::Colour cream    { 0xfff3ead8 };
inline const juce::Colour capFill  { 0xfff6eedc };
inline const juce::Colour capEdge  { 0xffc9662c };

/** One pictogram, centred on `at`. */
void draw (juce::Graphics&, Kind, juce::Point<float> at, float scale, juce::Colour colour = ink);

/** The box round it, as drawn there — for the hand's spot over it. */
juce::Rectangle<float> boxOf (Kind, juce::Point<float> at, float scale);

/** A KEY'S OR A BUTTON'S CAP in its socket `hole`: cream, its edge orange, a soft shade along its foot (the mockup's) —
    pressed, a shade darker; lit, its edge heavier. */
void drawCap (juce::Graphics&, juce::Rectangle<float> hole, bool held = false, bool lit = false);

/** A LETTER in Shrikhand (the face's own subset, CaliforniaData), centred on `x`, its baseline at `baseline`, `size` the
    font's em in stage px — as the mockup's SVG text. */
void drawLetter (juce::Graphics&, const juce::String&, float x, float baseline, float size, juce::Colour colour = ink);

/** Shrikhand itself, made once. */
juce::Typeface::Ptr shrikhand();

} // namespace orbitamp::california::glyphs
