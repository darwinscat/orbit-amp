// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CaliforniaGlyphs.h"

#include <CaliforniaData.h>

#include <cmath>
#include <functional>

namespace orbitamp::california::glyphs
{

namespace
{
    constexpr float W = 2.6f;       // the line, in the glyph's units
    constexpr float pi = juce::MathConstants<float>::pi;

    /** A polyline of `n` + 1 points, x from `a` to `b`, y = f (x) — the mockup's `pts`. */
    juce::Path along (const std::function<float (float)>& f, float a, float b, int n)
    {
        juce::Path p;
        for (int i = 0; i <= n; ++i)
        {
            const float x = a + (b - a) * (float) i / (float) n;
            if (i == 0)
                p.startNewSubPath (x, f (x));
            else
                p.lineTo (x, f (x));
        }
        return p;
    }

    /** `cycles` of a sine across 28 units, `amp` high — the mockup's `sine`. */
    juce::Path sine (int cycles, float amp, float half = 14.0f)
    {
        return along ([=] (float x) { return -amp * std::sin (2.0f * pi * (float) cycles * (x + half) / (2.0f * half)); },
                      -half, half, 24 * cycles + 24);
    }

    juce::Path polyline (std::initializer_list<juce::Point<float>> points)
    {
        juce::Path p;
        bool first = true;
        for (const auto& pt : points)
        {
            if (first)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
            first = false;
        }
        return p;
    }

    juce::Path polygon (std::initializer_list<juce::Point<float>> points)
    {
        auto p = polyline (points);
        p.closeSubPath();
        return p;
    }

    /** What one pictogram is made of, in its units: lines, each its width, and solids. */
    struct Parts
    {
        struct Line { juce::Path path; float width; juce::Colour colour; bool own = false; };
        std::vector<Line> lines;
        std::vector<juce::Path> solids;
        void line (juce::Path p, float width = W) { lines.push_back ({ std::move (p), width, {}, false }); }
        void lineIn (juce::Path p, float width, juce::Colour c) { lines.push_back ({ std::move (p), width, c, true }); }
        void solid (juce::Path p) { solids.push_back (std::move (p)); }
    };

    void speaker (Parts& s, int arcs)
    {
        s.solid (polygon ({ { -13.0f, -3.6f }, { -8.5f, -3.6f }, { -3.0f, -8.5f }, { -3.0f, 8.5f }, { -8.5f, 3.6f }, { -13.0f, 3.6f } }));
        for (int i = 0; i < arcs; ++i)
        {
            const float r = 4.5f + 4.0f * (float) i;
            juce::Path arc;     // from -0.8 to 0.8 rad off the x axis, through it; JUCE's angles start at twelve o'clock
            arc.addCentredArc (-1.0f, 0.0f, r, r, 0.0f, 0.5f * pi - 0.8f, 0.5f * pi + 0.8f, true);
            s.line (arc, 2.2f);
        }
    }

    void flower (Parts& s, int dots)
    {
        for (int i = 0; i < 5; ++i)
        {
            const float a = -0.5f * pi + (float) i * 2.0f * pi / 5.0f;
            juce::Path petal;
            petal.addEllipse (4.6f * std::cos (a) - 5.0f - 3.6f, 4.6f * std::sin (a) - 3.6f, 7.2f, 7.2f);
            s.line (petal, 2.0f);
        }
        juce::Path heart;
        heart.addEllipse (-5.0f - 2.2f, -2.2f, 4.4f, 4.4f);
        s.solid (heart);
        for (int i = 0; i < dots; ++i)
        {
            juce::Path dot;
            dot.addEllipse (8.0f + 6.0f * (float) i - 2.3f, -2.3f, 4.6f, 4.6f);
            s.solid (dot);
        }
    }

    void turnArrow (Parts& s, float flip)
    {
        const float R = 7.6f, a0 = -14.0f * pi / 180.0f, a1 = 188.0f * pi / 180.0f;
        const auto P = [=] (float t) { return juce::Point<float> (R * std::cos (t) * flip, 1.6f - R * std::sin (t)); };
        juce::Path bow;
        for (int i = 0; i <= 40; ++i)
        {
            const auto p = P (a0 + (a1 - a0) * (float) i / 40.0f);
            if (i == 0)
                bow.startNewSubPath (p);
            else
                bow.lineTo (p);
        }
        s.line (bow);
        const auto E = P (a1);
        const juce::Point<float> d { -std::sin (a1) * flip, -std::cos (a1) };
        const juce::Point<float> n { d.y, -d.x };
        const auto q = [&] (float u, float v) { return E + d * u + n * v; };
        const auto head = polygon ({ q (5.6f, 0.0f), q (-0.6f, 4.4f), q (-0.6f, -4.4f) });
        s.solid (head);
        s.line (head, 1.2f);
    }

    Parts partsOf (Kind kind)
    {
        Parts s;
        switch (kind)
        {
            case Kind::bass:    s.line (sine (1, 5.5f)); break;
            case Kind::middle:  s.line (sine (2, 4.5f)); break;
            case Kind::treble:  s.line (sine (4, 3.2f), 2.2f); break;
            case Kind::gain:    s.solid (polygon ({ { -13.0f, 6.5f }, { 13.0f, 6.5f }, { 13.0f, -7.0f } })); break;
            case Kind::volume:  speaker (s, 1); break;
            case Kind::out:     speaker (s, 3); break;
            case Kind::reverb:
                s.line (along ([] (float x) { return -6.5f * std::exp (-(x + 14.0f) / 8.0f) * std::sin (2.0f * pi * 3.5f * (x + 14.0f) / 28.0f); },
                               -14.0f, 14.0f, 120), 2.2f);
                break;
            case Kind::delay:   flower (s, 1); break;
            case Kind::phaser:  flower (s, 2); break;
            case Kind::comp:
                s.line (polyline ({ { -15.0f, 0.0f }, { -4.0f, 0.0f } }));
                s.line (polyline ({ { -8.0f, -4.0f }, { -4.0f, 0.0f }, { -8.0f, 4.0f } }));
                s.line (polyline ({ { 15.0f, 0.0f }, { 4.0f, 0.0f } }));
                s.line (polyline ({ { 8.0f, -4.0f }, { 4.0f, 0.0f }, { 8.0f, 4.0f } }));
                s.line (polyline ({ { 0.0f, -7.0f }, { 0.0f, 7.0f } }), 2.2f);
                break;
            case Kind::gate:
                s.line (along ([] (float x) { return -5.0f * std::sin (2.0f * pi * 1.5f * (x + 14.0f) / 14.0f); }, -14.0f, 0.0f, 40));
                s.line (polyline ({ { 0.0f, 0.0f }, { 14.0f, 0.0f } }));
                s.line (polyline ({ { 0.0f, -7.5f }, { 0.0f, 7.5f } }), 2.2f);
                break;
            case Kind::limiter:
                s.line (polyline ({ { -15.0f, -6.5f }, { 15.0f, -6.5f } }), 1.6f);
                s.line (along ([] (float x) { return juce::jmax (-3.6f, -6.5f * std::sin (2.0f * pi * 2.0f * (x + 14.0f) / 28.0f)); },
                               -14.0f, 14.0f, 90));
                break;
            case Kind::input:
            {
                juce::Path body;        // the plug's body, its corners rounded a unit
                body.addRoundedRectangle (-15.0f, -5.0f, 10.0f, 10.0f, 1.0f);
                s.solid (body);
                s.line (polyline ({ { -5.0f, 0.0f }, { 7.0f, 0.0f } }), 3.2f);
                s.line (polyline ({ { 7.0f, 0.0f }, { 10.0f, 0.0f } }), 2.2f);
                s.solid (polygon ({ { 10.0f, -2.6f }, { 13.5f, 0.0f }, { 10.0f, 2.6f } }));
                s.lineIn (polyline ({ { 3.0f, -3.0f }, { 3.0f, 3.0f } }), 1.4f, cream);     // the ring on its tip, cream
                break;
            }
            case Kind::fork:
            {
                juce::Path prongs;
                prongs.startNewSubPath (-4.5f, -9.0f);
                prongs.lineTo (-4.5f, 1.0f);
                prongs.quadraticTo (-4.5f, 5.5f, 0.0f, 5.5f);
                prongs.quadraticTo (4.5f, 5.5f, 4.5f, 1.0f);
                prongs.lineTo (4.5f, -9.0f);
                s.line (prongs);
                s.line (polyline ({ { 0.0f, 5.5f }, { 0.0f, 12.0f } }), 3.0f);
                break;
            }
            case Kind::clean:   s.line (sine (2, 4.5f)); break;
            case Kind::dirty:
                s.line (polyline ({ { -14.0f, 0.0f }, { -12.5f, -5.0f }, { -7.5f, -5.0f }, { -6.0f, 0.0f }, { -4.5f, 5.0f }, { 0.5f, 5.0f },
                                    { 2.0f, 0.0f }, { 3.5f, -5.0f }, { 8.5f, -5.0f }, { 10.0f, 0.0f }, { 11.5f, 5.0f }, { 14.0f, 5.0f } }));
                break;
            case Kind::undo:    turnArrow (s, 1.0f); break;
            case Kind::redo:    turnArrow (s, -1.0f); break;
        }
        return s;
    }

    /** Its box in its own units. */
    juce::Rectangle<float> unitBox (Kind kind)
    {
        switch (kind)
        {
            case Kind::gain:                    return { -14.0f, -8.0f, 28.0f, 16.0f };
            case Kind::volume:                  return { -14.0f, -9.0f, 19.0f, 18.0f };
            case Kind::out:                     return { -14.0f, -13.0f, 27.0f, 26.0f };
            case Kind::delay: case Kind::phaser: return { -14.0f, -9.0f, 31.0f, 18.0f };
            case Kind::input:                   return { -16.0f, -6.0f, 31.0f, 12.0f };
            case Kind::fork:                    return { -6.0f, -10.0f, 12.0f, 23.0f };
            case Kind::undo: case Kind::redo:   return { -10.0f, -8.0f, 20.0f, 18.0f };
            case Kind::bass: case Kind::middle: case Kind::treble: case Kind::reverb: case Kind::comp:
            case Kind::gate: case Kind::limiter: case Kind::clean: case Kind::dirty:
                break;
        }
        return { -16.0f, -9.0f, 32.0f, 18.0f };
    }
}

void draw (juce::Graphics& g, Kind kind, juce::Point<float> at, float scale, juce::Colour colour)
{
    const auto toStage = juce::AffineTransform::scale (scale).translated (at);
    const auto parts = partsOf (kind);
    g.setColour (colour);
    for (auto solid : parts.solids)
    {
        solid.applyTransform (toStage);
        g.fillPath (solid);
    }
    for (const auto& l : parts.lines)
    {
        auto path = l.path;
        path.applyTransform (toStage);
        g.setColour (l.own ? l.colour.withMultipliedAlpha (colour.getFloatAlpha()) : colour);
        g.strokePath (path, juce::PathStrokeType (l.width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::Rectangle<float> boxOf (Kind kind, juce::Point<float> at, float scale)
{
    return unitBox (kind).transformedBy (juce::AffineTransform::scale (scale).translated (at));
}

void drawCap (juce::Graphics& g, juce::Rectangle<float> hole, bool held, bool lit)
{
    const auto cap = hole.reduced (3.0f);
    g.setColour (held ? capFill.darker (0.12f) : capFill);
    g.fillRoundedRectangle (cap, 7.0f);
    g.setColour (capEdge);
    g.drawRoundedRectangle (cap, 7.0f, lit ? 2.6f : 1.6f);
    g.setColour (juce::Colour::fromRGBA (120, 60, 20, 46));     // the shade along its foot, rgba(120,60,20,0.18)
    g.fillRoundedRectangle (juce::Rectangle<float> (hole.getX() + 6.0f, hole.getBottom() - 7.0f, hole.getWidth() - 12.0f, 2.0f), 1.0f);
}

juce::Typeface::Ptr shrikhand()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (CaliforniaData::ShrikhandLatin_ttf,
                                                                                    (size_t) CaliforniaData::ShrikhandLatin_ttfSize);
    return face;
}

void drawLetter (juce::Graphics& g, const juce::String& text, float x, float baseline, float size, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (juce::Font (juce::FontOptions (shrikhand()).withPointHeight (size)));
    g.drawSingleLineText (text, juce::roundToInt (x), juce::roundToInt (baseline), juce::Justification::horizontallyCentred);
}

} // namespace orbitamp::california::glyphs
