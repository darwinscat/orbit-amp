// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MeterRail.h"
#include "Theme.h"

#include <functional>
#include <memory>

namespace orbitamp
{

/** A PEDAL'S DOUBLE COLUMN (the owner, 21.11): two thin meters, what comes IN to the block and what goes OUT of it — peak,
    falling back 20 dB a second — in the house's rail grammar (MeterRail.h): the violet flood for the working level, the
    thermometer's heat from -6 dBFS up (nothing above -1 was ever trained), and the AMP's trained-input ZONE as two
    lilac marks on both columns — the IN meter's zone (captureColdDb..captureHotDb) carried back to this block's output
    by what the chain still adds before the network (the pack's chain input_db, TURBO's push): the player sees whether
    the block lands the amp in its zone.

    THE OUT COLUMN MAY BE A FADER TOO (`attachOutFader`: the DRIVE's VOLUME): an orange hand on the rail — orange is the
    hand — its travel the column's height, the range's middle in the middle; dragged, scrolled, double-clicked home to
    its default. Nothing printed: the words and the numbers are the hover hint (the house's tooltip).

    Dumb: the levels and the zone come in through callbacks, read on its own timer; the fader through its parameter.
    One class for every pedal that wants it — the BOOSTER's and DRIVE's. */
class LevelColumns final : public juce::Component,
                           public juce::TooltipClient,
                           private juce::Timer
{
public:
    /** Linear peaks since the last read — the reader takes and clears (an atomic's exchange). */
    std::function<float()> takeIn, takeOut;
    /** The zone at this block's output, dBFS: low and high. */
    std::function<juce::Range<float>()> zone;

    static constexpr float floorDb = -48.0f, hotDb = -6.0f;
    static constexpr float columnW = 8.0f, gapW = 6.0f, handOver = 3.0f;
    static constexpr int designWidth = 26;   // two thin columns, and the hand's reach past the OUT one

    LevelColumns() { startTimerHz (30); }

    /** The OUT column as `parameter`'s fader (its range, its default for a double click), one undo step a gesture. */
    void attachOutFader (juce::RangedAudioParameter& parameter)
    {
        fader = &parameter;
        faderAttachment = std::make_unique<juce::ParameterAttachment> (parameter, [this] (float) { repaint(); });
        faderAttachment->sendInitialUpdate();
    }

    /** Where the hand stands, px from the top, and the value it shows — for the gate. */
    float handY() const
    {
        const auto range = fader->getNormalisableRange();
        return trackOf (1).getCentreY() - (value() - 0.5f * (range.start + range.end)) * pixelsPerUnit();
    }
    float value() const { return fader == nullptr ? 0.0f : fader->convertFrom0to1 (fader->getValue()); }

    void paint (juce::Graphics& g) override
    {
        const auto zoneNow = zone ? zone() : juce::Range<float> (-18.0f, -6.0f);
        for (int k = 0; k < 2; ++k)
        {
            const auto track = trackOf (k);
            g.setColour (theme::panel2.darker (0.4f));
            g.fillRoundedRectangle (track, 2.0f);
            meterrail::paintFill (g, track, yOf (track, shown[k]), 0.0f, shown[k] > hotDb);
            g.setColour (theme::lilac);
            for (const float db : { zoneNow.getStart(), zoneNow.getEnd() })
                g.fillRect (juce::Rectangle<float> (track.getX(), yOf (track, db) - 0.75f, track.getWidth(), 1.5f));
        }

        if (fader != nullptr)
        {
            const auto track = trackOf (1);
            const float y = handY();
            const juce::Rectangle<float> hand (track.getX() - handOver, y - 2.0f, track.getWidth() + 2.0f * handOver, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));   // seated over the flood
            g.fillRoundedRectangle (hand.expanded (1.0f), 2.0f);
            g.setColour (dragging ? theme::orange.brighter (0.25f) : theme::orange);
            g.fillRoundedRectangle (hand, 1.5f);
        }
    }

    /** The hover hint: the column's name, its level, the amp's window here — and on a fader, its value and its ways. */
    juce::String getTooltip() override
    {
        const auto zoneNow = zone ? zone() : juce::Range<float> (-18.0f, -6.0f);
        const bool out = (float) getMouseXYRelative().x >= trackOf (1).getX() - gapW * 0.5f;
        const float level = shown[out ? 1 : 0];
        juce::String text = out ? "OUT, what the pedal hands the amp" : "IN, what comes into the pedal";
        text << ": " << (level > floorDb + 0.5f ? juce::String (level, 1) + " dBFS peak" : juce::String ("silence"))
             << ". The amp's trained window here: " << juce::String (zoneNow.getStart(), 0) << " to "
             << juce::String (zoneNow.getEnd(), 0) << " dBFS.";
        if (out && fader != nullptr)
            text << "\nVOLUME " << meterrail::trimText (value()) << " dB: drag or scroll the orange hand, double-click for noon"
                 << " (as loud as what came in).";
        return text;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! onFader (e))
            return;
        dragging = true;
        dragFrom = value();
        faderAttachment->beginGesture();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            faderAttachment->setValueAsPartOfGesture (legal (dragFrom - (float) e.getDistanceFromDragStartY() / pixelsPerUnit()));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;
        dragging = false;
        faderAttachment->endGesture();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (onFader (e))
            faderAttachment->setValueAsCompleteGesture (fader->convertFrom0to1 (fader->getDefaultValue()));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! onFader (e) || std::abs (wheel.deltaY) <= 0.0f)
            return;
        const float step = (wheel.deltaY > 0.0f ? 0.5f : -0.5f) * (wheel.isReversed ? -1.0f : 1.0f);
        faderAttachment->setValueAsCompleteGesture (legal (value() + step));
    }

private:
    juce::Rectangle<float> trackOf (int k) const
    {
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        return { r.getX() + (float) k * (columnW + gapW), r.getY(), columnW, r.getHeight() };
    }

    float yOf (juce::Rectangle<float> track, float db) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
        return track.getBottom() - t * track.getHeight();
    }

    float legal (float v) const { return fader->getNormalisableRange().snapToLegalValue (v); }

    /** The hand's travel: the OUT column less half a hand at each end. */
    float pixelsPerUnit() const
    {
        const auto range = fader->getNormalisableRange();
        return juce::jmax (1.0e-3f, (trackOf (1).getHeight() - 6.0f) / juce::jmax (1.0e-3f, range.end - range.start));
    }

    bool onFader (const juce::MouseEvent& e) const
    {
        return fader != nullptr && (float) e.x >= trackOf (1).getX() - gapW * 0.5f;
    }

    void timerCallback() override
    {
        const float fall = 20.0f / 30.0f;
        const std::function<float()>* from[2] = { &takeIn, &takeOut };
        for (int k = 0; k < 2; ++k)
        {
            const float peak = *from[k] ? (*from[k])() : 0.0f;
            const float db = juce::Decibels::gainToDecibels (peak, floorDb);
            shown[k] = juce::jmax (db, shown[k] - fall);
        }
        repaint();
    }

    float shown[2] { floorDb, floorDb };
    juce::RangedAudioParameter* fader = nullptr;
    std::unique_ptr<juce::ParameterAttachment> faderAttachment;
    bool dragging = false;
    float dragFrom = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelColumns)
};

/** THE OUT FADER'S NUMBER under the columns (the owner, 21.11: an editable OUT in dB): "+0.0 dB", following the hand, and
    typed there in the house's way (Knob's value row) — a click opens the field, Enter writes (clamped to the range, at
    its step), Esc leaves it as it was. 13 px, the floor; its dashed frame while typed in is the hand's orange. Wider
    than the columns: its owner centres it under them. */
class LevelReadout final : public juce::Label
{
public:
    static constexpr float fontH = 13.0f;
    static constexpr int designWidth = 64, designHeight = 16;

    explicit LevelReadout (juce::RangedAudioParameter& p) : parameter (p)
    {
        setEditable (true, false, false);
        setJustificationType (juce::Justification::centred);
        setFont (theme::displayFont (fontH));
        setMouseCursor (juce::MouseCursor::IBeamCursor);
        setRepaintsOnMouseActivity (true);
        setColour (textWhenEditingColourId, theme::tx);
        setColour (backgroundWhenEditingColourId, theme::bezel);
        setTooltip ("VOLUME, what the pedal hands the amp in dB: click to type, -12 to +12 (0 is as loud as what came in)");
        attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();

        // The field keeps no text of its own: the parameter's value is the text, so a number typed twice is typed twice.
        onTextChange = [this]
        {
            const auto typed = getText();
            setText ({}, juce::dontSendNotification);
            if (typed.containsAnyOf ("0123456789"))
                attachment->setValueAsCompleteGesture (parameter.getNormalisableRange().snapToLegalValue (
                    typed.retainCharacters ("0123456789.,+-").replaceCharacter (',', '.').getFloatValue()));
            repaint();
        };
    }

    /** The number as the face writes it: one decimal, a sign, "dB". */
    juce::String shownText() const
    {
        const float v = (float) juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue()) * 10.0f) / 10.0f;
        return (v >= 0.0f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
    }

    void paint (juce::Graphics& g) override
    {
        if (isBeingEdited())
            return;
        g.setColour (isMouseOver() ? theme::tx : theme::txDim);
        theme::drawTracked (g, shownText(), getLocalBounds().toFloat(), theme::displayFont (fontH), 0.04f, juce::Justification::centred);
    }

    void editorShown (juce::TextEditor* ed) override
    {
        ed->setBorder ({});
        ed->setIndents (0, 0);
        ed->setJustification (juce::Justification::centred);
        ed->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        ed->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        ed->setColour (juce::TextEditor::highlightColourId, theme::orange.withAlpha (0.4f));
        ed->setText (juce::String (parameter.convertFrom0to1 (parameter.getValue()), 1), false);
        ed->selectAll();
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (! isBeingEdited())
            return;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float dash[] = { 3.0f, 2.0f };
        g.setColour (theme::orange);
        for (const auto& side : { juce::Line<float> (r.getTopLeft(), r.getTopRight()), juce::Line<float> (r.getTopRight(), r.getBottomRight()),
                                  juce::Line<float> (r.getBottomRight(), r.getBottomLeft()), juce::Line<float> (r.getBottomLeft(), r.getTopLeft()) })
            g.drawDashedLine (side, dash, 2, 1.0f);
    }

private:
    juce::RangedAudioParameter& parameter;
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelReadout)
};

} // namespace orbitamp
