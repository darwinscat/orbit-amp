// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

namespace orbitamp
{

/** A faceplate knob. The value is drawn ON the face — no separate readout box — because the number
    is the amp-panel setting, not a parameter dump: the knob answers "where is it set" at a glance.
    On the face's own knobs it stands UNDER the dial instead, and is typed there (setValueRowShown).

    A juce::Slider underneath, so host automation, keyboard, and APVTS attachments come for free;
    only the drawing is ours. Double-click returns to the parameter's default. */
class Knob : public juce::Slider
{
public:
    Knob (juce::String knobLabel, juce::Colour knobAccent, int notchCount = 11)
        : label (std::move (knobLabel)), accent (knobAccent), notches (notchCount)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (startAngle, endAngle, true);
        setVelocityBasedMode (false);

        addChildComponent (valueField);
        valueField.onTextChange = [this]
        {
            // The field keeps no text of its own: the knob's value is the text, so the same number
            // typed twice in a row is still a number typed.
            const auto typed = valueField.getText();
            valueField.setText ({}, juce::dontSendNotification);

            if (typed.containsAnyOf ("0123456789"))
                setValue (valueForText (typed), juce::sendNotificationSync);   // the range clamps it

            repaint();
        };
    }

    /** THE VALUE UNDER THE DIAL (Oleh, 24.10): the knob's number written under it at reading size, and
        typed there — a click on it opens a field, Enter writes, Esc leaves the knob as it was. The
        number then leaves the dial, unless `valueInDial` keeps it there too: GAIN's, the amp's
        setting at a glance. */
    void setValueRowShown (bool shown)
    {
        valueRow = shown;
        valueField.setVisible (shown);
        resized();
        repaint();
    }

    bool valueInDial = true;

    static constexpr int valueRowH = 16;   // 13 px type, the floor, with air above and below

    /** How far the number's line rides up into the dial's square — under the cap, between the ends of
        the arc, where the square is empty anyway — so the number stands nearer its knob (Oleh, 24.10).
        A block lays out valueRowH - valueLift under such a dial, not the whole line. */
    int valueLift = 0;
    static int snugLift (int dialSide) { return juce::roundToInt ((float) dialSide * 0.16f); }

    /** What a typed text means: by default the first number in it, so "35%" is 35. */
    std::function<double (const juce::String&)> valueForText = [] (const juce::String& t)
    {
        return t.retainCharacters ("+-.0123456789").getDoubleValue();
    };

    /** The hint while the mouse rests on the knob — its name and what it does — on the dial and on
        its number alike. */
    void setHint (const juce::String& text)
    {
        setTooltip (text);
        valueField.setTooltip (text);
    }

    void resized() override
    {
        juce::Slider::resized();
        valueField.setBounds (getLocalBounds().removeFromBottom (valueRowH));

        // A fader's hand follows the mouse: the whole travel is the whole drag.
        if (fader)
            setMouseDragSensitivity (juce::jmax (1, juce::roundToInt (faderTravel (faderArea()).getHeight())));
    }

    /** A FADER instead of a dial (Oleh, 24.10): the delay's and the reverb's MIX, a vertical slider as
        tall as its block, its name over it and its number under it like any knob's. A rail of the
        bezel's black, the amount flooding it in the accent from the foot, and a grip across it with
        the accent drawn through as a line — the channel lever's grip. It moves by the drag; a click
        does not throw it to the mouse. */
    void setFader (bool isFader)
    {
        fader = isFader;
        setSliderStyle (fader ? juce::Slider::LinearVertical : juce::Slider::RotaryVerticalDrag);
        setSliderSnapsToMousePosition (! fader);
        setRepaintsOnMouseActivity (fader);   // the grip lights under the hand
        resized();
        repaint();
    }

    static constexpr int faderW   = 46;   // the column: "100%" at 13 px under it
    static constexpr int faderGap = 10;   // between the fader's column and what stands beside it

    /** How many detents to mark. The pack decides for a captured control — twenty-one for one pedal,
        something else for the next — so it cannot be a constructor argument any more. */
    void setNotches (int n)
    {
        if (n != notches)
        {
            notches = n;
            repaint();
        }
    }

    /** Whether the hand can only land ON a notch. A captured dial in STEP mode: the notches are the
        captured positions, and between them the player would pick the nearer one anyway — so the
        knob says so by refusing to stop there. Host automation is not bound by this; the block
        rounds for it. */
    bool snapToNotches = false;

    double snapValue (double attemptedValue, DragMode) override
    {
        if (! snapToNotches || notches < 2)
            return attemptedValue;

        const double lo = getMinimum(), hi = getMaximum();
        const double step = (hi - lo) / (double) (notches - 1);
        return juce::jlimit (lo, hi, lo + std::round ((attemptedValue - lo) / step) * step);
    }

    /** HEAT: the arc wears the character ramp along its travel — clean green at the bottom of the
        dial through edge and crunch to hi-gain at the top, the colours the device list is written
        in — and the pointer wears the colour of the place it points at. For the one dial whose position IS a temperature: how
        hard a capture is driven. Off, the arc is the accent, which for a console band is the
        colour that ties it to its point on the curve and must stay one colour. */
    bool heat = false;

    /** WHERE ITS TRAVEL RUNS, in radians clockwise from twelve: the family's 7:30 to 4:30 unless a knob says
        otherwise — the drive's runs 7:00 to 5:00, so that 2 points straight left and 8 straight right. */
    void setArc (float start, float end)
    {
        startAngle = start;
        endAngle = end;
        setRotaryParameters (startAngle, endAngle, true);
        repaint();
    }

    /** How the number on the face is written. Defaults to one decimal — the 0-10 amp scale. */
    std::function<juce::String (double)> textForValue = [] (double v) { return juce::String (v, 1); };

    /** The label row's type, overridable for a hero control whose NAME is part of the face —
        a zoomed block's one big decision reads at reading size, not at panel size. */
    float labelFontHeight = 8.0f;
    float labelTracking   = 0.15f;
    int   labelRowHeight  = labelHeight;

    /** THE small dial: the EQ's bands. */
    static constexpr int smallSide = 42;

    /** The reverb's DECAY and PRE, the delay's REPS, DARK and OFFSET: one dial a little under the
        EQ's, each in an even share of the row beside the MIX fader (Oleh, 24.10). */
    static constexpr int fxSide = 36;

    /** The names over the delay's and the reverb's hands: 7 and 8 px read too small (Oleh, 24.10), and
        OFFSET at 10 still fits a third of the delay's row. */
    static constexpr float fxLabelH        = 10.0f;
    static constexpr float fxLabelTracking = 0.06f;
    static constexpr int   fxLabelRowH     = 13;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        g.setColour (theme::txDim);
        theme::drawTracked (g, label.toUpperCase(), r.removeFromTop (labelRowHeight).toFloat(),
                            theme::displayFont (labelFontHeight), labelTracking, juce::Justification::centred);

        if (valueRow)
            r.removeFromBottom (valueRowH - valueLift);   // the number's own line: its field draws it

        if (fader)
        {
            paintFader (g);
            return;
        }

        const float d = (float) juce::jmin (r.getWidth(), r.getHeight());
        const juce::Point<float> c { (float) r.getCentreX(), (float) r.getCentreY() };
        const float ringR = d * 0.42f;
        const float bodyR = d * 0.33f;

        const float pos   = (float) valueToProportionOfLength (getValue());
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Notches first, outside everything — the detent marks that make 0-10 readable without a dial.
        if (notches > 1)
        {
            for (int i = 0; i < notches; ++i)
            {
                const float a  = startAngle + (endAngle - startAngle) * (float) i / (float) (notches - 1);
                const float t0 = ringR + d * 0.045f;
                const float t1 = t0 + d * 0.05f;
                const juce::Point<float> p0 { c.x + std::sin (a) * t0, c.y - std::cos (a) * t0 };
                const juce::Point<float> p1 { c.x + std::sin (a) * t1, c.y - std::cos (a) * t1 };

                g.setColour (a <= angle + 1.0e-4f ? accent.withAlpha (0.75f) : theme::hair2);
                g.drawLine ({ p0, p1 }, d * 0.018f);
            }
        }

        const float arcW = d * 0.055f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::hair2);
        g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float upto = (angle - startAngle) / (endAngle - startAngle);   // 0..1 of the travel

        if (angle > startAngle && ! heat)
        {
            juce::Path filled;
            filled.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, angle, true);
            g.setColour (accent);
            g.strokePath (filled, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (angle > startAngle)
        {
            // A gradient along an ARC: JUCE draws them along lines, so the arc is walked in short
            // butt-ended pieces, each in the colour of its own place on the travel, overlapping by
            // a hair so no seam shows — and a rounded cap in the end colour closes each end.
            constexpr int   pieces = 72;
            const float     span   = endAngle - startAngle;
            const auto rounded = juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            const auto butt    = juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

            const auto cap = [&] (float t)
            {
                juce::Path p;
                const float a = startAngle + t * span;
                p.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, a - 0.002f, a + 0.002f, true);
                g.setColour (heatAt (t));
                g.strokePath (p, rounded);
            };

            cap (0.0f);
            for (int i = 0; i < pieces; ++i)
            {
                const float t0 = (float) i / (float) pieces;
                if (t0 >= upto)
                    break;
                const float t1 = juce::jmin (upto, (float) (i + 1) / (float) pieces);

                juce::Path piece;
                piece.addCentredArc (c.x, c.y, ringR, ringR, 0.0f,
                                     startAngle + t0 * span - 0.01f, startAngle + t1 * span + 0.01f, true);
                g.setColour (heatAt ((t0 + t1) * 0.5f));
                g.strokePath (piece, butt);
            }
            cap (upto);
        }

        // The body: a dark disc lit from above, so the face reads as a physical cap.
        juce::ColourGradient body (juce::Colour (0xff23222d), c.x, c.y - bodyR,
                                   juce::Colour (0xff0e0e14), c.x, c.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (theme::hair2);
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer.
        const juce::Point<float> pa { c.x + std::sin (angle) * bodyR * 0.55f, c.y - std::cos (angle) * bodyR * 0.55f };
        const juce::Point<float> pb { c.x + std::sin (angle) * bodyR * 0.94f, c.y - std::cos (angle) * bodyR * 0.94f };
        g.setColour (heat ? heatAt (upto).brighter (0.15f) : accent.brighter (0.3f));
        g.drawLine ({ pa, pb }, d * 0.035f);

        if (valueRow && ! valueInDial)
            return;

        g.setColour (juce::Colour (0xfff6f4fd));
        const auto valueFont = theme::displayFont (d * 0.19f);
        const auto text = textForValue (getValue());
        theme::drawTracked (g, text, juce::Rectangle<float> (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f),
                            valueFont, 0.01f, juce::Justification::centred);
    }

private:
    /** The temperature of a place on the travel, 0..1 — the family's one heat scale. */
    static juce::Colour heatAt (float t) { return theme::heatColour (t); }

    static constexpr int   labelHeight = 11;
    static constexpr float defaultStart = juce::MathConstants<float>::pi * 1.25f;     // the family's travel, 7:30...
    static constexpr float defaultEnd   = juce::MathConstants<float>::pi * 2.75f;     // ...to 4:30
    float startAngle = defaultStart;
    float endAngle   = defaultEnd;

    juce::String label;
    juce::Colour accent;
    int notches;

    // ---- the fader (setFader) ----
    bool fader = false;
    static constexpr float gripW = 30.0f, gripH = 12.0f, railW = 10.0f;

    /** Between the name and the number. */
    juce::Rectangle<int> faderArea() const
    {
        auto r = getLocalBounds();
        r.removeFromTop (labelRowHeight);
        if (valueRow)
            r.removeFromBottom (valueRowH - valueLift);
        return r.reduced (0, 4);
    }

    /** Where the grip's centre travels: half a grip in from either end, so it never stands half off. */
    static juce::Rectangle<float> faderTravel (juce::Rectangle<int> area) { return area.toFloat().reduced (0.0f, gripH * 0.5f); }

    void paintFader (juce::Graphics& g)
    {
        const auto area   = faderArea().toFloat();
        const auto travel = faderTravel (faderArea());
        const float pos   = (float) valueToProportionOfLength (getValue());
        const float y     = travel.getBottom() - pos * travel.getHeight();
        const float cx    = area.getCentreX();

        // The rail: a cut in the bezel's black, the amount flooding it from the foot, a lip round it.
        const auto rail = juce::Rectangle<float> (railW, area.getHeight()).withCentre ({ cx, area.getCentreY() });
        g.setColour (theme::bezel);
        g.fillRoundedRectangle (rail, railW * 0.5f);

        g.setColour (accent.withAlpha (0.55f));
        g.fillRoundedRectangle (rail.reduced (2.0f).withTop (y), (railW - 4.0f) * 0.5f);

        g.setColour (theme::violet.withAlpha (0.45f));
        g.drawRoundedRectangle (rail.reduced (0.5f), railW * 0.5f, 1.2f);

        // A tick across the rail at every quarter.
        g.setColour (theme::hair2);
        for (int i = 0; i <= 4; ++i)
            g.fillRect (juce::Rectangle<float> (railW + 8.0f, 1.0f)
                            .withCentre ({ cx, travel.getBottom() - travel.getHeight() * (float) i / 4.0f }));

        // The grip: the channel lever's, the accent through it as one line — brighter under the hand.
        const auto bar = juce::Rectangle<float> (juce::jmin (gripW, area.getWidth() - 4.0f), gripH).withCentre ({ cx, y });

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar.translated (0.0f, 2.0f).expanded (1.0f), gripH * 0.5f + 1.0f);

        juce::ColourGradient body (juce::Colour (0xff2c2a38), 0.0f, bar.getY(),
                                   juce::Colour (0xff111018), 0.0f, bar.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (bar, gripH * 0.5f);

        const bool handled = isMouseOverOrDragging();
        g.setColour (handled ? accent.withAlpha (0.8f) : theme::hair2);
        g.drawRoundedRectangle (bar.reduced (0.5f), gripH * 0.5f, 1.0f);

        g.setColour (handled ? accent.brighter (0.3f) : accent);
        g.fillRect (juce::Rectangle<float> (bar.getWidth() - 10.0f, 1.5f).withCentre (bar.getCentre()));
    }

    /** The number under the dial: written in the face's own tracked type, a field only while typed in. */
    struct ValueField final : public juce::Label
    {
        explicit ValueField (Knob& k) : owner (k)
        {
            setEditable (true, false, false);
            setJustificationType (juce::Justification::centred);
            setFont (theme::displayFont (fontH));
            setMouseCursor (juce::MouseCursor::IBeamCursor);
            setRepaintsOnMouseActivity (true);

            setColour (textWhenEditingColourId,       theme::tx);
            setColour (backgroundWhenEditingColourId, theme::bezel);
        }

        void paint (juce::Graphics& g) override
        {
            if (isBeingEdited())
                return;

            g.setColour (isMouseOver() ? theme::tx : theme::txDim);
            theme::drawTracked (g, owner.textForValue (owner.getValue()), getLocalBounds().toFloat(),
                                theme::displayFont (fontH), 0.04f, juce::Justification::centred);
        }

        void editorShown (juce::TextEditor* ed) override
        {
            // No indents and no border: the editor's own sat the number a few pixels low of where
            // the face writes it (Oleh, 24.10). Centred, it stands on the same line.
            ed->setBorder ({});
            ed->setIndents (0, 0);
            ed->setJustification (juce::Justification::centred);

            // Its frame is ours, drawn over it: a thin dashed line (paintOverChildren).
            ed->setColour (juce::TextEditor::outlineColourId,        juce::Colours::transparentBlack);
            ed->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            ed->setColour (juce::TextEditor::highlightColourId, owner.accent.withAlpha (0.4f));

            ed->setText (owner.textForValue (owner.getValue()), false);
            ed->selectAll();
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (! isBeingEdited())
                return;

            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float dash[] = { 3.0f, 2.0f };
            g.setColour (owner.accent);

            for (const auto& side : { juce::Line<float> (r.getTopLeft(),     r.getTopRight()),
                                      juce::Line<float> (r.getTopRight(),    r.getBottomRight()),
                                      juce::Line<float> (r.getBottomRight(), r.getBottomLeft()),
                                      juce::Line<float> (r.getBottomLeft(),  r.getTopLeft()) })
                g.drawDashedLine (side, dash, 2, 1.0f);
        }

        static constexpr float fontH = 13.0f;
        Knob& owner;
    };

    bool valueRow = false;
    ValueField valueField { *this };

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace orbitamp
