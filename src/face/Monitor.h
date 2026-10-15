// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "../core/ScopeTap.h"
#include "../ui/scope/EnvelopeView.h"
#include "../ui/scope/ShapeView.h"
#include "../ui/scope/TransferView.h"
#include "../ui/scope/WaveView.h"

#include <felitronics/analysis/SpectrumPane.h>

#include <array>

namespace orbitamp::face
{

/** A PICTURE OF THE AMP AT WORK, on a face's glass: one of the pictures OrbitAmp draws, as a layer over
    whatever is behind it — the cockpit's sky through its windshield, the rover's nav screen.

      SPECTRUM   what the tone stack eats, grey, and what it makes of it, orange. No curve and no
                 zero line: what BASS, MIDDLE and TREBLE do is the distance between the two;
      WAVE, SHAPE, ENVELOPE, TRANSFER
                 OrbitAmp's own scope pictures, drawn by OrbitAmp's own painters, looking at the amp
                 the throttle has chosen.

    All of them as BARE LIGHT: no well, no grid, no frame, nothing behind them but the glass — in the
    band the face gives them (setBand), edge to edge across it and cut off by the face's frame.
    (TRANSFER is a square by nature — input against output — and stands in the middle.) Which view is
    up is the window's business, not the sound's: the face keeps it with the machine's settings.

    Reads the processor's taps on its own clock, and only while it is showing. */
class Monitor final : public juce::Component, private juce::Timer
{
public:
    enum class View { none, spectrum, wave, shape, envelope, transfer, numViews };

    /** The views' names, in order — `none` first: the glass alone, with nothing of this component's on it. */
    static const juce::StringArray& names();

    explicit Monitor (AmpProcessor&);

    /** Puts a view up over the glass; `none` takes this layer off. */
    void show (View);
    View shown() const noexcept { return view; }

    /** Where a picture lies, in this component's own space: by default all of it. */
    void setBand (juce::Rectangle<float> local) { area = local; repaint(); }

    void paint (juce::Graphics&) override;

private:
    AmpProcessor& amp;
    View view = View::none;
    juce::Rectangle<float> area;

    felitronics::analysis::SpectrumPane before, after;

    std::array<float, core::ScopeTap::size> dry {}, wet {};   // the scope tap, copied out once a frame
    scope::WaveView waveView;

    juce::Rectangle<float> band() const;      // where a picture lies
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void timerCallback() override;
};

} // namespace orbitamp::face
