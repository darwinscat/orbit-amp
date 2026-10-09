// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitControls.h"
#include "CatpitLayout.h"

#include <felitronics/appkit/BrandAssets.h>

#include <vector>

namespace orbitamp::catpit
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

namespace
{
    constexpr float frameDt = 1.0f / 60.0f;

    /** A soft glow under a path — the light the renders give their lit parts. */
    void glow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius)
    {
        juce::DropShadow (c, juce::jmax (1, juce::roundToInt (radius)), {}).drawForPath (g, p);
    }

    /** Draws the slider cap render centred on `centre`, its long axis `length` long, turned by `angle`. */
    void drawCap (juce::Graphics& g, juce::Point<float> centre, float length, float angle)
    {
        const auto& img = Art::get().sliderCap;
        if (! img.isValid())
        {
            g.setColour (juce::Colours::grey);
            g.fillRoundedRectangle (juce::Rectangle<float> (length, length * 0.4f).withCentre (centre), 4.0f);
            return;
        }
        static const auto src = opaqueBounds (img);
        const float k = length / src.getWidth();
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
    v = juce::jlimit (0.0f, 1.0f, v);
    if (std::abs (v - value) < 1.0e-6f)
        return;
    value = v;
    repaint();
    if (notify && onChange)
        onChange (value);
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
    const float angle = juce::degreesToRadians (-135.0f + 270.0f * value);
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
    dragStartValue = value;
    dragStartY = e.getScreenY();
    if (onGestureStart) onGestureStart();
}

void ArtKnob::mouseDrag (const juce::MouseEvent& e)
{
    const float pixelsForFullTurn = e.mods.isShiftDown() ? 1400.0f : 280.0f;
    setValue (dragStartValue + (float) (dragStartY - e.getScreenY()) / pixelsForFullTurn, true);
}

void ArtKnob::mouseUp (const juce::MouseEvent&)
{
    if (onGestureEnd) onGestureEnd();
}

void ArtKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onGestureStart) onGestureStart();
    setValue (defaultValue, true);
    if (onGestureEnd) onGestureEnd();
}

void ArtKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (onGestureStart) onGestureStart();
    setValue (value + w.deltaY * (w.isReversed ? -0.12f : 0.12f), true);
    if (onGestureEnd) onGestureEnd();
}

//==============================================================================
// The lever's own space: the 261 x 217 AMP MODE frame plus `overhang` to its left, where the grip
// reaches past the frame. The slot on the left, the four names on the right, a lamp beside each.
namespace
{
    constexpr float slotX      = (float) Lever::overhang + 42.0f;
    constexpr float firstY     = 40.0f;
    constexpr float detentStep = 46.0f;
    constexpr float lampX      = (float) Lever::overhang + 92.0f;
    constexpr float namesX     = (float) Lever::overhang + 102.0f;
    constexpr float gripLength = 92.0f;      // end to end; the stem is not in its middle (Art::leverAxis)
}

Lever::Lever()
{
    pos.stiffness = 260.0f;     // heavier than a toggle: it overshoots the notch a hair and comes back
    pos.damping   = 22.0f;
    pos.jump (0.0f);
}

float Lever::detentY (int i) const noexcept { return firstY + detentStep * (float) i; }

float Lever::posFromY (float y) const noexcept
{
    return juce::jlimit (0.0f, (float) (numDetents - 1), (y - firstY) / detentStep);
}

int Lever::labelAt (juce::Point<float> p) const noexcept
{
    if (p.x < lampX - 8.0f)
        return -1;
    return juce::jlimit (0, numDetents - 1, juce::roundToInt (posFromY (p.y)));
}

void Lever::setDetent (int d, bool animate, bool notify)
{
    d = juce::jlimit (0, numDetents - 1, d);
    const bool changed = d != detent;
    detent = d;
    if (animate)
    {
        pos.target = (float) d;
        startTimerHz (60);
    }
    else
    {
        pos.jump ((float) d);
        travelled();
    }
    if (changed && notify && onDetent)
        onDetent (detent);
}

void Lever::travelled()
{
    repaint();
    if (onTravel)
        onTravel (pos.value);
}

void Lever::timerCallback()
{
    if (! pos.step (frameDt))
        stopTimer();
    travelled();
}

void Lever::paint (juce::Graphics& g)
{
    // The slot: a dark cut with a violet lip, the detents marked across it.
    const auto slot = juce::Rectangle<float> (slotX - 9.0f, firstY - 22.0f, 18.0f, detentStep * 3.0f + 44.0f);
    g.setColour (juce::Colour (0xff030306));
    g.fillRoundedRectangle (slot, 9.0f);
    g.setColour (violet.withAlpha (0.55f));
    g.drawRoundedRectangle (slot, 9.0f, 1.4f);
    for (int i = 0; i < numDetents; ++i)
    {
        g.setColour (juce::Colour (0xff2a2836));
        g.fillRect (juce::Rectangle<float> (slotX - 20.0f, detentY (i) - 1.0f, 40.0f, 2.0f));
    }

    // The names, a lamp beside each; the one it sits in lit orange.
    for (int i = 0; i < numDetents; ++i)
    {
        const bool here = i == detent;
        const auto lamp = juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ lampX, detentY (i) });
        if (here)
        {
            juce::Path lp;
            lp.addRoundedRectangle (lamp, 2.0f);
            glow (g, lp, orange, 6.0f);
        }
        g.setColour (here ? orange : juce::Colour (0xff3a2a22));
        g.fillRoundedRectangle (lamp, 2.0f);

        const auto row = juce::Rectangle<float> (namesX, detentY (i) - 14.0f, (float) getWidth() - namesX - 6.0f, 28.0f);
        g.setColour (here ? orange : lilac.withAlpha (0.75f));
        g.setFont (labelFont (layout::px13));
        g.drawText (names[i], row, juce::Justification::centredLeft, false);
    }

    // The grip: pulled toward you it comes a little closer, so it grows as it travels down. Its stem
    // stays in the slot; the bar hangs further to the left of it than to the right, clear of the names.
    const float y = firstY + detentStep * pos.value;
    const float length = gripLength * (1.0f + 0.05f * pos.value);
    const auto& art = Art::get();
    if (art.leverGrip.isValid())
    {
        const float height = length * (float) art.leverGrip.getHeight() / (float) art.leverGrip.getWidth();
        g.setOpacity (1.0f);     // an image is drawn at the brush's opacity, and the last name left it dimmed
        g.drawImage (art.leverGrip, { slotX - art.leverAxis * length, y - height * 0.5f, length, height });
    }
    else
    {
        g.setColour (juce::Colours::darkgrey);
        g.fillRoundedRectangle ({ slotX - length * 0.63f, y - 13.0f, length, 26.0f }, 10.0f);
    }
}

void Lever::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (const int l = labelAt (p); l >= 0)
    {
        setDetent (l, true);
        return;
    }
    held = true;
    stopTimer();
    const float gripY = firstY + detentStep * pos.value;
    grabOffset = std::abs (p.y - gripY) < 30.0f ? gripY - p.y : 0.0f;
    mouseDrag (e);
}

void Lever::mouseDrag (const juce::MouseEvent& e)
{
    if (! held)
        return;

    // The notch: within reach of a detent the grip sits IN it, and the hand has to pull it out.
    const float raw = posFromY (e.position.y + grabOffset);
    const int nearest = juce::roundToInt (raw);
    const float p = std::abs (raw - (float) nearest) < 0.22f ? (float) nearest : raw;

    pos.jump (p);
    if (juce::exactlyEqual (p, (float) nearest) && nearest != detent)
    {
        detent = nearest;
        if (onDetent) onDetent (detent);
    }
    travelled();
}

void Lever::mouseUp (const juce::MouseEvent&)
{
    if (! held)
        return;
    held = false;
    setDetent (juce::roundToInt (pos.value), true);
    if (onRelease)
        onRelease();
}

void Lever::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (d) > 0.0f)
        setDetent (detent + (d < 0.0f ? 1 : -1), true);
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

void PushButton::mouseDown (const juce::MouseEvent& e)
{
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
        g.beginTransparencyLayer (0.3f);

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
GuardButton::GuardButton()
{
    lid.stiffness  = 700.0f;  lid.damping  = 40.0f;
    glow.stiffness = 900.0f;  glow.damping = 60.0f;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> GuardButton::artArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 6.0f);
}

void GuardButton::setOn (bool v, bool notify)
{
    if (v == on)
        return;
    on = v;
    glow.target = on ? 1.0f : 0.0f;
    if (! notify || (on && ! open))     // set from elsewhere — a recalled channel: the guard goes with the switch
    {
        open = on;
        lid.target = on ? 1.0f : 0.0f;
    }
    startTimerHz (60);
    if (notify && onChange)
        onChange (on);
}

void GuardButton::timerCallback()
{
    const bool a = lid.step (frameDt);
    const bool b = glow.step (frameDt);
    if (! a && ! b)
        stopTimer();
    repaint();
}

void GuardButton::mouseDown (const juce::MouseEvent& e)
{
    if (! open)
    {
        open = true;
        lid.target = 1.0f;
        startTimerHz (60);
        return;
    }

    // Open: the lifted lid is the top third of the render; the button is below it.
    const auto a = artArea();
    if (e.position.y < a.getY() + a.getHeight() * 0.36f)
    {
        open = false;
        lid.target = 0.0f;
        setOn (false, true);
        startTimerHz (60);
        return;
    }
    setOn (! on, true);
}

void GuardButton::paint (juce::Graphics& g)
{
    const auto& art = Art::get();
    const auto a = artArea();

    // All three renders share one canvas, so they are fitted by ONE box — the open one's — and the
    // closed lid lands exactly where it is hinged.
    static const auto ref = opaqueBounds (art.guardOpen);
    const auto fit = [&] (const juce::Image& img, float alpha)
    {
        if (! img.isValid() || alpha <= 0.001f || ref.isEmpty())
            return;
        const float k = juce::jmin (a.getWidth() / ref.getWidth(), a.getHeight() / ref.getHeight());
        const auto dst = juce::Rectangle<float> (ref.getWidth() * k, ref.getHeight() * k).withCentre (a.getCentre());
        g.setOpacity (alpha);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-ref.getX(), -ref.getY())
                                         .scaled (k).translated (dst.getX(), dst.getY()));
        g.setOpacity (1.0f);
    };

    const float l = juce::jlimit (0.0f, 1.0f, lid.value);
    const float o = juce::jlimit (0.0f, 1.0f, glow.value);
    fit (art.guardClosed, 1.0f - l);
    fit (art.guardOpen,   l * (1.0f - o));
    fit (art.guardOn,     l * o);
}

//==============================================================================
namespace
{
    /** A soft blur of a mask: three passes of a sliding box each way, which is a Gaussian near enough. */
    juce::Image blurred (const juce::Image& mask, int pad, int radius)
    {
        const int w = mask.getWidth() + 2 * pad, h = mask.getHeight() + 2 * pad;
        std::vector<float> a ((size_t) w * (size_t) h, 0.0f), b (a.size(), 0.0f);
        {
            const juce::Image::BitmapData src (mask, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < mask.getHeight(); ++y)
                for (int x = 0; x < mask.getWidth(); ++x)
                    a[(size_t) (y + pad) * (size_t) w + (size_t) (x + pad)] = src.getPixelColour (x, y).getFloatAlpha();
        }

        const auto pass = [radius] (const float* from, float* to, int count, int stride)
        {
            const float norm = 1.0f / (float) (2 * radius + 1);
            float sum = 0.0f;
            for (int i = -radius; i <= radius; ++i)
                sum += i >= 0 && i < count ? from[i * stride] : 0.0f;
            for (int i = 0; i < count; ++i)
            {
                to[i * stride] = sum * norm;
                const int out = i - radius, in = i + radius + 1;
                sum += (in < count ? from[in * stride] : 0.0f) - (out >= 0 ? from[out * stride] : 0.0f);
            }
        };
        for (int n = 0; n < 3; ++n)
        {
            for (int y = 0; y < h; ++y) pass (a.data() + (size_t) y * (size_t) w, b.data() + (size_t) y * (size_t) w, w, 1);
            for (int x = 0; x < w; ++x) pass (b.data() + x, a.data() + x, h, w);
        }

        juce::Image out (juce::Image::SingleChannel, w, h, true);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                dst.setPixelColour (x, y, juce::Colours::white.withAlpha (juce::jlimit (0.0f, 1.0f, a[(size_t) y * (size_t) w + (size_t) x] * 1.6f)));
        return out;
    }
}

const MarkCut& MarkCut::at (float scale)
{
    static MarkCut made;
    static float madeFor = 0.0f;
    if (made.cut.isValid() && std::abs (scale - madeFor) < 1.0e-3f)
        return made;

    madeFor = scale;
    made = {};
    auto* mark = felitronics::appkit::brand::catMark();
    const auto box = layout::emblemMark;
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
    made.halo = blurred (made.cut, juce::roundToInt (haloPad * scale), juce::jmax (1, juce::roundToInt (2.6f * scale)));
    return made;
}

Emblem::Emblem()
{
    light.stiffness = 220.0f;
    light.damping   = 30.0f;     // no overshoot: a lamp coming up, not a spring
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool Emblem::hitTest (int x, int y)
{
    juce::Path face;
    face.startNewSubPath (layout::emblemFace[0]);
    for (size_t i = 1; i < layout::emblemFace.size(); ++i)
        face.lineTo (layout::emblemFace[i]);
    face.closeSubPath();
    return face.contains ((float) (x + getX()), (float) (y + getY()));
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

    const auto& mark = MarkCut::at (g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! mark.cut.isValid())
        return;

    const auto box = layout::emblemMark.translated (-(float) getX(), -(float) getY());
    g.setColour (orange.withAlpha (0.5f * l));
    g.drawImage (mark.halo, box.expanded (MarkCut::haloPad), juce::RectanglePlacement::stretchToFit, true);
    g.setColour (juce::Colour (0xffffb872).withAlpha (l));
    g.drawImage (mark.cut, box, juce::RectanglePlacement::stretchToFit, true);
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

void Legend::paint (juce::Graphics& g)
{
    const bool lit = isMouseOverOrDragging();
    const auto nameFont  = labelFont (px).withExtraKerningFactor (0.08f);
    const auto valueFont = labelFont (px).withExtraKerningFactor (0.03f);
    const auto n = upper ? name.toUpperCase() : name, v = upper ? value.toUpperCase() : value;

    constexpr float gap = 14.0f, dotSize = 7.0f, dotGap = 9.0f;
    const float nw = n.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (nameFont, n);
    const float vw = v.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (valueFont, v);
    const float between = nw > 0.0f && vw > 0.0f ? gap : 0.0f;
    const float total = nw + between + vw + (dot ? dotGap + dotSize : 0.0f);

    auto row = getLocalBounds().toFloat().withSizeKeepingCentre (total, (float) getHeight());
    if (nw > 0.0f)
    {
        g.setColour (lit ? nameColour.brighter (0.5f) : nameColour);
        g.setFont (nameFont);
        g.drawText (n, row.removeFromLeft (nw).expanded (2.0f, 0.0f), juce::Justification::centred, false);
        row.removeFromLeft (between);
    }
    if (vw > 0.0f)
    {
        g.setColour (lit ? valueColour.brighter (0.5f) : valueColour);
        g.setFont (valueFont);
        g.drawText (v, row.removeFromLeft (vw).expanded (2.0f, 0.0f), juce::Justification::centred, false);
    }
    if (dot)
    {
        juce::Path p;
        p.addEllipse (juce::Rectangle<float> (dotSize, dotSize).withCentre ({ row.getX() + dotGap + dotSize * 0.5f, row.getCentreY() }));
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
PlateSlider::PlateSlider (juce::Colour a, juce::Colour b, int n) : from (a), to (b), steps (n)
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

    drawCap (g, { x, cy }, (float) getHeight() - 4.0f, juce::MathConstants<float>::halfPi);
}

void PlateSlider::mouseDown (const juce::MouseEvent& e)
{
    const auto t = travel();
    const float x = t.getStart() + t.getLength() * value;
    if (onGestureStart) onGestureStart();
    if (std::abs (e.position.x - x) > 16.0f)
        moveTo ((e.position.x - t.getStart()) / t.getLength());
    dragStartValue = value;
}

void PlateSlider::mouseDrag (const juce::MouseEvent& e)
{
    moveTo (dragStartValue + (float) e.getDistanceFromDragStartX() / travel().getLength());
}

void PlateSlider::mouseUp (const juce::MouseEvent& e)
{
    if (onGestureEnd) onGestureEnd();
    if (onHover && ! getLocalBounds().contains (e.getPosition()))
        onHover (false);
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
ColumnFader::ColumnFader()
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Range<float> ColumnFader::travel() const
{
    return { 26.0f, (float) getHeight() - 26.0f };
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

    g.setColour (juce::Colour (0xff020205));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - 3.0f, t.getStart() - 8.0f, 6.0f, t.getLength() + 16.0f), 3.0f);

    // A scale down the right edge, every tenth; the middle one (unity) longer.
    for (int i = 0; i <= 10; ++i)
    {
        const float y = t.getEnd() - t.getLength() * (float) i / 10.0f;
        const float len = i == 5 ? 12.0f : 7.0f;
        g.setColour (orange.withAlpha (i == 5 ? 0.95f : 0.6f));
        g.fillRect (juce::Rectangle<float> ((float) getWidth() - 6.0f - len, y - 0.8f, len, 1.6f));
    }

    const float y = t.getEnd() - t.getLength() * value;
    drawCap (g, { cx, y }, (float) getWidth() - 8.0f, 0.0f);
}

void ColumnFader::mouseDown (const juce::MouseEvent& e)
{
    const auto t = travel();
    const float y = t.getEnd() - t.getLength() * value;
    if (onGestureStart) onGestureStart();
    if (std::abs (e.position.y - y) > 16.0f)
        setValue ((t.getEnd() - e.position.y) / t.getLength(), true);
    dragStartValue = value;
}

void ColumnFader::mouseDrag (const juce::MouseEvent& e)
{
    setValue (dragStartValue - (float) e.getDistanceFromDragStartY() / travel().getLength(), true);
}

void ColumnFader::mouseUp (const juce::MouseEvent&)
{
    if (onGestureEnd) onGestureEnd();
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

void TunerFace::paint (juce::Graphics& g)
{
    const juce::Colour home { 0xff5fc97a };   // the family's in-tune green

    auto area = getLocalBounds().toFloat().reduced (18.0f, 10.0f);
    const bool live = ear.live();
    const auto note = ear.nearestNote();

    // The note: name big, octave small beside it; a dash while nothing is heard.
    auto noteArea = area.removeFromLeft (96.0f);
    if (live)
    {
        const auto name = juce::String (core::PitchTracker::noteName (note.midi));
        g.setColour (ear.green() ? home : juce::Colour (0xffece9f6));
        g.setFont (labelFont (52.0f));
        g.drawText (name, noteArea.withTrimmedRight (26.0f), juce::Justification::centredRight, false);
        g.setColour (lilac.withAlpha (0.8f));
        g.setFont (labelFont (layout::px13));
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
        g.setFont (labelFont (layout::px13));
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

} // namespace orbitamp::catpit
