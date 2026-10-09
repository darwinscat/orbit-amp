// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "CatpitControls.h"
#include "CatpitSpace.h"

#include <array>
#include <memory>
#include <vector>

namespace orbitamp::catpit
{

/** The cockpit itself: the render with its windshield cut out, and every printed word on it. Drawn
    over Space, under the controls, and never takes a click. */
class Cockpit final : public juce::Component
{
public:
    Cockpit();
    void paint (juce::Graphics&) override;

    /** The overhead toggle under the hand, if any — its full name and state are shown beneath the
        panel, since the panel only has room for a short name. */
    void showHover (int hole, juce::String text);

private:
    int hoverHole = -1;
    juce::String hoverText;

    // The render and the printed words never change, but the windshield repaints sixty times a second
    // and every one of those repaints the cockpit over it. So the cockpit is painted ONCE, at the
    // screen's own pixel size, and each frame is a straight copy of the part that changed.
    juce::Image layer;
    float layerScale = 0.0f;
    void paintStatic (juce::Graphics&) const;
    void paintEmblem (juce::Graphics&) const;
};

//==============================================================================
/** CatpitAmp's face over OrbitAmp's processor. Everything is laid out in the render's own 1672 x 941
    space on one stage, and the stage is scaled to the window as a whole. */
class CatpitEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CatpitEditor (AmpProcessor&);
    ~CatpitEditor() override;

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    void resized() override;

private:
    AmpProcessor& amp;

    juce::Component stage;
    Space   space;
    Cockpit cockpit;

    ArtKnob drive;
    std::array<std::unique_ptr<ArtKnob>, 3> tones;
    Lever lever;
    std::array<std::unique_ptr<BatToggle>, 6> toggles;
    GuardButton turbo;
    std::array<std::unique_ptr<PlateSlider>, 3> sliders;
    ColumnFader inFader, outFader;
    SegmentMeter inMeter, outMeter;

    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    template <typename Control>
    void bind (Control&, const char* paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

} // namespace orbitamp::catpit
