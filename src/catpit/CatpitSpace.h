// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace orbitamp::catpit
{

/** WHAT IS OUTSIDE THE WINDSHIELD. Stars coming at you and nebulae drifting past; the throttle sets the
    speed, from a cruise at CLEAN to the edge of light at OVERDRIVE, and the ship spools up and down
    rather than jumping. Drawn on the CPU with the ordinary 2D renderer — a first look at the idea; the
    volumetric nebula belongs on the GPU once the idea is kept. Runs only while the window is on screen. */
class Space final : public juce::Component
{
public:
    Space();

    /** 0 = CLEAN .. 1 = OVERDRIVE, continuous (the lever's travel, not just its detents). */
    void setThrottle (float t) noexcept { throttle = juce::jlimit (0.0f, 1.0f, t); }

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
    juce::Rectangle<float> window;
    double last = 0.0, lastFrame = 0.0;

    struct Cloud { int image = 0; float phase = 0.0f, angle = 0.0f; juce::Point<float> drift; };
    std::array<juce::Image, 3> nebulae;
    std::array<Cloud, 2> clouds;

    juce::VBlankAttachment vblank { this, [this] { tick(); } };

    void respawn (Star&, bool anywhere);
    void tick();
};

} // namespace orbitamp::catpit
