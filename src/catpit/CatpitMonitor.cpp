// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitMonitor.h"

#include "CatpitControls.h"
#include "CatpitLayout.h"

#include <felitronics/analysis/PlotMap.h>

namespace orbitamp::catpit
{

namespace
{
    constexpr int preampBlock   = 1;     // the processor's second captured block: the one the tone stack follows
    constexpr double displayRate = 48000.0;
}

const juce::StringArray& Monitor::names()
{
    static const juce::StringArray all { "Stars only", "Spectrum", "Wave", "Shape", "Envelope", "Transfer" };
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
    setVisible (v != View::stars);

    // The spectrum arrives in frames; the wave slides, and at less than this it reads as a slideshow.
    if (v == View::stars)          stopTimer();
    else if (v == View::spectrum)  startTimerHz (30);
    else                           startTimerHz (60);

    repaint();
}

juce::Rectangle<float> Monitor::band() const
{
    // In the render's space the glass is clear of the overhead panel above and the emblem below
    // between these two heights; across, the picture takes the windshield's whole width and runs out
    // under the frame on both sides rather than stopping short of it.
    constexpr float top = 100.0f, bottom = 234.0f;
    return { 0.0f, top - layout::windshield.getY(), (float) getWidth(), bottom - top };
}

void Monitor::timerCallback()
{
    if (view != View::spectrum)
    {
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
    // Nothing under the picture but the sky: it is a second layer on the glass, and the stars fly on
    // behind it.
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

    const scope::Frame frame = amp.preamp.scope.read (dry, wet) ? scope::Frame { dry.data(), wet.data(), core::ScopeTap::size }
                                                                : scope::Frame {};
    if (view == View::shape)          scope::ShapeView::paint (g, r, frame);
    else if (view == View::envelope)  scope::EnvelopeView::paint (g, r, frame, true);
    else if (view == View::transfer)  scope::TransferView::paint (g, r, frame);
}

void Monitor::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    // No well, no grid, no frame: the spectrum is light on the glass, from one edge of the windshield to
    // the other, and the cockpit's frame is what cuts it off.
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

} // namespace orbitamp::catpit
