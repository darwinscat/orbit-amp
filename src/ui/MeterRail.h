#pragma once

#include "Theme.h"

namespace orbitamp::meterrail
{

/** tabby's meter rail, transplanted — the ONE way a level column looks in this family.

    The fill is dB-anchored: the gradient spans the whole rail and the level CLIPS it, so a given
    height always reads the same colour — mostly violet, warming to orange only near the top. The
    peak hold is a white line; the top sliver is a clip cap, red while latched. The trim grip is a
    hollow sliding frame with a sight: the meter keeps running through it, and the two ticks biting
    in from the sides mark the exact value, which a 10 px-tall frame alone could not. */

/** `desat` drains the colour out of the rail, 0..1 — the gate strip uses it to show pressure:
    the deeper the gate squeezes, the greyer the signal's column, monochrome at full mute. */
inline void paintFill (juce::Graphics& g, juce::Rectangle<float> r, float yLevel, float desat = 0.0f)
{
    if (yLevel >= r.getBottom() - 1.0f)
        return;

    const float keep = 1.0f - juce::jlimit (0.0f, 1.0f, desat);

    juce::ColourGradient grad (theme::violet.withMultipliedSaturation (keep).withAlpha (0.50f),
                               0.0f, r.getBottom(),
                               theme::orange.withMultipliedSaturation (keep), 0.0f, r.getY(), false);
    grad.addColour (0.58, theme::violet.withMultipliedSaturation (keep).withAlpha (0.60f));

    const juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion ((int) std::floor (r.getX()), (int) yLevel,
                        (int) std::ceil (r.getWidth()) + 1,
                        (int) std::ceil (r.getBottom() - yLevel) + 1);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 2.0f);
}

inline void paintHold (juce::Graphics& g, juce::Rectangle<float> r, float y)
{
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.fillRect (r.getX(), y - 0.75f, r.getWidth(), 1.5f);
}

/** The top sliver: red while a clip is latched, a faint ceiling mark otherwise. Click target. */
inline void paintClipCap (juce::Graphics& g, juce::Rectangle<float> r, bool clipped)
{
    g.setColour (clipped ? juce::Colour (0xffff3b30) : juce::Colours::white.withAlpha (0.06f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 3.0f);
}

/** The unity nubs: a scale mark, deliberately NOT a full line so it never reads as one of the
    meter's own ticks. */
inline void paintUnityNubs (juce::Graphics& g, juce::Rectangle<float> track, float y)
{
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillRect (track.getX(), y - 0.5f, 3.0f, 1.0f);
    g.fillRect (track.getRight() - 3.0f, y - 0.5f, 3.0f, 1.0f);
}

inline void paintGrip (juce::Graphics& g, juce::Rectangle<float> track, float y, bool lit = false)
{
    constexpr float gripH = 11.0f, sightW = 4.0f;

    const juce::Rectangle<float> frame (track.getX() + 0.75f, y - gripH * 0.5f,
                                        track.getWidth() - 1.5f, gripH);

    g.setColour (juce::Colours::black.withAlpha (0.45f));   // seat it over the gradient
    g.drawRoundedRectangle (frame, 2.5f, 3.0f);
    g.setColour (lit ? theme::orange.brighter (0.25f) : theme::orange);
    g.drawRoundedRectangle (frame, 2.5f, 1.4f);
    g.fillRect (frame.getX(),               y - 0.75f, sightW, 1.5f);
    g.fillRect (frame.getRight() - sightW,  y - 0.75f, sightW, 1.5f);
}

/** The value under the rail: "+3.5", "-24", "0" — one decimal only when it is earning its keep. */
inline juce::String trimText (float db)
{
    const float r = (float) juce::roundToInt (db);
    const auto  n = std::abs (db - r) < 0.05f ? juce::String ((int) r) : juce::String (db, 1);
    return db > 0.05f ? "+" + n : n;
}

/** Wires an editable dB readout under a rail: type a number, Enter, done. The attachment is
    fetched through a getter because the label outlives no one — it is a member next to it. */
inline void initReadout (juce::Label& l, juce::RangedAudioParameter& p,
                         std::function<juce::ParameterAttachment*()> att)
{
    l.setFont (theme::displayFont (9.5f));
    l.setColour (juce::Label::textColourId, theme::txDim);
    l.setColour (juce::Label::backgroundWhenEditingColourId, theme::bezel);
    l.setColour (juce::Label::textWhenEditingColourId, theme::lilac);
    l.setColour (juce::TextEditor::highlightColourId, theme::violet.withAlpha (0.35f));
    l.setJustificationType (juce::Justification::centred);
    l.setEditable (true, false, false);

    l.onTextChange = [&l, &p, att]
    {
        const auto t = l.getText().retainCharacters ("0123456789.+-");
        if (t.isNotEmpty() && att() != nullptr)
            att()->setValueAsCompleteGesture (t.getFloatValue());
        l.setText (trimText (p.convertFrom0to1 (p.getValue())), juce::dontSendNotification);
    };

    l.onEditorShow = [&l, &p]
    {
        if (auto* ed = l.getCurrentTextEditor())
        {
            ed->setJustification (juce::Justification::centred);
            ed->setText (juce::String (p.convertFrom0to1 (p.getValue()), 1), false);
            ed->selectAll();
        }
    };
}

} // namespace orbitamp::meterrail
