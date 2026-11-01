// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "MeterRail.h"
#include "../PluginProcessor.h"
#include "../core/ChorusTap.h"

#include <felitronics/chorus/ChorusLaw.h>

#include <array>

namespace orbitamp
{

/** CHORUS'S PICTURE: its two voices as two dots running on a circle — the LFO's phase their angle, the RIGHT voice a
    quarter period ahead of the LEFT (the stage swings them in quadrature: sin and cos), the circle's radius the
    swing the slider gives (felitronics::chorus::paramsForAmount, 0 … 3 ms), their speed the RATE. Each dot's height
    on the circle is its head's place: up is a longer delay. A short trail behind each, fading, so the speed reads
    at a glance — translucent, so where the two cross neither hides the other. The slider's whole reach is a faint
    lilac ring, the radius it gives now an orange one; the dots and their trails are violet, the stage at work. On a
    mono bus the stage makes one voice: one dot.

    Under the circle a lilac line and the CROSSOVER: below it the voice falls away and the bass stays untouched,
    centred, mono. Both numbers on it — the RATE at the top, the crossover at the bottom — are its menu's parameters,
    so a click types them (Enter takes it, one undo step); a click anywhere else, or a right click, is its menu.

    The phase is the processor's tap (core::ChorusTap): read on its own timer only while it is on screen — the timer
    asks every tick, so a hidden ancestor stops it too, and the audio thread writes it only then — and it moves only
    while the stage runs: an idle chorus stands still, and so does one standing by or at 0 — nothing is written then, and
    it dims with its block (the owner, 31.10). Every word on it is 13 px. */
class ChorusPicture final : public juce::Component,
                            private juce::Timer
{
public:
    explicit ChorusPicture (AmpProcessor& processor)
        : amp (processor),
          state (processor.apvts),
          amountP (*processor.apvts.getParameter (params::chorusAmount)),
          rateP (*processor.apvts.getParameter (params::chorusRateHz)),
          crossP (*processor.apvts.getParameter (params::chorusCrossoverHz))
    {
        for (auto* p : { &amountP, &rateP, &crossP })
            atts.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); }));
        startTimerHz (50);   // its look, every tick: on screen or not (look)
    }

    ~ChorusPicture() override
    {
        stopTimer();
        amp.chorusWatched.store (false, std::memory_order_relaxed);
    }

    static constexpr float textHeight = 13.0f, rowH = 18.0f, dotR = 4.5f, labelGap = 12.0f;
    static constexpr double trailSeconds = 0.2;
    static constexpr int    trailHops = (int) (trailSeconds / core::ChorusTap::hopSeconds + 0.5);

    enum class Field { rate, crossover };

    std::function<void (juce::Point<int> screenPos)> onMenu;

    // ---- what it shows ------------------------------------------------------------------------------------------
    /** The circle's centre and the slider's whole reach (amount 1), in its own coordinates. */
    juce::Point<float> centre() const { return circleArea().getCentre(); }
    float reach() const
    {
        const auto a = circleArea();
        return juce::jmax (4.0f, juce::jmin (a.getWidth(), a.getHeight()) * 0.5f - labelGap - 5.0f);
    }

    /** The radius now as a fraction of the reach: the swing the slider gives over the swing it gives at its top. */
    float radiusFraction() const
    {
        const float a = amountP.convertFrom0to1 (amountP.getValue());
        return felitronics::chorus::paramsForAmount (a).depthMs / felitronics::chorus::kDepthMsAtFull;
    }

    /** The LFO's phase the dots stand at: the newest entry taken whole, 0 before any. */
    float shownPhase() const noexcept { return nowPhase; }

    /** How many voices it draws: the stage's, one on a mono bus. */
    int voices() const noexcept { return amp.bassBackPlanes() == 1 ? 1 : 2; }

    /** Where voice `v` (0 the left, 1 the right) stands, in its own coordinates. */
    juce::Point<float> dotAt (int v) const { return pointAt (nowPhase, v, reach() * radiusFraction()); }

    /** The two numbers' boxes, each a field. */
    juce::Rectangle<float> fieldBox (Field f) const
    {
        const auto r = getLocalBounds().toFloat();
        if (f == Field::rate)
            return r.withHeight (rowH).withTrimmedLeft (r.getWidth() * 0.4f);
        return r.withTop (r.getBottom() - rowH).withTrimmedLeft (r.getWidth() * 0.45f);
    }

    /** A field opened as a click opens it; its editor (a gate types into it and takes it). */
    void openField (Field f)
    {
        auto& p = param (f);
        const float v = p.convertFrom0to1 (p.getValue());
        editing = f;
        editor.open (*this, fieldBox (f).toNearestInt(), v, f == Field::rate ? theme::orange : theme::lilac,
                     [this, f] (float typed)
                     {
                         // A typed number is ONE undo step of its own: two typed in quick succession stay two.
                         amp.history.beginGesture();
                         bass::setPlain (state, f == Field::rate ? params::chorusRateHz : params::chorusCrossoverHz, typed);
                         amp.history.endGesture();
                     });
        // The house's grip editor opens on a whole number; these are not whole, and they read at 13 px.
        editor.ed.setFont (theme::displayFont (textHeight));
        editor.ed.setText (numberText (f, v), false);
        editor.ed.selectAll();
    }

    meterrail::GripEditor& fieldEditor() noexcept { return editor; }

    /** What the taps hold, the newest last — the timer's work, and a picture's made off screen. */
    void pull()
    {
        shown = amp.chorusTap.read (trail.data(), (int) trail.size());
        for (int i = shown; --i >= 0;)
            if (trail[(size_t) i].ok)
            {
                nowPhase = trail[(size_t) i].phase;
                break;
            }
        if (const auto w = amp.chorusTap.written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    // ---- the picture --------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto c = centre();
        const float R = reach(), r = R * radiusFraction();

        // The reach, faint lilac; the radius the slider gives, orange; the centre a hair.
        g.setColour (theme::lilac.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (2.0f * R, 2.0f * R).withCentre (c), 1.0f);
        if (r > 0.5f)
        {
            g.setColour (theme::orange);
            g.drawEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c), 1.2f);
        }
        g.setColour (theme::hair2);
        g.fillRect (juce::Rectangle<float> (7.0f, 1.0f).withCentre (c));
        g.fillRect (juce::Rectangle<float> (1.0f, 7.0f).withCentre (c));

        // The trails: an arc per hop, the oldest faintest; a restart or a hop not written breaks them.
        const int nv = voices();
        if (r > 0.5f)
            for (int i = 1; i < shown; ++i)
            {
                const auto& a = trail[(size_t) i - 1];
                const auto& b = trail[(size_t) i];
                const double step = (double) b.phase - (double) a.phase + (b.phase < a.phase ? 1.0 : 0.0);
                if (! (a.ok && b.ok && b.joined) || step > 0.25)
                    continue;
                const float alpha = 0.7f * (float) i / (float) (shown - 1);
                for (int v = 0; v < nv; ++v)
                {
                    juce::Path arc;
                    arc.addCentredArc (c.x, c.y, r, r, 0.0f, arcAngle (a.phase, v), arcAngle (a.phase, v) - (float) (step * juce::MathConstants<double>::twoPi), true);
                    g.setColour (theme::violet.withAlpha (alpha));
                    g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

        // The dots, solid violet, and their names riding outside them.
        for (int v = 0; v < nv; ++v)
        {
            const auto at = dotAt (v);
            g.setColour (theme::violet);
            g.fillEllipse (juce::Rectangle<float> (2.0f * dotR, 2.0f * dotR).withCentre (at));
            if (nv == 2)
            {
                const auto out = pointAt (nowPhase, v, r + labelGap);
                g.setColour (theme::tx);
                theme::drawTracked (g, v == 0 ? "L" : "R", juce::Rectangle<float> (16.0f, textHeight + 2.0f).withCentre (out),
                                    theme::displayFont (textHeight), 0.0f, juce::Justification::centred);
            }
        }

        // The RATE, top right: the hand's, orange.
        paintField (g, Field::rate, theme::orange, juce::Justification::centredRight);

        // THE CROSSOVER: a lilac line across the foot, the number under it — below it the bass stays mono.
        const auto foot = getLocalBounds().toFloat().removeFromBottom (rowH);
        g.setColour (theme::lilac);
        g.fillRect (foot.getX(), foot.getY() - 2.0f, foot.getWidth(), 1.5f);
        g.setColour (theme::lilac.withAlpha (0.75f));
        theme::drawTracked (g, "MONO", foot.withRight (fieldBox (Field::crossover).getX()), theme::displayFont (textHeight), 0.06f,
                            juce::Justification::centredLeft);
        paintField (g, Field::crossover, theme::lilac, juce::Justification::centredRight);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto over = fieldAt (e.position);
        setMouseCursor (over != nullptr ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
        if (over != hovered)
        {
            hovered = over;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = nullptr;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            if (const auto* f = fieldAt (e.position))
            {
                openField (*f);
                return;
            }
        if (onMenu != nullptr)
            onMenu (e.getScreenPosition());
    }

    /** The timer's work: whether it is on screen NOW — its own visibility, every ancestor's, the window's (a hidden
        block or a minimised window tells nobody below it) — the tap written only while it is, and read then. */
    void look()
    {
        const bool on = isShowing();
        amp.chorusWatched.store (on, std::memory_order_relaxed);
        if (on)
            pull();
    }

private:
    juce::RangedAudioParameter& param (Field f) const { return f == Field::rate ? rateP : crossP; }

    /** The circle's room: under the rate's row, over the crossover's. */
    juce::Rectangle<float> circleArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (rowH * 0.5f).withTrimmedBottom (rowH + 4.0f);
    }

    /** Voice v's angle at `phase`, counter-clockwise from three o'clock: the left voice's swing is sin, the right's
        cos — a quarter turn on. */
    static double angleOf (float phase, int v)
    {
        return juce::MathConstants<double>::twoPi * (double) phase + (v == 1 ? juce::MathConstants<double>::halfPi : 0.0);
    }

    juce::Point<float> pointAt (float phase, int v, float radius) const
    {
        const double a = angleOf (phase, v);
        const auto   c = centre();
        return { c.x + radius * (float) std::cos (a), c.y - radius * (float) std::sin (a) };
    }

    /** The same angle as juce::Path's arcs count it: clockwise from twelve o'clock. */
    static float arcAngle (float phase, int v) { return (float) (juce::MathConstants<double>::halfPi - angleOf (phase, v)); }

    static juce::String numberText (Field f, float v)
    {
        if (f == Field::crossover)
            return juce::String (juce::roundToInt (v));
        auto s = juce::String (v, 2);
        while (s.containsChar ('.') && (s.endsWithChar ('0') || s.endsWithChar ('.')))
            s = s.dropLastCharacters (1);
        return s;
    }

    const Field* fieldAt (juce::Point<float> p) const
    {
        static constexpr Field all[] { Field::rate, Field::crossover };
        for (const auto& f : all)
            if (fieldBox (f).contains (p))
                return &f;
        return nullptr;
    }

    void paintField (juce::Graphics& g, Field f, juce::Colour colour, juce::Justification just)
    {
        if (editor.isOpen() && editing == f)
            return;
        auto& p = param (f);
        const bool over = hovered != nullptr && *hovered == f;
        g.setColour (over ? colour.brighter (0.35f) : colour);
        theme::drawTracked (g, numberText (f, p.convertFrom0to1 (p.getValue())) + " Hz", fieldBox (f).reduced (2.0f, 0.0f),
                            theme::displayFont (textHeight), 0.04f, just);
    }

    void timerCallback() override { look(); }

    AmpProcessor& amp;
    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& rateP;
    juce::RangedAudioParameter& crossP;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::array<core::ChorusTap::Entry, (size_t) trailHops + 1> trail {};
    int   shown = 0;
    float nowPhase = 0.0f;
    std::uint32_t lastWritten = 0;
    meterrail::GripEditor editor;
    Field editing = Field::rate;
    const Field* hovered = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusPicture)
};

/** THE BASS'S CHORUS, A BLOCK WITH ITS PICTURE (the owner, 31.10): at the head of the cabinet's row, as in the strip.
    Its SLIDER on the left (a rail over CHORUS's one knob, 0..1: the swing 0 … 3 ms and the voice's level with it),
    its PICTURE right of it — the slider seen another way. RATE, CROSSOVER and CENTRE are its menu, on a click anywhere
    on it but the picture's numbers. Our own DSP, so violet; its arrow in the strip switches it, and standing by it
    dims in place, still in reach. */
class ChorusBlock final : public BlockFrame
{
public:
    explicit ChorusBlock (AmpProcessor& processor)
        : BlockFrame ("CHORUS", Kind::dsp),
          state (processor.apvts),
          rail (*processor.apvts.getParameter (params::chorusAmount), *processor.apvts.getParameter (params::chorusOn), 0.0f, 1.0f, false),
          circle (processor)
    {
        shieldsWhenOff = false;
        bass::asBassRail (rail);   // the amount in percent, a field too
        bass::hint (rail, "CHORUS: how much of the swirl is mixed in, 0-100 %");
        rail.onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };   // one undo step a drag, as COMP's
        addAndMakeVisible (rail);
        addAndMakeVisible (circle);
        const auto menu = [this] (juce::Point<int>) { bass::showChorusMenu (state, this); };
        rail.onMenu = circle.onMenu = menu;
        attachPower (*processor.apvts.getParameter (params::chorusOn));
        dimWhenIdle (circle, *processor.apvts.getParameter (params::chorusAmount));
    }

    void mouseDown (const juce::MouseEvent&) override { bass::showChorusMenu (state, this); }

    ChorusPicture& picture() noexcept { return circle; }
    GuardRail&     fader() noexcept   { return rail; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        rail.setBounds (r.removeFromLeft (GuardRail::designWidth));
        r.removeFromLeft (gap);
        circle.setBounds (r);
    }

private:
    static constexpr int gap = 10;

    juce::AudioProcessorValueTreeState& state;
    GuardRail     rail;
    ChorusPicture circle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusBlock)
};

} // namespace orbitamp
