// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Sheet.h"

namespace orbitamp::face
{

NameTag::NameTag()
{
    setInterceptsMouseClicks (false, false);
}

void NameTag::show (juce::Point<float> anchor, const juce::String& words)
{
    text = words;
    setVisible (text.isNotEmpty());
    if (text.isEmpty())
        return;

    const auto font = labelFont (stage::px13).withExtraKerningFactor (0.06f);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 30.0f;
    // A pixel of room all round for the outline, which is stroked on the edge of the plate.
    auto box = juce::Rectangle<float> (w, 32.0f).withCentre (anchor).expanded (2.0f).getSmallestIntegerContainer();
    if (const auto* parent = getParentComponent())
        box = box.constrainedWithin (parent->getLocalBounds());      // a tag by the stage's edge stays on the stage
    setBounds (box);
    repaint();
}

void NameTag::paint (juce::Graphics& g)
{
    const auto tag = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xe8050509));
    g.fillRoundedRectangle (tag, 6.0f);
    g.setColour (violet.withAlpha (0.7f));
    g.drawRoundedRectangle (tag, 6.0f, 1.2f);
    drawLabel (g, text, tag, stage::px13, juce::Colour (0xffece9f6), 0.06f);
}

//==============================================================================
Sheet::Sheet (std::unique_ptr<juce::Component> c) : content (std::move (c))
{
    addAndMakeVisible (*content);
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> Sheet::plate() const
{
    constexpr int margin = 12;
    return getLocalBounds().withSizeKeepingCentre (content->getWidth() + margin * 2, content->getHeight() + margin * 2);
}

void Sheet::resized()
{
    content->setBounds (getLocalBounds().withSizeKeepingCentre (content->getWidth(), content->getHeight()));
}

void Sheet::paint (juce::Graphics& g)
{
    // A light scrim, as the build window has: the face steps back, it does not go dark.
    g.fillAll (juce::Colours::black.withAlpha (0.22f));

    // The plate is the name tag's, grown to a window: near-black, the family's violet round its edge.
    const auto face = plate().toFloat();
    g.setColour (juce::Colour (0xff0b0a12));
    g.fillRoundedRectangle (face, 8.0f);
    g.setColour (violet.withAlpha (0.7f));
    g.drawRoundedRectangle (face.reduced (0.6f), 8.0f, 1.2f);
}

void Sheet::mouseDown (const juce::MouseEvent& e)
{
    if (! plate().contains (e.getPosition()) && onDismiss)
        onDismiss();
}

void Sheet::childBoundsChanged (juce::Component*)
{
    resized();
    repaint();
}

bool Sheet::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onDismiss)
        onDismiss();
    return true;
}

} // namespace orbitamp::face
