// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_opengl/juce_opengl.h>

#include <atomic>
#include <memory>

namespace orbitamp::catpit
{

/** THE SKY ON THE GPU. The same idea as Space — stars streaming past, nebulae swelling and fading, the
    throttle setting the speed and the ship spooling up and down — computed per pixel in a fragment
    shader, so the CPU does nothing per frame but hand over four numbers. It renders only the
    windshield's rectangle; the editor's components are painted over it by the same context, the
    cockpit's cut-out window letting it show through.

    Lives on the GL thread except for the setters, which are atomics. */
class GlSky final : public juce::OpenGLRenderer
{
public:
    explicit GlSky (juce::OpenGLContext& c) : context (c) {}

    /** 0 = CLEAN .. 1 = OVERDRIVE, continuous. */
    void setThrottle (float t) noexcept { throttle.store (juce::jlimit (0.0f, 1.0f, t)); }

    /** Where the windshield is, and how big the whole surface is, in the editor's logical pixels. */
    void setArea (juce::Rectangle<float> windshield, juce::Point<float> editorSize) noexcept;

    /** Off while the windshield shows a picture instead: the surface is cleared and nothing is shaded. */
    void setFlying (bool f) noexcept { flying.store (f); }

    /** False once the shader has failed to build — the editor falls back to the CPU sky then. */
    bool failed() const noexcept { return broken.load(); }

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

private:
    juce::OpenGLContext& context;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    juce::uint32 quad = 0;

    std::atomic<float> throttle { 0.0f };
    std::atomic<float> areaX { 0 }, areaY { 0 }, areaW { 0 }, areaH { 0 }, fullW { 0 }, fullH { 0 };
    std::atomic<bool>  broken { false };
    std::atomic<bool>  flying { true };

    // GL thread only.
    float speed = 0.05f;
    double travel = 0.0, last = 0.0, start = 0.0;
};

} // namespace orbitamp::catpit
