// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveControls.h"

namespace orbitamp::redgroove
{

using face::orange;

//==============================================================================
void writeAsIs (juce::Graphics& g, const face::Ink& ink, const juce::String& words, juce::Rectangle<float> area, float em,
                juce::Colour colour)
{
    const auto font = face::labelFont (em).withExtraKerningFactor (0.04f);
    if (! ink.edge.isTransparent())
    {
        g.setColour (ink.under (colour));
        face::writeWords (g, words, font, area.translated (0.0f, face::Ink::edgeDrop), ink.weight);
    }
    g.setColour (colour);
    face::writeWords (g, words, font, area, ink.weight);
}

//==============================================================================
Readout::Readout (const face::Ink& i, float size) : colour (i.values), ink (i), em (size)
{
    setInterceptsMouseClicks (false, false);
}

void Readout::set (const juce::String& w)
{
    if (w != words)
    {
        words = w;
        repaint();
    }
}

void Readout::paint (juce::Graphics& g)
{
    writeAsIs (g, ink, words, getLocalBounds().toFloat(), em, colour);
}

//==============================================================================
GrColumn::GrColumn (std::function<float()> grDb) : read (std::move (grDb))
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void GrColumn::timerCallback()
{
    const float now = juce::jlimit (-rangeDb, 0.0f, read != nullptr ? read() : 0.0f);
    if (std::abs (now - shown) > 0.05f)
    {
        shown = now;
        repaint();
    }
}

void GrColumn::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    if (across)
    {
        // Its own slot, dark with a lit rim, as the faders' are; the squeeze inside it from the left.
        g.setColour (juce::Colour (0xff020205));
        g.fillRoundedRectangle (box, 0.5f * box.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (box.reduced (0.5f), 0.5f * box.getHeight(), 1.0f);
        const auto r = box.reduced (2.0f);
        const float depth = r.getWidth() * juce::jlimit (0.0f, 1.0f, -shown / rangeDb);
        if (depth < 0.5f)
            return;
        g.setColour (juce::Colour (0xff8b6cff));
        g.fillRoundedRectangle (r.withWidth (depth), 0.5f * r.getHeight());
        g.setColour (orange);
        g.fillRect (r.getX() + depth - 1.0f, r.getY() - 1.0f, 2.0f, r.getHeight() + 2.0f);
        return;
    }
    const auto r = box;
    const float depth = r.getHeight() * juce::jlimit (0.0f, 1.0f, -shown / rangeDb);
    if (depth < 0.5f)
        return;
    g.setColour (juce::Colour (0xff8b6cff));
    g.fillRoundedRectangle (r.withHeight (depth), 2.0f);
    g.setColour (orange);
    g.fillRect (r.getX() - 1.0f, r.getY() + depth - 1.0f, r.getWidth() + 2.0f, 2.0f);
}

//==============================================================================
namespace
{
    const juce::Colour capFill { 0xff22222e }, capRim { 0xff8b6cff };

    /** A word set condensed into `room` about its centre, as tall, where it is wider. */
    void writeInto (juce::Graphics& g, const face::Ink& ink, const juce::String& words, juce::Point<float> centre, float room,
                    float em, juce::Colour colour)
    {
        const float wide = juce::GlyphArrangement::getStringWidth (face::labelFont (em).withExtraKerningFactor (0.04f), words);
        const juce::Graphics::ScopedSaveState keep (g);
        if (wide > room)
            g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, centre.x, centre.y));
        writeAsIs (g, ink, words, juce::Rectangle<float> (juce::jmax (room, wide) + 8.0f, 30.0f).withCentre (centre), em, colour);
    }
}

ChannelLever::ChannelLever (const face::Ink& i, juce::StringArray n, std::vector<float> h, float slot,
                            std::vector<juce::Point<float>> centres, juce::Point<float> origin)
    : DetentLever (n.size(), 0, 260.0f, 22.0f),      // a heavy lever: it overshoots the place a hair and comes back
      ink (i), names (std::move (n)), slotX (slot - origin.x)
{
    for (const float y : h)
        heights.push_back (y - origin.y);
    for (const auto c : centres)
        nameCentres.push_back (c - origin);
}

float ChannelLever::yOf (float travel) const noexcept
{
    const int last = (int) heights.size() - 1;
    if (last < 1)
        return heights.front();
    const float p = juce::jlimit (0.0f, (float) last, travel);
    const int i = juce::jmin (last - 1, (int) p);
    return juce::jmap (p - (float) i, heights[(size_t) i], heights[(size_t) i + 1]);
}

float ChannelLever::travelAt (float y) const noexcept
{
    const int last = (int) heights.size() - 1;
    if (y <= heights.front())
        return 0.0f;
    for (int i = 0; i < last; ++i)
        if (y <= heights[(size_t) i + 1])
            return (float) i + (y - heights[(size_t) i]) / (heights[(size_t) i + 1] - heights[(size_t) i]);
    return (float) last;
}

juce::Rectangle<float> ChannelLever::capAt (float y) const noexcept
{
    return juce::Rectangle<float> (capWidth, capHeight).withCentre ({ slotX, y });
}

void ChannelLever::paint (juce::Graphics& g)
{
    // Each place's name where the face puts it, the one the lever stands nearest lit.
    const int nearest = juce::roundToInt (travel());
    for (int i = 0; i < names.size() && i < (int) nameCentres.size(); ++i)
        writeAsIs (g, ink, names[i], juce::Rectangle<float> (80.0f, 30.0f).withCentre (nameCentres[(size_t) i]),
                   face::stage::px13, i == nearest ? ink.words : ink.at (ink.words, 0.45f));

    // The cap across the slot, its orange line where it stands.
    const auto cap = capAt (yOf (travel()));
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.5f), 6.0f);
    g.setColour (capFill);
    g.fillRoundedRectangle (cap, 6.0f);
    g.setColour (capRim);
    g.drawRoundedRectangle (cap.reduced (0.9f), 6.0f, 1.8f);
    g.setColour (face::orange);
    g.fillRoundedRectangle (cap.withSizeKeepingCentre (cap.getWidth() - 18.0f, 2.6f), 1.3f);
    if (isMouseOverOrDragging() && ! handLight.isTransparent())
    {
        juce::Path rim;
        rim.addRoundedRectangle (cap.expanded (1.5f), 7.0f);
        face::drawHandRim (g, rim, handLight);
    }
}

void ChannelLever::mouseDown (const juce::MouseEvent& e)
{
    // Taken by its cap, it goes with the hand from where it was taken; anywhere else — a name, the slot — it waits,
    // and let go goes to the place nearest the press.
    take (capAt (yOf (travel())).contains (e.position), e.position.y);
}

void ChannelLever::mouseDrag (const juce::MouseEvent& e)
{
    if (isHeld() && e.mouseWasDraggedSinceMouseDown())
        follow (e.position.y);
}

void ChannelLever::mouseUp (const juce::MouseEvent& e)
{
    if (isHeld())
        letGo (e.mouseWasDraggedSinceMouseDown() ? travel() : underHand (e.position.y));
}

//==============================================================================
SlideSwitch::SlideSwitch (const face::Ink& i, juce::String n, juce::String left, juce::String right, int positions, bool rev)
    : ink (i), name (std::move (n)), words { std::move (left), std::move (right) }, count (juce::jlimit (2, 3, positions)),
      reversed (rev)
{
}

void SlideSwitch::setPosition (int to, bool notify)
{
    to = juce::jlimit (0, count - 1, to);
    if (to == at)
        return;
    at = to;
    repaint();
    if (notify && onFlip)
        onFlip (at);
}

void SlideSwitch::paint (juce::Graphics& g)
{
    if (slotPicture.isValid())
        g.drawImage (slotPicture, slot);

    // One line: the name, then the end positions' words either side of the slot — the one it stands at lit, a
    // three-way's middle lighting neither.
    const float y = slot.getCentreY();
    const auto dim = ink.at (ink.words, 0.5f);
    writeInto (g, ink, words[0], { slot.getX() - 0.5f * wordRoom - 3.0f, y }, wordRoom, em, shown() == 0 ? ink.values : dim);
    writeInto (g, ink, words[1], { slot.getRight() + 0.5f * wordRoom + 3.0f, y }, wordRoom, em,
               shown() == count - 1 ? ink.values : dim);
    if (name.isNotEmpty())
        writeInto (g, ink, name, { slot.getX() - wordRoom - 3.0f - 0.5f * nameRoom, y }, nameRoom, em, ink.words);

    const float d = slot.getHeight() - 6.0f;
    const float x = slot.getX() + 3.0f + 0.5f * d + (slot.getWidth() - 6.0f - d) * (float) shown() / (float) (count - 1);
    const auto cap = juce::Rectangle<float> (d, d).withCentre ({ x, y });
    g.setColour (capFill);
    g.fillEllipse (cap);
    g.setColour (capRim);
    g.drawEllipse (cap.reduced (0.9f), 1.8f);
    g.setColour (face::orange);
    g.fillRoundedRectangle (cap.withSizeKeepingCentre (2.6f, d * 0.46f), 1.3f);
    if (isMouseOverOrDragging() && ! handLight.isTransparent())
    {
        juce::Path rim;
        rim.addRoundedRectangle (slot.expanded (1.0f), 0.5f * slot.getHeight() + 1.0f);
        face::drawHandRim (g, rim, handLight);
    }
}

void SlideSwitch::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
        return;
    }
    if (count == 2)
    {
        setPosition (1 - at, true);
        return;
    }
    const int towardRight = e.position.x < slot.getCentreX() ? -1 : 1;
    setPosition (at + (reversed ? -towardRight : towardRight), true);
}

//==============================================================================
CabinetScreen::CabinetScreen (const face::Ink& i, const juce::Image& cap) : ink (i), slider (cap)
{
    // The slider: MIDDLE's frequency's, smaller, a mark a notch and none drawn longer.
    slider.setAcross (true);
    slider.scale = 0.7f;
    slider.capLength = 10.0f;
    slider.capSquash = 0.5f;
    slider.marks = notches - 1;
    slider.longMark = -1;
    slider.onGestureStart = [this] { moving = notch != nullptr ? notch() : -1; };
    slider.onChange = [this] (float t)
    {
        const int at = juce::jlimit (0, notches - 1, juce::roundToInt (t * last()));
        if (at == moving)
            return;
        moving = at;
        if (onMove)
            onMove (at);
        refresh();
    };
    slider.onGestureEnd = [this]
    {
        // Settled in the nearest notch — kept even where it began, the sound having passed through others.
        const int at = juce::jlimit (0, notches - 1, juce::roundToInt (slider.getValue() * last()));
        moving = -1;
        slider.setValue ((float) at / last(), false);
        if (onLetGo)
            onLetGo (at);
        refresh();
    };
    slider.onMenu = [this] { if (onMenu) onMenu(); };
    addAndMakeVisible (slider);
    startTimerHz (4);
}

void CabinetScreen::resized()
{
    slider.marks = notches - 1;
    slider.handLight = handLight;
    const auto r = getLocalBounds();
    slider.setBounds (r.withTrimmedRight (juce::roundToInt (chevronRoom) + 4).withTrimmedLeft (4).withTrimmedTop (juce::roundToInt (nameH) - 2));
}

void CabinetScreen::refresh()
{
    const auto now = name != nullptr ? name() : juce::String();
    if (now != shown)
    {
        shown = now;
        repaint();
    }
    // The cap where the cabinet that plays stands — unless the hand has it.
    if (moving < 0 && notch != nullptr)
        if (const int at = notch(); at >= 0 && std::abs (slider.getValue() - (float) at / last()) > 0.001f)
            slider.setValue ((float) at / last(), false);
}

void CabinetScreen::paint (juce::Graphics& g)
{
    // The name over the slider, from its left edge — squeezed where it is wider than the room.
    const auto r = getLocalBounds().toFloat();
    const auto text = r.withTrimmedLeft (12.0f).withTrimmedRight (chevronRoom + 6.0f).withTrimmedTop (1.0f).withHeight (nameH);
    const float wide = juce::GlyphArrangement::getStringWidth (face::labelFont (face::stage::px13).withExtraKerningFactor (0.04f), shown);
    {
        const juce::Graphics::ScopedSaveState keep (g);
        if (wide > text.getWidth())
            g.addTransform (juce::AffineTransform::scale (text.getWidth() / wide, 1.0f, text.getX(), text.getCentreY()));
        writeAsIs (g, ink, shown, juce::Rectangle<float> (wide + 4.0f, text.getHeight()).withPosition (text.getTopLeft()),
                   face::stage::px13, shown == "OFF" ? ink.at (ink.values, 0.45f) : ink.values);
    }

    // The divider and the chevron: what a click opens.
    const float x = r.getRight() - chevronRoom;
    g.setColour (juce::Colour (0xff51465f));
    g.fillRect (x, r.getY() + 4.0f, 1.2f, r.getHeight() - 8.0f);
    juce::Path chevron;
    const auto c = juce::Point<float> (x + 0.5f * chevronRoom, r.getCentreY());
    chevron.startNewSubPath (c.x - 6.0f, c.y - 3.0f);
    chevron.lineTo (c.x, c.y + 3.5f);
    chevron.lineTo (c.x + 6.0f, c.y - 3.0f);
    g.setColour (ink.words);
    g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (isMouseOver() && onChevron (getMouseXYRelative().toFloat()) && ! handLight.isTransparent())
    {
        juce::Path rim;
        rim.addRoundedRectangle (juce::Rectangle<float> (x + 2.0f, r.getY() + 2.0f, chevronRoom - 4.0f, r.getHeight() - 4.0f), 5.0f);
        face::drawHandRim (g, rim, handLight);
    }
}

void CabinetScreen::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
    }
    else if (onChevron (e.position) && onClick)
        onClick();
}

void CabinetScreen::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (onChevron (e.position) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

} // namespace orbitamp::redgroove
