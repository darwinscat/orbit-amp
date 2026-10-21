// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../core/TunerEar.h"
#include "Theme.h"
#include "ZoneSwitch.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** The tuner as a STRIP: full width, always on screen, above the footer — the needle you glance
    at between phrases without opening anything. The note reads at the left, the cents ruler runs
    the width, the distance reads as a number at the right; green means in tune, silence shows a
    dash and a dim scale.

    It reads the same TunerEar every other needle reads, on its own repaint clock — the ear is
    pumped by the processor, so this is drawing only. */
class TunerStrip final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerStrip (const core::TunerEar& tunerEar) : ear (tunerEar)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (30);

        mute.setTooltip ("Mute the output while tuning");
        mute.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (mute);
    }

    /** The MUTE pill, under the cents — the strip's one switch. The owner attaches it to the
        tuner's mute parameter; the strip only gives it a place. It takes its own clicks, so a
        click on it never opens the big tuner. */
    ZoneSwitch mute;

    void resized() override
    {
        mute.setBounds (muteArea().toNearestInt());
    }

    /** A click opens the big tuner — the strip is the glance, the zoom is the look. */
    std::function<void()> onClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Left only — a right-click is never "open the lens".
        if (! e.mods.isPopupMenu() && onClick != nullptr)
            onClick();
    }

    // 44 -> 32 when the footer's facts joined its row in one line: the MUTE pill stands beside the
    // cents instead of under them, and the needle and its scale are as tall as the strip lets them be.
    static constexpr int designHeight = 32;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // No border any more: the badges it used to match are gone, and a needle needs a well,
        // not a frame — the quiet fill alone seats it into the panel.
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        const bool live  = ear.live();
        const auto note  = ear.nearestNote();
        const bool green = ear.green();

        auto inner = r.reduced (14.0f, insetY);

        // ---- the note, left ----
        auto noteArea = inner.removeFromLeft (52.0f);
        if (live)
        {
            const auto name = juce::String (core::PitchTracker::noteName (note.midi));
            const auto font = theme::displayFont (20.0f);
            const float w   = theme::trackedWidth (name, font, 0.05f);

            g.setColour (green ? inTune : theme::tx);
            theme::drawTracked (g, name, noteArea.withWidth (w), font, 0.05f,
                                juce::Justification::centredLeft);

            g.setColour (theme::txDim);
            theme::drawTracked (g, juce::String (core::PitchTracker::noteOctave (note.midi)),
                                noteArea.withTrimmedLeft (w + 6.0f).withTrimmedTop (noteArea.getHeight() * 0.35f),
                                theme::displayFont (13.0f), 0.05f, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::txFaint);
            g.fillRoundedRectangle (noteArea.getX(), noteArea.getCentreY() - 2.0f, 30.0f, 4.0f, 2.0f);
        }

        // ---- the MUTE pill at the far right, the cents just before it ----
        inner.removeFromRight (muteW + muteGap);
        auto centsArea = inner.removeFromRight (52.0f);
        if (live)
        {
            const auto cents = juce::String (juce::roundToInt (note.cents));
            g.setColour (theme::txDim);
            theme::drawTracked (g, (note.cents >= 0.5f ? "+" : "") + cents + " C", centsArea,
                                theme::displayFont (13.0f), 0.12f, juce::Justification::centredRight);
        }

        // ---- the ruler between them, as wide as the note and the cents allow: the ruler is
        //      the tuner's actual instrument, the numbers are its captions ----
        const auto ruler = inner.reduced (9.0f, 0.0f);
        const float midY = ruler.getCentreY();
        const float bandH = ruler.getHeight();   // the scale's wash and its zero: the strip's inner height
        const auto centsX = [&ruler] (float cents)
        {
            return ruler.getX() + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, cents) + 50.0f) / 100.0f;
        };

        // Inverted on request: the edge wash is OUR violet — it deepens over the violet face
        // instead of muddying like orange did — and the TICKS carry the orange. Green heart at zero.
        {
            const auto band = juce::Rectangle<float> (ruler.getX(), midY - bandH * 0.5f, ruler.getWidth(), bandH);

            const auto heatEdge = theme::violet.withAlpha (0.38f);
            juce::ColourGradient heat (heatEdge, band.getX(), midY,
                                       heatEdge, band.getRight(), midY, false);
            heat.addColour (0.35, theme::violet.withAlpha (0.0f));
            heat.addColour (0.65, theme::violet.withAlpha (0.0f));
            g.setGradientFill (heat);
            g.fillRect (band);

            const float cx = centsX (0.0f);
            juce::ColourGradient heart (inTune.withAlpha (0.24f), cx, midY,
                                        inTune.withAlpha (0.0f), cx + ruler.getWidth() * 0.07f, midY, true);
            g.setGradientFill (heart);
            g.fillRect (band);
        }

        for (int c = -50; c <= 50; c += 5)
        {
            const float h = c == 0 ? bandH - 4.0f : (c % 25 == 0 ? bandH * 0.5f : bandH * 0.3f);
            g.setColour (c == 0 ? theme::lilac
                                : (c % 25 == 0 ? theme::orange.withAlpha (0.9f)
                                               : theme::orange.withAlpha (0.5f)));
            g.fillRect (juce::Rectangle<float> (centsX ((float) c) - 0.5f, midY - h * 0.5f,
                                                c == 0 ? 2.0f : 1.0f, h));
        }

        // The zero mark glows — it is the destination.
        g.setColour (theme::lilac.withAlpha (0.25f));
        g.fillRoundedRectangle (centsX (0.0f) - 2.5f, midY - bandH * 0.5f, 5.0f, bandH, 2.5f);

        if (live)
        {
            // The needle says HOW off with its colour: green at home, orange by 25 cents out.
            const float x = centsX (ear.needle());
            const auto  needle = green ? inTune
                                       : inTune.interpolatedWith (theme::orange,
                                             juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / 25.0f));

            // A little taller than the scale, as it always stood — inside the strip's own well.
            const float nh = bandH + 4.0f;
            g.setColour (needle.withAlpha (0.18f));
            g.fillRoundedRectangle (x - 5.0f, midY - nh * 0.5f - 2.0f, 10.0f, nh + 4.0f, 5.0f);
            g.setColour (needle.withAlpha (0.45f));
            g.fillRoundedRectangle (x - 2.5f, midY - nh * 0.5f - 1.0f, 5.0f, nh + 2.0f, 2.5f);
            g.setColour (needle);
            g.fillRoundedRectangle (x - 1.25f, midY - nh * 0.5f, 2.5f, nh, 1.25f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    static constexpr float muteW = 26.0f, muteH = 13.0f, muteGap = 8.0f;
    static constexpr float insetY = 5.0f;

    /** Where the pill stands: at the far right, centred on the strip, the cents just before it —
        the same insets `paint` lays the row out with. */
    juce::Rectangle<float> muteArea() const
    {
        return getLocalBounds().toFloat().reduced (14.0f, insetY)
                   .removeFromRight (muteW).withSizeKeepingCentre (muteW, muteH);
    }

public:
    /** WORKING or standing by — see VolumeColumn::setLive. A tuner standing by hears nothing (the
        processor stops pumping its ear), so a needle left running would be drawing the last note
        it caught, for ever, on a face that is supposed to read as off. */
    void setLive (bool nowLive)
    {
        if (live == nowLive)
            return;

        live = nowLive;
        setAlpha (live ? 1.0f : theme::offAlpha);

        if (live)
            startTimerHz (30);
        else
            stopTimer();

        repaint();
    }

private:
    bool live = true;

    inline static const juce::Colour inTune { 0xff5fc97a };   // the character ramp's clean green

    const core::TunerEar& ear;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerStrip)
};

} // namespace orbitamp
