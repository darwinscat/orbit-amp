// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitHelm.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace orbitamp::catpit
{

/** WHAT IS OUTSIDE THE WINDSHIELD. Stars coming at you and nebulae drifting past; the throttle sets the
    speed, from a cruise at CLEAN to the edge of light at HI-GAIN, and the ship spools up and down
    rather than jumping; the helm turns it, as it turns the GPU's sky (CatpitHelm.h). Drawn on the CPU
    with the ordinary 2D renderer — a first look at the idea; the volumetric nebula belongs on the GPU
    once the idea is kept. Runs only while the window is on screen. */
class Space final : public juce::Component
{
public:
    Space();

    /** 0 = CLEAN .. 1 = HI-GAIN, continuous (the lever's travel, not just its detents). */
    void setThrottle (float t) noexcept { throttle = juce::jlimit (0.0f, 1.0f, t); }

    /** The hand on the helm, -1 full left .. 1 full right (face::Steering); the ship answers it as a ship
        does (Helm). */
    void setSteer (float s) noexcept { steer = juce::jlimit (-1.0f, 1.0f, s); }

    /** The part of its bounds the picture is composed for: the stars fly out of ITS centre and are sized
        by ITS width. The sky is still painted over all the bounds, so glass that runs past this window
        has stars in it too. Unset, the window is the bounds. */
    void setWindow (juce::Rectangle<float> w) noexcept { window = w; }

    void paint (juce::Graphics&) override;

private:
    struct Star { float x, y, z, tint; };

    std::vector<Star> stars;
    juce::Random rng { 0x0ca7 };
    float throttle = 0.0f, speed = 0.04f;
    float steer = 0.0f;
    Helm helm;
    float slide = 0.0f;          // how fast the turn sweeps the stars sideways, in their own measure a second
    juce::Rectangle<float> window;
    double last = 0.0, lastFrame = 0.0;

    struct Cloud { int image = 0; float phase = 0.0f, angle = 0.0f, sweep = 0.0f; juce::Point<float> drift; };
    std::array<juce::Image, 3> nebulae;
    std::array<Cloud, 2> clouds;

    juce::VBlankAttachment vblank { this, [this] { tick(); } };

    void respawn (Star&, bool anywhere);
    void tick();
};

//==============================================================================
/** THE SKY, STANDING STILL: what the glass shows when the stars are switched off — a long exposure of
    the GPU's sky at the playing channel's speed, one for each channel (Art's stillSky). Nothing flies and
    nothing is computed: the picture is fitted to the screen's pixels once and copied from then on.
    Hidden, it holds no picture at all. */
class StillSky final : public juce::Component
{
public:
    StillSky();

    void setChannel (int);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    int channel = 0, pictureOf = -1;
    juce::Image picture;         // the channel's, as it was made
    juce::Image fitted;          // ...and at the size the glass has on this screen
};

} // namespace orbitamp::catpit
