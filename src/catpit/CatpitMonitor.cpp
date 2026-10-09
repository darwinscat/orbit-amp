// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitMonitor.h"

#include "CatpitControls.h"
#include "CatpitLayout.h"

#include "../ui/EqDisplay.h"

#include <felitronics/analysis/PlotMap.h>

namespace orbitamp::catpit
{

namespace
{
    constexpr int preampBlock   = 1;     // the processor's second captured block, and its console's link
    constexpr double displayRate = 48000.0;

    DeviceScope::Mode scopeModeFor (Monitor::View v)
    {
        switch (v)
        {
            case Monitor::View::wave:      return DeviceScope::Mode::wave;
            case Monitor::View::shape:     return DeviceScope::Mode::shape;
            case Monitor::View::envelope:  return DeviceScope::Mode::envelope;
            case Monitor::View::transfer:  return DeviceScope::Mode::transfer;
            case Monitor::View::stars:
            case Monitor::View::spectrum:
            case Monitor::View::numViews:  break;
        }
        return DeviceScope::Mode::wave;
    }

    bool isScopeView (Monitor::View v)
    {
        return v == Monitor::View::wave || v == Monitor::View::shape
            || v == Monitor::View::envelope || v == Monitor::View::transfer;
    }
}

const juce::StringArray& Monitor::names()
{
    static const juce::StringArray all { "Stars", "Spectrum", "Wave", "Shape", "Envelope", "Transfer" };
    return all;
}

Monitor::Monitor (AmpProcessor& p) : amp (p)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);    // a right click on the glass is still a click on the panel
    setVisible (false);
    console.prepare (displayRate, 1);
}

void Monitor::show (View v)
{
    view = v;
    setVisible (v != View::stars);

    if (isScopeView (v))
    {
        if (scope == nullptr)
        {
            // OrbitAmp's scope asks for the tone curve only for its own TONE page, which the cockpit
            // does not put up — its spectrum is drawn here, with the before beside the after.
            scope = std::make_unique<DeviceScope> (amp.preamp.scope, amp.preamp.ribbon, [] (double) { return 0.0; });
            scope->waveHalf = true;
            addChildComponent (*scope);
            resized();
        }
        scope->setSampleRate (amp.currentSampleRate());
        scope->setMode (scopeModeFor (v));
    }

    if (scope != nullptr)
        scope->setVisible (isScopeView (v));

    if (v == View::spectrum)
        startTimerHz (30);
    else
        stopTimer();

    repaint();
}

juce::Rectangle<float> Monitor::glass() const
{
    // In the render's space: clear of the overhead panel above, the emblem below, and the corners
    // the windshield's frame cuts off.
    return juce::Rectangle<float> (236.0f, 100.0f, 1200.0f, 134.0f)
               .translated (-layout::windshield.getX(), -layout::windshield.getY());
}

void Monitor::resized()
{
    if (scope != nullptr)
        scope->setBounds (glass().toNearestInt());
}

void Monitor::timerCallback()
{
    // The tone stack as the parameters have it now, and a frame from each side of it if one is waiting.
    console.setSettings (eqSettingsFor (amp.apvts, preampBlock));

    const auto pull = [] (felitronics::analysis::RollingSpectrumTap& tap, felitronics::analysis::SpectrumPane& pane)
    {
        int order = 0;
        if (tap.tryPull (pane.frameInput(), order) && order == AmpProcessor::eqSpectrumOrder)
            pane.ingest (order);
        else
            pane.starve();
    };
    pull (amp.blockInSpectrumTap[(size_t) preampBlock], before);
    pull (amp.blockSpectrumTap[(size_t) preampBlock],   after);

    repaint (glass().getSmallestIntegerContainer());
}

void Monitor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff030306));      // the glass, dark: the sky is not flying behind a picture

    if (view == View::spectrum)
        paintSpectrum (g, glass());
}

void Monitor::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double lo = 20.0, hi = 20000.0;
    const auto xOf = [&] (double hz) { return r.getX() + (float) (std::log10 (hz / lo) / std::log10 (hi / lo)) * r.getWidth(); };

    // The well, and a grid: a line at every decade, fainter ones at 2 and 5 between.
    g.setColour (juce::Colour (0xff050509));
    g.fillRoundedRectangle (r, 6.0f);
    for (const double decade : { 10.0, 100.0, 1000.0, 10000.0 })
        for (const double step : { 2.0, 5.0, 10.0 })
            if (const double hz = decade * step; hz > lo && hz < hi)
            {
                g.setColour (violet.withAlpha (juce::exactlyEqual (step, 10.0) ? 0.22f : 0.09f));
                g.fillRect (juce::Rectangle<float> (xOf (hz) - 0.5f, r.getY() + 3.0f, 1.0f, r.getHeight() - 6.0f));
            }
    g.setColour (violet.withAlpha (0.16f));
    g.fillRect (juce::Rectangle<float> (r.getX() + 3.0f, r.getCentreY() - 0.5f, r.getWidth() - 6.0f, 1.0f));   // 0 dB for the curve

    {
        const juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (r.toNearestInt());

        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = lo;
        pm.freqMax    = hi;
        pm.specTop    = 0.0;
        pm.specBottom = -90.0;

        const double rate = amp.currentSampleRate() > 0.0 ? amp.currentSampleRate() : displayRate;
        const auto draw = [&] (felitronics::analysis::SpectrumPane& pane, juce::Colour tint,
                               float fillTop, float fillBottom, float line)
        {
            juce::Path fill, peak;
            fill.startNewSubPath (r.getX(), r.getBottom());
            bool first = true;
            pane.buildColumns (pm, rate, 4.5, 1000.0, [&] (int, float x, float yFill, float yPeak)
            {
                fill.lineTo (r.getX() + x, r.getY() + yFill);
                if (first) { peak.startNewSubPath (r.getX() + x, r.getY() + yPeak); first = false; }
                else       peak.lineTo (r.getX() + x, r.getY() + yPeak);
            });
            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();

            g.setGradientFill (juce::ColourGradient (tint.withAlpha (fillTop), 0.0f, r.getY() + r.getHeight() * 0.30f,
                                                     tint.withAlpha (fillBottom), 0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (tint.withAlpha (line));
            g.strokePath (peak, juce::PathStrokeType (1.2f));
        };

        // OrbitAmp's grammar for a pair: what goes in is the quiet grey ground, what comes out is drawn
        // over it in the hot colour.
        draw (before, juce::Colour (0xff7f93b5), 0.16f, 0.02f, 0.35f);
        draw (after,  orange,                    0.20f, 0.02f, 0.60f);

        // The curve the three tone knobs are holding: +/-18 dB about the middle line.
        juce::Path curve;
        constexpr double span = 18.0;
        for (float x = 0.0f; x <= r.getWidth(); x += 2.0f)
        {
            const double hz = lo * std::pow (hi / lo, (double) x / (double) r.getWidth());
            const float  y  = r.getCentreY() - (float) (juce::jlimit (-span, span, console.magnitudeDb (hz)) / span)
                                                   * r.getHeight() * 0.5f;
            if (x <= 0.0f) curve.startNewSubPath (r.getX() + x, y);
            else           curve.lineTo (r.getX() + x, y);
        }
        g.setColour (lilac);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (violet.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

} // namespace orbitamp::catpit
