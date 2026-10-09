// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "CatpitChannels.h"
#include "CatpitControls.h"
#include "CatpitGlSky.h"
#include "CatpitMonitor.h"
#include "CatpitPresets.h"
#include "CatpitSpace.h"

#include <felitronics/appkit/VersionBadge.h>

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

    /** What the signs by the overhead keys say: an arrow with nothing to step back or forward to stands
        dim, and the register that is playing has its letter and its lamp lit. */
    void setHistory (bool canUndo, bool canRedo, int activeRegister);

private:
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
/** A dark name tag — the full name and state of whatever is under the hand, where the panel itself
    only has room for a short name or none. It lies OVER everything on the stage: the keys stand right
    under the tone knobs, and a tag drawn with the panel slid under the knob above it. Never takes a
    click. */
class NameTag final : public juce::Component
{
public:
    NameTag();
    void paint (juce::Graphics&) override;

    /** Centred on `anchor`, in the stage's own space. Empty text hides it. */
    void show (juce::Point<float> anchor, const juce::String& text);

private:
    juce::String text;
};

//==============================================================================
/** A WINDOW INSIDE THE COCKPIT: a plate in the cockpit's own colours, centred over the face on a light
    scrim — where the build window opens, and for the same reason: it belongs to the amp, so it stays
    on the amp rather than hanging off its edge over whatever lies behind. A click that misses the
    plate closes it, and so does Escape. */
class Sheet final : public juce::Component
{
public:
    explicit Sheet (std::unique_ptr<juce::Component> content);

    std::function<void()> onDismiss;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    std::unique_ptr<juce::Component> content;
    juce::Rectangle<int> plate() const;
};

//==============================================================================
/** CatpitAmp's face over OrbitAmp's processor. Everything is laid out in the render's own 1672 x 941
    space on one stage, and the stage is scaled to the window as a whole. */
class CatpitEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit CatpitEditor (AmpProcessor&);
    ~CatpitEditor() override;

    // Nothing to paint: the cockpit covers the whole stage, and where its windshield is cut out the sky
    // shows through — from the GL context underneath, or from the CPU Space component.
    void paint (juce::Graphics&) override {}
    void resized() override;

    /** Cmd+Z and Cmd+Shift+Z step the history; 1 2 3 4 pull the throttle to a channel. */
    bool keyPressed (const juce::KeyPress&) override;

    /** A right click on the panel itself — anywhere that is not a control — opens the cockpit's
        settings: the things a player sets once for the machine and the room, which have no place of
        their own on the console. */
    void mouseDown (const juce::MouseEvent&) override;

private:
    AmpProcessor& amp;
    Channels channels;
    Presets  presets;

    bool adoptionPending = false;
    void adoptState();

    juce::Component stage;
    Space   space;
    Monitor monitor;             // a picture of the amp at work, when the windshield shows one instead of the sky
    Cockpit cockpit;
    NameTag nameTag;             // over the controls: the last thing on the stage

    // The sky on the GPU when it can be; the CPU Space stays as the fallback (and with CATPIT_SKY=cpu).
    juce::OpenGLContext gl;
    std::unique_ptr<GlSky> glSky;
    bool gpuSky    = true;       // the player's choice, kept with the machine's settings
    bool gpuBroken = false;      // the sky's shader would not build here: the choice is moot
    void applySky();
    void placeSky();
    void showSettingsMenu();
    void showInWindow (Monitor::View);

    ArtKnob drive;
    std::array<std::unique_ptr<ArtKnob>, 3> tones;
    Lever lever;
    std::array<std::unique_ptr<PushButton>, 6> buttons;      // the keys under the tone knobs
    std::array<std::unique_ptr<PushButton>, 6> overhead;     // the keys in the overhead holes
    GuardButton turbo;
    std::array<std::unique_ptr<PlateSlider>, 3> sliders;
    Legend reverbName;                       // REVERB and its character; a click picks another
    juce::ParameterAttachment* reverbType = nullptr;
    // The delay's plate reads three things, and each answers the hand its own way:
    Legend delayWord;                        // DELAY — tapped, it takes the tempo from the taps
    Legend delayNote;                        // the note value — a click opens the list of them
    Legend delayTempo;                       // the tempo — dragged up or down like a knob
    juce::ParameterAttachment* delayDiv = nullptr;
    juce::ParameterAttachment* delayBpm = nullptr;
    float tempoTravel = 0.0f;                // the hand's travel not yet spent on a whole BPM
    bool  tempoIsOwn  = true;                // false while a host is conducting

    // Tapping. However many taps it takes to find the beat, the history sees one step: the count is
    // held open as a gesture from the first tap until the taps have stopped for a moment.
    struct After final : juce::Timer
    {
        std::function<void()> then;
        void timerCallback() override { stopTimer(); if (then) then(); }
    };
    After tapPause;
    std::vector<double> tapGaps;             // the last few gaps between taps, ms
    double lastTap = 0.0;
    bool   tapping = false;
    void tapTempo();
    void endTapping();
    ColumnFader inFader, outFader;
    SegmentMeter inMeter, outMeter;
    TunerFace tuner;
    Emblem emblem;

    // The version on its plate, the family's orange dot beside it when a newer release has been seen.
    // A click opens the same window OrbitAmp's footer does — appkit's badge, kept invisible and only
    // for that window.
    Legend version;
    felitronics::appkit::VersionBadge about;

    // CPU on the plate under the grille: the share of each audio block's time the engine spends —
    // the number OrbitAmp's footer calls DSP. It warms as it climbs, and a click opens the same
    // stage-by-stage window (ui/LoadBreakdown.h) — here inside the cockpit, in the cockpit's colours.
    Legend load;
    std::unique_ptr<Sheet> sheet;            // the window that click opens, while it is open
    void showLoad();

    // MEM on the mirrored plate: what the process is holding, as the system's own monitor counts it
    // (core/ProcessMemory.h). Only while the cockpit stands alone — inside a host the process is the
    // host, and the figure would be the host's.
    Legend memory;
    int memoryTick = 0;          // the timer runs four times a second; this is asked once a second

    // PRESET on the mirrored plate: the one word, the family's orange dot beside it while the sound has
    // moved away from the preset it came from, and everything else — which preset, the list, saving,
    // renaming — behind a click.
    Legend presetDoor;
    void showPresetMenu();
    void timerCallback() override;

    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    template <typename Control>
    void bind (Control&, const juce::String& paramId);
    void bindButton (PushButton&, const char* paramId);
    juce::ParameterAttachment* follow (const char* paramId, std::function<void (float)> moved);
    void  setParam (const char* paramId, float value);
    float getParam (const char* paramId) const;

    void setUpChain (bool firstTime);
    void showReverbMenu();
    void showDelayMenu();
    void showDelayTime();
    void refreshHistory();
    void showRegisterMenu (int index);
    void showChannel();
    void chooseCombo (int step);
    void showComboTag (bool show);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

} // namespace orbitamp::catpit
