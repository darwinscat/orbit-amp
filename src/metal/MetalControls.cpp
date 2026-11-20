// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "MetalControls.h"

#include <RockerFontData.h>
#include <felitronics/appkit/BrandAssets.h>

#include <cmath>

namespace orbitamp::metal
{

namespace
{
    const juce::Colour capTop { 0xff2a2b2f }, capBottom { 0xff0c0c0e };
    const juce::Colour black { 0xff000000 };

    juce::Typeface::Ptr rocker()
    {
        static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (RockerFontData::DCRockerRegular_ttf,
                                                                                          (size_t) RockerFontData::DCRockerRegular_ttfSize);
        return face;
    }

    /** The mockup's outline under a word: 0.7 at its 16-px type, as thick for every size. */
    float outlineOf (float height) noexcept { return 0.7f * (height / rockerHeightOfEm (16.0f)); }

    float smooth (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    /** cabin-logo.js's hash, in doubles as the page computes it. */
    double hash (double n) noexcept
    {
        const double s = std::sin (n * 127.1 + 311.7) * 43758.5453;
        return s - std::floor (s);
    }

    juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, juce::jlimit (0.0f, 1.0f, a)); }
}

juce::Font rockerFont (float height)
{
    return juce::Font (juce::FontOptions (rocker()).withHeight (height));
}

juce::ColourGradient silverFill (float top, float bottom, float alpha)
{
    juce::ColourGradient g (juce::Colour (0xfff2f5f7).withMultipliedAlpha (alpha), 0.0f, top,
                            juce::Colour (0xffd8dde1).withMultipliedAlpha (alpha), 0.0f, bottom, false);
    g.addColour (0.45, juce::Colour (0xffc4cad0).withMultipliedAlpha (alpha));
    g.addColour (0.55, juce::Colour (0xff9aa2a9).withMultipliedAlpha (alpha));
    return g;
}

void silverWords (juce::Graphics& g, const juce::String& words, juce::Point<float> centre, float height, float room, float alpha)
{
    if (words.isEmpty() || alpha <= 0.0f)
        return;
    const auto font = rockerFont (height);
    const float wide = juce::GlyphArrangement::getStringWidth (font, words);
    const float baseline = centre.y + 0.5f * (font.getAscent() - font.getDescent());

    juce::GlyphArrangement ga;
    ga.addLineOfText (font, words, centre.x - 0.5f * wide, baseline);
    juce::Path outline;
    ga.createPath (outline);

    const juce::Graphics::ScopedSaveState keep (g);
    if (wide > room)
        g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, centre.x, centre.y));
    // The outline first and the silver over it, as the mockup's paint-order: half the stroke shows, outside the letters.
    g.setColour (black.withMultipliedAlpha (alpha));
    g.strokePath (outline, juce::PathStrokeType (outlineOf (height), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setGradientFill (silverFill (baseline - font.getAscent(), baseline + font.getDescent(), alpha));
    g.fillPath (outline);
}

void drawCap (juce::Graphics& g, juce::Rectangle<float> cap, bool down, bool lit, bool live)
{
    const float a = live ? 1.0f : 0.45f;
    const auto box = cap.translated (0.0f, down ? 1.0f : 0.0f);
    g.setGradientFill (juce::ColourGradient (capTop.darker (down ? 0.4f : 0.0f), 0.0f, box.getY(),
                                             capBottom, 0.0f, box.getBottom(), false));
    g.fillRoundedRectangle (box, 5.0f);
    if (lit)
        g.setColour (face::orange);
    else
    {
        juce::ColourGradient rim (juce::Colour (0xff9aa0a6).withMultipliedAlpha (a), 0.0f, box.getY(),
                                  juce::Colour (0xff6a6e74).withMultipliedAlpha (a), 0.0f, box.getBottom(), false);
        rim.addColour (0.5, juce::Colour (0xff3a3c40).withMultipliedAlpha (a));
        g.setGradientFill (rim);
    }
    g.drawRoundedRectangle (box, 5.0f, 1.4f);
}

void drawHistoryArrow (juce::Graphics& g, juce::Point<float> centre, bool redo, float alpha)
{
    constexpr double r = 9.6, pi = juce::MathConstants<double>::pi;
    constexpr double a0 = -14.0 * pi / 180.0, a1 = 188.0 * pi / 180.0;
    const double flip = redo ? -1.0 : 1.0;
    const auto at = [&] (double t) { return juce::Point<float> ((float) (centre.x + flip * r * std::cos (t)), (float) (centre.y + 1.2 - r * std::sin (t))); };

    juce::Path arc;
    for (int i = 0; i <= 40; ++i)
    {
        const auto p = at (a0 + (a1 - a0) * i / 40.0);
        if (i == 0) arc.startNewSubPath (p); else arc.lineTo (p);
    }
    // The head at the arc's far end, along its tangent.
    const auto e = at (a1);
    const juce::Point<float> dv ((float) (-std::sin (a1) * flip), (float) -std::cos (a1));
    const juce::Point<float> n (dv.y, -dv.x);
    const auto q = [&] (float u, float v) { return e + dv * u + n * v; };
    juce::Path head;
    head.addTriangle (q (6.4f, 0.0f), q (-0.6f, 4.6f), q (-0.6f, -4.6f));

    const auto box = arc.getBounds().getUnion (head.getBounds());
    const juce::PathStrokeType round (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (black.withMultipliedAlpha (alpha));
    g.strokePath (arc, round);
    g.setGradientFill (silverFill (box.getY(), box.getBottom(), alpha));
    g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath (head);
    g.setColour (black.withMultipliedAlpha (alpha));
    g.strokePath (head, juce::PathStrokeType (0.8f));
}

//==============================================================================
GlyphLever::GlyphLever (std::vector<float> stops, float slot, float glyphLeft, juce::Point<float> origin)
    : DetentLever ((int) stops.size(), 1, 260.0f, 22.0f),      // a heavy lever, as the walker's; it opens on the first channel
      slotX (slot - origin.x), glyphX (glyphLeft - origin.x)
{
    for (const float y : stops)
        heights.push_back (y - origin.y);
}

float GlyphLever::yOf (float travel) const noexcept
{
    const int last = (int) heights.size() - 1;
    const float p = juce::jlimit (0.0f, (float) last, travel);
    const int i = juce::jmin (last - 1, (int) p);
    return juce::jmap (p - (float) i, heights[(size_t) i], heights[(size_t) i + 1]);
}

float GlyphLever::travelAt (float y) const noexcept
{
    const int last = (int) heights.size() - 1;
    if (y <= heights.front())
        return 0.0f;
    for (int i = 0; i < last; ++i)
        if (y <= heights[(size_t) i + 1])
            return (float) i + (y - heights[(size_t) i]) / (heights[(size_t) i + 1] - heights[(size_t) i]);
    return (float) last;
}

juce::Rectangle<float> GlyphLever::capAt (float y) const noexcept
{
    return juce::Rectangle<float> (capWidth, capHeight).withCentre ({ slotX, y });
}

int GlyphLever::stopNearest (float y) const noexcept
{
    return juce::jlimit (0, (int) heights.size() - 1, juce::roundToInt (travelAt (y)));
}

juce::Point<float> GlyphLever::glyphAt (int stop) const
{
    return { glyphX + 10.0f, heights[(size_t) juce::jlimit (0, (int) heights.size() - 1, stop)] };
}

void GlyphLever::paint (juce::Graphics& g)
{
    // THE GLYPHS, each centred on its stop, from the glyphs' left edge (e530.html): the one the lever stands nearest full,
    // the others a shade back.
    const int nearest = juce::roundToInt (travel());
    for (int i = 0; i < (int) heights.size(); ++i)
    {
        const float y = heights[(size_t) i];
        const float alpha = i == nearest ? 1.0f : 0.62f;
        if (i == 0)
        {
            silverWords (g, "P", { glyphX + 6.0f, y }, rockerHeightOfEm (18.0f), 1.0e6f, alpha);
            continue;
        }
        const bool filled = i >= 3;
        const std::vector<float> xs = (i % 2 == 1) ? std::vector<float> { glyphX + 6.0f } : std::vector<float> { glyphX + 4.0f, glyphX + 16.0f };
        for (const float x : xs)
        {
            const auto ring = juce::Rectangle<float> (9.2f, 9.2f).withCentre ({ x, y });
            if (filled)
            {
                g.setGradientFill (silverFill (ring.getY(), ring.getBottom(), alpha));
                g.fillEllipse (ring);
                g.setColour (black.withMultipliedAlpha (alpha));
                g.drawEllipse (ring, 0.7f);
            }
            else
            {
                g.setColour (black.withMultipliedAlpha (alpha * 0.8f));
                g.drawEllipse (ring, 2.8f);
                g.setGradientFill (silverFill (ring.getY() - 1.0f, ring.getBottom() + 1.0f, alpha));
                g.drawEllipse (ring, 2.0f);
            }
        }
    }

    // The cap across the slot: dark, a chrome rim, its orange line where it stands.
    const auto cap = capAt (yOf (travel()));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.5f), 5.0f);
    drawCap (g, cap, false, false);
    g.setColour (face::orange);
    g.fillRoundedRectangle (cap.withSizeKeepingCentre (cap.getWidth() - 16.0f, 2.4f), 1.2f);
    if (isMouseOverOrDragging() && ! handLight.isTransparent())
    {
        juce::Path rim;
        rim.addRoundedRectangle (cap.expanded (1.5f), 6.0f);
        face::drawHandRim (g, rim, handLight);
    }
}

void GlyphLever::mouseDown (const juce::MouseEvent& e)
{
    take (capAt (yOf (travel())).contains (e.position), e.position.y);
}

void GlyphLever::mouseDrag (const juce::MouseEvent& e)
{
    if (isHeld() && e.mouseWasDraggedSinceMouseDown())
        follow (e.position.y);
}

void GlyphLever::mouseUp (const juce::MouseEvent& e)
{
    if (isHeld())
        letGo (e.mouseWasDraggedSinceMouseDown() ? travel() : underHand (e.position.y));
}

void GlyphLever::mouseMove (const juce::MouseEvent& e)
{
    repaint();
    if (const int at = stopNearest (e.position.y); at != hovered)
    {
        hovered = at;
        if (onHover)
            onHover (at);
    }
}

void GlyphLever::mouseExit (const juce::MouseEvent&)
{
    repaint();
    hovered = -1;
    if (onHover)
        onHover (-1);
}

//==============================================================================
WordButton::WordButton (juce::String w) : word (std::move (w))
{
    setRepaintsOnMouseActivity (true);
}

void WordButton::paint (juce::Graphics& g)
{
    const auto cap = getLocalBounds().toFloat().reduced (inset);
    const bool down = isMouseButtonDown() && isMouseOver();
    drawCap (g, cap, down, false);
    silverWords (g, word, cap.getCentre().translated (0.0f, down ? 1.0f : 0.0f), face::stage::px13, cap.getWidth() - 24.0f);
    if (isMouseOverOrDragging() && ! handLight.isTransparent())
    {
        juce::Path rim;
        rim.addRoundedRectangle (cap.expanded (1.0f), 6.0f);
        face::drawHandRim (g, rim, handLight);
    }
}

void WordButton::mouseUp (const juce::MouseEvent& e)
{
    repaint();
    if (getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu() && onClick)
        onClick();
}

//==============================================================================
namespace
{
    /** The logo's two palettes (cabin-logo.js): the gradient's top, middle and bottom, and the highlight. */
    struct Palette { juce::uint8 top[3], mid[3], bot[3], hi[3]; };
    constexpr Palette silverPalette  { { 0xe3, 0xe8, 0xec }, { 0xb8, 0xbe, 0xc4 }, { 0x8f, 0x97, 0x9e }, { 0xf7, 0xf9, 0xfb } };
    constexpr Palette crimsonPalette { { 0xff, 0x6e, 0x62 }, { 0xdc, 0x14, 0x3c }, { 0x7c, 0x05, 0x18 }, { 0xff, 0xd9, 0xc4 } };

    /** A flame's root on the seat's rim: every ~10 px along it, its height, width, lean and own clocks (roots()). */
    struct Root { float x, y, height, up, w, lean, a, ph, f1, f2, f3; int n; };

    std::vector<Root> rootsOf (const std::vector<juce::Point<float>>& rim)
    {
        float top = 1.0e9f, bottom = -1.0e9f, left = 1.0e9f, right = -1.0e9f;
        for (const auto p : rim)
        {
            top = juce::jmin (top, p.y); bottom = juce::jmax (bottom, p.y);
            left = juce::jmin (left, p.x); right = juce::jmax (right, p.x);
        }
        const float cx = 0.5f * (left + right), half = 0.5f * (right - left);
        std::vector<Root> out;
        int n = 0;
        for (size_t e = 0; e < rim.size(); ++e)
        {
            const auto a = rim[e], b = rim[(e + 1) % rim.size()];
            const float len = a.getDistanceFrom (b);
            const int steps = juce::jmax (1, juce::roundToInt (len / 10.0f));
            for (int k = 0; k < steps; ++k)
            {
                const float u = ((float) k + 0.5f) / (float) steps;
                const float x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
                const float up = (bottom - y) / (bottom - top);         // 1 on the top edge, 0 at the bottom
                if (up < 0.12f)
                    continue;                                           // nothing from under the seat
                const float side = x < cx ? -1.0f : 1.0f, edge = std::abs (x - cx) / half;
                const float h = (24.0f + 40.0f * std::pow (up, 1.4f) * (1.0f - 0.2f * edge * edge)) * (0.7f + 0.55f * (float) hash (n * 4.3));
                out.push_back ({ x, y, h, up, 12.0f + 6.0f * up, side * (0.12f + 0.3f * (1.0f - up)), 0.65f + 0.35f * up,
                                 (float) (hash (n * 3.1) * 6.283), (float) (7.0 + 5.0 * hash (n * 1.7)),
                                 (float) (13.0 + 9.0 * hash (n * 2.3)), (float) (2.2 + 1.6 * hash (n * 5.9)), n });
                ++n;
            }
        }
        return out;
    }

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
}

CatLogo::CatLogo (juce::Rectangle<float> b, juce::Rectangle<float> l, std::vector<juce::Point<float>> rim,
                  std::vector<juce::Point<float>> inside)
    : box (b), logo (l)
{
    setInterceptsMouseClicks (false, false);
    for (size_t i = 0; i < rim.size(); ++i)
        i == 0 ? rimPath.startNewSubPath (rim[i]) : rimPath.lineTo (rim[i]);
    rimPath.closeSubPath();
    for (size_t i = 0; i < inside.size(); ++i)
        i == 0 ? insidePath.startNewSubPath (inside[i]) : insidePath.lineTo (inside[i]);
    insidePath.closeSubPath();
    rimPoints = std::move (rim);
}

CatLogo::~CatLogo()
{
    if (watched != nullptr)
        watched->removeMouseListener (this);
}

void CatLogo::watch (juce::Component& stage)
{
    watched = &stage;
    stage.addMouseListener (this, true);
}

void CatLogo::force (bool hovered, double t)
{
    forced = hovered;
    over = hovered;
    h = hovered ? 1.0f : 0.0f;
    frozenAt = t;
    stopTimer();
    if (hovered && t < 0.0)
        aim (true);
    repaint();
}

void CatLogo::aim (bool hovered)
{
    over = hovered || forced;
    if (! isTimerRunning())
    {
        last = nowSeconds();
        startTimerHz (60);
    }
}

void CatLogo::timerCallback()
{
    const double t = nowSeconds(), dt = juce::jmin (0.1, t - last);
    last = t;
    const float want = over ? 1.0f : 0.0f;
    h += (want - h) * (float) (1.0 - std::exp (-dt / (want > h ? 0.16 : 0.6)));
    if (! over && h < 0.002f)
        h = 0.0f;
    repaint();
    if (! over && h <= 0.0f)
        stopTimer();                // at rest: drawn, and still
}

void CatLogo::mouseMove (const juce::MouseEvent& e)
{
    if (watched == nullptr || forced)
        return;
    const bool inside = rimPath.contains (e.getEventRelativeTo (watched).position);
    if (inside != over)
        aim (inside);
}

void CatLogo::mouseExit (const juce::MouseEvent& e)
{
    if (watched == nullptr || forced || ! over)
        return;
    // Gone off the stage, or off whatever lies over the seat: the next component under the hand says the rest.
    const auto under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    if (under == nullptr || (under != watched && ! watched->isParentOf (under))
        || ! rimPath.contains (e.getEventRelativeTo (watched).position))
        aim (false);
}

/** The logo, brushed (cabin-logo.js, brushed()): the mark drawn where it stands at `scale` pixels to the stage's unit,
    then each of its pixels shaded by its own grey level on the palette's gradient, streaked a row at a time and grained
    a pixel at a time from the page's own seed and in its order, and lit along the top edges of its light shapes. */
juce::Image CatLogo::brushed (float scale, int palette) const
{
    const auto& pal = palette == 0 ? silverPalette : crimsonPalette;
    const int w = juce::jmax (2, juce::roundToInt (box.getWidth() * scale)), hgt = juce::jmax (2, juce::roundToInt (box.getHeight() * scale));
    juce::Image img (juce::Image::ARGB, w, hgt, true);
    auto* mark = felitronics::appkit::brand::catMark();
    if (mark == nullptr)
        return img;

    // Where the mark lands: fitted inside the logo's box, its aspect kept, centred.
    const auto bounds = mark->getDrawableBounds();
    const float s = juce::jmin (logo.getWidth() / bounds.getWidth(), logo.getHeight() / bounds.getHeight());
    const auto fitted = juce::Rectangle<float> (bounds.getWidth() * s, bounds.getHeight() * s).withCentre (logo.getCentre()) - box.getPosition();
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        mark->drawWithin (g, fitted, juce::RectanglePlacement::centred, 1.0f);
    }

    const int x0 = juce::jmax (0, (int) std::floor (fitted.getX() * scale)), y0 = juce::jmax (0, (int) std::floor (fitted.getY() * scale));
    const int W = juce::jmin (w, (int) std::ceil (fitted.getRight() * scale)) - x0;
    const int H = juce::jmin (hgt, (int) std::ceil (fitted.getBottom() * scale)) - y0;
    if (W <= 0 || H <= 0)
        return img;

    juce::Image::BitmapData px (img, x0, y0, W, H, juce::Image::BitmapData::readWrite);
    std::vector<float> lum ((size_t) (W * H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const auto c = px.getPixelColour (x, y);
            lum[(size_t) (y * W + x)] = ((float) c.getRed() + (float) c.getGreen() + (float) c.getBlue()) / 765.0f;
        }

    juce::int64 seed = 7;
    const auto rnd = [&seed] { seed = (seed * 16807) % 2147483647; return (double) seed / 2147483647.0; };
    std::vector<double> rowN ((size_t) H);
    for (auto& r : rowN)
        r = rnd() - 0.5;
    const int step = juce::jmax (1, juce::roundToInt (scale));
    for (int y = 0; y < H; ++y)
    {
        const float t = (float) y / (float) juce::jmax (1, H - 1);
        float grad[3];
        for (int q = 0; q < 3; ++q)
            grad[q] = t < 0.5f ? (float) pal.top[q] + ((float) pal.mid[q] - (float) pal.top[q]) * t * 2.0f
                               : (float) pal.mid[q] + ((float) pal.bot[q] - (float) pal.mid[q]) * (t - 0.5f) * 2.0f;
        for (int x = 0; x < W; ++x)
        {
            const auto c = px.getPixelColour (x, y);
            if (c.getAlpha() == 0)
                continue;
            const float l = lum[(size_t) (y * W + x)];
            const float f = (0.18f + 0.82f * std::pow (l, 1.2f)) * (float) (1.0 + 0.07 * rowN[(size_t) y] + 0.03 * (rnd() - 0.5));
            float col[3] = { grad[0] * f, grad[1] * f, grad[2] * f };
            const float above = y >= step ? lum[(size_t) ((y - step) * W + x)] : 0.0f;
            if (l > 0.45f && above < l - 0.3f)
                for (int q = 0; q < 3; ++q)
                    col[q] += ((float) pal.hi[q] - col[q]) * 0.75f;
            const auto byte = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (v)); };
            px.setPixelColour (x, y, juce::Colour (byte (col[0]), byte (col[1]), byte (col[2]), c.getAlpha()));
        }
    }
    return img;
}

void CatLogo::make (float scale) const
{
    if (made.silver.isValid() && std::abs (made.scale - scale) < 1.0e-3f)
        return;
    made.scale = scale;
    made.silver  = brushed (scale, 0);
    made.crimson = brushed (scale, 1);
    made.mix = juce::Image (juce::Image::ARGB, made.silver.getWidth(), made.silver.getHeight(), true);
}

/** THE FLAMES (cabin-logo.js, flames()): the glow the fire throws round the seat; a tongue from each root, rising, its
    height flickering on two clocks and a jitter, its tip swaying — an outer red tongue and a hot core in it; sparks rising
    off the top. Never over the seat's inside: the logo stands clear in its frame. */
void CatLogo::flames (juce::Graphics& g, double t, float amount) const
{
    static const auto roots = rootsOf (rimPoints);     // one seat, one set of roots

    const juce::Graphics::ScopedSaveState keep (g);
    g.addTransform (juce::AffineTransform::translation (-box.getX(), -box.getY()));

    // The glow on the face round the seat.
    const float fl = (float) (0.8 + 0.2 * std::sin (t * 11.3) * std::sin (t * 7.1 + 1.0));
    {
        const juce::Point<float> c { 836.0f, 690.0f };
        const auto inner = rgba (255, 70, 24, 0.32f * amount * fl);
        juce::ColourGradient glow (inner, c, rgba (120, 0, 10, 0.0f), c.translated (118.0f, 0.0f), true);
        glow.addColour (30.0 / 118.0, inner);
        glow.addColour ((30.0 + 0.55 * 88.0) / 118.0, rgba (200, 20, 30, 0.14f * amount * fl));
        g.setGradientFill (glow);
        g.fillRect (box);
    }

    // The tongues, outside the seat's inside.
    juce::Path clip;
    clip.addRectangle (box);
    clip.addPath (insidePath);
    clip.setUsingNonZeroWinding (false);
    g.reduceClipRegion (clip);

    const auto tongue = [&g] (float x, float y, float w, float height, float sway, bool outer, float a)
    {
        const float tx = x + sway, ty = y - height, r = w * 0.5f, by = y - r * 0.2f;
        juce::Path p;
        p.startNewSubPath (tx, ty);
        p.cubicTo (tx - w * 0.2f, ty + height * 0.4f, x - w * 0.62f, by - height * 0.3f, x - r, by);
        p.addCentredArc (x, by, r, r * 0.8f, 0.0f, 1.5f * juce::MathConstants<float>::pi, 0.5f * juce::MathConstants<float>::pi);
        p.cubicTo (x + w * 0.62f, by - height * 0.32f, tx + w * 0.26f, ty + height * 0.42f, tx, ty);
        p.closeSubPath();
        juce::ColourGradient fill;
        fill.point1 = { 0.0f, by + r * 0.8f };
        fill.point2 = { 0.0f, ty };
        fill.isRadial = false;
        if (outer)
        {
            fill.addColour (0.0, rgba (255, 90, 20, 0.0f));
            fill.addColour (0.14, rgba (255, 110, 28, 0.7f * a));
            fill.addColour (0.4, rgba (232, 38, 22, 0.62f * a));
            fill.addColour (0.78, rgba (150, 4, 24, 0.36f * a));
            fill.addColour (1.0, rgba (90, 0, 12, 0.0f));
        }
        else
        {
            fill.addColour (0.0, rgba (255, 220, 140, 0.0f));
            fill.addColour (0.18, rgba (255, 240, 190, 0.8f * a));
            fill.addColour (0.5, rgba (255, 176, 52, 0.62f * a));
            fill.addColour (0.85, rgba (255, 80, 20, 0.25f * a));
            fill.addColour (1.0, rgba (255, 60, 10, 0.0f));
        }
        g.setGradientFill (fill);
        g.fillPath (p);
    };

    const double tick = std::floor (t * 18.0);
    for (const auto& r : roots)
    {
        const float jit = (float) (0.85 + 0.3 * (hash (r.n * 7.7 + tick) * 0.6 + hash (r.n * 7.7 + tick + 1.0) * 0.4));
        const float life = (float) (0.62 + 0.22 * std::sin (t * r.f1 + r.ph) + 0.16 * std::sin (t * r.f2 + r.ph * 2.3));
        const float height = r.height * life * jit * (0.35f + 0.65f * amount);
        const float sway = height * (float) (r.lean + 0.18 * std::sin (t * r.f3 + r.ph * 1.7) + 0.08 * std::sin (t * r.f2 * 0.7 + r.ph));
        tongue (r.x, r.y, r.w, height, sway, true, amount * r.a);
        tongue (r.x + sway * 0.05f, r.y - 2.0f, r.w * 0.5f, height * 0.55f, sway * 0.5f, false, amount * r.a * (0.25f + 0.75f * r.up * r.up));
    }

    // Sparks off the top edge, rising and drifting, each on its own life.
    for (int i = 0; i < 16; ++i)
    {
        const double life = 1.1 + 0.9 * hash (i * 3.3), age = std::fmod (t + hash (i * 9.1) * life, life) / life;
        const float x = (float) (790.0 + 92.0 * hash (i * 4.7) + 14.0 * std::sin (t * 2.1 + i) * age);
        const float y = (float) (644.0 - 95.0 * age - 10.0 * hash (i * 2.9));
        const float a = amount * (float) (1.0 - age) * smooth (0.0f, 0.1f, (float) age);
        g.setColour (rgba (255, 150 + (int) (80.0 * (1.0 - age)), 60, 0.9f * a));
        const float rad = (float) (0.9 + 0.8 * (1.0 - age));
        g.fillEllipse (juce::Rectangle<float> (2.0f * rad, 2.0f * rad).withCentre ({ x, y }));
    }
}

void CatLogo::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    make (scale);
    const double t = frozenAt >= 0.0 ? frozenAt : nowSeconds();
    if (const float amt = smooth (0.12f, 0.8f, h); amt > 0.002f)       // the flames die a little ahead of the colour
        flames (g, t, amt);

    // The logo: silver and crimson crossfaded pixel by pixel — both are the one shape, shaded two ways.
    const auto area = getLocalBounds().toFloat();
    if (h < 0.002f)
        g.drawImage (made.silver, area);
    else if (h > 0.998f)
        g.drawImage (made.crimson, area);
    else
    {
        const juce::Image::BitmapData a (made.silver, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData b (made.crimson, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData m (made.mix, juce::Image::BitmapData::writeOnly);
        const int k = juce::roundToInt (h * 256.0f);
        for (int y = 0; y < m.height; ++y)
        {
            const auto* pa = reinterpret_cast<const juce::PixelARGB*> (a.getLinePointer (y));
            const auto* pb = reinterpret_cast<const juce::PixelARGB*> (b.getLinePointer (y));
            auto* pm = reinterpret_cast<juce::PixelARGB*> (m.getLinePointer (y));
            for (int x = 0; x < m.width; ++x)
            {
                pm[x] = pa[x];
                pm[x].tween (pb[x], (juce::uint32) k);
            }
        }
        g.drawImage (made.mix, area);
    }
}

//==============================================================================
TunerNote::TunerNote()
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
}

void TunerNote::show (const juce::String& w)
{
    words = w;
    shownAt = nowSeconds();
    setAlpha (1.0f);
    const bool was = isVisible();
    setVisible (true);
    repaint();
    if (! was && onShow)
        onShow (true);
    startTimerHz (30);
}

void TunerNote::timerCallback()
{
    constexpr double hold = 1.5, fade = 0.2;
    const double age = nowSeconds() - shownAt;
    if (age <= hold)
        return;
    const float a = (float) (1.0 - (age - hold) / fade);
    if (a > 0.0f)
    {
        setAlpha (a);
        return;
    }
    stopTimer();
    setVisible (false);
    if (onShow)
        onShow (false);
}

void TunerNote::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    silverWords (g, words, area.getCentre(), height > 0.0f ? height : face::stage::px13, area.getWidth() - 16.0f);
}

} // namespace orbitamp::metal
