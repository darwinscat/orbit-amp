// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "CatpitControls.h"
#include "CatpitEngine.h"
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

    /** Which of the six keys under the tone knobs answer the hand. The name under one that does not
        is dimmed with it. */
    void setKeysLive (const std::array<bool, 6>&);

private:
    bool canUndo = false, canRedo = false;
    int  activeRegister = 0;
    void paintOverhead (juce::Graphics&) const;

    std::array<bool, 6> keysLive { true, true, true, true, true, true };
    void paintKeyNames (juce::Graphics&) const;

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

    /** Centred on `anchor`, in the stage's own space, and kept inside it. Empty text hides it. */
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

    /** The content may change its own size — a list that grew a row — and the plate goes with it. */
    void childBoundsChanged (juce::Component*) override;

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

    // Nothing to paint where the cockpit is: it covers its whole stage, and where its windshield is cut
    // out the sky shows through — from the GL context underneath, or from the CPU's Space, or standing
    // still (StillSky).
    // Only in full screen is there window left round the stage, and that is painted black.
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Cmd+Z and Cmd+Shift+Z step the history; 1 2 3 4 pull the throttle to a channel; A B C D play a
        register. */
    bool keyPressed (const juce::KeyPress&) override;

    /** A right click on the panel itself — anywhere that is not a control — opens the cockpit's
        settings: the things a player sets once for the machine and the room, which have no place of
        their own on the console. */
    void mouseDown (const juce::MouseEvent&) override;

private:
    AmpProcessor& amp;
    Engine&   engine;       // the cockpit's half of the processor: the channels live there, window or no window
    Channels& channels;     // ...and these are its
    Presets   presets;

    bool adoptionPending = false;
    void adoptState();
    void followEngine();

    juce::Component stage;
    Space   space;
    StillSky still;              // the sky when the stars are switched off: a picture for each channel
    Monitor monitor;             // a picture of the amp at work, when the windshield shows one instead of the sky
    Cockpit cockpit;
    NameTag nameTag;             // over the controls: the last thing on the stage

    // THE STARS, three ways: flying on the GPU (the shader, in a GL context of the cockpit's own), flying
    // on the CPU (Space), or switched off — a still picture for each channel and nothing running at all.
    // Where the GPU's sky cannot be had, the CPU's flies in its place.
    enum class Stars { gpu, cpu, off };
    Stars stars = Stars::gpu;    // the player's choice, kept with the machine's settings
    juce::OpenGLContext gl;
    std::unique_ptr<GlSky> glSky;
    bool gpuBroken = false;      // the sky's shader would not build here
    void chooseStars (Stars);
    void applySky();
    void placeSky();

    // THE STAGE IN THE WINDOW. The window has the cockpit's shape and the stage fills it — except in
    // full screen, where the screen rarely has that shape: there the stage is the largest that fits,
    // centred.
    float stageScale() const;
    juce::Point<float> stageOrigin() const;

    // FULL SCREEN, where the cockpit stands alone: the window takes the whole screen — no title bar, no
    // menu bar, no Dock — and the cockpit keeps its shape in the middle of it. From the settings menu,
    // Cmd+Ctrl+F or F11; Esc leaves.
    bool fullScreen = false;
    juce::Point<int> windowedAt;             // where the window stood before it took the screen
    void setFullScreen (bool);
    void showSettingsMenu();
    void showInWindow (Monitor::View);

    // A LOOP IN PLACE OF THE GUITAR — OrbitAmp's audition player (core/DemoPlayer.h), here in the
    // settings menu. Only on a machine that has the loops (params::demoLoopsPresent); anywhere else the
    // menu does not offer it. A loop picked plays from its top; -1 starts the one last picked, or stops it.
    void playDemo (int loop);

    ArtKnob drive;
    std::array<std::unique_ptr<ArtKnob>, 3> tones;
    Lever lever;
    std::array<std::unique_ptr<PushButton>, 6> buttons;      // the keys under the tone knobs
    std::array<std::unique_ptr<PushButton>, 6> overhead;     // the keys in the overhead holes
    GuardButton turbo;
    std::array<std::unique_ptr<PlateSlider>, 3> sliders;
    Legend reverbName;                       // REVERB and its character; a click picks another
    Legend cabName;                        // CAB and the cabinet that is playing
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

    // MIDI. A Program Change the engine heard (AmpProcessor::takeProgramChange) is a pull of the
    // throttle: programs 1 2 3 4 are the four channels. A controller's value (takeControlChange) is a
    // foot on TURBO, HPF or LPF, or a pedal under DRIVE or a tone knob (midi::Controller). They are
    // asked for a hundred times a second — a foot must not feel the wait, and the face's own timer
    // ticks four times.
    struct Pulse final : juce::Timer
    {
        std::function<void()> beat;
        void timerCallback() override { if (beat) beat(); }
    };
    Pulse midiPulse;
    void playMidi();
    std::vector<double> tapGaps;             // the last few gaps between taps, ms
    double lastTap = 0.0;
    bool   tapping = false;
    void tapTempo();
    void endTapping();
    ColumnFader inFader, outFader;

    // THE INPUT LEVER'S ZERO is the machine's to say (the settings): what the engine's input trim stands
    // at with the lever in the middle of its travel — -9 dB unless the player says otherwise, a guitar
    // straight into an interface being hotter than what the captures were fed. The lever's ends are the
    // trim's own, so each half of the travel has the decibels between them and the zero.
    // The state carries the trim in decibels and, beside it, the zero it was dialled against
    // (Channels::leverZeroId). Where that is not the zero in force — a session from before there was
    // one, a register, another setting — the LEVER stays where it stood and the trim goes to what that
    // place gives now.
    //
    // TURBO'S PUSH is the machine's the same way (Channels::turboDb): the state says that TURBO is on, by
    // the preamp's input trim standing above nothing, and the settings say how hard that is — +9 dB
    // unless told otherwise, which with the lever's zero at -9 feeds the captures as they were taken.
    //
    // HOW DRIVE GOES BETWEEN THE PACK'S CAPTURES is the machine's too. Smoothly, unless told otherwise:
    // between two captures the player mixes them by the knob's angle, and the sound goes with the hand.
    // In steps, it lands on the nearer capture and holds it until the knob is nearer the next — one
    // capture playing at a time, each exactly as it was shot. The knob goes the same way: it clicks from
    // one capture's place to the next under the hand, and stands on one whatever DRIVE was left at.
    int  leverZero = 0;
    bool driveSteps = false;
    juce::ParameterAttachment* inputTrim = nullptr;
    bool settingsDiffer() const;
    void settleOnSettings();
    void chooseLeverZero (int db);
    void chooseTurbo (int db);
    void chooseDriveSteps (bool steps);

    // A right click on DRIVE picks between the two ways, and offers a capture as a `.nam`: in steps the
    // one it stands on, turning smoothly the nearer of the two it stands between.
    std::unique_ptr<juce::FileChooser> captureChooser;
    void showDriveMenu();
    void saveCapture();

    // A right click on CAB offers the cabinet that is playing as the IR file it is.
    std::unique_ptr<juce::FileChooser> cabChooser;
    void showCabMenu();
    void saveCab();

    // While a lever is held, a tag over it says where it stands, in the lever's own decibels — which
    // for INPUT count from the lever's zero, so the middle of the travel reads 0.
    void sayLever (ColumnFader&, const juce::String& name, std::function<float()> reading);
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
    void showMidi();                         // the MIDI inputs, from the settings menu
    void showSheet (std::unique_ptr<juce::Component> content);

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

    void showReverbMenu();
    void showDelayMenu();
    void showDelayTime();
    void refreshHistory();
    void showRegisterMenu (int index);
    void showChannel();
    void chooseCab (int step);
    void showCab();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatpitEditor)
};

} // namespace orbitamp::catpit
