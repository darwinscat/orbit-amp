// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IoGlyphs.h"

#include <IoData.h>

#include <cmath>
#include <vector>

namespace orbitamp::io::glyphs
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

    /** `cycles` of a sine across 2 * `half` units, `amp` high — the mockup's `sine`. */
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
        struct Line { juce::Path path; float width; };
        std::vector<Line> lines;
        std::vector<juce::Path> solids;
        void line (juce::Path p, float width = W) { lines.push_back ({ std::move (p), width }); }
        void solid (juce::Path p) { solids.push_back (std::move (p)); }
    };

    /** The play triangle, its tip to the right, about (dx, 0) — the mockup's `tri`. */
    juce::Path triangle (float dx)
    {
        return polygon ({ { dx - 6.3f, -7.7f }, { dx + 7.0f, 0.0f }, { dx - 6.3f, 7.7f } });
    }

    /** E530'S SEMICIRCLE ARROW, variant A (io.html's `arrow`): an arc of radius 9.6 from -14 to 188 degrees, its line 2.2,
        a head 6.4 long and 4.6 to each side at its end — REDO the mirror, `flip` -1. */
    void turnArrow (Parts& s, float flip)
    {
        const float R = 9.6f, a0 = -14.0f * pi / 180.0f, a1 = 188.0f * pi / 180.0f;
        const auto P = [=] (float t) { return juce::Point<float> (flip * R * std::cos (t), 1.2f - R * std::sin (t)); };
        juce::Path bow;
        for (int i = 0; i <= 40; ++i)
        {
            const auto p = P (a0 + (a1 - a0) * (float) i / 40.0f);
            if (i == 0)
                bow.startNewSubPath (p);
            else
                bow.lineTo (p);
        }
        s.line (bow, 2.2f);
        const auto E = P (a1);
        const juce::Point<float> d { -std::sin (a1) * flip, -std::cos (a1) };
        const juce::Point<float> n { d.y, -d.x };
        const auto q = [&] (float u, float v) { return E + d * u + n * v; };
        s.solid (polygon ({ q (6.4f, 0.0f), q (-0.6f, 4.6f), q (-0.6f, -4.6f) }));
    }

    Parts partsOf (Kind kind)
    {
        Parts s;
        switch (kind)
        {
            case Kind::drive:
                s.solid (polygon ({ { -13.0f, 6.5f }, { 13.0f, 6.5f }, { 13.0f, -7.0f } }));
                break;
            case Kind::presence:
            {
                juce::Path star;
                star.startNewSubPath (0.0f, -8.0f);
                star.quadraticTo (1.4f, -1.4f, 8.0f, 0.0f);
                star.quadraticTo (1.4f, 1.4f, 0.0f, 8.0f);
                star.quadraticTo (-1.4f, 1.4f, -8.0f, 0.0f);
                star.quadraticTo (-1.4f, -1.4f, 0.0f, -8.0f);
                star.closeSubPath();
                s.line (star, 2.0f);
                break;
            }
            case Kind::blend:
            {
                juce::Path ring;
                ring.addEllipse (-7.0f, -7.0f, 14.0f, 14.0f);
                s.line (ring, 2.2f);
                juce::Path half;        // from twelve o'clock clockwise to six: the right half
                half.addCentredArc (0.0f, 0.0f, 7.0f, 7.0f, 0.0f, 0.0f, pi, true);
                half.closeSubPath();
                s.solid (half);
                break;
            }
            case Kind::bass:    s.line (sine (1, 5.5f)); break;
            case Kind::middle:  s.line (sine (2, 4.5f)); break;
            case Kind::treble:  s.line (sine (4, 3.2f), 2.2f); break;
            case Kind::octave:
                s.line (along ([] (float x) { return -4.2f - 2.6f * std::sin (2.0f * pi * 2.0f * (x + 13.0f) / 26.0f); }, -13.0f, 13.0f, 60), 2.0f);
                s.line (along ([] (float x) { return 4.2f - 2.6f * std::sin (2.0f * pi * (x + 13.0f) / 26.0f); }, -13.0f, 13.0f, 60), 2.0f);
                break;
            case Kind::chorus:
                s.line (sine (2, 4.2f), 2.0f);
                s.line (along ([] (float x) { return -4.2f * std::sin (2.0f * pi * 2.0f * (x + 14.0f) / 28.0f + 0.9f); }, -14.0f, 14.0f, 70), 2.0f);
                break;
            case Kind::reverb:
                s.line (along ([] (float x) { return -6.5f * std::exp (-(x + 14.0f) / 8.0f) * std::sin (2.0f * pi * 3.5f * (x + 14.0f) / 28.0f); },
                               -14.0f, 14.0f, 120), 2.2f);
                break;
            case Kind::out:
                s.solid (polygon ({ { -13.0f, -3.6f }, { -8.5f, -3.6f }, { -3.0f, -8.5f }, { -3.0f, 8.5f }, { -8.5f, 3.6f }, { -13.0f, 3.6f } }));
                for (int i = 0; i < 3; ++i)
                {
                    const float r = 4.5f + 4.0f * (float) i;
                    juce::Path arc;     // from -0.8 to 0.8 rad off the x axis, through it; JUCE's angles start at twelve o'clock
                    arc.addCentredArc (-1.0f, 0.0f, r, r, 0.0f, 0.5f * pi - 0.8f, 0.5f * pi + 0.8f, true);
                    s.line (arc, 2.2f);
                }
                break;
            case Kind::comp:
                s.line (polyline ({ { -15.0f, 0.0f }, { -4.0f, 0.0f } }));
                s.line (polyline ({ { -8.0f, -4.0f }, { -4.0f, 0.0f }, { -8.0f, 4.0f } }));
                s.line (polyline ({ { 15.0f, 0.0f }, { 4.0f, 0.0f } }));
                s.line (polyline ({ { 8.0f, -4.0f }, { 4.0f, 0.0f }, { 8.0f, 4.0f } }));
                s.line (polyline ({ { 0.0f, -7.0f }, { 0.0f, 7.0f } }), 2.2f);
                break;
            case Kind::input:
            {
                s.line (polyline ({ { -13.0f, 0.0f }, { 3.0f, 0.0f } }));
                s.line (polyline ({ { -2.0f, -5.0f }, { 3.0f, 0.0f }, { -2.0f, 5.0f } }));
                juce::Path box;         // open on its left, its corners rounded two units
                box.startNewSubPath (-3.0f, -8.0f);
                box.lineTo (9.0f, -8.0f);
                box.quadraticTo (11.0f, -8.0f, 11.0f, -6.0f);
                box.lineTo (11.0f, 6.0f);
                box.quadraticTo (11.0f, 8.0f, 9.0f, 8.0f);
                box.lineTo (-3.0f, 8.0f);
                s.line (box, 2.4f);
                break;
            }
            case Kind::slow:    s.solid (triangle (0.0f)); break;
            case Kind::fast:    s.solid (triangle (-5.0f)); s.solid (triangle (8.0f)); break;
            case Kind::undo:    turnArrow (s, 1.0f); break;
            case Kind::redo:    turnArrow (s, -1.0f); break;
        }
        return s;
    }

    juce::Font b612Font (float em, bool bold)
    {
        return juce::Font (juce::FontOptions (bold ? b612Bold() : b612()).withPointHeight (em)).withExtraKerningFactor (0.02f);
    }
}

juce::Colour inkAt (float dark)
{
    return dayInk.interpolatedWith (nightInk, juce::jlimit (0.0f, 1.0f, dark));
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
        g.strokePath (path, juce::PathStrokeType (l.width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::Rectangle<float> boxOf (Kind kind, juce::Point<float> at, float scale)
{
    const auto parts = partsOf (kind);
    juce::Rectangle<float> box;
    for (const auto& solid : parts.solids)
        box = box.isEmpty() ? solid.getBounds() : box.getUnion (solid.getBounds());
    for (const auto& l : parts.lines)
    {
        const auto b = l.path.getBounds().expanded (0.5f * l.width);
        box = box.isEmpty() ? b : box.getUnion (b);
    }
    return box.transformedBy (juce::AffineTransform::scale (scale).translated (at));
}

juce::Typeface::Ptr b612()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (IoData::B612RegularLatin_ttf,
                                                                                    (size_t) IoData::B612RegularLatin_ttfSize);
    return face;
}

juce::Typeface::Ptr b612Bold()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (IoData::B612BoldLatin_ttf,
                                                                                    (size_t) IoData::B612BoldLatin_ttfSize);
    return face;
}

float wordWidth (const juce::String& text, float em, bool bold)
{
    return juce::GlyphArrangement::getStringWidth (b612Font (em, bold), text);
}

void word (juce::Graphics& g, const juce::String& text, float x, float baseline, float em, juce::Colour colour, Anchor anchor, bool bold)
{
    const auto font = b612Font (em, bold);
    const float wide = juce::GlyphArrangement::getStringWidth (font, text);
    const float left = anchor == Anchor::middle ? x - 0.5f * wide : anchor == Anchor::end ? x - wide : x;
    juce::GlyphArrangement line;
    line.addLineOfText (font, text, left, baseline);
    g.setColour (colour);
    line.draw (g);
}

void paintLabels (juce::Graphics& g, juce::Rectangle<float> area, float dark,
                  const std::function<void (juce::Graphics&, juce::Colour, bool)>& labels)
{
    dark = juce::jlimit (0.0f, 1.0f, dark);

    // The day's shadow under the words, fading as the cabin darkens: 0.8 and 0.9 px down and right, the mockup's.
    if (dark < 1.0f)
    {
        const juce::Graphics::ScopedSaveState keep (g);
        g.addTransform (juce::AffineTransform::translation (0.8f, 0.9f));
        labels (g, shadow.withMultipliedAlpha (1.0f - dark), true);
    }

    // The night's halo: the labels drawn into a mask of their own at the screen's own pixels, blurred — two box passes, as
    // near a Gaussian of 1.3 stage px as the pixels allow — and laid under them in the lit paint, as strong as it is dark.
    if (dark > 0.0f && ! area.isEmpty())
    {
        const float k = juce::jmax (0.25f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::roundToInt (area.getWidth() * k), h = juce::roundToInt (area.getHeight() * k);
        if (w > 2 && h > 2)
        {
            juce::Image mask (juce::Image::SingleChannel, w, h, true, juce::SoftwareImageType());
            {
                juce::Graphics m (mask);
                m.addTransform (juce::AffineTransform::translation (-area.getX(), -area.getY()).scaled (k));
                labels (m, juce::Colours::white, false);
            }
            const int radius = juce::jmax (1, juce::roundToInt (1.35f * k));
            mask.getPixelData()->applySingleChannelBoxBlurEffect (radius);
            mask.getPixelData()->applySingleChannelBoxBlurEffect (radius);
            g.setColour (nightInk.withAlpha (dark));
            g.drawImage (mask, area, juce::RectanglePlacement::stretchToFit, true);
        }
    }

    labels (g, inkAt (dark), false);
}

void drawCap (juce::Graphics& g, juce::Rectangle<float> hole, bool held, juce::Colour lit)
{
    const auto cap = hole.reduced (3.0f);
    const juce::Colour fill { 0xff2a2c2e }, edge { 0xff6a6e72 };
    g.setColour (held ? fill.darker (0.35f) : fill);
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (lit.isTransparent() ? edge : lit);
    g.drawRoundedRectangle (cap, 4.0f, lit.isTransparent() ? 1.2f : 1.8f);
    g.setColour (juce::Colours::white.withAlpha (held ? 0.06f : 0.15f));      // the light along its top
    g.drawLine (hole.getX() + 7.0f, hole.getY() + 4.6f, hole.getRight() - 7.0f, hole.getY() + 4.6f, 1.0f);
}

} // namespace orbitamp::io::glyphs
