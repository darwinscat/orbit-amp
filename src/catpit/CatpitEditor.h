// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Console.h"
#include "CatpitControls.h"
#include "../face/Monitor.h"
#include "../face/Steering.h"
#include "CatpitGlSky.h"
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
    void paintStrips (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override { return face::labelLilac.withAlpha (0.75f); }     // as quiet as the filter keys' signs at rest
    void paintLive (juce::Graphics&) const override;
    juce::Rectangle<int> historyArea() const override;

    void paintOverhead (juce::Graphics&) const;
    void paintKeyNames (juce::Graphics&) const;
    void paintEmblem (juce::Graphics&, float scale) const;
};

//==============================================================================
/** CatpitAmp's face over OrbitAmp's processor: the family's console (face/Console.h), laid out on the
    cockpit's render, with what is the cockpit's own over it — the throttle that pulls the four channels,
    TURBO under its guard, the sky through the windshield, and the helm. */
class CatpitEditor final : public face::Console
{
public:
    explicit CatpitEditor (AmpProcessor&);
    ~CatpitEditor() override;

    // THE HELM: a drag left or right across the glass, or ← and → held, steers the ship — the sky banks
    // and sweeps (CatpitHelm.h) — and it straightens as soon as the hand lets go. A click on the glass
    // that does not move asks what the windshield shows, once the button is up.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;
    void visibilityChanged() override;

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

    face::Steering steering;        // the hand on the helm, handed to the sky that is flying
    bool handOnGlass = false;       // the button went down on the glass: a drag steers, a click is the menu

    // The throttle picks the channel — its pack, and everything that channel was left with — and TURBO
    // is the AMP's boost (params::preampBoost), which each channel keeps.
    Lever lever;
    GuardButton turbo;

    // GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader in the slot cut beside its lamp, with
    // the columns' cap. While its block is off it stands dim, and still moves.
    ColumnFader threshold { Art::get().sliderCap };
    ColumnFader ceiling { Art::get().sliderCap };

    void showSelector (int channel) override;
    bool channelKey (const juce::KeyPress&) override;
    void programChange (int program) override;
    void turboChange (bool thrown) override;
    void addWindowSections (juce::PopupMenu&) override;
    void stagePlaced() override { placeSky(); }
    void webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject& world) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

/** The cockpit's face (Product::makeEditor) — or, with its pictures missing, the notice that says where
    they belong. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);

} // namespace orbitamp::catpit

