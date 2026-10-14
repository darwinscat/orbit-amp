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
    constexpr float namesRight = layout::gearNamesRight - GearLever::left;
    constexpr float nameH = 26.0f;

    /** Where the knob's middle stands on the lever's render, down from its top, and how far down the
        knob goes before the stem — fractions of the render's side. */
    constexpr float gripMiddle = 0.33f, gripBottom = 0.72f;
}

GearLever::GearLever()
{
    pos.stiffness = 260.0f;     // a heavy lever: it overshoots the notch a hair and comes back
    pos.damping   = 22.0f;
    pos.jump (0.0f);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

float GearLever::yOf (float position) const noexcept
{
    const auto& n = layout::notches;
    const float p = juce::jlimit (0.0f, (float) (numGears - 1), position);
    const int i = juce::jmin (numGears - 2, (int) p);
    return juce::jmap (p - (float) i, n[(size_t) i], n[(size_t) i + 1]) - top;
}

float GearLever::positionAt (float y) const noexcept
{
    const auto& n = layout::notches;
    y += top;
    if (y <= n.front()) return 0.0f;
    for (int i = 0; i + 1 < numGears; ++i)
        if (y <= n[(size_t) i + 1])
            return (float) i + (y - n[(size_t) i]) / (n[(size_t) i + 1] - n[(size_t) i]);
    return (float) (numGears - 1);
}

int GearLever::nameAt (juce::Point<float> p) const noexcept
{
    if (p.x > namesRight + 4.0f)
        return -1;
    for (int i = 0; i < numGears; ++i)
        if (std::abs (p.y - yOf ((float) i)) < nameH * 0.5f)
            return i;
    return -1;
}

void GearLever::setGear (int g, bool animate, bool notify)
{
    g = juce::jlimit (0, numGears - 1, g);
    const bool changed = g != gear;
    gear = g;
    if (animate)
    {
        pos.target = (float) g;
        startTimerHz (60);
    }
    else
    {
        pos.jump ((float) g);
        repaint();
    }
    if (changed && notify && onGear)
        onGear (gear);
}

void GearLever::timerCallback()
{
    if (! pos.step (face::frameDt))
        stopTimer();
    repaint();
}

void GearLever::paint (juce::Graphics& g)
{
    // The names, left of the slot and set against it; the gear the lever stands in lit orange.
    const auto font = face::labelFont (layout::px13).withExtraKerningFactor (0.06f);
    for (int i = 0; i < numGears; ++i)
    {
        g.setColour (i == gear ? orange : labelLilac.withAlpha (0.8f));
        g.setFont (font);
        g.drawText (names[i], juce::Rectangle<float> (0.0f, yOf ((float) i) - nameH * 0.5f, namesRight, nameH),
                    juce::Justification::centredRight, false);
    }

    // The lever: its knob where it stands, its stem down the slot. In a notch the stem leans into it.
    const auto& art = Art::get().lever;
    const float p = pos.value;
    const float inNotch = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (p - std::round (p)) / 0.2f);
    const float x = slotX + (notchX - slotX) * inNotch;
    const float y = yOf (p);
    const float s = layout::leverSize;
    const auto box = juce::Rectangle<float> (x - s * 0.5f, y - s * gripMiddle, s, s);

    if (! art.isValid())
    {
        g.setColour (juce::Colour (0xff2a2a30));
        g.fillEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.6f).withCentre ({ x, y }));
        return;
    }

    // The stem shows only down the slot: below it, it is under the plate.
    const auto slot = layout::gateSlot.translated (-left, -top).reduced (2.0f, 1.0f);
    juce::Path shown;
    shown.addRectangle (box.withHeight (s * gripBottom));
    shown.addRectangle (slot.withLeft (box.getX()).withRight (box.getRight()).getIntersection (box));
    const juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (shown);
    g.drawImage (art, box);
}

void GearLever::mouseDown (const juce::MouseEvent& e)
{
    if (const int n = nameAt (e.position); n >= 0)
    {
        setGear (n, true);
        return;
    }
    held = true;
    stopTimer();
    const float gripY = yOf (pos.value);
    grabOffset = std::abs (e.position.y - gripY) < 30.0f ? gripY - e.position.y : 0.0f;
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
    if (juce::exactlyEqual (p, (float) nearest) && nearest != gear)
    {
        gear = nearest;
        if (onGear) onGear (gear);
    }
    repaint();
}

void GearLever::mouseUp (const juce::MouseEvent&)
{
    if (! held)
        return;
    held = false;
    setGear (juce::roundToInt (pos.value), true);
    if (onRelease)
        onRelease();
}

void GearLever::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = w.isReversed ? -w.deltaY : w.deltaY;
    if (std::abs (d) > 0.0f)
        setGear (gear + (d < 0.0f ? 1 : -1), true);
}

} // namespace orbitamp::redpaw
