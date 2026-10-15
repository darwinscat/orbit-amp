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

    /** Where the grip's collar ends and the stem begins on the lever's render, down from its top — a
        fraction of the render's side. */
    constexpr float stemTop = 0.69f;
}

GearLever::GearLever()
{
    pos.stiffness = 260.0f;     // a heavy lever: it overshoots the notch a hair and comes back
    pos.damping   = 22.0f;
    pos.jump (1.0f);
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
    if (! pos.step (face::frameDt))
        stopTimer();
    repaint();
}

void GearLever::paint (juce::Graphics& g)
{
    // The lever: its grip over the position, its stem entering the slot under the grip's collar. In a
    // notch the stem stands in the notch; at PARK, the slot's top end, there is none.
    const auto& art = Art::get().lever;
    const float p = pos.value;
    const float inNotch = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (p - std::round (p)) / 0.2f)
                        * juce::jlimit (0.0f, 1.0f, p / 0.5f);
    const float x = slotX + (notchX - slotX) * inNotch;
    const float y = yOf (p);
    const float s = layout::leverSize;
    const float enters = y - layout::stemIn;
    const auto box = juce::Rectangle<float> (x - s * 0.5f, enters - s * stemTop, s, s);

    if (! art.isValid())
    {
        g.setColour (juce::Colour (0xff2a2a30));
        g.fillEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.6f).withCentre ({ x, y }));
        return;
    }

    // Of the stem, only what enters the slot is seen: a little way under the collar, then it is in the
    // slot's dark.
    juce::Path shown;
    shown.addRectangle (box.withBottom (enters + layout::stemIn + layout::stemStub));
    const juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (shown);
    g.drawImage (art, box);
}

void GearLever::mouseDown (const juce::MouseEvent& e)
{
    held = true;
    stopTimer();
    // Taken by the grip, it goes with the hand from where it was taken; anywhere else on the gate it
    // goes where the hand is.
    const float at = yOf (pos.value);
    const float grip = at - layout::stemIn - layout::leverSize * (stemTop - 0.35f);
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

void GearLever::mouseUp (const juce::MouseEvent&)
{
    if (! held)
        return;
    held = false;
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
        const auto box = area.removeFromLeft (w).withSizeKeepingCentre (w, h);
        juce::Path sign;
        sign.addTriangle (box.getCentreX(), box.getY(), box.getRight(), box.getBottom(), box.getX(), box.getBottom());
        sign = sign.createPathWithRoundedCorners (2.5f);
        g.setColour (juce::Colour (0xffe8463a));
        g.fillPath (sign);
        g.setColour (juce::Colour (0xff160606));
        g.fillRoundedRectangle (juce::Rectangle<float> (2.6f, 8.0f).withCentre ({ box.getCentreX(), box.getY() + h * 0.52f }), 1.3f);
        g.fillEllipse (juce::Rectangle<float> (2.8f, 2.8f).withCentre ({ box.getCentreX(), box.getBottom() - 3.6f }));
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
namespace
{
    const juce::Colour ringCyan { 0xff3fe6f2 };     // the painted ring's light, a shade brighter
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
