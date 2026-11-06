// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../core/PitchTracker.h"
#include "../core/TunerPoly.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace orbitamp::polyview
{

/** HOW THE POLYPHONIC TUNER IS DRAWN — the one painter every face draws its lanes with (core::PolyLanes): the
    OrbitAmp strip and the rovers' strip as one row of lanes, the cockpit's screen as a row of big ones, the
    Titan dial as a little grid above its pivot. Dumb: the lanes and the ink in, pixels out.

    A LANE is a string, lowest on the left: its target's name, then its scale — ±50 cents, sharp to the right
    as the needle swings — with the needle on it, in the needle's own ink: green when the string is home,
    warming to orange by `warmCents` out. Nothing heard of the string: the name quiet and a dash. FAR: the
    word, toward the side it is off — or, where the scale is narrower than the word, across the whole lane.
    MASKED: a plectrum's outline, orange — this string stands on another's partial (a guitar's B on the low E's
    3rd, a cent or three apart: one bin of the span), so damp the others and pluck it alone; plucked over a chord
    still ringing, it stays covered. Where a face has room for words it says so ("DAMP · PLUCK ALONE": the
    cockpit's screen); the strips and the dial show the plectrum only. Orange is only ever a line or a solid. */
struct Ink
{
    juce::Font   font;              // names and words: 13 px at the face's reading size
    juce::Colour name;              // a string's name while it has a reading
    juce::Colour quiet;             // ...while nothing of it is heard, and the dash
    juce::Colour home;              // in tune: the name and the needle
    juce::Colour warm;              // the needle's far end; FAR and the plectrum
    juce::Colour scale;             // the scale: its line and its ends at ±50
    juce::Colour zero;              // its centre
    float warmCents = 25.0f;
    float needle    = 2.0f;         // the needle's width, px
};

inline juce::String nameOf (const core::PolyLane& l) { return core::PitchTracker::noteName (l.midi); }

inline float widthOf (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }

inline juce::Colour needleOf (const Ink& ink, const core::PolyLane& l)
{
    return l.green ? ink.home
                   : ink.home.interpolatedWith (ink.warm, juce::jlimit (0.0f, 1.0f, std::abs (l.cents) / ink.warmCents));
}

inline juce::Colour nameInk (const Ink& ink, const core::PolyLane& l)
{
    return l.green ? ink.home : l.mark == core::PolyLane::Mark::none ? ink.quiet : ink.name;
}

/** The widest of the lanes' names, and the air after it: the slot every lane of a row gives its name. */
inline float nameSlot (const core::PolyLanes& lanes, const juce::Font& f)
{
    float w = 0.0f;
    for (int s = 0; s < lanes.count; ++s)
        w = juce::jmax (w, widthOf (f, nameOf (lanes.lane[(size_t) s])));
    return std::ceil (w + f.getHeight() * 0.25f);
}

/** THE PLUCK-ALONE MARK: a plectrum's outline, `h` tall, point down. */
inline void paintPick (juce::Graphics& g, juce::Point<float> c, float h, juce::Colour colour)
{
    const float w = h * 0.86f, top = c.y - h * 0.38f;
    juce::Path p;
    p.startNewSubPath (c.x - w * 0.5f, top);
    p.cubicTo (c.x - w * 0.3f, top - h * 0.16f, c.x + w * 0.3f, top - h * 0.16f, c.x + w * 0.5f, top);
    p.cubicTo (c.x + w * 0.52f, c.y + h * 0.05f, c.x + w * 0.16f, c.y + h * 0.42f, c.x, c.y + h * 0.5f);
    p.cubicTo (c.x - w * 0.16f, c.y + h * 0.42f, c.x - w * 0.52f, c.y + h * 0.05f, c.x - w * 0.5f, top);
    p.closeSubPath();
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.2f, h * 0.09f)));
}

/** One lane's scale and what stands on it, in `t`. */
inline void paintScale (juce::Graphics& g, juce::Rectangle<float> t, const core::PolyLane& l, const Ink& ink)
{
    using Mark = core::PolyLane::Mark;
    const float cy = t.getCentreY(), fh = ink.font.getHeight();

    switch (l.mark)
    {
        case Mark::none:
        {
            const float w = juce::jmin (t.getWidth() * 0.45f, fh * 0.85f), th = juce::jmax (2.0f, fh * 0.14f);
            g.setColour (ink.quiet);
            g.fillRoundedRectangle (juce::Rectangle<float> (w, th).withCentre (t.getCentre()), th * 0.5f);
            return;
        }
        case Mark::masked:
            paintPick (g, t.getCentre(), juce::jmin (t.getHeight(), fh) * 0.85f, ink.warm);
            return;
        case Mark::far:
            g.setColour (ink.warm);
            g.setFont (ink.font);
            g.drawText ("FAR", t, t.getWidth() > widthOf (ink.font, "FAR") * 1.6f
                                      ? (l.sharp ? juce::Justification::centredRight : juce::Justification::centredLeft)
                                      : juce::Justification::centred, false);
            return;
        case Mark::near:
            break;
    }

    // The scale, quiet so the needle reads: a hairline from -50 to +50 with its ends turned up, the centre tall.
    const auto xOf = [&t] (float c) { return t.getX() + t.getWidth() * (c + core::PolyLane::showCents) / (2.0f * core::PolyLane::showCents); };
    g.setColour (ink.scale);
    g.fillRect (juce::Rectangle<float> (t.getX(), cy - 0.5f, t.getWidth(), 1.0f));
    g.fillRect (juce::Rectangle<float> (t.getX(), cy - t.getHeight() * 0.25f, 1.0f, t.getHeight() * 0.5f));
    g.fillRect (juce::Rectangle<float> (t.getRight() - 1.0f, cy - t.getHeight() * 0.25f, 1.0f, t.getHeight() * 0.5f));
    g.setColour (ink.zero);
    g.fillRect (juce::Rectangle<float> (1.5f, t.getHeight() * 0.9f).withCentre ({ xOf (0.0f), cy }));

    const float x = juce::jlimit (t.getX() + ink.needle * 0.5f, t.getRight() - ink.needle * 0.5f, xOf (l.cents));
    g.setColour (needleOf (ink, l));
    g.fillRoundedRectangle (juce::Rectangle<float> (ink.needle, t.getHeight()).withCentre ({ x, cy }), ink.needle * 0.5f);
}

/** One lane in a line: the name in its slot `nameW`, the scale in the rest. Where the scale is too narrow
    for FAR, the NAME stays — which string is far is the point — drawn warm, and the scale's slot says which
    way: "+" sharp, "-" flat. (FAR across the whole lane once hid the name: a dial read "E FAR D".) */
inline void paintLane (juce::Graphics& g, juce::Rectangle<float> a, const core::PolyLane& l, const Ink& ink, float nameW)
{
    g.setFont (ink.font);
    if (l.mark == core::PolyLane::Mark::far && a.getWidth() - nameW < widthOf (ink.font, "FAR"))
    {
        g.setColour (ink.warm);
        g.drawText (nameOf (l), a.removeFromLeft (nameW), juce::Justification::centredLeft, false);
        g.drawText (l.sharp ? "+" : "-", a, juce::Justification::centred, false);
        return;
    }
    g.setColour (nameInk (ink, l));
    g.drawText (nameOf (l), a.removeFromLeft (nameW), juce::Justification::centredLeft, false);
    paintScale (g, a, l, ink);
}

/** The lanes side by side in `area`, `gap` apart: the strips. */
inline void paintRow (juce::Graphics& g, juce::Rectangle<float> area, const core::PolyLanes& lanes, const Ink& ink, float gap)
{
    if (lanes.count <= 0)
        return;
    const float w = (area.getWidth() - gap * (float) (lanes.count - 1)) / (float) lanes.count;
    const float nameW = nameSlot (lanes, ink.font);
    for (int s = 0; s < lanes.count; ++s)
        paintLane (g, area.withX (area.getX() + (float) s * (w + gap)).withWidth (w), lanes.lane[(size_t) s], ink, nameW);
}

/** The lanes in `cols` columns, row by row from the top, `gap` apart either way: the dial. */
inline void paintGrid (juce::Graphics& g, juce::Rectangle<float> area, const core::PolyLanes& lanes, const Ink& ink,
                       int cols, float gapX, float gapY)
{
    if (lanes.count <= 0 || cols <= 0)
        return;
    const int rows = (lanes.count + cols - 1) / cols;
    const float w = (area.getWidth() - gapX * (float) (cols - 1)) / (float) cols;
    const float h = (area.getHeight() - gapY * (float) (rows - 1)) / (float) rows;
    const float nameW = nameSlot (lanes, ink.font);
    for (int s = 0; s < lanes.count; ++s)
        paintLane (g, { area.getX() + (float) (s % cols) * (w + gapX), area.getY() + (float) (s / cols) * (h + gapY), w, h },
                   lanes.lane[(size_t) s], ink, nameW);
}

/** The lanes as big columns side by side, `gap` apart: each its name over its scale, the name in `nameFont`.
    The cockpit's screen. */
inline void paintColumns (juce::Graphics& g, juce::Rectangle<float> area, const core::PolyLanes& lanes, const Ink& ink,
                          const juce::Font& nameFont, float gap)
{
    if (lanes.count <= 0)
        return;
    const float w = (area.getWidth() - gap * (float) (lanes.count - 1)) / (float) lanes.count;
    for (int s = 0; s < lanes.count; ++s)
    {
        const auto& l = lanes.lane[(size_t) s];
        auto col = area.withX (area.getX() + (float) s * (w + gap)).withWidth (w);
        g.setColour (nameInk (ink, l));
        g.setFont (nameFont);
        g.drawText (nameOf (l), col.removeFromTop (nameFont.getHeight()), juce::Justification::centred, false);
        col.removeFromTop (nameFont.getHeight() * 0.15f);
        paintScale (g, col, l, ink);
    }
}

/** Whether any lane asks for its string alone — where a face has the room, it says so in words. */
inline bool anyMasked (const core::PolyLanes& lanes)
{
    for (int s = 0; s < lanes.count; ++s)
        if (lanes.lane[(size_t) s].mark == core::PolyLane::Mark::masked)
            return true;
    return false;
}

} // namespace orbitamp::polyview
