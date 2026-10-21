// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Controls.h"

#include <felitronics/appkit/BrandAssets.h>

#include <map>
#include <vector>

namespace orbitamp::face
{

juce::Font labelFont (float px)
{
    // The family counts a label's size the way the rest of OrbitAmp does — the font's HEIGHT, as
    // brand::wordmarkFont sets it — so a 13 here is the same 13 the type rule means everywhere else.
    return felitronics::appkit::brand::wordmarkFont (px);
}

void writeWords (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Rectangle<float> box, float weight)
{
    if (weight <= 0.0f)
    {
        g.setFont (font);
        g.drawText (text, box, juce::Justification::centred, false);
        return;
    }

    // Laid out the way Graphics::drawText lays it, then filled and traced round at the weight.
    juce::GlyphArrangement glyphs;
    glyphs.addCurtailedLineOfText (font, text, 0.0f, 0.0f, box.getWidth(), false);
    glyphs.justifyGlyphs (0, glyphs.getNumGlyphs(), box.getX(), box.getY(), box.getWidth(), box.getHeight(),
                          juce::Justification::centred);
    juce::Path outline;
    glyphs.createPath (outline);
    g.fillPath (outline);
    g.strokePath (outline, juce::PathStrokeType (weight, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawSign (juce::Graphics& g, juce::Rectangle<float> box, const std::vector<float>& curve, float stroke, juce::Colour colour)
{
    if (curve.size() < 2)
        return;
    juce::Path p;
    for (size_t i = 0; i < curve.size(); ++i)
    {
        const float x = box.getX() + box.getWidth() * (float) i / (float) (curve.size() - 1);
        const float y = box.getBottom() - box.getHeight() * juce::jlimit (0.0f, 1.0f, curve[i]);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawFilterSign (juce::Graphics& g, juce::Rectangle<float> face, bool highpass, juce::Colour colour)
{
    // In a box 0.52 of the face wide and 0.25 tall: the slope over the outer 0.42 of it, rising (or,
    // mirrored, falling) into the flat by a knee rounded over a tenth of the width.
    const float side = juce::jmin (face.getWidth(), face.getHeight());
    constexpr int n = 48;
    std::vector<float> curve;
    for (int i = 0; i < n; ++i)
    {
        constexpr float knee = 0.42f, r = 0.06f;                          // the knee, and half the width it is rounded over
        const float x = (float) i / (float) (n - 1), u = highpass ? x : 1.0f - x;
        if (u <= knee - r)       curve.push_back (u / knee);
        else if (u >= knee + r)  curve.push_back (1.0f);
        else
        {
            // The quadratic from the slope into the flat, its control point the corner: x runs on it
            // evenly, so t is where u is across the knee.
            const float t = (u - (knee - r)) / (2.0f * r), y0 = (knee - r) / knee;
            curve.push_back ((1.0f - t) * (1.0f - t) * y0 + 2.0f * t * (1.0f - t) + t * t);
        }
    }
    drawSign (g, juce::Rectangle<float> (side * 0.52f, side * 0.25f).withCentre (face.getCentre()), curve, signStroke, colour);
}

void drawToneSign (juce::Graphics& g, juce::Rectangle<float> place, const std::vector<float>& curve, juce::Colour colour, float height)
{
    const auto box = juce::Rectangle<float> (juce::jmin (toneSignSize.x, place.getWidth()), height).withCentre (place.getCentre());
    drawSign (g, box, curve.size() >= 2 ? curve : std::vector<float> { 0.5f, 0.5f }, signStroke, colour);
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float em,
                juce::Colour colour, float trackingEm, float weight)
{
    g.setColour (colour);
    writeWords (g, text.toUpperCase(), labelFont (em).withExtraKerningFactor (trackingEm), area, weight);
}

void Ink::label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float em,
                 juce::Colour colour, float trackingEm) const
{
    if (! shade.isTransparent())
        drawLabel (g, text, area.translated (0.0f, -edgeDrop), em, over (colour), trackingEm, weight);
    if (! edge.isTransparent())
        drawLabel (g, text, area.translated (0.0f, edgeDrop), em, under (colour), trackingEm, weight);
    drawLabel (g, text, area, em, colour, trackingEm, weight);
}

void glow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius)
{
    juce::DropShadow (c, juce::jmax (1, juce::roundToInt (radius)), {}).drawForPath (g, p);
}

juce::Rectangle<float> opaqueBounds (const juce::Image& img)
{
    if (! img.isValid())
        return {};

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int x0 = img.getWidth(), y0 = img.getHeight(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (bd.getPixelColour (x, y).getAlpha() > 128)
            {
                x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y);
                x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y);
            }
    return x1 < 0 ? juce::Rectangle<float>()
                  : juce::Rectangle<float> ((float) x0, (float) y0, (float) (x1 - x0 + 1), (float) (y1 - y0 + 1));
}

namespace
{
    /** A render's opaque box, measured once per image and remembered by the image's pixel data. */
    juce::Rectangle<float> opaqueBoundsOnce (const juce::Image& img)
    {
        static std::map<const void*, juce::Rectangle<float>> boxes;
        auto it = boxes.find (img.getPixelData().get());
        if (it == boxes.end())
            it = boxes.emplace (img.getPixelData().get(), opaqueBounds (img)).first;
        return it->second;
    }
}

juce::Rectangle<float> fittedArea (const juce::Image& img, juce::Rectangle<float> area)
{
    if (! img.isValid())
        return area;
    const auto src = opaqueBoundsOnce (img);
    const float k  = juce::jmin (area.getWidth() / src.getWidth(), area.getHeight() / src.getHeight());
    return juce::Rectangle<float> (src.getWidth() * k, src.getHeight() * k).withCentre (area.getCentre());
}

void drawHandRim (juce::Graphics& g, const juce::Path& outline, juce::Colour colour)
{
    juce::Path line;
    juce::PathStrokeType (1.6f).createStrokedPath (line, outline);
    juce::DropShadow (colour, 6, {}).drawForPath (g, line);
    g.setColour (colour.withMultipliedAlpha (0.75f));
    g.fillPath (line);
}

void drawFitted (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> area, float alpha)
{
    if (! img.isValid() || alpha <= 0.0f)
        return;

    const auto src = opaqueBoundsOnce (img);
    const auto dst = fittedArea (img, area);
    const float k  = dst.getWidth() / src.getWidth();

    g.setOpacity (alpha);
    g.drawImageTransformed (img, juce::AffineTransform::translation (-src.getX(), -src.getY())
                                     .scaled (k)
                                     .translated (dst.getX(), dst.getY()));
    g.setOpacity (1.0f);
}

namespace
{
    /** The hand's rim round a cap drawn as drawCap draws it — in `colour`, or nothing while that is
        transparent. */
    void rimCap (juce::Graphics& g, const juce::Image& img, juce::Point<float> centre, float length, float angle, juce::Colour colour)
    {
        if (colour.isTransparent() || ! img.isValid())
            return;
        const auto src = opaqueBoundsOnce (img);
        const float k = length / src.getWidth();
        const auto box = juce::Rectangle<float> (src.getWidth() * k, src.getHeight() * k).withCentre ({});
        juce::Path shape;
        shape.addRoundedRectangle (box.reduced (0.8f), juce::jmin (box.getWidth(), box.getHeight()) * 0.12f);
        shape.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
        drawHandRim (g, shape, colour);
    }

    /** Draws a cap render centred on `centre`, its long axis `length` long, turned by `angle`. */
    void drawCap (juce::Graphics& g, const juce::Image& img, juce::Point<float> centre, float length, float angle)
    {
        if (! img.isValid())
        {
            g.setColour (juce::Colours::grey);
            g.fillRoundedRectangle (juce::Rectangle<float> (length, length * 0.4f).withCentre (centre), 4.0f);
            return;
        }
        const auto src = opaqueBoundsOnce (img);
        const float k = length / src.getWidth();
        g.setOpacity (1.0f);    // an image is drawn at the colour's opacity: a dim mark's, set before, let the well show through
        g.drawImageTransformed (img, juce::AffineTransform::translation (-src.getCentreX(), -src.getCentreY())
                                         .scaled (k)
                                         .rotated (angle)
                                         .translated (centre));
    }
}

//==============================================================================
DetentLever::DetentLever (int n, int start, float stiffness, float damping)
    : places (n), place (start)
{
    pos.stiffness = stiffness;
    pos.damping   = damping;
    pos.jump ((float) start);
}

void DetentLever::setPlace (int to, bool animate, bool notify)
{
    to = juce::jlimit (0, places - 1, to);
    const bool changed = to != place;
    place = to;
    if (animate)
    {
        pos.target = (float) to;
        startTimerHz (60);
    }
    else
    {
        pos.jump ((float) to);
        travelled();
    }
    if (changed && notify && onPlace)
        onPlace (place);
}

void DetentLever::travelled()
{
    repaint();
    if (onTravel)
        onTravel (pos.value);
}

void DetentLever::timerCallback()
{
    if (! glide())
        stopTimer();
    travelled();
}

void DetentLever::take (bool byTheGrip, float y)
{
    held = true;
    stopTimer();
    grabOffset = byTheGrip ? yOf (pos.value) - y : 0.0f;
}

void DetentLever::follow (float y)
{
    // The notch: within reach of a place the lever sits IN it, and the hand has to pull it out.
    const float raw = underHand (y);
    const int nearest = juce::roundToInt (raw);
    const float p = std::abs (raw - (float) nearest) < notchReach ? (float) nearest : raw;

    pos.jump (p);
    if (juce::exactlyEqual (p, (float) nearest) && nearest != place)
    {
        place = nearest;
        if (onPlace) onPlace (place);
    }
    travelled();
}

void DetentLever::letGo (float to)
{
    held = false;
    setPlace (juce::roundToInt (to), true);
    if (onRelease)
        onRelease();
}

void DetentLever::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (d) > 0.0f)
        setPlace (place + (d < 0.0f ? 1 : -1), true);
}

//==============================================================================
ArtKnob::ArtKnob (const juce::Image& a, Pointer p) : art (a), ptr (p)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void ArtKnob::setValue (float v, bool notify)
{
    hand = juce::jlimit (0.0f, 1.0f, v);
    land (hand, notify);
}

void ArtKnob::turn (float place)
{
    hand = juce::jlimit (0.0f, 1.0f, place);
    land (detent ? detent (hand) : hand, true);
}

void ArtKnob::land (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) < 1.0e-6f)
        return;
    value = v;
    repaint();
    if (notify && onChange)
        onChange (value);
}

float ArtKnob::shown() const
{
    return detent ? juce::jlimit (0.0f, 1.0f, detent (value)) : value;
}

void ArtKnob::settle()
{
    if (std::abs (shown() - painted) > 1.0e-6f)
        repaint();
}

void ArtKnob::paint (juce::Graphics& g)
{
    const auto b   = getLocalBounds().toFloat();
    const float d  = juce::jmin (b.getWidth(), b.getHeight());
    const auto box = b.withSizeKeepingCentre (d, d);
    const float R  = d * 0.5f;

    if (art.isValid())
        drawFitted (g, art, box);
    else
    {
        g.setColour (juce::Colour (0xff1a1822));
        g.fillEllipse (box);
    }

    // The pointer: a lit slot from `inner` to `outer` of the radius, turned 270 degrees end to end.
    painted = shown();
    const float angle = juce::degreesToRadians (-135.0f + 270.0f * painted);
    const float w = R * ptr.width;
    juce::Path p;
    p.addRoundedRectangle (-w * 0.5f, -R * ptr.outer, w, R * (ptr.outer - ptr.inner), w * 0.5f);
    p.applyTransform (juce::AffineTransform::rotation (angle).translated (box.getCentre()));

    const bool lit = isMouseOverOrDragging();
    glow (g, p, violet.withAlpha (lit ? 1.0f : 0.85f), R * 0.10f);
    g.setColour (juce::Colour (0xffd9c8ff).brighter (lit ? 0.15f : 0.0f));
    g.fillPath (p);
}

void ArtKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu)
    {
        onMenu();
        return;
    }
    turning = true;
    dragStartValue = shown();
    dragStartY = e.getScreenY();
    if (onGestureStart) onGestureStart();
}

void ArtKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! turning)
        return;
    const float pixelsForFullTurn = e.mods.isShiftDown() ? 1400.0f : 280.0f;
    turn (dragStartValue + (float) (dragStartY - e.getScreenY()) / pixelsForFullTurn);
}

void ArtKnob::mouseUp (const juce::MouseEvent&)
{
    if (! turning)
        return;
    turning = false;
    if (onGestureEnd) onGestureEnd();
}

void ArtKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onGestureStart) onGestureStart();
    turn (defaultValue);
    if (onGestureEnd) onGestureEnd();
}

void ArtKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (onGestureStart) onGestureStart();
    turn (hand + w.deltaY * (w.isReversed ? -0.12f : 0.12f));      // the hand's place: small turns add up to a click
    if (onGestureEnd) onGestureEnd();
}

//==============================================================================
PushButton::PushButton (juce::String n) : name (std::move (n))
{
    depth.stiffness = 1800.0f;  depth.damping = 46.0f;
    light.stiffness = 600.0f;   light.damping = 50.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PushButton::aim()
{
    // Held: bottomed out. Latched on: resting a little down. Off: all the way up.
    depth.target = held ? 1.0f : (on ? 0.45f : 0.0f);
    light.target = on ? 1.0f : 0.0f;
    startTimerHz (60);
}

void PushButton::setOn (bool v, bool notify)
{
    if (v == on)
        return;
    on = v;
    aim();
    if (notify && onChange)
        onChange (on);
    if (onHover && isMouseOver())
        onHover (true);
}

void PushButton::timerCallback()
{
    const bool a = depth.step (frameDt);
    const bool b = light.step (frameDt);
    if (! a && ! b)
        stopTimer();
    repaint();
}

void PushButton::enablementChanged()
{
    // A key that will not go down does not offer itself to the hand either.
    setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (onHover && isMouseOver())       // what is said over it changes with it
        onHover (true);
    repaint();
}

void PushButton::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    if (e.mods.isPopupMenu() && onMenu)
    {
        onMenu();
        return;
    }
    held = true;
    aim();
}

void PushButton::mouseUp (const juce::MouseEvent& e)
{
    if (! held)
        return;
    held = false;
    if (getLocalBounds().contains (e.getPosition()))
    {
        if (latching)
            setOn (! on, true);
        else if (onPress)
            onPress();
    }
    aim();
}

void PushButton::drawChromeKey (juce::Graphics& g, juce::Rectangle<float> box, float depth, float light)
{
    // Drawn for a 40-px box — an overhead hole — and scaled to whatever box it is given.
    const float u = juce::jmin (box.getWidth(), box.getHeight()) / 40.0f;
    const auto c = box.getCentre();
    const float d = juce::jlimit (0.0f, 1.2f, depth);
    const float l = juce::jlimit (0.0f, 1.0f, light);

    // The bezel: a chrome ring set into the hole, the light ring inside it.
    const auto bezel = juce::Rectangle<float> (34.0f * u, 34.0f * u).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8f8c9c), c.translated (-6.0f * u, -9.0f * u),
                                             juce::Colour (0xff1e1d25), c.translated (8.0f * u, 12.0f * u), true));
    g.fillEllipse (bezel);
    g.setColour (juce::Colour (0xff07060a));
    g.fillEllipse (bezel.reduced (3.0f * u));

    const auto ring = bezel.reduced (4.5f * u);
    if (l > 0.01f)
    {
        juce::Path rp;
        rp.addEllipse (ring);
        glow (g, rp, violet.withAlpha (0.9f * l), 7.0f * u);
    }
    g.setColour (violet.interpolatedWith (juce::Colour (0xff2a2140), 1.0f - l));
    g.drawEllipse (ring, 1.8f * u);

    // The cap: it shrinks a touch and darkens at its rim as it goes down — farther from the eye.
    const float capD = 20.0f * u * (1.0f - 0.10f * d);
    const auto cap = juce::Rectangle<float> (capD, capD).withCentre (c.translated (0.0f, 0.8f * u * d));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb9b5c6).interpolatedWith (juce::Colour (0xff6c6878), d * 0.5f),
                                             cap.getCentre().translated (-3.0f * u, -4.0f * u),
                                             juce::Colour (0xff24222c), cap.getCentre().translated (5.0f * u, 6.0f * u), true));
    g.fillEllipse (cap);

    // Latched on, the cap's face carries the light — a lilac pool in its middle.
    if (l > 0.01f)
    {
        g.setGradientFill (juce::ColourGradient (lilac.withAlpha (0.75f * l), cap.getCentre(),
                                                 violet.withAlpha (0.0f), cap.getCentre().translated (capD * 0.5f, 0.0f), true));
        g.fillEllipse (cap);
    }
    g.setColour (juce::Colour (0xff0b0a10));
    g.drawEllipse (cap, 1.0f);
}

void PushButton::drawWideChromeKey (juce::Graphics& g, juce::Rectangle<float> box, float depth, float light)
{
    const float h = box.getHeight();
    if (box.getWidth() <= h)
    {
        drawChromeKey (g, box, depth, light);
        return;
    }
    // The square key at the pixels it will be shown at, then its three slices.
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int px = juce::jmax (8, juce::roundToInt (h * scale)), half = px / 2;
    juce::Image key (juce::Image::ARGB, px, px, true);
    {
        juce::Graphics kg (key);
        drawChromeKey (kg, { 0.0f, 0.0f, (float) px, (float) px }, depth, light);
    }
    const float edge = 0.5f * h;
    g.drawImage (key.getClippedImage ({ 0, 0, half, px }), { box.getX(), box.getY(), edge, h });
    g.drawImage (key.getClippedImage ({ half, 0, 1, px }), { box.getX() + edge - 0.25f, box.getY(), box.getWidth() - 2.0f * edge + 0.5f, h });
    g.drawImage (key.getClippedImage ({ half, 0, px - half, px }), { box.getRight() - edge, box.getY(), edge, h });
}

void PushButton::paint (juce::Graphics& g)
{
    if (! isEnabled())
        g.beginTransparencyLayer (dimmed);

    if (drawKey)
    {
        drawKey (g, getLocalBounds().toFloat(), held || on, juce::jlimit (0.0f, 1.0f, light.value));
        if (! isEnabled())
            g.endTransparencyLayer();
        return;
    }

    drawChromeKey (g, getLocalBounds().toFloat(), juce::jlimit (0.0f, 1.2f, depth.value), juce::jlimit (0.0f, 1.0f, light.value));


    if (! isEnabled())
        g.endTransparencyLayer();
}

//==============================================================================
const MarkCut& MarkCut::at (float scale, juce::Rectangle<float> box)
{
    static MarkCut made;
    static float madeFor = 0.0f;
    static juce::Rectangle<float> madeAs;
    if (made.cut.isValid() && std::abs (scale - madeFor) < 1.0e-3f && box == madeAs)
        return made;

    madeFor = scale;
    madeAs = box;
    made = {};
    auto* mark = felitronics::appkit::brand::catMark();
    const int w = juce::roundToInt (box.getWidth() * scale), h = juce::roundToInt (box.getHeight() * scale);
    if (mark == nullptr || w <= 0 || h <= 0)
        return made;

    // The mark drawn white on black: how bright a pixel comes out is how much of the cut it is.
    juce::Image lit (juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g (lit);
        g.fillAll (juce::Colours::black);
        g.addTransform (juce::AffineTransform::scale (scale));
        mark->drawWithin (g, box.withZeroOrigin(), juce::RectanglePlacement::centred, 1.0f);
    }
    made.cut = juce::Image (juce::Image::SingleChannel, w, h, true);
    {
        const juce::Image::BitmapData from (lit, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData to (made.cut, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                to.setPixelColour (x, y, juce::Colours::white.withAlpha (from.getPixelColour (x, y).getBrightness()));
    }
    return made;
}

void engraveMark (juce::Graphics& g, juce::Rectangle<float> box, float scale, const MarkInk& ink)
{
    const auto& cut = MarkCut::at (scale, box).cut;
    if (! cut.isValid())
        return;

    const float px = 1.0f / juce::jmax (1.0f, scale) * 1.6f;   // the wall's depth, about a pixel and a half
    g.setColour (juce::Colours::black.withAlpha (ink.shadow));
    g.drawImage (cut, box.translated (-0.5f * px, -px), juce::RectanglePlacement::stretchToFit, true);
    g.setColour (ink.lipLight.withAlpha (ink.lip));
    g.drawImage (cut, box.translated (0.4f * px, px), juce::RectanglePlacement::stretchToFit, true);
    g.setGradientFill (juce::ColourGradient (ink.floorTop, box.getCentreX(), box.getY(),
                                             ink.floorBottom, box.getCentreX(), box.getBottom(), false));
    g.drawImage (cut, box, juce::RectanglePlacement::stretchToFit, true);
}

namespace
{
    /** THE PLATE'S WEAR: fine scratches, most of them the way a cloth wipes the plate and a few across it, and
        a scuff here and there where something rubbed it — each in the plate's own tone, a line lighter where
        it catches the light or darker, faint enough to read as used metal and never as a drawing. The same
        plate is worn the same way every time: its scratches are drawn from where it stands. */
    void wear (juce::Graphics& g, const juce::Path& plate, const BadgeLook& look)
    {
        if (look.wear <= 0.0f)
            return;
        const auto box = plate.getBounds();
        juce::Random rng ((juce::int64) (box.getX() * 131.0f + box.getY() * 7919.0f + box.getWidth() * 31.0f));
        const juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (plate);
        const auto light = look.plateTop.brighter (0.9f), dark = look.plateBottom.darker (0.6f);
        const int count = juce::roundToInt (look.wear * box.getWidth() * box.getHeight() / 220.0f);
        for (int i = 0; i < count; ++i)
        {
            const float angle = (rng.nextFloat() < 0.75f ? -0.35f : 1.1f) + (rng.nextFloat() - 0.5f) * 0.5f;
            const float length = box.getWidth() * (0.04f + 0.18f * rng.nextFloat() * rng.nextFloat());
            const juce::Point<float> from (box.getX() + rng.nextFloat() * box.getWidth(), box.getY() + rng.nextFloat() * box.getHeight());
            const juce::Point<float> along (std::cos (angle), std::sin (angle)), across (-along.y, along.x);
            const auto to = from + along * length;
            juce::Path scratch;
            scratch.startNewSubPath (from);
            scratch.quadraticTo ((from + to) * 0.5f + across * (length * 0.06f * (rng.nextFloat() - 0.5f)), to);
            const bool lit = rng.nextFloat() < 0.7f;
            g.setColour ((lit ? light : dark).withAlpha (look.wear * (lit ? 0.09f + 0.11f * rng.nextFloat() : 0.12f + 0.12f * rng.nextFloat())));
            g.strokePath (scratch, juce::PathStrokeType (0.3f + 0.4f * rng.nextFloat(), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int i = 0; i < count / 8; ++i)
        {
            const juce::Point<float> at (box.getX() + rng.nextFloat() * box.getWidth(), box.getY() + rng.nextFloat() * box.getHeight());
            const float angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const juce::Point<float> along (std::cos (angle), std::sin (angle));
            g.setColour (light.withAlpha (look.wear * 0.1f));
            for (int k = 0, n = 4 + rng.nextInt (4); k < n; ++k)
            {
                const auto from = at + juce::Point<float> (rng.nextFloat() - 0.5f, rng.nextFloat() - 0.5f) * 3.0f;
                g.drawLine (juce::Line<float> (from, from + along * (1.5f + 2.5f * rng.nextFloat())), 0.4f);
            }
        }
    }
}

void paintBadge (juce::Graphics& g, const juce::Path& plate, const std::vector<juce::Point<float>>& screws, float screw,
                 juce::Rectangle<float> mark, float scale, const BadgeLook& look)
{
    const auto box = plate.getBounds();
    g.setGradientFill (juce::ColourGradient (look.plateTop, box.getCentreX(), box.getY(),
                                             look.plateBottom, box.getCentreX(), box.getBottom(), false));
    g.fillPath (plate);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), box.getX(), box.getY(),
                                             juce::Colours::transparentWhite, box.getCentreX(), box.getCentreY(), false));
    g.fillPath (plate);
    wear (g, plate, look);
    g.setColour (juce::Colour (0xff4a4757).withAlpha (0.8f));
    g.strokePath (plate, juce::PathStrokeType (1.2f));

    for (const auto& c : screws)
    {
        const auto head = juce::Rectangle<float> (screw, screw).withCentre (c);
        const float k = screw / 8.0f;      // the slot and the light, as on an 8-unit head
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6d6a78), c.translated (-1.5f * k, -2.0f * k),
                                                 juce::Colour (0xff141318), c.translated (2.5f * k, 3.0f * k), true));
        g.fillEllipse (head);
        g.setColour (juce::Colour (0xff09080c));
        g.drawLine (c.x - 2.2f * k, c.y - 2.2f * k, c.x + 2.2f * k, c.y + 2.2f * k, 1.0f);
    }

    engraveMark (g, mark, scale, look.ink);
}

namespace
{
    juce::Path polygon (const std::vector<juce::Point<float>>& corners)
    {
        juce::Path p;
        if (corners.empty())
            return p;
        p.startNewSubPath (corners.front());
        for (size_t i = 1; i < corners.size(); ++i)
            p.lineTo (corners[i]);
        p.closeSubPath();
        return p;
    }
}

Emblem::Emblem (std::vector<juce::Point<float>> corners, juce::Rectangle<float> markBox)
    : plate (polygon (corners)), mark (markBox)
{
    light.stiffness = 220.0f;
    light.damping   = 30.0f;     // no overshoot: a lamp coming up, not a spring
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool Emblem::hitTest (int x, int y)
{
    return plate.contains ((float) (x + getX()), (float) (y + getY()));
}

void Emblem::aim (float to)
{
    light.target = to;
    startTimerHz (60);
}

void Emblem::timerCallback()
{
    if (! light.step (frameDt))
        stopTimer();
    repaint();
}

void Emblem::mouseUp (const juce::MouseEvent& e)
{
    if (onClick && hitTest (e.getPosition().x, e.getPosition().y))
        onClick();
}

void Emblem::setLamp (juce::Colour c)
{
    lamp = c;
    lit = {};
    litScale = 0.0f;
    repaint();
}

float Emblem::lightSeconds() const noexcept
{
    // The spring stepped as the timer steps it, from dark to full: the time to 95 % of the way. It is the
    // same down: the spring is linear.
    Glide g = light;
    g.jump (0.0f);
    g.target = 1.0f;
    float t = 0.0f;
    while (g.value < 0.95f && t < 5.0f)
    {
        g.step (frameDt);
        t += frameDt;
    }
    return t;
}

juce::Image Emblem::litPicture (float scale) const
{
    if (lamp.isTransparent())
        return {};
    if (lit.isValid() && std::abs (scale - litScale) < 1.0e-3f)
        return lit;

    const auto& cut = MarkCut::at (scale, mark).cut;
    if (! cut.isValid())
        return {};

    const int pad = juce::roundToInt (bloom * scale);
    juce::Image made (juce::Image::ARGB, cut.getWidth() + 2 * pad, cut.getHeight() + 2 * pad, true);
    {
        juce::Graphics g (made);

        // The bloom: the cut's own shape, blurred, in the lamp's colour — the glow the light throws past
        // the glass's edges onto the plate.
        juce::Image shape (juce::Image::ARGB, made.getWidth(), made.getHeight(), true);
        {
            juce::Graphics sg (shape);
            sg.setColour (juce::Colours::white);
            sg.drawImageAt (cut, pad, pad, true);
        }
        juce::DropShadow (lamp.withAlpha (0.55f), juce::jmax (1, juce::roundToInt (0.8f * bloom * scale)), {}).drawForImage (g, shape);

        // The floor, lit through its frosting: the lamp's colour all over it, whiter and brighter at the
        // middle where the lamp is, softer to its edges — the cut's walls, outside the floor, left as they are.
        const auto box = juce::Rectangle<float> ((float) pad, (float) pad, (float) cut.getWidth(), (float) cut.getHeight());
        g.setGradientFill (juce::ColourGradient (lamp.interpolatedWith (juce::Colours::white, 0.45f).withAlpha (0.92f), box.getCentre(),
                                                 lamp.withAlpha (0.55f), { box.getCentreX(), box.getY() - 0.1f * box.getHeight() }, true));
        g.drawImageAt (cut, pad, pad, true);
    }
    lit = made;
    litScale = scale;
    return lit;
}

void Emblem::paint (juce::Graphics& g)
{
    const float l = juce::jlimit (0.0f, 1.0f, light.value);
    if (l < 0.004f)
        return;

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! lamp.isTransparent())
    {
        if (const auto glass = litPicture (scale); glass.isValid())
        {
            g.setOpacity (l);
            g.drawImage (glass, litBox().translated (-(float) getX(), -(float) getY()), juce::RectanglePlacement::stretchToFit);
        }
        return;
    }

    const auto& cut = MarkCut::at (scale, mark);
    if (! cut.cut.isValid())
        return;

    const auto box = mark.translated (-(float) getX(), -(float) getY());
    // Only the colour changes, and not by much: the bare metal takes a violet cast — half the dark
    // violet over the engraving, so the metal still shows through it — and nothing glows.
    g.setColour (juce::Colour (0xff4a36a2).withAlpha (0.5f * l));
    g.drawImage (cut.cut, box, juce::RectanglePlacement::stretchToFit, true);
}

//==============================================================================
Legend::Legend (float size) : px (size)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Legend::set (juce::String n, juce::String v)
{
    if (n == name && v == value)
        return;
    name  = std::move (n);
    value = std::move (v);
    repaint();
}

void Legend::setDot (bool d)
{
    if (d != dot)
    {
        dot = d;
        repaint();
    }
}

namespace
{
    constexpr float legendGap = 14.0f, legendDot = 7.0f, legendDotGap = 9.0f;
}

Legend::Row Legend::row() const
{
    Row r { upper ? name.toUpperCase() : name, upper ? value.toUpperCase() : value,
            labelFont (px).withExtraKerningFactor (0.08f), labelFont (px).withExtraKerningFactor (0.03f), 0.0f, 0.0f, 0.0f, 0.0f };
    r.nameWidth  = r.name.isEmpty()  ? 0.0f : juce::GlyphArrangement::getStringWidth (r.nameFont, r.name);
    r.valueWidth = r.value.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (r.valueFont, r.value);
    r.gap   = r.nameWidth > 0.0f && r.valueWidth > 0.0f ? legendGap : 0.0f;
    r.width = r.nameWidth + r.gap + r.valueWidth + (dot ? legendDotGap + legendDot : 0.0f);
    return r;
}

float Legend::idealWidth() const { return row().width; }

void Legend::flash()
{
    flashUntil = juce::Time::getMillisecondCounterHiRes() + 110.0;
    repaint();
    juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<Legend> (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

void Legend::paint (juce::Graphics& g)
{
    const bool lit = isMouseOverOrDragging();
    const bool flashing = juce::Time::getMillisecondCounterHiRes() < flashUntil;
    const auto r = row();

    // Under the pointer a word lifts: on a dark panel it brightens; cut into bright metal it deepens,
    // and its edge catches more of the light.
    const bool cut = ! edgeColour.isTransparent();
    const auto shown = [lit, flashing, cut] (juce::Colour c)
    {
        return flashing ? c.interpolatedWith (juce::Colours::white, 0.85f) : ! lit ? c : cut ? c.darker (0.6f) : c.brighter (0.5f);
    };
    const auto edge = lit ? edgeColour.withMultipliedAlpha (1.6f) : edgeColour;
    const auto write = [this, &g, cut, edge] (const juce::String& text, const juce::Font& font, juce::Rectangle<float> box, juce::Colour c)
    {
        if (! shadeColour.isTransparent())
        {
            g.setColour (shadeColour.withMultipliedAlpha (c.getFloatAlpha()));
            writeWords (g, text, font, box.translated (0.0f, -Ink::edgeDrop), weight);
        }
        if (cut)
        {
            g.setColour (edge.withMultipliedAlpha (c.getFloatAlpha()));
            writeWords (g, text, font, box.translated (0.0f, Ink::edgeDrop), weight);
        }
        g.setColour (c);
        writeWords (g, text, font, box, weight);
    };

    const juce::Graphics::ScopedSaveState keep (g);
    if (fit && r.width > (float) getWidth())
        g.addTransform (juce::AffineTransform::scale ((float) getWidth() / r.width, 1.0f, fromLeft ? 0.0f : (float) getWidth() * 0.5f, 0.0f));
    auto area = fromLeft ? getLocalBounds().toFloat().withWidth (r.width)
                         : getLocalBounds().toFloat().withSizeKeepingCentre (r.width, (float) getHeight());
    if (r.nameWidth > 0.0f)
    {
        write (r.name, r.nameFont, area.removeFromLeft (r.nameWidth).expanded (2.0f, 0.0f), shown (nameColour));
        area.removeFromLeft (r.gap);
    }
    if (r.valueWidth > 0.0f)
        write (r.value, r.valueFont, area.removeFromLeft (r.valueWidth).expanded (2.0f, 0.0f), shown (valueColour));
    if (dot)
    {
        juce::Path p;
        p.addEllipse (juce::Rectangle<float> (legendDot, legendDot).withCentre ({ area.getX() + legendDotGap + legendDot * 0.5f, area.getCentreY() }));
        glow (g, p, orange, 5.0f);
        g.setColour (orange);
        g.fillPath (p);
    }
}

void Legend::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    lastY = e.getScreenY();
}

void Legend::mouseDrag (const juce::MouseEvent& e)
{
    if (! onDrag)
        return;
    if (! dragging && std::abs (e.getDistanceFromDragStartY()) < 4)
        return;                                   // not a drag yet — it may still be a click
    if (! dragging)
    {
        dragging = true;
        lastY = e.getScreenY();
        if (onDragStart) onDragStart();
    }
    const int y = e.getScreenY();
    if (y != lastY)
        onDrag ((float) (lastY - y));
    lastY = y;
}

void Legend::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        if (onDragEnd) onDragEnd();
    }
    else if (onClick && getLocalBounds().contains (e.getPosition()))
        onClick();
    repaint();
}

void Legend::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! onDrag || juce::approximatelyEqual (w.deltaY, 0.0f))
    {
        juce::Component::mouseWheelMove (e, w);
        return;
    }
    if (onDragStart) onDragStart();
    onDrag ((w.isReversed ? -w.deltaY : w.deltaY) * 40.0f);
    if (onDragEnd) onDragEnd();
}

//==============================================================================
PlateSlider::PlateSlider (const juce::Image& c, juce::Colour a, juce::Colour b, int n) : cap (c), from (a), to (b), steps (n)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
}

int PlateSlider::getStep() const noexcept
{
    return steps > 1 ? juce::roundToInt (value * (float) (steps - 1)) : 0;
}

void PlateSlider::setStep (int i, bool notify)
{
    if (steps > 1)
    {
        const int before = getStep();
        setValue ((float) juce::jlimit (0, steps - 1, i) / (float) (steps - 1), false);
        if (notify && onStep && getStep() != before)
            onStep (getStep());
    }
}

void PlateSlider::setSteps (int count)
{
    if (count == steps)
        return;

    const int at = getStep();
    steps = count;
    value = steps > 1 ? (float) juce::jlimit (0, steps - 1, at) / (float) (steps - 1) : 0.0f;
    repaint();
}

void PlateSlider::moveTo (float v)
{
    if (steps > 1)
        setStep (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) (steps - 1)), true);
    else
        setValue (v, true);
}

juce::Range<float> PlateSlider::travel() const
{
    return { 26.0f, (float) getWidth() - 26.0f };
}

void PlateSlider::setValue (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) < 1.0e-6f)
        return;
    value = v;
    repaint();
    if (notify && onChange)
        onChange (value);
}

void PlateSlider::paint (juce::Graphics& g)
{
    const float cy = (float) getHeight() * 0.5f;
    const auto t = travel();
    const float x = t.getStart() + t.getLength() * value;

    g.setColour (juce::Colour (0xff020205));
    g.fillRoundedRectangle (juce::Rectangle<float> (t.getStart() - 6.0f, cy - 3.5f, t.getLength() + 12.0f, 7.0f), 3.5f);

    if (x > t.getStart() + 1.0f)
    {
        juce::Path line;
        line.addRoundedRectangle (juce::Rectangle<float> (t.getStart() - 4.0f, cy - 1.8f, x - t.getStart() + 4.0f, 3.6f), 1.8f);
        glow (g, line, to.withAlpha (0.9f), 7.0f);
        g.setGradientFill (juce::ColourGradient (from, t.getStart(), cy, to, x, cy, false));
        g.fillPath (line);
    }

    // A selector shows where it can stop: a notch under each position, the current one lit.
    if (steps > 1)
        for (int i = 0; i < steps; ++i)
        {
            const float nx = t.getStart() + t.getLength() * (float) i / (float) (steps - 1);
            g.setColour (i == getStep() ? to : lilac.withAlpha (0.45f));
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 9.0f).withCentre ({ nx, cy + 15.0f }), 1.5f);
        }

    const float length = capLength > 0.0f ? capLength : (float) getHeight() - 4.0f;
    drawCap (g, cap, { x, cy }, length, capAngle);
    if (isMouseOverOrDragging (true))
        rimCap (g, cap, { x, cy }, length, capAngle, handLight);
}

void PlateSlider::mouseDown (const juce::MouseEvent& e)
{
    sliding = ! (e.mods.isPopupMenu() && onMenu);
    if (! sliding)
    {
        onMenu();
        return;
    }
    const auto t = travel();
    const float x = t.getStart() + t.getLength() * value;
    if (onGestureStart) onGestureStart();
    if (std::abs (e.position.x - x) > 16.0f)
        moveTo ((e.position.x - t.getStart()) / t.getLength());
    dragStartValue = value;
}

void PlateSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (! sliding)
        return;
    moveTo (dragStartValue + (float) e.getDistanceFromDragStartX() / travel().getLength());
}

void PlateSlider::mouseUp (const juce::MouseEvent& e)
{
    if (sliding && onGestureEnd) onGestureEnd();
    if (onHover && ! getLocalBounds().contains (e.getPosition()))
        onHover (false);
    repaint();      // let go away from it, the hand's light goes
}

void PlateSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (steps > 1)
        return;
    if (onGestureStart) onGestureStart();
    setValue (defaultValue, true);
    if (onGestureEnd) onGestureEnd();
}

//==============================================================================
ColumnFader::ColumnFader (const juce::Image& c) : cap (c)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Range<float> ColumnFader::travel() const
{
    const float end = 26.0f * scale;
    return { end, runLength() - end };
}

void ColumnFader::setAcross (bool a)
{
    across = a;
    setMouseCursor (a ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor);
    repaint();
}

void ColumnFader::setValue (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) < 1.0e-6f)
        return;
    value = v;
    repaint();
    if (notify && onChange)
        onChange (value);
}

void ColumnFader::paint (juce::Graphics& g)
{
    // Lying across, it is drawn upright and turned a quarter to the right: its head to the right.
    const juce::Graphics::ScopedSaveState keep (g);
    if (across)
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi).translated ((float) getWidth(), 0.0f));

    const float cx = crossWidth() * 0.5f;
    const auto t = travel();

    const float k = scale;
    g.setColour (juce::Colour (0xff020205));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - 3.0f * k, t.getStart() - 8.0f * k, 6.0f * k, t.getLength() + 16.0f * k), 3.0f * k);

    // A scale down the right edge, every tenth unless told otherwise; one of them (unity) longer.
    for (int i = 0; i <= marks; ++i)
    {
        const float y = t.getEnd() - t.getLength() * (float) i / (float) marks;
        const float len = (i == longMark ? 12.0f : 7.0f) * k;
        g.setColour (orange.withAlpha (i == longMark ? 0.95f : 0.6f));
        g.fillRect (juce::Rectangle<float> (crossWidth() - 6.0f * k - len, y - 0.8f, len, 1.6f));
    }

    const float y = t.getEnd() - t.getLength() * value;
    const float length = capLength > 0.0f ? capLength : crossWidth() - 8.0f;
    drawCap (g, cap, { cx, y }, length, 0.0f);
    if (isMouseOverOrDragging (true))
        rimCap (g, cap, { cx, y }, length, 0.0f, handLight);
}

void ColumnFader::mouseDown (const juce::MouseEvent& e)
{
    sliding = ! (e.mods.isPopupMenu() && onMenu);
    if (! sliding)
    {
        onMenu();
        return;
    }
    const auto t = travel();
    const float y = t.getEnd() - t.getLength() * value;
    if (onGestureStart) onGestureStart();
    if (std::abs (along (e.position) - y) > 16.0f)
        setValue ((t.getEnd() - along (e.position)) / t.getLength(), true);
    dragStartValue = value;
}

void ColumnFader::mouseDrag (const juce::MouseEvent& e)
{
    if (! sliding)
        return;
    const float moved = across ? (float) -e.getDistanceFromDragStartX() : (float) e.getDistanceFromDragStartY();
    setValue (dragStartValue - moved / travel().getLength(), true);
}

void ColumnFader::mouseUp (const juce::MouseEvent&)
{
    if (sliding && onGestureEnd) onGestureEnd();
    repaint();      // let go away from it, the hand's light goes
}

void ColumnFader::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onGestureStart) onGestureStart();
    setValue (defaultValue, true);
    if (onGestureEnd) onGestureEnd();
}

//==============================================================================
SegmentMeter::SegmentMeter (std::function<float()> r) : readDb (std::move (r))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void SegmentMeter::timerCallback()
{
    const float db = readDb ? readDb() : -90.0f;
    const float next = juce::jmax (db, shown - 1.3f);     // falls ~40 dB/s, rises at once
    if (std::abs (next - shown) > 0.05f)
    {
        shown = next;
        repaint();
    }
}

Lamp::Lamp (const juce::Image& l, std::function<float()> lv, double hold, double fade)
    : lit (l), level (std::move (lv)), holdMs (hold), fadeMs (juce::jmax (1.0, fade))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Lamp::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float in = juce::jlimit (0.0f, 1.0f, level ? level() : 0.0f);
    if (in >= held || now - heldAt > holdMs + fadeMs)
    {
        held = in;
        heldAt = now;
    }

    const double age = now - heldAt;
    const float fading = age <= holdMs ? held : held * (float) juce::jmax (0.0, 1.0 - (age - holdMs) / fadeMs);
    const float next = juce::jmax (in, fading);
    if (std::abs (next - shown) > 0.01f || (next == 0.0f) != (shown == 0.0f))
    {
        shown = next;
        repaint();
    }
}

void Lamp::paint (juce::Graphics& g)
{
    if (! lit.isValid() && dot > 0.0f)
    {
        // Out, a dark glass in the panel with a faint rim of light; lit, a light as strong as it burns,
        // and its glow round it.
        const auto glass = juce::Rectangle<float> (dot, dot).withCentre (getLocalBounds().toFloat().getCentre());
        g.setColour (juce::Colour (0xff070709));
        g.fillEllipse (glass);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawEllipse (glass.reduced (0.6f), 0.9f);
        if (shown > 0.0f)
        {
            juce::Path burning;
            burning.addEllipse (glass.reduced (dot * 0.12f));
            glow (g, burning, light.withAlpha (shown), dot * 0.9f);
            g.setColour (light.withAlpha (shown));
            g.fillPath (burning);
        }
        return;
    }
    if (shown <= 0.0f || ! lit.isValid())
        return;
    g.setOpacity (shown);
    g.drawImage (lit, getLocalBounds().toFloat());
}

void SegmentMeter::setDots (const Dots& d)
{
    dots = d;
    repaint();
}

void SegmentMeter::paintDots (juce::Graphics& g, float level)
{
    // The foot's dot lights over the scale's floor and each one over it a pitch further up the scale; the
    // top ones take the segments' orange and red. Out, a dot is what the picture painted.
    const auto area = getLocalBounds().toFloat();
    const float pitch = area.getHeight() / (float) dots.count;
    for (int i = 0; i < dots.count && (float) i / (float) dots.count < level; ++i)
    {
        const float top = (float) (i + 1) / (float) dots.count;
        const juce::Colour hue = top > 0.94f ? juce::Colour (0xffff4646) : top > 0.8f ? orange : violet;
        juce::Path lamp;
        lamp.addEllipse (juce::Rectangle<float> (dots.size, dots.size)
                             .withCentre ({ area.getCentreX(), area.getBottom() - pitch * ((float) i + 0.5f) }));
        glow (g, lamp, hue, dots.size * 0.6f);
        g.setColour (hue);
        g.fillPath (lamp);
    }
}

void SegmentMeter::paint (juce::Graphics& g)
{
    constexpr int segments = 26;
    constexpr float floorDb = -60.0f, topDb = 6.0f;

    if (dots.count > 0)
    {
        paintDots (g, juce::jlimit (0.0f, 1.0f, (shown - floorDb) / (topDb - floorDb)));
        return;
    }

    const auto area = getLocalBounds().toFloat().reduced (12.0f, 14.0f);
    const float step = area.getHeight() / (float) segments;
    const float level = juce::jlimit (0.0f, 1.0f, (shown - floorDb) / (topDb - floorDb));

    for (int i = 0; i < segments; ++i)
    {
        const float at = ((float) i + 0.5f) / (float) segments;
        const auto seg = juce::Rectangle<float> (area.getX(), area.getBottom() - step * (float) (i + 1) + 1.5f,
                                                 area.getWidth(), step - 3.0f);
        const juce::Colour hue = at > 0.94f ? juce::Colour (0xffff4646) : at > 0.8f ? orange : violet;
        g.setColour (at <= level ? hue : hue.withAlpha (0.10f));
        g.fillRoundedRectangle (seg, 1.5f);
    }
}

//==============================================================================
TunerFace::TunerFace (const core::TunerEar& e) : ear (e)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void TunerFace::setState (bool isListening, bool isMuting)
{
    if (listening == isListening && muting == isMuting)
        return;
    listening = isListening;
    muting    = isMuting;
    repaint();
}

void TunerFace::setStrip (bool s)
{
    strip = s;
    repaint();
}

void TunerFace::setDial (const Dial& d)
{
    dial = d;
    repaint();
}

void TunerFace::paintDial (juce::Graphics& g)
{
    const auto origin = getPosition().toFloat();
    const auto pivot  = dial.pivot - origin;
    const auto window = dial.window - origin;
    const float hub   = dial.reach * 0.07f;
    const bool live   = listening && ear.live();
    const auto note   = ear.nearestNote();
    g.setFont (labelFont (stage::px13));

    // The note in the window, a dash while nothing is heard.
    if (live)
    {
        g.setColour (ear.green() ? home : heard);
        g.drawText (juce::String (core::PitchTracker::noteName (note.midi)) + juce::String (core::PitchTracker::noteOctave (note.midi)),
                    window, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (lilac.withAlpha (idleAlpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 3.0f).withCentre (window.getCentre()), 1.5f);
    }

    // Its state under the pivot — ON, OFF, or MUTE — and over it what its A is tuned to, when that is
    // not the 440 everyone assumes.
    g.setColour (! listening ? violet : orange);
    g.drawText (! listening ? "OFF" : muting ? "MUTE" : "ON",
                juce::Rectangle<float> (window.getWidth() * 2.0f, window.getY() - pivot.y - hub)
                    .withPosition (pivot.x - window.getWidth(), pivot.y + hub),
                juce::Justification::centred, false);
    if (const int a4 = juce::roundToInt (ear.referenceHz()); a4 != juce::roundToInt (core::PitchTracker::concertA))
    {
        g.setColour (lilac.withAlpha (quietAlpha));
        g.drawText ("A " + juce::String (a4), juce::Rectangle<float> (window.getWidth() * 2.0f, window.getHeight())
                                                  .withCentre ({ pivot.x, pivot.y - dial.reach * 0.45f }),
                    juce::Justification::centred, false);
    }

    // The needle, while a string is heard: green when it is home, warming to orange by 25 cents out,
    // tapering from the pivot to the ticks, with a short tail behind it.
    if (live)
    {
        const auto colour = ear.green() ? home
                                        : home.interpolatedWith (orange, juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / warmCents));
        const float tail = dial.reach * 0.14f;
        juce::Path needle;
        needle.startNewSubPath (-1.6f, tail);
        needle.lineTo (-0.6f, -dial.reach);
        needle.lineTo (0.6f, -dial.reach);
        needle.lineTo (1.6f, tail);
        needle.closeSubPath();
        needle.applyTransform (juce::AffineTransform::rotation (dial.sweep * juce::jlimit (-50.0f, 50.0f, ear.needle()) / 50.0f)
                                   .translated (pivot));
        glow (g, needle, colour, 6.0f);
        g.setColour (colour);
        g.fillPath (needle);
    }

    // The hub it turns on, over it: a dark boss with the light catching its upper edge.
    const auto boss = juce::Rectangle<float> (hub * 2.0f, hub * 2.0f).withCentre (pivot);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a52), boss.getCentreX(), boss.getY(),
                                             juce::Colour (0xff101013), boss.getCentreX(), boss.getBottom(), false));
    g.fillEllipse (boss);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (boss, 0.8f);
}

void TunerFace::paintStrip (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (10.0f, 1.0f);
    const bool live = listening && ear.live();
    const auto note = ear.nearestNote();
    g.setFont (labelFont (stage::px13));

    // Its state first — ON, OFF, or MUTE — then the note, a dash while nothing is heard.
    g.setColour (! listening ? violet : orange);
    g.drawText (! listening ? "OFF" : muting ? "MUTE" : "ON", area.removeFromLeft (64.0f), juce::Justification::centredLeft, false);
    auto noteArea = area.removeFromLeft (54.0f);
    if (live)
    {
        g.setColour (ear.green() ? home : heard);
        g.drawText (juce::String (core::PitchTracker::noteName (note.midi)) + juce::String (core::PitchTracker::noteOctave (note.midi)),
                    noteArea, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (lilac.withAlpha (idleAlpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 3.0f).withCentre ({ noteArea.getX() + 12.0f, noteArea.getCentreY() }), 1.5f);
    }

    // The ruler: a tick every five cents, the quarters taller, the centre orange; the needle over it.
    area.removeFromLeft (6.0f);
    const auto ruler = area;
    const auto xOf = [&ruler] (float c) { return ruler.getX() + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, c) + 50.0f) / 100.0f; };
    for (int c = -50; c <= 50; c += 5)
    {
        const bool major = c % 25 == 0;
        const float h = ruler.getHeight() * (major ? 0.8f : 0.4f);
        g.setColour ((c == 0 ? orange : violet).withAlpha (major ? majorAlpha : minorAlpha));
        g.fillRect (juce::Rectangle<float> (xOf ((float) c) - 0.75f, ruler.getCentreY() - h * 0.5f, 1.5f, h));
    }
    if (live)
    {
        const auto colour = ear.green() ? home
                                        : home.interpolatedWith (orange, juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / warmCents));
        juce::Path needle;
        needle.addRoundedRectangle (juce::Rectangle<float> (3.0f, ruler.getHeight()).withCentre ({ xOf (ear.needle()), ruler.getCentreY() }), 1.5f);
        glow (g, needle, colour, 5.0f);
        g.setColour (colour);
        g.fillPath (needle);
    }
}

void TunerFace::paint (juce::Graphics& g)
{
    if (dial.reach > 0.0f)
    {
        paintDial (g);
        return;
    }
    if (strip)
    {
        paintStrip (g);
        return;
    }

    auto area = getLocalBounds().toFloat().reduced (18.0f, 10.0f);
    const bool live = listening && ear.live();      // switched off, the ear keeps the last note it heard
    const auto note = ear.nearestNote();

    // The note: name big, octave small beside it; a dash while nothing is heard.
    auto noteArea = area.removeFromLeft (96.0f);

    // What it measures from, over the note — said only when it is not the 440 everyone assumes.
    if (const int a4 = juce::roundToInt (ear.referenceHz()); a4 != juce::roundToInt (core::PitchTracker::concertA))
    {
        g.setColour (lilac.withAlpha (quietAlpha));
        g.setFont (labelFont (stage::px13));
        g.drawText ("A " + juce::String (a4), noteArea.withHeight (20.0f), juce::Justification::centredLeft, false);
    }

    // Its state, always, in the corner under the note and on the numbers' line: on, off, or muting the amp.
    g.setColour (! listening ? violet : orange);
    g.setFont (labelFont (stage::px13));
    g.drawText (! listening ? "OFF" : muting ? "MUTE" : "ON", noteArea.withTop (noteArea.getBottom() - 24.0f),
                juce::Justification::centredLeft, false);
    if (live)
    {
        const auto name = juce::String (core::PitchTracker::noteName (note.midi));
        g.setColour (ear.green() ? home : heard);
        g.setFont (labelFont (52.0f));
        g.drawText (name, noteArea.withTrimmedRight (26.0f), juce::Justification::centredRight, false);
        g.setColour (lilac.withAlpha (quietAlpha));
        g.setFont (labelFont (stage::px13));
        g.drawText (juce::String (core::PitchTracker::noteOctave (note.midi)),
                    noteArea.removeFromRight (24.0f).withTrimmedTop (noteArea.getHeight() * 0.42f),
                    juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (lilac.withAlpha (idleAlpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (30.0f, 5.0f).withCentre (noteArea.getCentre()), 2.5f);
    }

    // The ruler: a tick every five cents, the quarters long, the centre orange; numbers under it — the
    // ruler stops short of the right edge so "+50" stays on the glass.
    area.removeFromLeft (8.0f);
    area.removeFromRight (18.0f);
    auto numbers = area.removeFromBottom (24.0f);
    const auto ruler = area;
    const auto xOf = [&ruler] (float c) { return ruler.getX() + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, c) + 50.0f) / 100.0f; };
    for (int c = -50; c <= 50; c += 5)
    {
        const bool major = c % 25 == 0;
        const float h = major ? ruler.getHeight() * 0.8f : ruler.getHeight() * 0.45f;
        g.setColour ((c == 0 ? orange : violet).withAlpha (major ? majorAlpha : minorAlpha));
        g.fillRect (juce::Rectangle<float> (xOf ((float) c) - 1.0f, ruler.getCentreY() - h * 0.5f, 2.0f, h));
    }
    for (int c = -50; c <= 50; c += 25)
    {
        g.setColour (lilac.withAlpha (numberAlpha));
        g.setFont (labelFont (stage::px13));
        g.drawText ((c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (60.0f, numbers.getHeight())
                        .withCentre ({ xOf ((float) c), numbers.getCentreY() }), juce::Justification::centred, false);
    }

    if (live)
    {
        const float x = xOf (ear.needle());
        const auto colour = ear.green() ? home
                                        : home.interpolatedWith (orange, juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / warmCents));
        juce::Path needle;
        needle.addRoundedRectangle (juce::Rectangle<float> (4.0f, ruler.getHeight()).withCentre ({ x, ruler.getCentreY() }), 2.0f);
        glow (g, needle, colour, 8.0f);
        g.setColour (colour);
        g.fillPath (needle);
    }
}

//==============================================================================
juce::Font panel::textFont (float px) { return labelFont (px).withExtraKerningFactor (tracking); }

void panel::write (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float px, juce::Colour colour,
                   juce::Justification where)
{
    g.setColour (colour);
    g.setFont (textFont (px));
    g.drawText (text.toUpperCase(), area, where, true);
}

panel::WordKey::WordKey (juce::String w) : word (std::move (w))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void panel::WordKey::flash (const juce::String& text)
{
    said = text;
    startTimer (1400);
    repaint();
}

void panel::WordKey::timerCallback()
{
    stopTimer();
    said.clear();
    repaint();
}

void panel::WordKey::setLit (bool on)
{
    if (lit != on)
    {
        lit = on;
        repaint();
    }
}

void panel::WordKey::enablementChanged()
{
    setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void panel::WordKey::paint (juce::Graphics& g)
{
    const auto face = getLocalBounds().toFloat().reduced (0.6f);
    const bool live = isEnabled();
    const bool over = live && isMouseOver();
    if (over)
    {
        g.setColour (violet.withAlpha (0.18f));
        g.fillRoundedRectangle (face, 5.0f);
    }
    g.setColour (lit ? orange.withAlpha (over ? 1.0f : 0.85f) : violet.withAlpha (over ? 0.9f : live ? 0.55f : 0.22f));
    g.drawRoundedRectangle (face, 5.0f, 1.2f);
    write (g, said.isNotEmpty() ? said : word, getLocalBounds(), textPx,
           said.isNotEmpty() || lit ? orange : over ? ink : live ? dim : faint.withMultipliedAlpha (0.7f),
           juce::Justification::centred);
}

void panel::WordKey::mouseUp (const juce::MouseEvent& e)
{
    if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

} // namespace orbitamp::face
