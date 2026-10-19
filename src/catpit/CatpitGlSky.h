// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitFlight.h"
#include "CatpitSkyGl.h"

#include <juce_opengl/juce_opengl.h>

#include <atomic>

namespace orbitamp::catpit
{

/** THE SKY ON THE GPU. The same idea as Space — stars streaming past, nebulae swelling and fading, the
    throttle setting the speed and the ship spooling up and down, the helm turning it (CatpitHelm.h) —
    computed per pixel in a fragment shader, so the CPU does nothing per frame but hand over a few
    numbers. It renders only the glass's rectangle; the editor's components are painted over it by the
    same context, the cockpit's cut-out window letting it show through.

    This is the JUCE side: the context's frames, the clock, and where the glass is. The flight it steps
    (CatpitFlight.h) and the shader that draws it (CatpitSkyGl.h) are JUCE-free, and the site's sky is
    the same two in WebAssembly (tools/worlds-wasm/sky.cpp).

    Lives on the GL thread except for the setters, which are atomics. */
class GlSky final : public juce::OpenGLRenderer
{
public:
    explicit GlSky (juce::OpenGLContext& c) : context (c) {}

    /** 0 = CLEAN .. 1 = HI-GAIN, continuous. */
    void setThrottle (float t) noexcept { throttle.store (juce::jlimit (0.0f, 1.0f, t)); }

    /** The hand on the helm, -1 full left .. 1 full right (face::Steering); the ship answers it as a ship
        does (Helm). */
    void setSteer (float s) noexcept { steer.store (juce::jlimit (-1.0f, 1.0f, s)); }

    /** Where the glass is — all that is shaded — where the windshield is — what the picture is composed
        for: the stars fly out of its centre — and how big the whole surface is, in the editor's logical
        pixels. */
    void setArea (juce::Rectangle<float> glass, juce::Rectangle<float> windshield, juce::Point<float> editorSize) noexcept;

    /** False once the shader has failed to build — the editor falls back to the CPU sky then. */
    bool failed() const noexcept { return broken.load(); }

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

private:
    juce::OpenGLContext& context;
    SkyGl sky;

    std::atomic<float> throttle { 0.0f }, steer { 0.0f };
    std::atomic<float> areaX { 0 }, areaY { 0 }, areaW { 0 }, areaH { 0 }, fullW { 0 }, fullH { 0 };
    std::atomic<float> viewX { 0 }, viewY { 0 }, viewW { 0 }, viewH { 0 };     // the windshield, inside the area
    std::atomic<bool>  broken { false };

    // GL thread only.
    Flight flight;
    double last = 0.0;
};

} // namespace orbitamp::catpit
