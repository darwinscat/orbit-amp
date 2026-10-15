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

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float em,
                juce::Colour colour, float trackingEm)
{
    g.setColour (colour);
    g.setFont (labelFont (em).withExtraKerningFactor (trackingEm));
    g.drawText (text.toUpperCase(), area, juce::Justification::centred, false);
}

void Ink::label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float em,
                 juce::Colour colour, float trackingEm) const
{
    if (! edge.isTransparent())
        drawLabel (g, text, area.translated (0.0f, edgeDrop), em, under (colour), trackingEm);
    drawLabel (g, text, area, em, colour, trackingEm);
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

    // Drawn for a 40-px box — an overhead hole — and scaled to whatever box it is given.
    const float u = (float) juce::jmin (getWidth(), getHeight()) / 40.0f;
    const auto c = getLocalBounds().toFloat().getCentre();
    const float d = juce::jlimit (0.0f, 1.2f, depth.value);
    const float l = juce::jlimit (0.0f, 1.0f, light.value);

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

void engraveMark (juce::Graphics& g, juce::Rectangle<float> box, float scale)
{
    const auto& cut = MarkCut::at (scale, box).cut;
    if (! cut.isValid())
        return;

    const float px = 1.0f / juce::jmax (1.0f, scale) * 1.6f;   // the wall's depth, about a pixel and a half
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawImage (cut, box.translated (-0.5f * px, -px), juce::RectanglePlacement::stretchToFit, true);
    g.setColour (juce::Colour (0xffd8d2ee).withAlpha (0.28f));
    g.drawImage (cut, box.translated (0.4f * px, px), juce::RectanglePlacement::stretchToFit, true);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8698), box.getCentreX(), box.getY(),
                                             juce::Colour (0xff4b4858), box.getCentreX(), box.getBottom(), false));
    g.drawImage (cut, box, juce::RectanglePlacement::stretchToFit, true);
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

void Emblem::paint (juce::Graphics& g)
{
    const float l = juce::jlimit (0.0f, 1.0f, light.value);
    if (l < 0.004f)
        return;

    const auto& cut = MarkCut::at (g.getInternalContext().getPhysicalPixelScaleFactor(), mark);
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
    const auto write = [&g, cut, edge] (const juce::String& text, const juce::Font& font, juce::Rectangle<float> box, juce::Colour c)
    {
        g.setFont (font);
        if (cut)
        {
            g.setColour (edge.withMultipliedAlpha (c.getFloatAlpha()));
            g.drawText (text, box.translated (0.0f, Ink::edgeDrop), juce::Justification::centred, false);
        }
        g.setColour (c);
        g.drawText (text, box, juce::Justification::centred, false);
    };

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
    return { end, (float) getHeight() - end };
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
    const float cx = (float) getWidth() * 0.5f;
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
        g.fillRect (juce::Rectangle<float> ((float) getWidth() - 6.0f * k - len, y - 0.8f, len, 1.6f));
    }

    const float y = t.getEnd() - t.getLength() * value;
    const float length = capLength > 0.0f ? capLength : (float) getWidth() - 8.0f;
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
    if (std::abs (e.position.y - y) > 16.0f)
        setValue ((t.getEnd() - e.position.y) / t.getLength(), true);
    dragStartValue = value;
}

void ColumnFader::mouseDrag (const juce::MouseEvent& e)
{
    if (! sliding)
        return;
    setValue (dragStartValue - (float) e.getDistanceFromDragStartY() / travel().getLength(), true);
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
    if (shown <= 0.0f || ! lit.isValid())
        return;
    g.setOpacity (shown);
    g.drawImage (lit, getLocalBounds().toFloat());
}

void SegmentMeter::paint (juce::Graphics& g)
{
    constexpr int segments = 26;
    constexpr float floorDb = -60.0f, topDb = 6.0f;

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

void TunerFace::paintStrip (juce::Graphics& g)
{
    const juce::Colour home { 0xff5fc97a };   // the family's in-tune green

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
        g.setColour (ear.green() ? home : juce::Colour (0xffece9f6));
        g.drawText (juce::String (core::PitchTracker::noteName (note.midi)) + juce::String (core::PitchTracker::noteOctave (note.midi)),
                    noteArea, juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (lilac.withAlpha (0.35f));
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
        g.setColour ((c == 0 ? orange : violet).withAlpha (major ? 0.9f : 0.4f));
        g.fillRect (juce::Rectangle<float> (xOf ((float) c) - 0.75f, ruler.getCentreY() - h * 0.5f, 1.5f, h));
    }
    if (live)
    {
        const auto colour = ear.green() ? home
                                        : home.interpolatedWith (orange, juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / 25.0f));
        juce::Path needle;
        needle.addRoundedRectangle (juce::Rectangle<float> (3.0f, ruler.getHeight()).withCentre ({ xOf (ear.needle()), ruler.getCentreY() }), 1.5f);
        glow (g, needle, colour, 5.0f);
        g.setColour (colour);
        g.fillPath (needle);
    }
}

void TunerFace::paint (juce::Graphics& g)
{
    if (strip)
    {
        paintStrip (g);
        return;
    }

    const juce::Colour home { 0xff5fc97a };   // the family's in-tune green

    auto area = getLocalBounds().toFloat().reduced (18.0f, 10.0f);
    const bool live = listening && ear.live();      // switched off, the ear keeps the last note it heard
    const auto note = ear.nearestNote();

    // The note: name big, octave small beside it; a dash while nothing is heard.
    auto noteArea = area.removeFromLeft (96.0f);

    // What it measures from, over the note — said only when it is not the 440 everyone assumes.
    if (const int a4 = juce::roundToInt (ear.referenceHz()); a4 != juce::roundToInt (core::PitchTracker::concertA))
    {
        g.setColour (lilac.withAlpha (0.8f));
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
        g.setColour (ear.green() ? home : juce::Colour (0xffece9f6));
        g.setFont (labelFont (52.0f));
        g.drawText (name, noteArea.withTrimmedRight (26.0f), juce::Justification::centredRight, false);
        g.setColour (lilac.withAlpha (0.8f));
        g.setFont (labelFont (stage::px13));
        g.drawText (juce::String (core::PitchTracker::noteOctave (note.midi)),
                    noteArea.removeFromRight (24.0f).withTrimmedTop (noteArea.getHeight() * 0.42f),
                    juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (lilac.withAlpha (0.35f));
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
        g.setColour ((c == 0 ? orange : violet).withAlpha (major ? 0.9f : 0.4f));
        g.fillRect (juce::Rectangle<float> (xOf ((float) c) - 1.0f, ruler.getCentreY() - h * 0.5f, 2.0f, h));
    }
    for (int c = -50; c <= 50; c += 25)
    {
        g.setColour (lilac.withAlpha (0.6f));
        g.setFont (labelFont (stage::px13));
        g.drawText ((c > 0 ? "+" : "") + juce::String (c), juce::Rectangle<float> (60.0f, numbers.getHeight())
                        .withCentre ({ xOf ((float) c), numbers.getCentreY() }), juce::Justification::centred, false);
    }

    if (live)
    {
        const float x = xOf (ear.needle());
        const auto colour = ear.green() ? home
                                        : home.interpolatedWith (orange, juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / 25.0f));
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
