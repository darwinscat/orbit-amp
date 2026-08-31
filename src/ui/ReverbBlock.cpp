// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "ReverbBlock.h"

#include "../Parameters.h"
#include "Prefs.h"
#include "../PluginProcessor.h"

#include <felitronics/analysis/PlotMap.h>

namespace orbitamp
{

namespace
{
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;

    float xForFreq (juce::Rectangle<float> r, float f)
    {
        return r.getX() + r.getWidth() * std::log (f / kFMin) / std::log (kFMax / kFMin);
    }

    float freqForX (juce::Rectangle<float> r, float x)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth()));
        return kFMin * std::pow (kFMax / kFMin, t);
    }
}

ReverbBlock::ReverbBlock (AmpProcessor& processor)
    : BlockFrame ("Reverb", BlockFrame::Kind::dsp), amp (processor)
{
    startTimerHz (30);
    display.prepare (displayRate, (int) (displayRate * tailSeconds));

    addAndMakeVisible (character);
    addAndMakeVisible (view);
    addAndMakeVisible (mix);
    addAndMakeVisible (decay);
    addAndMakeVisible (pre);
    attachPower (*amp.apvts.getParameter (params::reverbOn));

    // The character IS the block's name: PLATE or HALL on the border where REVERB stood, set like
    // a name and in the block's colour. A quarter-width block has no room for both, and the
    // colour already says which kind of block this is.
    showTitle = false;
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::reverbCharacters)
            entries.add ({ name, 0, false });
        character.setEntries (std::move (entries));
    }
    character.fontHeight = 16.0f;
    character.tracking   = 0.15f;
    character.boxed      = false;
    character.tint       = theme::lilac;

    mix.textForValue   = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };
    decay.textForValue = [] (double v) { return juce::String (v, 1) + "x"; };
    pre.textForValue   = [] (double v) { return juce::String (juce::roundToInt (v)); };
    decay.labelFontHeight = 7.0f;
    pre.labelFontHeight   = 7.0f;

    mix.onValueChange   = [this] { refreshTail(); };
    decay.onValueChange = [this] { refreshTail(); };
    pre.onValueChange   = [this] { refreshTail(); };

    characterAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::reverbType),
        [this] (float v)
        {
            character.setSelection (juce::roundToInt (v));
            refreshTail();
            resized();   // the name on the border is as wide as the name
        });

    character.onPick = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbMix, mix);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbDecay, decay);
    preAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::reverbPredelay, pre);

    // The tail's HPF: the consoles' cut, at home in the reverb and ALWAYS in — the dashed
    // vertical is the handle, the curve is what it does, and there is nothing to switch.
    hpfHzAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::reverbHpfHz), [this] (float) { view.repaint(); });

    const auto plain = [this] (const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    };

    view.hitCut = [this, plain] (juce::Point<float> pos)
    {
        return std::abs (pos.x - xForFreq (view.getLocalBounds().toFloat(),
                                           plain (params::reverbHpfHz))) < 14.0f;
    };

    // Right-click: the block's own gear — the current reverb back to what it ships as. The
    // character stays; a reset is "clean the room", not "move house".
    view.onMenu = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "RESET TO DEFAULT");
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                         [safe = juce::Component::SafePointer<ReverbBlock> (this)] (int rr)
                         {
                             if (safe == nullptr || rr != 1)
                                 return;

                             for (const char* id : { params::reverbMix, params::reverbDecay,
                                                     params::reverbPredelay, params::reverbHpfHz })
                                 if (auto* p = safe->amp.apvts.getParameter (id))
                                 {
                                     p->beginChangeGesture();
                                     p->setValueNotifyingHost (p->getDefaultValue());
                                     p->endChangeGesture();
                                 }
                         });
    };

    view.onCut = [this] (float x, int phase)
    {
        if (phase == 0) hpfHzAtt->beginGesture();

        auto* p = amp.apvts.getParameter (params::reverbHpfHz);
        const auto range = amp.apvts.getParameterRange (params::reverbHpfHz);
        juce::ignoreUnused (p);
        hpfHzAtt->setValueAsPartOfGesture (
            juce::jlimit (range.start, range.end,
                          freqForX (view.getLocalBounds().toFloat(), x)));

        if (phase == 2) hpfHzAtt->endGesture();
    };

    view.paintBody = [this, plain] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // The ground and its bones: a dark well and the decade lines.
        g.setColour (juce::Colour (0xff101016));
        g.fillRoundedRectangle (r, theme::radiusSm);

        for (const float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = xForFreq (r, f);
            g.setColour (juce::Colour (0x14ffffff));
            g.drawVerticalLine ((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f);
            g.setColour (juce::Colour (0x55b0b0b0));
            g.setFont (theme::displayFont (8.5f));
            g.drawText (f < 1000.0f ? "100" : f < 10000.0f ? "1K" : "10K",
                        juce::Rectangle<float> (x + 3.0f, r.getBottom() - 13.0f, 30.0f, 11.0f),
                        juce::Justification::topLeft, false);
        }

        // The pair: the door as the quiet grey ground, the ADDED tail in the block's violet —
        // and the decay is how long the violet keeps glowing after the note.
        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = (double) kFMin;
        pm.freqMax    = (double) kFMax;
        pm.specTop    = 0.0;
        pm.specBottom = -90.0;

        const double fs = juce::jmax (8000.0, amp.currentSampleRate());

        const bool spectraOn = prefs::spectraShown();   // the gear's one switch for every analyser

        const auto draw = [&] (felitronics::analysis::SpectrumPane& pane, juce::Colour tint,
                               float fillTop, float fillBottom, float line)
        {
            juce::Path fill, peak;
            fill.startNewSubPath (r.getX(), r.getBottom());
            bool first = true;

            pane.buildColumns (pm, fs, 4.5, 1000.0,
                               [&] (int, float x, float yFill, float yPeak)
                               {
                                   fill.lineTo (r.getX() + x, r.getY() + yFill);

                                   // At the floor the comb LEAVES through it instead of crawling
                                   // along it: a point pinned to the plot's bottom is pushed past
                                   // the component's edge, and the clip swallows the run between
                                   // two sunken points — both flanks vanish under the floor.
                                   const float yp = yPeak >= r.getHeight() - 1.5f
                                                        ? r.getBottom() + 4.0f
                                                        : r.getY() + yPeak;

                                   if (first) { peak.startNewSubPath (r.getX() + x, yp); first = false; }
                                   else       peak.lineTo (r.getX() + x, yp);
                               });

            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();

            g.setGradientFill (juce::ColourGradient (tint.withAlpha (fillTop),
                                                     0.0f, r.getY() + r.getHeight() * 0.30f,
                                                     tint.withAlpha (fillBottom),
                                                     0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (tint.withAlpha (line));
            g.strokePath (peak, juce::PathStrokeType (1.0f));
        };

        if (spectraOn)
        {
            draw (panes[0], theme::spectrum, 0.14f, 0.02f, 0.30f);   // the door
            draw (panes[1], theme::orange,   0.18f, 0.02f, 0.55f);   // what the room adds — the cab's own pairing
        }

        // The decay envelope over it all — the reverb's signature silhouette, an impulse's peak
        // level over time. Another axis on purpose: it is a STAMP, not a second plot — the one
        // shape that says how long this room breathes.
        if (tailEnv.size() >= 2)
        {
            const auto inner = r.reduced (4.0f);

            // The stroke is the envelope's TOP alone — a closed path drew its own floor and read
            // as an axis nobody asked for. The fill still closes along the bottom, quietly.
            juce::Path line;

            for (size_t i = 0; i < tailEnv.size(); ++i)
            {
                const float t  = (float) i / (float) (tailEnv.size() - 1);
                const float up = juce::jlimit (0.0f, 1.0f, (tailEnv[i] + 60.0f) / 60.0f);
                const float x  = inner.getX() + t * inner.getWidth();
                // On the floor the silhouette sinks past the edge like the combs do — the
                // predelay's silence and the died-away tail leave no crawling line.
                const float y  = up <= 0.001f ? r.getBottom() + 4.0f
                                              : inner.getBottom() - up * inner.getHeight();
                if (i == 0) line.startNewSubPath (x, y);
                else        line.lineTo (x, y);
            }

            juce::Path tp = line;
            tp.lineTo (inner.getRight(), inner.getBottom());
            tp.lineTo (inner.getX(),     inner.getBottom());
            tp.closeSubPath();

            g.setColour (theme::violet.withAlpha (0.10f));
            g.fillPath (tp);
            g.setColour (theme::violet.withAlpha (0.60f));
            g.strokePath (line, juce::PathStrokeType (1.2f));

            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String (tailSeconds, 1) + " S", r.reduced (8.0f, 5.0f),
                                theme::displayFont (7.5f), 0.1f, juce::Justification::bottomRight);
        }

        // The tail's HPF, always in: the second-order curve and its dashed vertical — the
        // consoles' cut grammar, one Q, no ladder, no switch.
        {
            const float fc = plain (params::reverbHpfHz);

            // The cab picture's own placement: the passband rides low (0.62 of the height) and the
            // dive leaves through the floor — the curve is a hint under the spectra, not a plot
            // pinned to the ceiling.
            juce::Path curve;
            const int w = juce::jmax (2, (int) r.getWidth());
            const float top = r.getY() + r.getHeight() * 0.62f;
            const float bot = r.getY() + r.getHeight() * 1.30f;
            for (int px = 0; px < w; ++px)
            {
                const float f   = freqForX (r, r.getX() + (float) px);
                const float rr  = fc / juce::jmax (1.0f, f);
                const float mag = 1.0f / std::sqrt (1.0f + rr * rr * rr * rr);
                const float y   = bot - (bot - top) * mag;
                if (px == 0) curve.startNewSubPath (r.getX(), y);
                else         curve.lineTo (r.getX() + (float) px, y);
            }

            g.setColour (theme::orange.withAlpha (0.8f));
            g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

            const float x = xForFreq (r, fc);
            const float dashes[] = { 5.0f, 4.0f };
            g.setColour (theme::orange.withAlpha (view.dragging ? 1.0f : 0.65f));
            g.drawDashedLine ({ x, r.getY() + 2.0f, x, r.getBottom() - 2.0f },
                              dashes, 2, view.dragging ? 2.2f : 1.4f);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
    };

    characterAttachment->sendInitialUpdate();
}

ReverbBlock::~ReverbBlock() = default;

void ReverbBlock::refreshTail()
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1, character.getSelection());

    display.setCharacter (static_cast<core::ReverbStage::Character> (index));
    display.setMix ((float) mix.getValue() * 0.01f);
    display.setDecay ((float) decay.getValue());
    display.setPredelayMs ((float) pre.getValue());
    display.reset();

    // juce::Reverb ramps its gains over ~10 ms, so a fresh setting has to be run in before the
    // impulse — otherwise the tail is measured mid-ramp and the picture lags the sound.
    const int n = (int) (displayRate * tailSeconds);
    std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
    float* channels[2] = { left.data(), right.data() };

    display.process (channels, 2, juce::jmin (n, (int) (displayRate * 0.05)));

    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    left[0] = right[0] = 1.0f;
    display.process (channels, 2, n);

    // The stage ADDS the wet and leaves the dry alone — so the impulse itself is still sitting at
    // sample zero, and measured it would spike the envelope to 0 dB before the room says a word.
    // The silhouette is the TAIL's; take the dry back out.
    left[0] = right[0] = 0.0f;

    // Peak per bucket, in dB — the shape of the decay, not its every wiggle.
    std::vector<float> env ((size_t) tailBuckets, -60.0f);
    const int per = juce::jmax (1, n / tailBuckets);

    for (int b = 0; b < tailBuckets; ++b)
    {
        float peak = 0.0f;
        for (int i = b * per; i < juce::jmin (n, (b + 1) * per); ++i)
            peak = juce::jmax (peak, std::abs (left[(size_t) i]));

        env[(size_t) b] = peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -60.0f;
    }

    tailEnv = std::move (env);
    view.repaint();
}

void ReverbBlock::timerCallback()
{
    if (! isBlockOn() || ! view.isShowing())
        return;

    for (size_t i = 0; i < panes.size(); ++i)
    {
        int order = 0;

        if (amp.reverbSpectrumTap[i].tryPull (panes[i].frameInput(), order)
            && order == AmpProcessor::eqSpectrumOrder)
            panes[i].ingest (order);
        else
            panes[i].starve();
    }

    view.repaint();
}

void ReverbBlock::layOutContent (juce::Rectangle<int> area)
{
    // The character on the border where the name would stand: at the left, sized to itself.
    {
        const auto slot = borderSlotArea();
        character.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), character.idealWidth())));
        borderSlotUsed = character.getBounds();
    }

    // The picture takes the whole box; everything else overlays it.
    view.setBounds (area);

    const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight() / 2 + 24));
    auto row = area.removeFromTop (side);
    mix.setBounds (row.removeFromRight (side).translated (-2, 2));
    mix.toFront (false);

    // The two refinements back at the hero's left hand — small, and pressed to the TOP edge
    // rather than riding the hero's centre line.
    const int mini = 42;
    auto miniRow = row.removeFromRight (mini * 2 + 6).withHeight (mini + 11).translated (0, 2);
    decay.setBounds (miniRow.removeFromLeft (mini));
    miniRow.removeFromLeft (6);
    pre.setBounds (miniRow);
    decay.toFront (false);
    pre.toFront (false);
}

} // namespace orbitamp
