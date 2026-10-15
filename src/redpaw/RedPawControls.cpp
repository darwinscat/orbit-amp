// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawControls.h"

#include "../face/Console.h"

namespace orbitamp::redpaw
{

using face::orange;
using face::labelLilac;

namespace
{
    // The slot and its notches in the lever's own space.
    constexpr float slotX  = layout::slotX - GearLever::left;
    constexpr float notchX = layout::notchX - GearLever::left;

    const juce::Colour ringCyan { 0xff3fe6f2 };     // the painted rings' light, a shade brighter: the knobs' and the lever's

    /** Where the grip's bar ends and its collar begins on the lever's render, down from its top — a
        fraction of the render's height. */
    constexpr float barBottom = 0.85f;
}

juce::Rectangle<float> GearLever::leverBox (float y, float x) const
{
    const auto& art = Art::get().lever;
    const float w = layout::leverWidth;
    const float h = art.isValid() ? w * (float) art.getHeight() / (float) art.getWidth() : w / 3.0f;
    return { x - w * 0.5f, y + layout::collarSink - h, w, h };
}

GearLever::GearLever()
{
    pos.stiffness = 260.0f;     // a heavy lever: it overshoots the notch a hair and comes back
    pos.damping   = 22.0f;
    pos.jump (1.0f);
    lit.stiffness = 40.0f;      // soft, like the knobs' rings
    lit.damping   = 13.0f;
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

namespace
{
    /** Each position's height in the dashboard's space: PARK at the slot's top end, the gears in the notches. */
    const std::array<float, GearLever::numPositions> heights { layout::slotTopY, layout::notches[0], layout::notches[1], layout::notches[2] };
}

float GearLever::yOf (float at) const noexcept
{
    const float p = juce::jlimit (0.0f, (float) (numPositions - 1), at);
    const int i = juce::jmin (numPositions - 2, (int) p);
    return juce::jmap (p - (float) i, heights[(size_t) i], heights[(size_t) i + 1]) - top;
}

float GearLever::positionAt (float y) const noexcept
{
    y += top;
    if (y <= heights.front()) return 0.0f;
    for (int i = 0; i + 1 < numPositions; ++i)
        if (y <= heights[(size_t) i + 1])
            return (float) i + (y - heights[(size_t) i]) / (heights[(size_t) i + 1] - heights[(size_t) i]);
    return (float) (numPositions - 1);
}

void GearLever::setPosition (int to, bool animate, bool notify)
{
    to = juce::jlimit (0, numPositions - 1, to);
    const bool changed = to != position;
    position = to;
    if (animate)
    {
        pos.target = (float) to;
        startTimerHz (60);
    }
    else
    {
        pos.jump ((float) to);
        repaint();
    }
    if (changed && notify && onPosition)
        onPosition (position);
}

void GearLever::timerCallback()
{
    const bool moving = pos.step (face::frameDt);
    if (! lit.step (face::frameDt) && ! moving)
        stopTimer();
    repaint();
}

void GearLever::paint (juce::Graphics& g)
{
    // The lever from above: its grip across the slot, the collar under it standing on the position. In
    // a notch the collar stands in the notch; at PARK, the slot's top end, there is none.
    const auto& art = Art::get().lever;
    const float p = pos.value;
    const float inNotch = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (p - std::round (p)) / 0.2f)
                        * juce::jlimit (0.0f, 1.0f, p / 0.5f);
    const auto box = leverBox (yOf (p), slotX + (notchX - slotX) * inNotch);
    const auto bar = box.withHeight (box.getHeight() * barBottom);

    // Its shadow on the plate, the light being from the upper left: the bar's, soft-edged.
    for (const auto& [grow, alpha] : { std::pair { 3.0f, 0.12f }, std::pair { 1.5f, 0.18f }, std::pair { 0.0f, 0.25f } })
    {
        g.setColour (juce::Colours::black.withAlpha (alpha));
        g.fillRoundedRectangle (bar.translated (3.0f, 5.0f).expanded (grow), bar.getHeight() * 0.5f + grow);
    }

    // A soft cyan light round it, the knobs' rings' own: in sight on the dark plate, warmer under the hand.
    if (const auto& glow = Art::get().leverGlow; glow.isValid() && art.isValid())
    {
        const float k = box.getWidth() / (float) art.getWidth();
        g.setColour (ringCyan.withAlpha (0.6f + 0.4f * juce::jlimit (0.0f, 1.0f, lit.value)));
        g.drawImage (glow, box.expanded ((float) Art::leverGlowPad * k), juce::RectanglePlacement::stretchToFit, true);
    }

    g.setOpacity (1.0f);        // an image is drawn at the colour's opacity: the light's would let the slot through
    if (art.isValid())
        g.drawImage (art, box);
    else
    {
        g.setColour (juce::Colour (0xff2a2a30));
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    }
}

void GearLever::mouseDown (const juce::MouseEvent& e)
{
    held = true;
    stopTimer();
    // Taken by the grip, it goes with the hand from where it was taken; anywhere else on the gate it
    // goes where the hand is.
    const float at = yOf (pos.value);
    const float grip = leverBox (at, 0.0f).getCentreY();
    grabOffset = std::abs (e.position.y - grip) < 32.0f ? at - e.position.y : 0.0f;
    mouseDrag (e);
}

void GearLever::mouseDrag (const juce::MouseEvent& e)
{
    if (! held)
        return;

    // The notch: within reach of one the lever sits IN it, and the hand has to pull it out.
    const float raw = positionAt (e.position.y + grabOffset);
    const int nearest = juce::roundToInt (raw);
    const float p = std::abs (raw - (float) nearest) < 0.22f ? (float) nearest : raw;

    pos.jump (p);
    if (juce::exactlyEqual (p, (float) nearest) && nearest != position)
    {
        position = nearest;
        if (onPosition) onPosition (position);
    }
    repaint();
}

void GearLever::mouseUp (const juce::MouseEvent& e)
{
    if (! held)
        return;
    held = false;
    if (! isMouseOver (true) && ! getLocalBounds().contains (e.getPosition()))
    {
        lit.target = 0.0f;
        startTimerHz (60);
    }
    setPosition (juce::roundToInt (pos.value), true);
    if (onRelease)
        onRelease();
}

void GearLever::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (d) > 0.0f)
        setPosition (position + (d < 0.0f ? 1 : -1), true);
}

//==============================================================================
namespace
{
    /** The warning's triangle, its corners rounded, filling `box` — as wide as it is tall and a little more. */
    juce::Path warningShape (juce::Rectangle<float> box)
    {
        juce::Path sign;
        sign.addTriangle (box.getCentreX(), box.getY(), box.getRight(), box.getBottom(), box.getX(), box.getBottom());
        return sign.createPathWithRoundedCorners (2.5f * box.getHeight() / 20.0f);
    }

    /** The warning: the red triangle, an exclamation mark cut dark into it. */
    void drawWarning (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const float h = box.getHeight(), k = h / 20.0f;
        g.setColour (juce::Colour (0xffe8463a));
        g.fillPath (warningShape (box));
        g.setColour (juce::Colour (0xff160606));
        g.fillRoundedRectangle (juce::Rectangle<float> (2.6f * k, 8.0f * k).withCentre ({ box.getCentreX(), box.getY() + h * 0.52f }), 1.3f * k);
        g.fillEllipse (juce::Rectangle<float> (2.8f * k, 2.8f * k).withCentre ({ box.getCentreX(), box.getBottom() - 3.6f * k }));
    }
}

void ParkSign::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff050304).withAlpha (0.72f));
    g.fillRect (area);

    const float h = juce::jmin (area.getHeight() - 24.0f, 96.0f);
    const auto box = juce::Rectangle<float> (h * 1.15f, h).withCentre (area.getCentre());
    juce::DropShadow (juce::Colour (0xffe8463a).withAlpha (0.55f), 16, {}).drawForPath (g, warningShape (box));
    drawWarning (g, box);
}

//==============================================================================
NavHeader::NavHeader()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}

void NavHeader::set (const juce::String& g, const juce::String& c, bool p)
{
    if (g == gear && c == channel && p == parked)
        return;
    gear = g;
    channel = c;
    parked = p;
    repaint();
}

void NavHeader::timerCallback()
{
    if (const int kmh = speed ? juce::roundToInt (speed()) : 0; kmh != shownKmh)
    {
        shownKmh = kmh;
        repaint();
    }
}

void NavHeader::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    const auto words = face::labelFont (layout::px15).withExtraKerningFactor (0.04f);
    const auto small = face::labelFont (layout::px13).withExtraKerningFactor (0.04f);

    // The speed on the right: the number, then its unit a size down.
    const auto unit = juce::String ("KM/H"), number = juce::String (juce::jmax (0, shownKmh));
    g.setColour (labelLilac.withAlpha (0.8f));
    g.setFont (small);
    g.drawText (unit, area.removeFromRight (juce::GlyphArrangement::getStringWidth (small, unit) + 2.0f),
                juce::Justification::centredRight, false);
    area.removeFromRight (6.0f);
    g.setColour (juce::Colour (0xffece9f6));
    g.setFont (words);
    g.drawText (number, area.removeFromRight (juce::GlyphArrangement::getStringWidth (words, number) + 2.0f),
                juce::Justification::centredRight, false);
    area.removeFromRight (8.0f);

    // PARKED, a warning first: a red triangle with its mark — the amp is out of the sound.
    if (parked)
    {
        const float h = 20.0f, w = 23.0f;
        drawWarning (g, area.removeFromLeft (w).withSizeKeepingCentre (w, h));
        area.removeFromLeft (8.0f);
    }

    // The gear, then the channel.
    g.setColour (orange);
    g.drawText (gear, area.removeFromLeft (juce::GlyphArrangement::getStringWidth (words, gear) + 2.0f),
                juce::Justification::centredLeft, false);
    area.removeFromLeft (10.0f);
    g.setColour (juce::Colour (0xffece9f6));
    g.setFont (small);      // the channel a size down: at the gear's size the longest would not fit beside the speed
    g.drawText (channel, area, juce::Justification::centredLeft, true);
}

//==============================================================================
GlassFilm::GlassFilm()
{
    setInterceptsMouseClicks (false, false);
}

void GlassFilm::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! film.isValid() || std::abs (scale - filmScale) > 1.0e-3f)
    {
        filmScale = scale;
        film = make (scale);
    }
    g.drawImage (film, getLocalBounds().toFloat());
}

juce::Image GlassFilm::make (float scale) const
{
    const auto size = getLocalBounds().toFloat();
    juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (size.getWidth() * scale)),
                     juce::jmax (1, juce::roundToInt (size.getHeight() * scale)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random random (0x52504157);       // the same glass on every machine and every start

    const float w = size.getWidth(), h = size.getHeight();

    // MURK: the glass a shade milky toward one corner, a shade darker at its edges.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), w * 0.18f, h * 0.15f,
                                             juce::Colours::transparentWhite, w * 0.75f, h * 0.9f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                             juce::Colours::black.withAlpha (0.22f), 0.0f, 0.0f, true));
    g.fillAll();

    // A SHEEN: a soft band of light across it, the way a screen catches the room.
    {
        auto sheen = juce::ColourGradient (juce::Colours::transparentWhite, w * 0.30f, 0.0f, juce::Colours::transparentWhite, w * 0.58f, h, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.03f));
        g.setGradientFill (sheen);
        g.fillAll();
    }

    // DUST: fine specks, most of them a fraction of a pixel's light, a few soft larger motes.
    for (int i = 0; i < 420; ++i)
    {
        const float d = 0.5f + random.nextFloat() * random.nextFloat() * 1.6f;
        g.setColour (juce::Colour (0xffe8dccf).withAlpha (0.05f + random.nextFloat() * 0.11f));
        g.fillEllipse (random.nextFloat() * w, random.nextFloat() * h, d, d);
    }
    for (int i = 0; i < 18; ++i)
    {
        const float d = 3.0f + random.nextFloat() * 5.0f;
        const juce::Point<float> c { random.nextFloat() * w, random.nextFloat() * h };
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8c8b8).withAlpha (0.05f), c,
                                                 juce::Colours::transparentWhite, c.translated (d, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre (c));
    }

    // SCRATCHES: a few long light ones, mostly the one way a cloth went, gently bent, and a few short.
    for (int i = 0; i < 14; ++i)
    {
        const bool lengthy = i < 6;
        const float len = lengthy ? w * (0.25f + random.nextFloat() * 0.35f) : 12.0f + random.nextFloat() * 30.0f;
        const float angle = juce::degreesToRadians (-18.0f + random.nextFloat() * 14.0f + (i % 5 == 4 ? 70.0f : 0.0f));
        const juce::Point<float> a { random.nextFloat() * w, random.nextFloat() * h };
        const juce::Point<float> b = a + juce::Point<float> (std::cos (angle), std::sin (angle)) * len;
        const auto mid = (a + b) * 0.5f + juce::Point<float> (-std::sin (angle), std::cos (angle)) * (random.nextFloat() - 0.5f) * len * 0.08f;
        juce::Path line;
        line.startNewSubPath (a);
        line.quadraticTo (mid, b);
        g.setColour (juce::Colour (0xffece6f0).withAlpha (lengthy ? 0.06f + random.nextFloat() * 0.05f : 0.08f + random.nextFloat() * 0.06f));
        g.strokePath (line, juce::PathStrokeType (0.45f + random.nextFloat() * 0.3f));
    }
    return img;
}

//==============================================================================
namespace
{
    constexpr float ringFrom = -135.0f, ringSweep = 270.0f;     // a knob's travel, as ArtKnob turns it
    constexpr double breathHoldMs = 1000.0;
}

KnobRing::KnobRing (const face::ArtKnob& k, juce::Point<float> centre, float r, float w)
    : knob (k), radius (r), width (w)
{
    setInterceptsMouseClicks (false, false);
    breath.stiffness = 40.0f;       // soft: a lamp warming, not a spring
    breath.damping   = 13.0f;
    setBounds (juce::Rectangle<float> ((radius + width + 8.0f) * 2.0f, (radius + width + 8.0f) * 2.0f)
                   .withCentre (centre).getSmallestIntegerContainer());
    startTimerHz (30);
}

void KnobRing::timerCallback()
{
    // The hand on the knob, or a moment ago: the ring warms; a second after it has gone, it cools.
    const bool now = knob.isMouseOverOrDragging();
    const double ms = juce::Time::getMillisecondCounterHiRes();
    if (touched && ! now)
        leftAt = ms;
    touched = now;
    breath.target = now || ms - leftAt < breathHoldMs ? 1.0f : 0.0f;

    const bool easing = breath.step (1.0f / 30.0f);
    const bool moved = std::abs (knob.getValue() - shown) > 1.0e-4f;
    if (easing || moved)
        repaint();
}

void KnobRing::paint (juce::Graphics& g)
{
    shown = knob.getValue();
    const auto c = getLocalBounds().toFloat().getCentre();
    const float b = juce::jlimit (0.0f, 1.0f, breath.value);

    // The rest of the ring dimmed: the painted cyan under a dark veil.
    juce::Path whole;
    whole.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour (juce::Colour (0xff05090b).withAlpha (0.62f));
    g.strokePath (whole, juce::PathStrokeType (width + 1.5f));

    // From the start of the travel to the pointer, lit: the arc made a band of the ring's own width
    // first, so that the glow and the light are that band and nothing else — a shadow drawn for the
    // bare arc would fill the chord it spans, over the knob.
    const float to = juce::degreesToRadians (ringFrom + ringSweep * juce::jlimit (0.0f, 1.0f, shown));
    juce::Path arc, band;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::degreesToRadians (ringFrom), to, true);
    juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (band, arc);
    juce::DropShadow (ringCyan.withAlpha (0.35f + 0.4f * b), juce::roundToInt (3.0f + 4.0f * b), {}).drawForPath (g, band);
    g.setColour (ringCyan.withAlpha (0.7f + 0.3f * b));
    g.fillPath (band);
}

} // namespace orbitamp::redpaw
