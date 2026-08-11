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
    g.setColour (juce::Colours::white.withAlpha (0.60f));
    g.fillRect (track.getX(), y - 1.0f, 7.0f, 2.0f);
    g.fillRect (track.getRight() - 7.0f, y - 1.0f, 7.0f, 2.0f);
}

constexpr float gripH = 22.0f;

/** The grip carries its own number IN ITS OWN COLOUR: the frame is tall enough to hold the
    CURRENT value, so the reading rides in the hand — no bubble, no footnote row. Click it to
    type. Each runner keeps its identity by colour — orange trims, violet gate. */
inline void paintGrip (juce::Graphics& g, juce::Rectangle<float> track, float y,
                       const juce::String& text, juce::Colour colour, bool lit = false)
{
    constexpr float sightW = 4.0f;

    const juce::Rectangle<float> frame (track.getX() + 0.75f, y - gripH * 0.5f,
                                        track.getWidth() - 1.5f, gripH);

    g.setColour (juce::Colours::black.withAlpha (0.45f));   // seat it over the gradient
    g.drawRoundedRectangle (frame, 2.5f, 3.0f);
    g.setColour (lit ? colour.brighter (0.25f) : colour);
    g.drawRoundedRectangle (frame, 2.5f, 1.4f);
    g.fillRect (frame.getX(),               y - 0.75f, sightW, 1.5f);
    g.fillRect (frame.getRight() - sightW,  y - 0.75f, sightW, 1.5f);

    theme::drawTracked (g, text, frame, theme::displayFont (11.0f), 0.02f,
                        juce::Justification::centred);
}

/** The in-place editor the grip opens on a double-click: created once, shown over the frame,
    committed on Enter or focus lost, dismissed on Escape. */
struct GripEditor
{
    void open (juce::Component& parent, juce::Rectangle<int> box, float current,
               juce::Colour accent, std::function<void (float)> commitFn)
    {
        commit = std::move (commitFn);

        if (! inited)
        {
            inited = true;
            ed.setJustification (juce::Justification::centred);
            ed.setFont (theme::displayFont (12.0f));
            ed.setColour (juce::TextEditor::backgroundColourId, theme::bezel);
            ed.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            ed.setColour (juce::TextEditor::highlightColourId, theme::violet.withAlpha (0.35f));
            parent.addChildComponent (ed);

            ed.onReturnKey = [this] { takeAndHide(); };
            ed.onFocusLost = [this] { takeAndHide(); };
            ed.onEscapeKey = [this] { ed.setVisible (false); };
        }

        ed.setColour (juce::TextEditor::outlineColourId, accent);
        ed.setColour (juce::TextEditor::focusedOutlineColourId, accent);

        ed.setBounds (box);
        ed.setText (juce::String (current, 1), false);
        ed.setVisible (true);
        ed.selectAll();
        ed.grabKeyboardFocus();
    }

    bool isOpen() const { return inited && ed.isVisible(); }

    void takeAndHide()
    {
        if (! ed.isVisible())
            return;

        const auto t = ed.getText().retainCharacters ("0123456789.+-");
        ed.setVisible (false);

        if (t.isNotEmpty() && commit != nullptr)
            commit (t.getFloatValue());
    }

    juce::TextEditor ed;
    bool inited = false;
    std::function<void (float)> commit;
};

/** The METER's ruler: a numbered mark every 6 dBFS of the level scale — the scale is about the
    SIGNAL, not about any runner. Rows that would collide with the rail's name are skipped. */
inline void paintDbScale (juce::Graphics& g, juce::Rectangle<float> track,
                          const std::function<float (float)>& yOfDb, float floorDb)
{
    for (float db = 0.0f; db > floorDb; db -= 6.0f)
    {
        const float y = yOfDb (db);

        if (y < track.getY() + 6.0f || y > track.getBottom() - 24.0f)
            continue;

        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.fillRect (track.getX(), y - 1.0f, 4.5f, 2.0f);
        g.fillRect (track.getRight() - 4.5f, y - 1.0f, 4.5f, 2.0f);

        g.setColour (juce::Colours::white.withAlpha (0.38f));
        theme::drawTracked (g, juce::String ((int) db),
                            { track.getX(), y - 5.5f, track.getWidth(), 11.0f },
                            theme::displayFont (10.0f), 0.04f, juce::Justification::centred);
    }
}

/** The value under the rail: "+3.5", "-24", "0" — one decimal only when it is earning its keep. */
inline juce::String trimText (float db)
{
    const float r = (float) juce::roundToInt (db);
    const auto  n = std::abs (db - r) < 0.05f ? juce::String ((int) r) : juce::String (db, 1);
    return db > 0.05f ? "+" + n : n;
}

/** The rail's name, set INSIDE the column near its foot — part of the instrument, not a footnote. */
inline void paintName (juce::Graphics& g, juce::Rectangle<float> col, const juce::String& name)
{
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    theme::drawTracked (g, name, col.withTrimmedTop (col.getHeight() - 20.0f),
                        theme::displayFont (11.0f), 0.10f, juce::Justification::centred);
}

} // namespace orbitamp::meterrail
