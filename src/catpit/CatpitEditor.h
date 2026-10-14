// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Console.h"
#include "CatpitControls.h"
#include "CatpitGlSky.h"
#include "CatpitMonitor.h"
#include "CatpitSpace.h"

#include <memory>

namespace orbitamp::catpit
{

/** The cockpit itself: the render with its windshield cut out, the emblem, and every printed word on
    it. Drawn over Space, under the controls, and never takes a click. */
class Cockpit final : public face::Picture
{
private:
    void paintStatic (juce::Graphics&, float scale) const override;
    void paintLive (juce::Graphics&) const override;
    juce::Rectangle<int> historyArea() const override;

    void paintOverhead (juce::Graphics&) const;
    void paintKeyNames (juce::Graphics&) const;
    void paintEmblem (juce::Graphics&, float scale) const;
};

//==============================================================================
/** CatpitAmp's face over OrbitAmp's processor: the family's console (face/Console.h), laid out on the
    cockpit's render, with what is the cockpit's own over it — the throttle that pulls the four channels,
    TURBO under its guard, and the sky through the windshield. */
class CatpitEditor final : public face::Console
{
public:
    explicit CatpitEditor (AmpProcessor&);
    ~CatpitEditor() override;

private:
    Space   space;
    StillSky still;              // the sky when the stars are switched off: a picture for each channel
    Monitor monitor;             // a picture of the amp at work, when the windshield shows one instead of the sky

    // THE STARS, three ways: flying on the GPU (the shader, in a GL context of the cockpit's own), flying
    // on the CPU (Space), or switched off — a still picture for each channel and nothing running at all.
    // Where the GPU's sky cannot be had, the CPU's flies in its place.
    enum class Stars { gpu, cpu, off };
    Stars stars = Stars::gpu;    // the player's choice, kept with the machine's settings
    juce::OpenGLContext gl;
    std::unique_ptr<GlSky> glSky;
    bool gpuBroken = false;      // the sky's shader would not build here
   #if JUCE_LINUX || JUCE_BSD
    // On Linux JUCE's GL thread, told to repaint continuously, renders flat out: nothing paces it but the
    // buffer swap, and through XWayland that does not always hold — a core went on frames nobody saw.
    // The frames are asked for here instead, one per refresh of the screen.
    juce::VBlankAttachment glPulse { this, [this] { if (glSky != nullptr) gl.triggerRepaint(); } };
   #endif
    void chooseStars (Stars);
    void applySky();
    void placeSky();
    void showWindowMenu();          // a click on the glass: WINDOW and STARS, as the settings menu has them
    void showInWindow (Monitor::View);

    // The throttle picks the channel — its pack, and everything that channel was left with — and TURBO
    // pushes the capture: the preamp's input trim, at the push the settings give or at nothing.
    Lever lever;
    GuardButton turbo;

    void showSelector (int channel) override;
    bool channelKey (const juce::KeyPress&) override;
    void programChange (int program) override;
    void turboChange (bool thrown) override;
    bool turboPushesCapture() const override { return true; }
    void addWindowSections (juce::PopupMenu&) override;
    bool pictureClicked (juce::Point<int>) override;
    void stagePlaced() override { placeSky(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

} // namespace orbitamp::catpit
