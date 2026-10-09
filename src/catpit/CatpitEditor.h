// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "CatpitControls.h"
#include "CatpitGlSky.h"
#include "CatpitSpace.h"

#include <array>
#include <memory>
#include <vector>

namespace orbitamp::catpit
{

/** The cockpit itself: the render with its windshield cut out, the emblem, and every printed word on
    it. Drawn over Space, under the controls, and never takes a click. */
class Cockpit final : public juce::Component
{
public:
    Cockpit();
    void paint (juce::Graphics&) override;

    /** A dark name tag centred on `anchor` — the full name and state of whatever is under the hand,
        where the panel itself only has room for a short name or none. Empty text hides it. */
    void showTag (juce::Point<float> anchor, juce::String text);

    /** What the signs by the overhead keys say: an arrow with nothing to step back or forward to stands
        dim, and the register that is playing has its letter and its lamp lit. */
    void setHistory (bool canUndo, bool canRedo, int activeRegister);

private:
    juce::Point<float> tagAnchor;
    juce::String tagText;
    juce::Rectangle<float> tagBounds() const;

    bool canUndo = false, canRedo = false;
    int  activeRegister = 0;
    void paintOverhead (juce::Graphics&) const;

    // The render and the printed words never change, but the windshield repaints sixty times a second
    // and every one of those repaints the cockpit over it. So the cockpit is painted ONCE, at the
    // screen's own pixel size, and each frame is a straight copy of the part that changed.
    juce::Image layer;
    float layerScale = 0.0f;
    void paintStatic (juce::Graphics&, float scale) const;
    void paintEmblem (juce::Graphics&, float scale) const;
};

//==============================================================================
/** CatpitAmp's face over OrbitAmp's processor. Everything is laid out in the render's own 1672 x 941
    space on one stage, and the stage is scaled to the window as a whole. */
class CatpitEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CatpitEditor (AmpProcessor&);
    ~CatpitEditor() override;

    // Nothing to paint: the cockpit covers the whole stage, and where its windshield is cut out the sky
    // shows through — from the GL context underneath, or from the CPU Space component.
    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    AmpProcessor& amp;

    juce::Component stage;
    Space   space;
    Cockpit cockpit;

    // The sky on the GPU when it can be; the CPU Space stays as the fallback (and with CATPIT_SKY=cpu).
    juce::OpenGLContext gl;
    std::unique_ptr<GlSky> glSky;
    void useCpuSky();
    void placeSky();

    ArtKnob drive;
    std::array<std::unique_ptr<ArtKnob>, 3> tones;
    Lever lever;
    std::array<std::unique_ptr<PushButton>, 6> buttons;      // the keys under the tone knobs
    std::array<std::unique_ptr<PushButton>, 6> overhead;     // the keys in the overhead holes
    GuardButton turbo;
    std::array<std::unique_ptr<PlateSlider>, 3> sliders;
    ColumnFader inFader, outFader;
    SegmentMeter inMeter, outMeter;
    TunerFace tuner;

    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    template <typename Control>
    void bind (Control&, const juce::String& paramId);
    void bindButton (PushButton&, const char* paramId);
    void follow (const char* paramId, std::function<void (float)> moved);
    void  setParam (const char* paramId, float value);
    float getParam (const char* paramId) const;

    void setUpChain();
    void refreshHistory();
    void showRegisterMenu (int index);
    int  packIndexFor (int detent) const;
    void choosePack (int detent);
    void chooseCombo (int step);
    void showComboTag (bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

} // namespace orbitamp::catpit
