// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../PluginProcessor.h"
#include "../channels/Engine.h"
#include "../channels/Presets.h"
#include "Controls.h"
#include "Picture.h"
#include "Sheet.h"

#include <felitronics/appkit/VersionBadge.h>

#include <array>
#include <memory>
#include <vector>

namespace orbitamp::face
{

using channels::Engine;
using channels::Channels;
using channels::Presets;

/** The six keys under the tone knobs: the two filters, then the pack's own layers. A face has room for a
    short name under each; the full one shows on hover. */
struct Switch { const char* name; const char* shortName; };
enum { btHpf, btLpf, btFat, btBright, btDepth, btPresence };
inline constexpr std::array<Switch, 6> switches {{
    { "HIGHPASS", "HPF"    },
    { "LOWPASS",  "LPF"    },
    { "FAT",      "FAT"    },
    { "BRIGHT",   "BRIGHT" },
    { "DEPTH",    "DEPTH"  },
    { "PRESENCE", "PRES"   },
}};
inline constexpr float keyTracking = 0.04f;

/** The lilac the words printed on a face are written in. */
inline const juce::Colour labelLilac { 0xffc9b8f5 };

/** The keys in the holes of the roof, in hole order: the history on the left of the divider, the four
    registers — four whole amps to compare, each with a history of its own — on the right. */
enum { ovUndo, ovRedo, ovFirstRegister };
inline constexpr int numRegisterKeys = 4;

/** The lamps a face lights itself over its picture: the input's overload and the output's, the gate's
    and the limiter's. */
enum LampId { inClipLamp, outClipLamp, gateLamp, limiterLamp, numLamps };

/** A turning arrow — three quarters of a circle with a head on its open end. Forward turns clockwise
    (redo); back is its mirror (undo). */
void drawTurnArrow (juce::Graphics&, juce::Point<float> centre, float radius, bool back, juce::Colour);

//==============================================================================
/** A CONSOLE: the face of a product with channels, over OrbitAmp's processor. What every such face has
    — DRIVE, the three tones and the six keys under them, the history and the four registers in the
    roof, REVERB, DELAY and CAB on their plates, the INPUT and OUTPUT columns with their meters and
    lamps, GATE and LIMITER, the tuner's glass, the emblem, the version, CPU and MEM, PRESET, and every
    menu and window behind them — is here, bound to the engine, laid out where the product says (Places)
    and drawn with what the product hands it (Look). What is a face's own — its picture, how it picks a
    channel, what TURBO is on it, what shows through its window — the product's editor adds over this.

    Everything is laid out in the picture's own 1672 x 941 space on one stage (Stage.h), and the stage is
    scaled to the window as a whole. */
class Console : public juce::AudioProcessorEditor,
                public juce::FileDragAndDropTarget,
                private juce::Timer
{
public:
    /** WHERE EVERYTHING STANDS, in the stage's space — measured off the product's picture. */
    struct Places
    {
        juce::Rectangle<float> drive;                       // the knob's box
        std::array<juce::Rectangle<float>, 3> tones;        // BASS, MIDDLE, TREBLE
        std::array<juce::Point<float>, 6> keys;             // the six keys' centres
        float keySize = 34.0f;
        std::array<juce::Point<float>, 6> holes;            // the roof keys' centres
        float holeKeySize = 40.0f;
        std::array<juce::Rectangle<float>, 3> sliders;      // REVERB, DELAY, CAB
        std::array<juce::Rectangle<float>, 3> sliderNames;  // the row each plate's words stand in
        float sliderCapAngle = juce::MathConstants<float>::halfPi;  // how the sliders' caps lie (PlateSlider)
        float sliderCapLength = 0.0f;
        float faderCapLength = 0.0f;                        // ...and the columns' (ColumnFader)
        juce::Rectangle<float> inMeter, inFader, outMeter, outFader;
        juce::Rectangle<float> tuner;
        std::array<juce::Rectangle<float>, numLamps> lamps;
        std::vector<juce::Point<float>> emblemFace;         // the plate the mark is on, the hand's target
        juce::Rectangle<float> emblemMark;                  // the box the mark is cut in
        juce::Rectangle<float> version, load, memory, preset, gateDoor, limiterDoor;
        float gateDoorAngle = 0.0f, limiterDoorAngle = 0.0f;    // GATE and LIMITER turned about their middles, radians
        float readingPx = stage::px11;                      // what CPU and MEM are written at
        bool tunerStrip = false;                            // the tuner in one low line (TunerFace::setStrip)...
        bool tunerMenuOnClick = false;                      // ...and a click on it opens its menu, rather than switching it
    };

    /** WHAT IT IS DRAWN WITH: the product's renders, and its words. */
    struct Look
    {
        const juce::Image& driveKnob;
        ArtKnob::Pointer drivePointer;
        const juce::Image& toneKnob;
        ArtKnob::Pointer tonePointer;
        const juce::Image& cap;                             // the plate sliders' cap
        const juce::Image& faderCap;                        // ...and the columns' faders'
        std::array<const juce::Image*, numLamps> lampsLit;  // each lamp as its render painted it lit
        juce::String mark;                                  // the product's mark, as SVG: the build window wears it
        juce::String midiCommands;                          // what the MIDI window lists (Midi.h)
    };

    Console (AmpProcessor&, std::unique_ptr<Picture>, const Places&, const Look&);
    ~Console() override;

    // Nothing to paint where the face is: it covers its whole stage, and where its window is cut out,
    // what shows through is the product's. Only in full screen is there window left round the stage,
    // and that is painted black.
    void paint (juce::Graphics&) override;
    void resized() override;

    /** Cmd+Z and Cmd+Shift+Z step the history; A B C D play a register; what the number keys and the
        others do to the channels is the product's (channelKey). */
    bool keyPressed (const juce::KeyPress&) override;

    /** A right click on the panel itself — anywhere that is not a control — opens the face's settings:
        the things a player sets once for the machine and the room, which have no place of their own on
        the console. */
    void mouseDown (const juce::MouseEvent&) override;

    /** IRs DROPPED ANYWHERE ON THE FACE become cabinets of the player's own — files, folders of them,
        zips. While they hang over the window a tag over CAB says what letting go will do. */
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

protected:
    AmpProcessor& amp;
    Engine&   engine;       // the product's half of the processor: the channels live there, window or no window
    Channels& channels;     // ...and these are its
    Presets   presets;
    const Places places;

    juce::Component stage;
    std::unique_ptr<Picture> picture;
    NameTag nameTag;             // over the controls: the last thing on the stage

    /** A control of the product's own onto the stage, under the name tag. */
    void addToStage (juce::Component&);

    // THE PRODUCT'S OWN. Each is asked from the console's own code once the face is built.

    /** Where the state's channel is now: the product's selector goes there (without saying so). */
    virtual void showSelector (int /*channel*/) {}

    /** A key with no modifier that the console does not use itself — the number keys, say. True when it
        was the product's. */
    virtual bool channelKey (const juce::KeyPress&) { return false; }

    /** A Program Change from a MIDI input, counted from 0. */
    virtual void programChange (int program) { if (program >= 0 && program < channels.count()) go (program); }

    /** TURBO's controller, thrown on or off. */
    virtual void turboChange (bool /*thrown*/) {}

    /** Whether the face's TURBO pushes the capture — the cockpit's does — so that how hard it pushes is
        among the settings. */
    virtual bool turboPushesCapture() const { return false; }

    /** The WINDOW section the settings menu opens with: what the product's window shows, and full
        screen; then whatever else is the product's to set there. */
    virtual void addWindowSections (juce::PopupMenu&);

    /** A left click on the bare picture that the console does not use: true when the product took it. */
    virtual bool pictureClicked (juce::Point<int> /*onStage*/) { return false; }

    /** A right click on the bare picture, before the console's own: true when the product took it. */
    virtual bool pictureRightClicked (juce::Point<int> /*onStage*/) { return false; }

    /** The stage has been placed in the window again. */
    virtual void stagePlaced() {}

    /** To that channel, by a hand: one step of the history. False, and the face back on the channel
        that is playing, when its pack is not installed. */
    bool go (int channel);

    // THE STAGE IN THE WINDOW. The window has the face's shape and the stage fills it — except in full
    // screen, where the screen rarely has that shape: there the stage is the largest that fits, centred.
    float stageScale() const;
    juce::Point<float> stageOrigin() const;

    // FULL SCREEN, where the product stands alone: the window takes the whole screen — no title bar, no
    // menu bar, no Dock — and the face keeps its shape in the middle of it. From the settings menu,
    // Cmd+Ctrl+F or F11; Esc leaves.
    bool fullScreen = false;
    void setFullScreen (bool);

    // WHAT IS SET ON A CONTROL IS FOUND ON THAT CONTROL: a right click on it opens the few choices that
    // are about it, and nothing else. The settings menu keeps the machine's among them as well.
    void addChoice (juce::PopupMenu&, const juce::String& text, bool ticked,
                    std::function<void (Console&)> picked, bool enabled = true);

    // TURBO'S PUSH is the machine's to say (Channels::turboDb), on a face whose TURBO pushes the capture:
    // the state says that TURBO is on, by the preamp's input trim standing above nothing, and the
    // settings say how hard that is — +9 dB unless told otherwise, which with the lever's zero at -9
    // feeds the captures as they were taken.
    void showTurboMenu();           // how hard it pushes
    void chooseTurbo (int db);

    // Tapping — see tapTempo. Anything that moves the state ends a count left open.
    void endTapping();

    void  setParam (const char* paramId, float value);
    float getParam (const char* paramId) const;
    juce::ParameterAttachment* follow (const char* paramId, std::function<void (float)> moved);

    /** Puts the face where the state is. Reads only — it is called from inside the history's own
        callbacks. */
    void showChannel();

private:
    bool adoptionPending = false;
    void adoptState();
    void followEngine();

    // A LOOP IN PLACE OF THE GUITAR — OrbitAmp's audition player (core/DemoPlayer.h), here in the
    // settings menu. Only on a machine that has the loops (params::demoLoopsPresent); anywhere else the
    // menu does not offer it. A loop picked plays from its top; -1 starts the one last picked, or stops it.
    void playDemo (int loop);

    juce::Point<int> windowedAt;             // where the window stood before it took the screen
    void showSettingsMenu();

    const juce::String midiCommands;

    ArtKnob drive;
    std::array<std::unique_ptr<ArtKnob>, 3> tones;
    std::array<std::unique_ptr<PushButton>, 6> buttons;      // the keys under the tone knobs
    std::array<std::unique_ptr<PushButton>, 6> overhead;     // the keys in the roof's holes

protected:
    // A face's own sprites go onto these keys, where it has them (PushButton::setArt).
    PushButton& key (int index)     { return *buttons[(size_t) index]; }
    PushButton& roofKey (int index) { return *overhead[(size_t) index]; }
    ArtKnob& driveKnob()            { return drive; }
    ArtKnob& toneKnob (int index)   { return *tones[(size_t) index]; }

private:
    std::array<std::unique_ptr<PlateSlider>, 3> sliders;
    Legend reverbName;                       // REVERB and its character; a click picks another
    Legend cabName;                          // CAB and the cabinet that is playing
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

    // MIDI. A Program Change the engine heard (AmpProcessor::takeProgramChange) picks a channel the
    // product's way (programChange). A controller's value (takeControlChange) is a foot on TURBO, HPF
    // or LPF, or a pedal under DRIVE or a tone knob (midi::Controller). They are asked for a hundred
    // times a second — a foot must not feel the wait, and the face's own timer ticks four times.
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
    void chooseDriveSteps (bool steps);

    void showInputMenu();           // the INPUT column: the lever's zero
    void showOutputMenu();          // the OUTPUT column: the limiter, and where its ceiling is — and LIMITER's plate
    void showGateMenu();            // GATE's plate: the gate the settings menu holds too
    void showTunerMenu();           // the tuner: whether it listens, and whether the amp is silent meanwhile
    void showTunerState();          // ...both written on its glass

    // A right click on DRIVE picks between the two ways, and offers a capture as a `.nam`: in steps the
    // one it stands on, turning smoothly the nearer of the two it stands between.
    std::unique_ptr<juce::FileChooser> captureChooser;
    void showDriveMenu();
    void saveCapture();

    // A right click on CAB opens the cabinets' window (Cabs.h): the lists, and what its keys do — the
    // cabinet that is playing handed out as the IR file it is, files taken in, and one of the player's
    // own renamed or removed.
    std::unique_ptr<juce::FileChooser> cabChooser;
    void showCabs();
    void saveCab();
    void chooseCabsToAdd();
    void renameCab();
    void removeCab();

    // The player's own cabinets: files taken into the IRs folder, CAB given a position for each, and
    // the first of the new ones played.
    void addCabinets (const juce::StringArray& paths);
    void cabinetsChanged();

    // While a lever is held, a tag over it says where it stands, in the lever's own decibels — which
    // for INPUT count from the lever's zero, so the middle of the travel reads 0.
    void sayLever (ColumnFader&, const juce::String& name, std::function<float()> reading);
    SegmentMeter inMeter, outMeter;

    // The lamps over the columns: each lights when its side has gone over full scale — the input as it
    // arrives, the output as it leaves — and stays lit long enough for the peak to be seen.
    static constexpr double clipHoldMs = 1000.0, clipFadeMs = 500.0;
    Lamp inClip, outClip;

    // GATE burns as hard as the gate is pressing the signal, LIMITER as hard as the limiter is squeezing
    // it. A click on either name opens what it is set by.
    Lamp gateLight, limiterLight;
    Legend gateDoor, limiterDoor;

protected:
    TunerFace tuner;

private:
    Emblem emblem;

    // The version on its plate, the family's orange dot beside it when a newer release has been seen.
    // A click opens the same window OrbitAmp's footer does — appkit's badge, kept invisible and only
    // for that window.
    Legend version;
    felitronics::appkit::VersionBadge about;

    // CPU: the share of each audio block's time the engine spends — the number OrbitAmp's footer calls
    // DSP. It warms as it climbs, and a click opens the same stage-by-stage window (ui/LoadBreakdown.h)
    // — here inside the face, in the family's colours.
    Legend load;
    std::unique_ptr<Sheet> sheet;            // the window that click opens, while it is open
    void showLoad();
    void showMidi();                         // the MIDI inputs, from the settings menu
    void showSheet (std::unique_ptr<juce::Component> content);

    // MEM: what the process is holding, as the system's own monitor counts it (core/ProcessMemory.h).
    // Only while the product stands alone — inside a host the process is the host, and the figure
    // would be the host's.
    Legend memory;
    int memoryTick = 0;          // the timer runs four times a second; this is asked once a second

    // PRESET: the one word, the family's orange dot beside it while the sound has moved away from the
    // preset it came from, and everything else — which preset, the list, saving, renaming — behind a
    // click.
    Legend presetDoor;
    void showPresetMenu();
    void timerCallback() override;

    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    template <typename Control>
    void bind (Control&, const juce::String& paramId);
    template <typename Control>
    std::unique_ptr<juce::ParameterAttachment> attach (Control&, const juce::String& paramId);

    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> toneBindings;
    std::array<juce::String, 3> toneBound;
    void bindTones();
    void bindButton (PushButton&, const char* paramId);

    void showReverbMenu();
    void showDelayMenu();
    void showDelayTime();
    void refreshHistory();
    void showRegisterMenu (int index);
    void chooseCab (int step);
    void showCab();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Console)
};

} // namespace orbitamp::face
