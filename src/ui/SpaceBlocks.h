// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BassSettingsOpener.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "../PluginProcessor.h"
#include "../core/SpaceTap.h"

#include <array>
#include <vector>

namespace orbitamp
{

/** THE BASS'S DELAY AND REVERB PICTURES (the owner, 31.10): what the stage ADDS, live — the loudest of its wet every
    10 ms (core::SpaceTap), the newest at the right edge, on a level axis from -60 dB to 0 — over the law it follows,
    in lilac: DELAY's REPEATS, one bar a repeat at its time, as high as MIX and REPS make it (what one note at the left
    edge would do); REVERB's TAIL, a line from MIX at the pre-delay down to -60 dB where the room says its tail ends.
    The word at the top is DELAY's time (its division when synced) or REVERB's type. Every word 13 px.

    The tap is read on its own timer only while the picture is on screen — the timer asks every tick, so a hidden
    ancestor stops it too, and the audio thread writes it only then; out of the strip the stage is not run and the
    picture stands still. A click anywhere is the block's window. */
class SpacePicture final : public juce::Component,
                           private juce::Timer
{
public:
    enum class Which { delay, reverb };

    SpacePicture (AmpProcessor& processor, Which w)
        : amp (processor), which (w)
    {
        for (const char* id : w == Which::delay
                                  ? std::vector<const char*> { params::delayMix, params::delayRepeats, params::delayTimeMs,
                                                               params::delaySync, params::delayDiv }
                                  : std::vector<const char*> { params::reverbMix, params::reverbType, params::reverbDecay,
                                                               params::reverbPredelay })
            if (auto* p = processor.apvts.getParameter (id))
                atts.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); }));
        startTimerHz (30);   // its look, every tick: on screen or not (look)
    }

    ~SpacePicture() override
    {
        stopTimer();
        watched().store (false, std::memory_order_relaxed);
    }

    static constexpr float textHeight = 13.0f, rowH = 18.0f, floorDb = -60.0f, tracking = 0.0f;   // MODULATED fits the least

    std::function<void (juce::Point<int> screenPos)> onMenu;

    // ---- what it shows ------------------------------------------------------------------------------------------
    /** The seconds across: four and a half repeats of DELAY's time, REVERB's tail and a tenth — 1 to 5 s. */
    double spanSeconds() const
    {
        const double s = which == Which::delay ? 4.5 * timeSeconds() : 1.1 * (double) amp.reverbTailSeconds();
        return juce::jlimit (1.0, 5.0, std::isfinite (s) ? s : 1.0);
    }

    /** The word at the top: DELAY's division when synced, its milliseconds when free; REVERB's type. */
    juce::String headline() const
    {
        if (which == Which::reverb)
            return params::reverbCharacters[juce::jlimit (0, params::reverbCharacters.size() - 1,
                                                          juce::roundToInt (plain (params::reverbType)))].toUpperCase();
        if (plain (params::delaySync) > 0.5f)
            return params::delayDivisions[juce::jlimit (0, params::delayDivisions.size() - 1, juce::roundToInt (plain (params::delayDiv)))];
        return juce::String (juce::roundToInt (plain (params::delayTimeMs))) + " ms";
    }

    /** Every word the top row may ever write — a gate measures them against the picture's width. */
    static juce::StringArray everyHeadline (Which w)
    {
        juce::StringArray all;
        if (w == Which::reverb)
            for (const auto& c : params::reverbCharacters)
                all.add (c.toUpperCase());
        else
        {
            all.addArray (params::delayDivisions);
            all.add (juce::String (juce::roundToInt (params::delayTimeMaxMs)) + " ms");
        }
        return all;
    }

    /** THE LAW, as points (seconds after a note, level in dB): DELAY's repeats — one a repeat, MIX x REPS^(k-1); REVERB's
        tail — MIX at the pre-delay, -60 dB at the tail's end. Below the floor, none. */
    std::vector<juce::Point<float>> lawMarks() const
    {
        std::vector<juce::Point<float>> marks;
        const float mix = plain (which == Which::delay ? params::delayMix : params::reverbMix) * 0.01f;
        if (! (mix > 0.0f))
            return marks;
        if (which == Which::delay)
        {
            const double t = timeSeconds(), span = spanSeconds();
            const float  reps = plain (params::delayRepeats) * 0.01f;
            float level = mix;
            for (int k = 1; t > 0.0 && k * t <= span && level > 0.0f; ++k, level *= reps)
            {
                const float db = juce::Decibels::gainToDecibels (level, -200.0f);
                if (db < floorDb)
                    break;
                marks.push_back ({ (float) (k * t), db });
            }
        }
        else
        {
            const float pre  = plain (params::reverbPredelay) * 0.001f;
            const float tail = juce::jmax (pre + 0.05f, amp.reverbTailSeconds());
            marks.push_back ({ pre, juce::Decibels::gainToDecibels (mix, -200.0f) });
            marks.push_back ({ tail, floorDb });
        }
        return marks;
    }

    /** How many entries it holds now, and the newest's level (dB) — 0 entries before any. */
    int shownCount() const noexcept { return shown; }
    float newestDb() const noexcept
    {
        return shown > 0 ? juce::Decibels::gainToDecibels (trail[(size_t) shown - 1].level, floorDb) : floorDb;
    }

    /** What the tap holds, the newest last — the timer's work, and a picture's made off screen. */
    void pull()
    {
        shown = tap().read (trail.data(), (int) trail.size());
        if (const auto w = tap().written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    /** The timer's work: whether it is on screen NOW — its own visibility, every ancestor's, the window's — the tap
        written only while it is, and read then. */
    void look()
    {
        const bool on = isShowing();
        watched().store (on, std::memory_order_relaxed);
        if (on)
            pull();
    }

    // ---- the picture --------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto plot = plotArea();
        const double span = spanSeconds();
        const auto xAt = [&] (double seconds) { return plot.getX() + (float) (seconds / span) * plot.getWidth(); };
        const auto yAt = [&] (float db) { return plot.getBottom() - (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb * plot.getHeight(); };

        // The floor and the 0 dB line, hairs.
        g.setColour (theme::hair2);
        g.fillRect (plot.getX(), plot.getBottom() - 1.0f, plot.getWidth(), 1.0f);
        g.setColour (theme::hair);
        g.fillRect (plot.getX(), plot.getY(), plot.getWidth(), 1.0f);

        // THE LIVE TRACE: what the stage added, the newest at the right edge; a hop not written breaks it.
        const double hop = core::SpaceTap::hopSeconds;
        juce::Path fill, line;
        bool open = false;
        float lastX = 0.0f;
        for (int i = 0; i < shown; ++i)
        {
            const auto& e = trail[(size_t) i];
            const double ago = (double) (shown - 1 - i) * hop;
            if (ago > span || ! e.ok)
            {
                open = false;
                continue;
            }
            const float x = plot.getRight() - (float) (ago / span) * plot.getWidth();
            const float y = yAt (juce::Decibels::gainToDecibels (e.level, floorDb - 1.0f));
            if (! open || ! e.joined)
            {
                if (open)
                    fill.lineTo (lastX, plot.getBottom()), fill.closeSubPath();
                fill.startNewSubPath (x, plot.getBottom());
                fill.lineTo (x, y);
                line.startNewSubPath (x, y);
                open = true;
            }
            else
            {
                fill.lineTo (x, y);
                line.lineTo (x, y);
            }
            lastX = x;
        }
        if (open)
            fill.lineTo (lastX, plot.getBottom()), fill.closeSubPath();
        g.setColour (theme::violet.withAlpha (0.35f));
        g.fillPath (fill);
        g.setColour (theme::violet);
        g.strokePath (line, juce::PathStrokeType (1.4f));

        // THE LAW, lilac: the repeats as bars, the tail as a line.
        const auto marks = lawMarks();
        g.setColour (theme::lilac);
        if (which == Which::delay)
        {
            for (const auto& m : marks)
                g.fillRect (juce::Rectangle<float> (xAt (m.x) - 1.0f, yAt (m.y), 2.0f, plot.getBottom() - yAt (m.y)));
        }
        else if (marks.size() == 2)
        {
            g.drawLine (xAt (marks[0].x), yAt (marks[0].y), xAt (juce::jmin ((double) marks[1].x, span)),
                        yAt (marks[1].x > span ? marks[0].y + (marks[1].y - marks[0].y) * (float) ((span - marks[0].x) / (marks[1].x - marks[0].x))
                                               : marks[1].y),
                        1.5f);
        }

        // The word at the top.
        g.setColour (theme::tx);
        theme::drawTracked (g, headline(), getLocalBounds().toFloat().removeFromTop (rowH), theme::displayFont (textHeight), tracking,
                            juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onMenu != nullptr)
            onMenu (e.getScreenPosition());
    }

private:
    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().withTrimmedTop (rowH + 4.0f).reduced (0.0f, 1.0f); }

    float plain (const char* id) const
    {
        auto* p = amp.apvts.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    }

    /** DELAY's time now, seconds: the stage's own read-out once it has run, the knob's before. */
    double timeSeconds() const
    {
        const float shownMs = amp.delayTaps().shownTimeMs();
        return (double) (shownMs > 0.0f ? shownMs : plain (params::delayTimeMs)) * 0.001;
    }

    core::SpaceTap&    tap() const noexcept     { return which == Which::delay ? amp.delayTap : amp.reverbTap; }
    std::atomic<bool>& watched() const noexcept { return which == Which::delay ? amp.delayWatched : amp.reverbWatched; }

    void timerCallback() override { look(); }

    AmpProcessor& amp;
    const Which which;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    std::array<core::SpaceTap::Entry, (size_t) core::SpaceTap::slots> trail {};
    int shown = 0;
    std::uint32_t lastWritten = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpacePicture)
};

/** THE BASS'S DELAY OR REVERB, A COMPACT BLOCK (the owner, 31.10): before the cabinet, after CHORUS, as on the guitar.
    Its RAIL on the left — MIX, the bass's rails' look (an orange hand, its percent at 13 px, a click types it, one undo
    step a drag) — and its PICTURE beside it. Everything else is in its GEAR's window (ui/BassSettings.h), which a click
    anywhere else on it, or a right click, opens too. Out of the strip it is not on the face at all. */
class SpaceBlock final : public BlockFrame
{
public:
    SpaceBlock (AmpProcessor& processor, SpacePicture::Which w)
        : BlockFrame (w == SpacePicture::Which::delay ? "DELAY" : "REVERB", Kind::dsp),
          rail (*processor.apvts.getParameter (w == SpacePicture::Which::delay ? params::delayMix : params::reverbMix),
                *processor.apvts.getParameter (w == SpacePicture::Which::delay ? params::delayOn : params::reverbOn), 0.0f, 100.0f, false),
          picture (processor, w),
          name (w == SpacePicture::Which::delay ? "DELAY" : "REVERB")
    {
        setName (name);
        shieldsWhenOff = false;
        bass::asBassRail (rail);
        rail.textForValue = [] (float v) { return juce::String (juce::roundToInt (v)); };   // MIX is percent already
        rail.typedScale   = 1.0f;
        rail.onGesture = [&processor] (bool begin) { if (begin) processor.history.beginGesture(); else processor.history.endGesture(); };
        addAndMakeVisible (rail);
        addAndMakeVisible (picture);
        rail.onMenu = picture.onMenu = [this] (juce::Point<int>) { openWindow(); };
        attachPower (*processor.apvts.getParameter (w == SpacePicture::Which::delay ? params::delayOn : params::reverbOn));
    }

    void mouseDown (const juce::MouseEvent&) override { openWindow(); }

    SpacePicture& view() noexcept { return picture; }
    GuardRail&    fader() noexcept { return rail; }

    static constexpr int gap = 8;

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        rail.setBounds (r.removeFromLeft (GuardRail::designWidth));
        r.removeFromLeft (gap);
        picture.setBounds (r);
    }

private:
    void openWindow() { (void) bass::openSettingsFrom (this, name); }

    GuardRail    rail;
    SpacePicture picture;
    const juce::String name;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpaceBlock)
};

} // namespace orbitamp
