// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "../core/EqLink.h"
#include "../ui/scope/DeviceScope.h"

#include <felitronics/analysis/SpectrumPane.h>

#include <memory>

namespace orbitamp::catpit
{

/** WHAT THE WINDSHIELD SHOWS WHEN IT IS NOT THE SKY. The window can be turned into an instrument: the
    pictures OrbitAmp draws of an amp at work, thrown across the glass in place of the stars.

      SPECTRUM   what the tone stack eats, grey, and what it makes of it, orange — with the curve
                 BASS, MIDDLE and TREBLE are holding drawn over both. Bare light on the glass: no
                 grid, no frame, running out under the windshield's frame on both sides;
      WAVE, SHAPE, ENVELOPE, TRANSFER
                 OrbitAmp's own scope, as it stands, looking at the amp the throttle has chosen.

    It fills the windshield's rectangle under the cockpit's frame and stands the picture clear of what
    the frame hangs into the glass — the overhead panel, the emblem, the slanted corners. Which view is
    up is the window's business, not the sound's: the face keeps it with the machine's settings.

    Reads the processor's taps on its own clock, and only while it is showing. */
class Monitor final : public juce::Component, private juce::Timer
{
public:
    enum class View { stars, spectrum, wave, shape, envelope, transfer, numViews };

    /** The views' names, in order — `stars` first, which is not this component's to draw. */
    static const juce::StringArray& names();

    explicit Monitor (AmpProcessor&);

    /** Puts a view up; `stars` takes this component down and leaves the glass to the sky. */
    void show (View);
    View shown() const noexcept { return view; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    AmpProcessor& amp;
    View view = View::stars;

    std::unique_ptr<DeviceScope> scope;                    // made the first time one of its pictures is asked for
    core::EqLink console;                                  // a display copy of the tone stack, for its curve
    felitronics::analysis::SpectrumPane before, after;

    juce::Rectangle<float> glass() const;     // where a framed picture stands, clear of what hangs into the window
    juce::Rectangle<float> band() const;      // the spectrum's: the same height, from edge to edge
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void timerCallback() override;
};

} // namespace orbitamp::catpit
