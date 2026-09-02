// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Parameters.h"
#include "core/CapturedBlock.h"
#include "core/DemoPlayer.h"
#include "core/ScopeTap.h"
#include "core/TunerEar.h"
#include "core/TunerTap.h"
#include "core/WaveRibbon.h"
#include "core/EqLink.h"
#include "core/CabinetIr.h"
#include "core/SoftLimiter.h"
#include "core/DelayStage.h"
#include "core/ReverbStage.h"

#include <felitronics/analysis/RollingSpectrumTap.h>
#include <felitronics/appkit/CompareHistory.h>
#include <felitronics/dynamics/NoiseGate.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace orbitamp
{

/** The plugin shell. Deliberately thin: the chain (boost -> EQ -> preamp -> EQ -> delay -> reverb ->
    power amp -> cabinet) lands in src/core/ behind small engines, and this class only pumps
    buffers into them and owns state. */
class AmpProcessor final : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    AmpProcessor();
    ~AmpProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "OrbitAmp"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Undo/redo + the A/B/C/D registers, from felitronics-appkit. It lives on the PROCESSOR, not
        the editor: the history is part of the session, so it has to survive closing the window and
        be saved with the plugin state.

        PerRegister topology — each register carries its own undo history and switching registers is
        NOT an undo step (its inverse is simply selecting the other slot). That is the deliberate
        opposite of OrbitCab, whose whole workspace is one timeline. */
    felitronics::appkit::CompareHistory history;

    /** The editor's zoom, 50-200%. It lives here rather than in the editor so it survives closing
        and reopening the window; it is message-thread only and never read by the audio path.
        Persisting it across sessions comes with the state work. */
    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float s) noexcept { editorScale = juce::jlimit (minScale, maxScale, s); }

    static constexpr float minScale = 0.5f;
    static constexpr float maxScale = 4.0f;

    /** What a window OPENS at, before the screen has its say. Separate from editorScale on purpose:
        editorScale follows the window, and the window is set by whoever hosts it — so an editor that
        opened from it was reading back whatever the last host had shrunk it to, and once anything
        shrank it the value stuck. A wanted size and a current size are two different facts. */
    static constexpr float preferredScale = 1.0f;

private:
    /** Pumps the history's settle timer — a burst of edits that has been quiet for a moment commits
        as ONE undo step, so dragging a knob is not fifty of them. */
    void timerCallback() override
    {
        history.tick();
        pumpDeviceWork();
        pumpTuner();
    }

    /** Listens only while someone is watching: with no editor there is no needle, and an MPM pass
        thirty times a second for nobody is the definition of waste. */
    void pumpTuner()
    {
        if (getActiveEditor() == nullptr)
            return;

        if (const double sr = getSampleRate();
            sr > 0.0 && ! juce::approximatelyEqual (sr, tunerEar.preparedRate()))
            tunerEar.prepare (sr);

        tunerEar.update (tunerTap, juce::Time::getMillisecondCounter());
    }

    /** The chain's round-trip to the host: whatever the three players' models need for
        rate-matching — reported whenever any of them changes. */
    void reportLatency();

    /** One thread for both blocks' model builds. One, because a load is twenty milliseconds and
        two blocks asking at once still finish inside a frame; a second thread would only let two
        WaveNets fight over the same cores the audio thread wants. */
    juce::ThreadPool modelPool { 1 };

public:
    /** Everything a moved control needs done OFF the audio thread, for both captured blocks: landing
        the dial where the gain knob points, the tone knobs where their slots stand, handing the
        player's load jobs to the pool and its finished models back, retiring what nobody plays.

        The timer calls this thirty times a second. It is public because it is the plugin's only
        message-thread heartbeat, and something that is not a host — a test — has to be able to drive
        it; without a driver a knob moves a parameter and nothing ever reads it, which is exactly the
        bug this became. */
    void pumpDeviceWork();

    /** A model is BUILT off the message thread — bytes from the pack, a network parsed and warmed,
        some twenty milliseconds — and brought back to the player through the message queue. A driver
        without a message loop (a test) sets this and the jobs run inside pumpDeviceWork instead. */
    bool inlineLoads = false;

private:
    /** Reads each EQ link's parameters into its stack. Called per block from the audio thread;
        a stack only redesigns when something actually moved. */
    void updateEqSettings() noexcept;

    /** Same, for the reverb: character and mix, applied only when they move. */
    void updateReverbSettings() noexcept;

    /** Same, for the delay — including the one computation that needs the PROCESSOR: the sync
        time in milliseconds, from the host's tempo when it conducts and the BPM field when it
        does not. */
    void updateDelaySettings() noexcept;

    float editorScale = preferredScale;

    std::array<core::EqLink, params::numEqLinks> eqLinks;
    core::DelayStage  delay;
    core::ReverbStage reverb;

public:
    /** The delay's picture taps, read-only: the face draws its comb from the same numbers the
        heads are actually standing on. */
    const core::DelayStage& delayTaps() const noexcept { return delay; }

private:

    /** The noise gate, from felitronics-core — the same engine OrbitCab ships. It keys off the
        raw input at the front of the chain; where it MUTES is the player's parameter. */
    felitronics::dynamics::NoiseGate gate;

    /** The gate's voicing, ours to edit only where a parameter exists (Decay -> closeMs). Kept
        here because the engine does not read its config back. */
    felitronics::dynamics::NoiseGate::Config gateCfg;
    float lastGateDecay = -1.0f;

public:
    /** The captured pedal in front, and the captured preamp after it. Each is a device list, the
        stage playing what is chosen from it, that device's measured controls as filters, and the taps
        its pictures read. The library and the loading live on the message thread; the audio thread
        only ever meets a model that is already in memory. */
    core::CapturedBlock boost    { device::DeviceLibrary::Slot::pedal };
    core::CapturedBlock preamp   { device::DeviceLibrary::Slot::preamp };
    core::CapturedBlock poweramp { device::DeviceLibrary::Slot::poweramp };   // a captured power stage, after the reverb

    /** Re-scans the devices folder and loads whatever the device parameters point at. Message
        thread. */
    void rescanDevices();

    /** The cabinet shelf — the embedded IR set, in params::cabIrNames order. The face draws the
        same bytes the engine convolves. */
    struct IrBytes { const char* data; int size; };
    static const IrBytes& cabIrBytes (int index);

    /** TEMPORARY — the audition loop player. Goes with the demo strip it belongs to. */
    core::DemoPlayer demo;
    void selectDemoLoop (int index);

    /** The raw input, kept for the tuner. The audio thread only ever writes it; listening happens
        on the processor's own message-thread pump, and every needle — the strip's miniature, the
        zoomed tuner — reads the one ear, so they can never disagree. */
    core::TunerTap tunerTap;
    core::TunerEar tunerEar;

    /** The gate's effective gain at the last block end, in dB — what a pressure meter shows.
        Written by the audio thread, read by the strip's thumb and the zoomed gate on their
        repaint clocks. */
    std::atomic<float> gateMeterDb { 0.0f };

    /** The output level after everything, and the two latched clip caps (the UI clears them). */
    std::atomic<float> outDb   { -90.0f };

    /** The level at every joint of the chain — the gain-staging story, readable per stage. */
    std::atomic<float> boostOutDb  { -90.0f };
    std::atomic<float> preampOutDb { -90.0f };

    /** What each captured block is FEEDING ITS MODEL, after its own input trim. Measured at the
        model's door rather than the block's, because that is the level the question is about: a
        capture answers to what goes in, and the meter has to be reading the same point the trim
        beside it moves. Indexed like blockSpectrumTap — 0 boost, 1 preamp. */
    std::array<std::atomic<float>, 2> blockInDb { -90.0f, -90.0f };
    std::atomic<float> cabOutDb    { -90.0f };
    std::atomic<bool>  inClip  { false };
    std::atomic<bool>  outClip { false };

    /** How hard the limiter squeezed the last block, in dB (0 = untouched) — the LIMIT badge. */
    std::atomic<float> limiterGrDb { 0.0f };

    /** The per-stage DSP load, orbitcab's grammar: each stage's wall-clock as a smoothed % of
        the block's real-time budget. Indexed by Stage; the footer badge reads these. */
    // In chain order, which is now also the order the breakdown reads: each captured block is
    // followed by its own EQ.
    enum Stage { stTotal, stTuner, stGate, stBoost, stEq1, stPreamp, stEq2, stDelay, stReverb,
                 stPower, stCab, stLimit, stOut, numStages };
    std::atomic<float> stageLoad[numStages] {};

    /** The dropout evidence: every block that BLEW its budget counts, and each stage keeps the
        worst share it has hit since somebody last looked (the breakdown panel clears them).
        A mean of 7% hides a worst of 200% — and one blown block is one audible drop. */
    std::atomic<float>    stageWorst[numStages] {};
    std::atomic<uint32_t> overruns { 0 };

    /** The load's last ~12 seconds, one column per ~33 ms holding the WORST total share inside
        it — a strip chart the panel draws. Peaks survive here; an EMA would eat them. */
    static constexpr int loadHistSize = 360;
    std::atomic<float> loadHist[loadHistSize] {};
    std::atomic<int>   loadHistPos { 0 };

    /** Latched: the limiter has worked since somebody last looked (the badge clears it). */
    std::atomic<bool> limiterWorked { false };

    /** Latched: the gate pressed a signal that was ABOVE its own threshold — it ate a live
        note, not a pause. Closing on silence is the job; this is the accident worth a light. */
    std::atomic<bool> gateWorked { false };

    /** One tap per captured block, at its output — which, now that each block owns its EQ, is the
        EQ's output too. The curve and the spectrum drawn under it therefore describe the same
        point, and there is one FFT per block rather than two at the same place. Lock-free SPSC with
        a starve-tolerant reader, so several views may sip from it. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> blockSpectrumTap;

    /** The pair's other half: what the EQ eats — the capture's voice before the console colours
        it. With the output tap above, an EQ pane shows before and after, the way the cabinet's
        picture does. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> blockInSpectrumTap;

    /** The cabinet's own pair — what goes into the IR and what leaves it, for the picture's faint
        spectra. Pushed around the convolution, channel 0, only while the cabinet is on. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> cabSpectrumTap;

    /** The reverb's pair: the door, and what the room ADDS — the wet alone, post its own HPF, at
        the mix. Channel 0, only while the reverb is on; the block draws them as its picture. */
    std::array<felitronics::analysis::RollingSpectrumTap, 2> reverbSpectrumTap;

    /** One analysis resolution for every consumer of the taps: mixed orders would make the tap
        force-republish on every alternating pull. */
    static constexpr int eqSpectrumOrder = 11;   // 2048

    /** ...and the frame a block's tap publishes while its TONE picture is THROWN OPEN.

        The tile's 2048 points are 23 Hz a bin: between 20 and 100 Hz that is three and a half bins,
        which is why the bottom of the spectrum reads as mush no matter how many pixels it is given.
        The big view asks for the longest frame the tap can give — 16384 — and reads it through the
        constant-Q pane, which feeds several window lengths from that one frame and reports in bands
        of a twenty-fourth of an octave: fifty-six of them across the same two decades.

        Not everywhere, because it is not free. A constant-Q reading costs about fifteen times the
        classic one, and the face runs six to eight panes at once between the two consoles, the
        cabinet and the reverb — so it is spent where there is ONE pane and somebody looking at it.

        No second tap: `RollingSpectrumTap` decouples cadence from window on purpose, so one ring
        serves any order and the switch is click-free — it force-publishes at the new size and the
        reader discards frames of the wrong one. What it does NOT do is serve two orders at once,
        and it need not: the console stops pulling when a picture is thrown open over it. */
    static constexpr int eqSpectrumOrderBig = 14;   // 16384 — the constant-Q pane's longest tier

    /** What resolution each block's pair is publishing at, written by that block's face. */
    std::array<std::atomic<int>, 2> blockSpectrumOrder;

    /** The raw input's peak this block, in dB — the level the gate KEYS off, for the meter the
        thresholds are drawn on. Same writer, same readers. */
    std::atomic<float> gateKeyDb { -90.0f };

private:

    // Cached atomic parameter pointers — getRawParameterValue does a map lookup, which the audio
    // thread should not be doing per block.
    struct EqLinkParams
    {
        std::atomic<float>* hpfOn    = nullptr;
        std::atomic<float>* hpfHz    = nullptr;
        std::atomic<float>* hpfSlope = nullptr;
        std::atomic<float>* loDb     = nullptr;
        std::atomic<float>* loHz     = nullptr;
        std::atomic<float>* hiDb     = nullptr;
        std::atomic<float>* hiHz     = nullptr;
        std::atomic<float>* lpfOn    = nullptr;
        std::atomic<float>* lpfHz    = nullptr;
        std::atomic<float>* lpfSlope = nullptr;
        std::atomic<float>* level    = nullptr;
        std::atomic<float>* b3On     = nullptr;
        std::atomic<float>* bellDb[3] {};
        std::atomic<float>* bellHz[3] {};
        std::atomic<float>* bellQ[3]  {};
    };

    std::array<EqLinkParams, params::numEqLinks> eqParams;

    std::atomic<float>* inTrimParam        = nullptr;
    std::atomic<float>* outTrimParam       = nullptr;
    std::atomic<float>* limiterOnParam     = nullptr;
    std::atomic<float>* stereoModeParam    = nullptr;
    float histWorst   = 0.0f;
    int   histSamples = 0;
    std::atomic<float>* limiterCeilParam   = nullptr;
    float lastOutGain = 1.0f;
    core::SoftLimiter limiter;

    core::CabinetIr cab;
    std::atomic<float>* cabOnParam = nullptr;
    std::atomic<float>* cabIrParam = nullptr;
    std::atomic<float>* cabHpfOnParam = nullptr;
    std::atomic<float>* cabHpfHzParam = nullptr;
    std::atomic<float>* cabHpfSlopeParam = nullptr;
    std::atomic<float>* cabLpfOnParam = nullptr;
    std::atomic<float>* cabLpfHzParam = nullptr;
    std::atomic<float>* cabLpfSlopeParam = nullptr;
    std::atomic<float>* cabTrimOnParam = nullptr;
    std::atomic<float>* cabTrimParam = nullptr;
    std::atomic<float>* cabPhaseParam = nullptr;
    int lastCabIr = -1;

    std::atomic<float>* boostInParam  = nullptr;
    std::atomic<float>* preampInParam = nullptr;
    std::atomic<float>* boostSmoothParam  = nullptr;
    std::atomic<float>* preampSmoothParam = nullptr;
    float lastBoostInGain  = 1.0f;
    float lastPreampInGain = 1.0f;
    float lastTrimGain = 1.0f;

    std::atomic<float>* gateOnParam        = nullptr;
    std::atomic<float>* gateThresholdParam = nullptr;
    std::atomic<float>* gatePosParam       = nullptr;
    std::atomic<float>* gateDecayParam     = nullptr;

    std::atomic<float>* delayOnParam      = nullptr;
    std::atomic<float>* delaySyncParam    = nullptr;
    std::atomic<float>* delayTimeMsParam  = nullptr;
    std::atomic<float>* delayDivParam     = nullptr;
    std::atomic<float>* delayBpmParam     = nullptr;
    std::atomic<float>* delayRepeatsParam = nullptr;
    std::atomic<float>* delayDarkParam    = nullptr;
    std::atomic<float>* delayOffsetParam  = nullptr;
    std::atomic<float>* delayMixParam     = nullptr;

    std::atomic<float>* reverbOnParam   = nullptr;
    std::atomic<float>* reverbTypeParam = nullptr;
    std::atomic<float>* reverbMixParam  = nullptr;
    std::atomic<float>* reverbDecayParam    = nullptr;
    std::atomic<float>* reverbPredelayParam = nullptr;
    std::atomic<float>* reverbHpfHzParam    = nullptr;

    std::atomic<float>* packCompParam    = nullptr;
    std::atomic<float>* powerOnParam     = nullptr;
    std::atomic<float>* powerGainParam   = nullptr;
    std::atomic<float>* powerSmoothParam = nullptr;
    std::atomic<float>* boostOnParam    = nullptr;
    std::atomic<float>* boostGainParam  = nullptr;
    std::atomic<float>* preampOnParam   = nullptr;
    std::atomic<float>* preampGainParam = nullptr;
    juce::AudioBuffer<float> scopeDry;   // a block's input, kept for its pictures

public:
    /** What the footer reports: the run's own facts, not the sound's. */
    double currentSampleRate() const noexcept { return getSampleRate(); }
    float  dspLoadPercent() const noexcept    { return dspLoad.load(); }

private:
    std::atomic<float> dspLoad { 0.0f };

    /** The channel mode's default follows the ENVIRONMENT until somebody chooses: the standalone
        opens on STEREO SPACE (one guitar in, a wide room out), a plugin on a mono bus on MONO, on
        a stereo bus on STEREO. `modeAutoValue` is what the environment last set — while the
        parameter still reads that, nobody has chosen and the environment may set it again; one
        hand-move or one restored session ends it. */
    bool stateWasRestored = false;
    int  modeAutoValue    = (int) params::StereoMode::mono;   // the layout's own default

    JUCE_DECLARE_WEAK_REFERENCEABLE (AmpProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};

} // namespace orbitamp
