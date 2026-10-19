// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Monitor.h"

#include "Controls.h"

#include <felitronics/analysis/PlotMap.h>

namespace orbitamp::face
{

namespace
{
    constexpr int preampBlock   = 0;     // the processor's captured block: the one the tone stack follows
    constexpr double displayRate = 48000.0;
}

const juce::StringArray& Monitor::names()
{
    static const juce::StringArray all { "None", "Spectrum", "Wave", "Shape", "Envelope", "Transfer" };
    return all;
}

Monitor::Monitor (AmpProcessor& p) : amp (p)
{
    setInterceptsMouseClicks (false, false);    // a right click on the glass is still a click on the panel
    setVisible (false);
}

void Monitor::show (View v)
{
    view = v;
    setVisible (v != View::none);

    // The spectrum arrives in frames; the wave slides, and at less than this it reads as a slideshow.
    if (v == View::none)           stopTimer();
    else if (v == View::spectrum)  startTimerHz (30);
    else                           startTimerHz (60);

    repaint();
}

juce::Rectangle<float> Monitor::band() const
{
    return area.isEmpty() ? getLocalBounds().toFloat() : area;
}

void Monitor::followSilence()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt  = lastTickMs > 0.0 ? juce::jlimit (0.0, 100.0, now - lastTickMs) : 0.0;
    lastTickMs = now;

    const bool quiet = juce::Decibels::gainToDecibels (dryPeak, -120.0f) < quietDb;
    if (! quiet)
        quietSinceMs = -1.0;
    else if (quietSinceMs < 0.0)
        quietSinceMs = now;

    const bool gone = quiet && now - quietSinceMs >= quietMs;
    shapeShown = gone ? (float) juce::jmax (0.0, shapeShown - dt / fadeOutMs)
                      : (float) juce::jmin (1.0, shapeShown + dt / fadeInMs);
}

void Monitor::timerCallback()
{
    if (view != View::spectrum)
    {
        if (view == View::shape && shapeFadesInSilence)
            followSilence();
        repaint (band().getSmallestIntegerContainer());     // the scope pictures read their taps as they paint
        return;
    }

    // A frame from each side of the tone stack, if one is waiting.
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

    repaint (band().getSmallestIntegerContainer());
}

void Monitor::paint (juce::Graphics& g)
{
    // Nothing under the picture but what the glass shows behind it: it is a second layer on the glass.
    const auto r = band();
    if (view == View::spectrum)
    {
        paintSpectrum (g, r);
        return;
    }

    const juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (r.toNearestInt());

    // OrbitAmp's painters, told to draw bare: lines alone, no rules and nothing filled — over the sky a
    // translucent body is a stain on it.
    if (view == View::wave)
    {
        waveView.paint (g, r, amp.preamp.ribbon, true, true);
        return;
    }

    const bool fresh = amp.preamp.scope.read (dry, wet);
    const scope::Frame frame = fresh ? scope::Frame { dry.data(), wet.data(), core::ScopeTap::size } : scope::Frame {};
    // What SHAPE's silence is judged by: the peak of the input side it is drawn from; no frame is no sound.
    dryPeak = 0.0f;
    if (fresh)
        for (const float v : dry)
            dryPeak = juce::jmax (dryPeak, std::abs (v));
    if (view == View::shape)
    {
        if (! shapeFadesInSilence)
            scope::ShapeView::paint (g, r, frame);
        else if (shapeShown > 0.0f)
        {
            g.beginTransparencyLayer (shapeShown);
            scope::ShapeView::paint (g, r, frame);
            g.endTransparencyLayer();
        }
    }
    else if (view == View::envelope)  scope::EnvelopeView::paint (g, r, frame, true);
    else if (view == View::transfer)  scope::TransferView::paint (g, r, frame);
}

void Monitor::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    // No well, no grid, no frame: the spectrum is light on the glass, from one edge of the band to the
    // other, and the face's frame is what cuts it off.
    const double lo = 20.0, hi = 20000.0;

    const juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (r.withTrimmedBottom (2.0f).toNearestInt());   // a trace lying on the floor is not drawn: silence is dark glass

    felitronics::analysis::PlotMap pm;
    pm.width      = r.getWidth();
    pm.height     = r.getHeight();
    pm.plotBottom = r.getHeight();
    pm.freqMin    = lo;
    pm.freqMax    = hi;
    pm.specTop    = 0.0;
    pm.specBottom = -90.0;

    const double rate = amp.currentSampleRate() > 0.0 ? amp.currentSampleRate() : displayRate;
    // Lines alone: the peak trace of each spectrum, with nothing filled under it.
    const auto draw = [&] (felitronics::analysis::SpectrumPane& pane, juce::Colour tint, float thickness)
    {
        juce::Path peak;
        bool first = true;
        pane.buildColumns (pm, rate, 4.5, 1000.0, [&] (int, float x, float, float yPeak)
        {
            if (first) { peak.startNewSubPath (r.getX() + x, r.getY() + yPeak); first = false; }
            else       peak.lineTo (r.getX() + x, r.getY() + yPeak);
        });
        g.setColour (tint);
        g.strokePath (peak, juce::PathStrokeType (thickness));
    };

    // OrbitAmp's grammar for a pair: what goes in is the quiet grey, what comes out is the hot colour
    // over it.
    draw (before, juce::Colour (0xff9fb0cc).withAlpha (0.7f), 1.1f);
    draw (after,  orange, 1.4f);
}

} // namespace orbitamp::face
