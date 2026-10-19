// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitControls.h"
#include "CatpitLayout.h"

namespace orbitamp::catpit
{

//==============================================================================
// The lever's own space: the 261 x 217 AMP MODE frame plus `overhang` to its left, where the grip
// reaches past the frame. The slot on the left, the four names on the right, a lamp beside each.

Lever::Lever() : DetentLever (numDetents, 0, stiffness, damping) {}

float Lever::yOf (float travel) const noexcept { return firstY + detentStep * travel; }

float Lever::travelAt (float y) const noexcept
{
    return juce::jlimit (0.0f, (float) (numDetents - 1), (y - firstY) / detentStep);
}

int Lever::labelAt (juce::Point<float> p) const noexcept
{
    if (p.x < lampX - 8.0f)
        return -1;
    return juce::jlimit (0, numDetents - 1, juce::roundToInt (travelAt (p.y)));
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
        g.fillRect (juce::Rectangle<float> (slotX - 20.0f, yOf ((float) i) - 1.0f, 40.0f, 2.0f));
    }

    // The names, a lamp beside each; the one it sits in lit orange.
    for (int i = 0; i < numDetents; ++i)
    {
        const bool here = i == getPlace();
        const auto lamp = juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ lampX, yOf ((float) i) });
        if (here)
        {
            juce::Path lp;
            lp.addRoundedRectangle (lamp, 2.0f);
            glow (g, lp, orange, 6.0f);
        }
        g.setColour (here ? orange : juce::Colour (0xff3a2a22));
        g.fillRoundedRectangle (lamp, 2.0f);

        const auto row = juce::Rectangle<float> (namesX, yOf ((float) i) - 14.0f, (float) getWidth() - namesX - 6.0f, 28.0f);
        g.setColour (here ? orange : lilac.withAlpha (0.75f));
        g.setFont (labelFont (layout::px16));
        g.drawText (names[i], row, juce::Justification::centredLeft, false);
    }

    // The grip: pulled toward you it comes a little closer, so it grows as it travels down. Its stem
    // stays in the slot; the bar hangs further to the left of it than to the right, clear of the names.
    const float y = yOf (travel());
    const float length = gripLength * (1.0f + gripGrowth * travel());
    const auto& art = Art::get();
    if (art.leverGrip.isValid())
    {
        const float height = length * (float) art.leverGrip.getHeight() / (float) art.leverGrip.getWidth();
        g.setOpacity (1.0f);     // an image is drawn at the brush's opacity, and the last name left it dimmed
        g.drawImage (art.leverGrip, { slotX - layout::gripAxis * length, y - height * 0.5f, length, height });
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
        setPlace (l, true);
        return;
    }
    // Pressed on the slot it comes to the hand at once; within reach of the grip it is the grip that
    // goes with the hand, from where it was taken.
    take (std::abs (p.y - yOf (travel())) < 30.0f, p.y);
    follow (p.y);
}

void Lever::mouseDrag (const juce::MouseEvent& e)
{
    if (isHeld())
        follow (e.position.y);
}

void Lever::mouseUp (const juce::MouseEvent&)
{
    if (isHeld())
        letGo (travel());
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

void GuardButton::enablementChanged()
{
    setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GuardButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu)
    {
        onMenu();
        return;
    }
    if (! isEnabled())      // the guard stays down
        return;
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

} // namespace orbitamp::catpit
