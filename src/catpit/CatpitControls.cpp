// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitControls.h"
#include "CatpitLayout.h"

#include <felitronics/appkit/BrandAssets.h>

namespace orbitamp::catpit
{

juce::Font labelFont (float em)
{
    if (auto tf = felitronics::appkit::brand::wordmarkTypeface())
        return juce::Font (juce::FontOptions().withTypeface (tf).withPointHeight (em));
    return juce::Font (juce::FontOptions().withPointHeight (em).withStyle ("Bold"));
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
// The lever's own space, inside the 261 x 217 AMP MODE frame: the slot on the left, the four names on
// the right, a lamp beside each.
namespace
{
    constexpr float slotX      = 46.0f;
    constexpr float firstY     = 40.0f;
    constexpr float detentStep = 46.0f;
    constexpr float lampX      = 100.0f;
    constexpr float namesX     = 110.0f;
    constexpr float gripWidth  = 88.0f;
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

void Lever::setDetent (int d, bool animate)
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
    if (changed && onDetent)
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

    // The grip: pulled toward you it comes a little closer, so it grows as it travels down.
    const float y = firstY + detentStep * pos.value;
    const float grow = 1.0f + 0.05f * pos.value;
    const float gripW = gripWidth * grow;
    const auto& img = Art::get().leverGrip;
    const auto area = juce::Rectangle<float> (gripW, gripW * 0.6f).withCentre ({ slotX, y });
    if (img.isValid())
        drawFitted (g, img, area);
    else
    {
        g.setColour (juce::Colours::darkgrey);
        g.fillRoundedRectangle (area.withSizeKeepingCentre (gripW, 26.0f), 10.0f);
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
}

void Lever::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (d) > 0.0f)
        setDetent (detent + (d < 0.0f ? 1 : -1), true);
}

//==============================================================================
BatToggle::BatToggle (juce::String n, int p) : name (std::move (n)), positions (p)
{
    throw_.stiffness = 2200.0f;   // fast and springy: the bat slams over and settles
    throw_.damping   = 38.0f;
    throw_.jump (throwOf (0));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

float BatToggle::throwOf (int i) const noexcept
{
    if (positions == 2)
        return i == 0 ? -1.0f : 1.0f;
    return (float) i - 1.0f;
}

juce::String BatToggle::stateText() const
{
    if (positions == 2)
        return index == 0 ? "OFF" : "ON";
    return juce::StringArray { "I", "II", "III" }[index];
}

void BatToggle::setIndex (int i, bool notify)
{
    i = juce::jlimit (0, positions - 1, i);
    if (i == index)
        return;
    index = i;
    throw_.target = throwOf (i);
    startTimerHz (60);
    if (notify && onChange)
        onChange (index);
    if (onHover && isMouseOver())
        onHover (true);
}

void BatToggle::timerCallback()
{
    if (! throw_.step (frameDt))
        stopTimer();
    repaint();
}

void BatToggle::mouseDown (const juce::MouseEvent& e)
{
    if (positions == 2)
        setIndex (1 - index, true);
    else
        setIndex (index + (e.position.y < (float) getHeight() * 0.5f ? 1 : -1), true);
}

void BatToggle::paint (juce::Graphics& g)
{
    if (! isEnabled())
        g.beginTransparencyLayer (0.3f);

    const auto c = getLocalBounds().toFloat().getCentre();

    // The nut it is mounted through.
    const auto nut = juce::Rectangle<float> (26.0f, 26.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9b98a8), c.translated (-4.0f, -5.0f),
                                             juce::Colour (0xff2a2833), c.translated (8.0f, 10.0f), true));
    g.fillEllipse (nut);
    g.setColour (juce::Colour (0xff0b0a10));
    g.drawEllipse (nut, 1.2f);
    g.setColour (juce::Colour (0xff15141b));
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c));

    // The bat, foreshortened: straight up at you in the centre, leaning out as it is thrown.
    const float t   = throw_.value;
    const auto  tip = c.translated (0.0f, -t * 25.0f);
    juce::Path bat;
    bat.startNewSubPath (c);
    bat.lineTo (tip);
    const juce::PathStrokeType stroke (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e3ee), c.translated (-4.0f, 0.0f),
                                             juce::Colour (0xff55525f), c.translated (4.0f, 0.0f), false));
    g.strokePath (bat, stroke);

    const auto ball = juce::Rectangle<float> (12.0f, 12.0f).withCentre (tip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, tip.translated (-2.5f, -3.0f),
                                             juce::Colour (0xff4a4754), tip.translated (4.0f, 5.0f), true));
    g.fillEllipse (ball);

    // Thrown up (on, or the top of three) the bat's tip catches a violet light.
    if (t > 0.5f && isEnabled())
    {
        g.setColour (violet.withAlpha (0.55f * (t - 0.5f) * 2.0f));
        g.drawEllipse (ball.expanded (1.5f), 1.6f);
    }

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
PlateSlider::PlateSlider (juce::Colour a, juce::Colour b) : from (a), to (b)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
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

    drawCap (g, { x, cy }, (float) getHeight() - 4.0f, juce::MathConstants<float>::halfPi);
}

void PlateSlider::mouseDown (const juce::MouseEvent& e)
{
    const auto t = travel();
    const float x = t.getStart() + t.getLength() * value;
    if (onGestureStart) onGestureStart();
    if (std::abs (e.position.x - x) > 16.0f)
        setValue ((e.position.x - t.getStart()) / t.getLength(), true);
    dragStartValue = value;
}

void PlateSlider::mouseDrag (const juce::MouseEvent& e)
{
    setValue (dragStartValue + (float) e.getDistanceFromDragStartX() / travel().getLength(), true);
}

void PlateSlider::mouseUp (const juce::MouseEvent&)
{
    if (onGestureEnd) onGestureEnd();
}

void PlateSlider::mouseDoubleClick (const juce::MouseEvent&)
{
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

} // namespace orbitamp::catpit
