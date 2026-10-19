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

    /** SHAPE GOES OUT IN SILENCE, where the face asks for it (the cockpit does): once what the line is
        drawn from — the preamp's input, the picture's own dry side — has stayed under quietDb for quietMs,
        the line fades away and the glass is the sky alone; the first sound brings it back at once, so an
        attack is never missed. Judged there and not at the output: at high gain the output's hiss, and the
        reverb's and the delay's tails, keep it from ever being quiet in a pause. */
    bool shapeFadesInSilence = false;

    /** ...what counts as quiet at the preamp's input (a guitar plays there at -30 to -6 dBFS, its hum and
        hiss sit at -60 to -80), for how long, and how fast the line goes and comes back. */
    static constexpr float  quietDb   = -50.0f;
    static constexpr double quietMs   = 100.0;
    static constexpr double fadeOutMs = 200.0;
    static constexpr double fadeInMs  = 30.0;

    void paint (juce::Graphics&) override;

    /** SHAPE as the site's demo draws it (Console::webExtras): its two strokes and the taps they are drawn
        from, the window, the fill and the floor, the silence rules — and whether this glass fades it —
        and the band it lies in, in the parent's space. */
    juce::var shapeForWeb() const;

private:
    AmpProcessor& amp;
    View view = View::none;
    juce::Rectangle<float> area;

    felitronics::analysis::SpectrumPane before, after;

    std::array<float, core::ScopeTap::size> dry {}, wet {};   // the scope tap, copied out once a frame
    scope::WaveView waveView;

    float  shapeShown   = 1.0f;     // how much of SHAPE's line is up, 0..1
    double quietSinceMs = -1.0;     // when the preamp's input last went quiet, or -1 while it sounds
    float  dryPeak      = 0.0f;     // the last frame's peak on the preamp's input side
    double lastTickMs   = 0.0;
    void followSilence();

    juce::Rectangle<float> band() const;      // where a picture lies
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void timerCallback() override;
};

} // namespace orbitamp::face
