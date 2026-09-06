// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Parameters.h"
#include "ui/Prefs.h"
#include "core/CapturedBlock.h"
#include "core/DemoPlayer.h"
#include "core/ScopeTap.h"
#include "core/TunerEar.h"
#include "core/TunerTap.h"
#include "core/WaveRibbon.h"
#include "core/EqLink.h"
#include "core/BypassFade.h"
#include "core/BypassWire.h"
#include "core/CabinetIr.h"
#include "core/SoftLimiter.h"
#include "core/DelayStage.h"
#include "core/ReverbStage.h"

#include <felitronics/analysis/RollingSpectrumTap.h>
#include <felitronics/appkit/CompareHistory.h>
#include <felitronics/appkit/UpdateChecker.h>
#include <felitronics/dynamics/NoiseGate.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace orbitamp
{

/** The plugin shell. Deliberately thin: the chain (boost -> EQ -> preamp -> EQ -> delay -> reverb ->
    cabinet) lands in src/core/ behind small engines, and this class only pumps
    buffers into them and owns state. */
class AmpProcessor final : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    AmpProcessor();
    /** Defaulted, and it is worth writing down WHY, because it looks like the place a weak
        reference has to be cleared and it is not.

        `setStateInformation` can arrive on any thread, so it marshals the restore to the message
        thread behind a `juce::WeakReference<AmpProcessor>`, and the host may destroy the plugin
        while that call is still queued. The guard against that is real — but it is already armed:
        `JUCE_DECLARE_WEAK_REFERENCEABLE` does not declare a bare `WeakReference::Master` (whose
        destructor only asserts). It declares a `WeakRefMaster` whose destructor calls `clear()`,
        and declares it LAST, so it is the first member destroyed. Adding a clear here changes
        nothing at all. I wrote one anyway, on a reading of the wrong destructor, and the review
        caught it. */
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
    /** NOT zero any more. Standing a room or an echo by leaves it ringing on purpose — that is
        what an insert's bypass does — so a host told there is no tail may cut an offline render or
        a freeze exactly where we started holding one. The longest thing in here is the delay's own
        line; the reverb's decay is shorter than that. Declared generously: the cost of over-stating
        a tail is a little extra rendering, and the cost of under-stating it is a truncated one. */
    double getTailLengthSeconds() const override             { return 8.0; }

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

    /** The release check behind the footer's version badge. OPT-IN and offline-safe: nothing reaches
        the network until a player presses "Check for updates" in the badge's popover — the dot only
        reports a release seen by an earlier, deliberate check. It sits on the processor for the same
        reason the history does: the window comes and goes, and this outlives it. */
    felitronics::appkit::UpdateChecker& updateChecker() noexcept { return updater; }

    /** Which wrapper is actually running — VST3 / AU / CLAP / Standalone — the badge's second line. */
    juce::String pluginFormat() const { return juce::AudioProcessor::getWrapperTypeDescription (wrapperType); }

    /** The editor's zoom, 50-400%. It lives here rather than in the editor so it survives closing
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

    /** The suffix a switch slot's saved position name wears in the state tree. */
    static constexpr const char* switchAimSuffix = "_pos";

    /** And the one a captured block's saved DEVICE name wears, beside its index parameter. */
    static constexpr const char* deviceAimSuffix = "_dev";

private:
    static constexpr int aimWindowFrames = 150;   // ~5 s at the 30 Hz pump

    int switchAimFrames = 0;                      // ticks left to keep aiming after a state change

    /** WHAT THE NAME PUMP LAST SAW. A name is written when the PARAMETER moves and at no other
        moment, which is the whole of what keeps this out of the undo timeline: a pack finishing
        its load, a rescan, a block coming back into the rig — none of them are edits, and none of
        them may leave a mark the player then has to press Cmd-Z through. Writing on a move also
        puts the name in the same settle burst as the move itself, so the two can never be
        committed apart and can never disagree inside one undo step.

        `namesPrimed` is the first tick, which only takes the baseline. */
    bool  namesPrimed = false;
    float lastDeviceValue[2] { -1.0f, -1.0f };
    float lastSwitchValue[2][core::CapturedBlock::numMeasured] { };

    /** What the aim itself last wrote, so a value that no longer reads it can be told from one
        that does — which is how a hand on the control wins against an aim still in flight. */
    float aimWroteDevice[2] { -1.0f, -1.0f };
    float aimWroteSwitch[2][core::CapturedBlock::numMeasured] { };

    // MUST precede `updater`: the checker takes the store by reference and is destroyed before it.
    juce::SharedResourcePointer<prefs::UpdateStore> updateStore;
    felitronics::appkit::UpdateChecker updater;   // built in the ctor: it needs the store above

    /** Pumps the history's settle timer — a burst of edits that has been quiet for a moment commits
        as ONE undo step, so dragging a knob is not fifty of them. */
    void timerCallback() override
    {
        history.tick();
        pumpDeviceWork();
        pumpTuner();
        applySwitchAims();
        pumpSwitchNames();
    }

    /** A DEVICE AND ITS SWITCHES ARE SAVED BY NAME, and this is the half that puts them back.

        Both parameters a host sees are numbers, and both numbers are places in a list that belongs
        to this machine on this day. The device's is an index into whatever packs are on disk,
        sorted bundled-first and then along the gain ramp — drop one new pack in and everything
        after it renumbers. A switch's is a fraction, an index into the pack's own position list —
        repack a device one position shorter and every old session reopens on a different position.
        Both drift silently, and the second is the one a player would never think to check.

        So the state carries NAMES beside the numbers, and on the way back the name decides. The
        device first, because the positions belong to its pack; then the switches, as soon as that
        pack is actually here, which is not the moment the state arrives — models load on the pool.
        Until then the aim stands.

        It stops standing after a few seconds. A player who reaches for that switch before the pack
        lands must win: an aim that outlived its window and overwrote a live hand would be worse than
        the drift it was fixing. */
    void applySwitchAims()
    {
        if (switchAimFrames <= 0)
            return;

        --switchAimFrames;
        bool waiting = false;

        // Everything this pass wants to change, decided before anything is written. Two reasons.
        // A suppression scope flushes whatever burst is open when it starts, so opening one on a
        // tick with nothing to write would chop a player's knob move into one undo step per tick
        // for five seconds. And the writes have to go in TOGETHER, because they are one
        // reconciliation, not twelve.
        std::vector<std::pair<juce::RangedAudioParameter*, float>> writes;

        // A HAND OUTRANKS THE AIM. If a parameter no longer reads what this aim last put there,
        // somebody moved it — a player auditioning devices the moment a session opens, or host
        // automation — and the aim stands down for that parameter rather than dragging it back
        // five seconds later. The document promised this; the code did not do it.
        const auto stolen = [] (const juce::RangedAudioParameter* p, float wrote)
        {
            return p != nullptr && wrote >= 0.0f && ! juce::approximatelyEqual (p->getValue(), wrote);
        };

        // Standing down is not enough on its own. The name pump is quiet while an aim is running
        // and takes each tick's values as its baseline, so a hand that moved a control mid-window
        // would leave the STORED name still describing what the aim wanted — the player's choice
        // would sound now and be gone at the next load. Closing the window and forgetting the
        // baseline makes the next pump tick see the move for what it is and write it down.
        const auto yieldToTheHand = [this]
        {
            switchAimFrames = 0;

            lastDeviceValue[0] = lastDeviceValue[1] = -2.0f;   // outside 0..1: never equal to a value

            for (auto& block : lastSwitchValue)
                for (auto& v : block)
                    v = -2.0f;
        };

        // Whether this block's DEVICE is where its name says it should be. The switch half must
        // not run until it is: the positions being aimed at belong to the named pack, and asking
        // the pack that happens to be loaded whether it has a position called "Lead" is asking
        // the wrong device a question about the right one.
        bool deviceSettled[2] = { true, true };

        const auto aimDevice = [&] (core::CapturedBlock& block, size_t b, const char* id)
        {
            const juce::Identifier key (juce::String (id) + deviceAimSuffix);

            if (! apvts.state.hasProperty (key))
                return;

            const auto want = apvts.state.getProperty (key).toString();

            if (want.isEmpty())
                return;

            auto* p = apvts.getParameter (id);

            if (stolen (p, aimWroteDevice[b]))
            {
                yieldToTheHand();
                return;
            }

            const int index = block.indexOfName (want);

            if (index < 0)
            {
                // This machine has no such device — TODAY. The name STAYS: it is the identity the
                // player chose, and a folder is a thing that gets filled in later. Deleting it
                // here would mean that opening a project before installing its pack, or on a
                // machine whose Devices folder is still empty, silently replaces the chosen device
                // with the fallback for ever. The number stands, the block prints whose voice is
                // actually playing, and the moment the pack arrives the name resolves on its own.
                return;
            }

            if (p != nullptr)
            {
                const float v = p->convertTo0to1 ((float) index);

                if (! juce::approximatelyEqual (p->getValue(), v))
                {
                    writes.emplace_back (p, v);
                    aimWroteDevice[b] = v;
                }
            }

            // Satisfied only when the block is actually PLAYING the named device, not when the
            // number has been written: setting the parameter is one tick and the pump that loads
            // the pack is the next. A window that closed in between would let the half that
            // writes names back record the device it was aiming away from.
            if (block.selectedName() != want)
            {
                waiting = true;
                deviceSettled[b] = false;
            }
        };

        aimDevice (boost,  0, params::boostDevice);
        aimDevice (preamp, 1, params::preampDevice);

        const auto aim = [&] (core::CapturedBlock& block, size_t b, auto idFor)
        {
            if (! deviceSettled[b])
            {
                waiting = true;   // ask again once the named device is the one that is playing
                return;
            }

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                const auto id = idFor (i);
                const juce::Identifier key (id + switchAimSuffix);

                if (! apvts.state.hasProperty (key))
                    continue;

                const auto want = apvts.state.getProperty (key).toString();

                if (want.isEmpty())
                    continue;

                auto* p = apvts.getParameter (id);

                if (stolen (p, aimWroteSwitch[b][(size_t) i]))
                {
                    yieldToTheHand();
                    continue;
                }

                const float v = block.switchParameterFor (i, want);

                if (v < 0.0f)
                {
                    // Still on its way: ask again next tick. Here and with no position by that
                    // name — a pack updated since the session was saved — leave the name where it
                    // is, for the same reason the device's stays: it is what the player chose, it
                    // costs nothing to keep, and a pack put back the way it was resolves it again.
                    // These never leak to another device: changing the device drops them.
                    if (! block.isReady())
                        waiting = true;

                    continue;
                }

                if (p != nullptr && ! juce::approximatelyEqual (p->getValue(), v))
                {
                    writes.emplace_back (p, v);
                    aimWroteSwitch[b][(size_t) i] = v;
                }
            }
        };

        aim (boost,  0, [] (int i) { return params::boostMeasured (i); });
        aim (preamp, 1, [] (int i) { return params::preampMeasured (i); });

        if (! writes.empty())
        {
            // NOT an edit, and not a gesture. Putting a number back where its name says it belongs
            // is the plugin agreeing with the state it was handed — so it must not become an undo
            // step the player has to press Cmd-Z through on a session they just opened, and it
            // must not look to a host in automation Write mode like a hand on the control. The
            // suppression scope absorbs the drift into the baseline and leaves the workspace
            // marked dirty, which is honest: what is in memory no longer matches what is on disk.
            const felitronics::appkit::CompareHistory::ScopedSuppress hush (history);

            for (auto& [p, v] : writes)
                p->setValueNotifyingHost (v);
        }

        if (! waiting)
            switchAimFrames = 0;
    }

    void pumpSwitchNames()
    {
        // While an aim is in flight the STORED names are the truth and the parameters are being
        // moved to match them: read the values as the new baseline and write nothing at all.
        const bool quiet = switchAimFrames > 0 || ! namesPrimed;

        const auto note = [&] (core::CapturedBlock& block, size_t b, const char* devId, auto idFor)
        {
            const float dv = apvts.getRawParameterValue (devId)->load();
            const bool deviceMoved = ! quiet && ! juce::approximatelyEqual (dv, lastDeviceValue[b]);
            lastDeviceValue[b] = dv;

            if (deviceMoved)
            {
                const juce::Identifier key (juce::String (devId) + deviceAimSuffix);

                if (const auto name = block.selectedName(); name.isNotEmpty())
                    apvts.state.setProperty (key, name, nullptr);
                else
                    apvts.state.removeProperty (key, nullptr);

                // A position name is only ever about the device it was read from, so the device
                // leaving takes all of them with it — that is what stops a name resolving on some
                // later pack that happens to spell a position the same way. They are written again
                // from the NEW pack in the same breath, below, because a device that arrives with
                // no names is a device saved by number until every switch has been touched.
                for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
                    apvts.state.removeProperty (juce::Identifier (idFor (i) + switchAimSuffix), nullptr);
            }

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                const auto  id = idFor (i);
                const float v  = apvts.getRawParameterValue (id)->load();
                const bool moved = ! quiet
                                     && (deviceMoved
                                          || ! juce::approximatelyEqual (v, lastSwitchValue[b][(size_t) i]));
                lastSwitchValue[b][(size_t) i] = v;

                if (! moved)
                    continue;

                const juce::Identifier key (id + switchAimSuffix);

                if (const auto name = block.switchValueAt (i, v); name.isNotEmpty())
                    apvts.state.setProperty (key, name, nullptr);
                else
                    apvts.state.removeProperty (key, nullptr);   // a swept knob owns no name
            }
        };

        note (boost,  0, params::boostDevice,  [] (int i) { return params::boostMeasured (i); });
        note (preamp, 1, params::preampDevice, [] (int i) { return params::preampMeasured (i); });

        namesPrimed = true;
    }

    /** WRITES EVERY NAME, ONCE, BEFORE ANYBODY IS WATCHING — called from the constructor, after
        the first scan and before the history takes its baseline.

        Without it a fresh instance has no names in it at all, because the pump only writes when a
        parameter MOVES and nothing has moved yet. A player who opens the plugin, likes what the
        default device does, dials a sound around it and saves that as a preset would get a preset
        that identifies its device by number — which is the whole disease. The seed costs one pass
        and, because the baseline is taken after it, it is not an edit and leaves no undo step.

        `RigPlayer::load` reads the manifest synchronously, so the positions are knowable here even
        though the model bytes are not. */
    void seedSwitchNames()
    {
        const auto seed = [this] (core::CapturedBlock& block, const char* devId, auto idFor)
        {
            if (const auto name = block.selectedName(); name.isNotEmpty())
                apvts.state.setProperty (juce::Identifier (juce::String (devId) + deviceAimSuffix),
                                         name, nullptr);

            if (! block.isReady())
                return;

            for (int i = 0; i < core::CapturedBlock::numMeasured; ++i)
            {
                const auto id = idFor (i);

                if (const auto n = block.switchValueAt (i, apvts.getRawParameterValue (id)->load());
                    n.isNotEmpty())
                    apvts.state.setProperty (juce::Identifier (id + switchAimSuffix), n, nullptr);
            }
        };

        seed (boost,  params::boostDevice,  [] (int i) { return params::boostMeasured (i); });
        seed (preamp, params::preampDevice, [] (int i) { return params::preampMeasured (i); });
    }

    /** The state changed under us — a session opened, a register recalled: aim the switches again. */
    void markSwitchAimsPending() noexcept
    {
        switchAimFrames = aimWindowFrames;

        // Nothing has been written by THIS aim yet, so nothing can have been taken from under it.
        aimWroteDevice[0] = aimWroteDevice[1] = -1.0f;

        for (auto& block : aimWroteSwitch)
            for (auto& v : block)
                v = -1.0f;
    }

    /** Listens only while someone is watching: with no editor there is no needle, and an MPM pass
        thirty times a second for nobody is the definition of waste. */
    void pumpTuner()
    {
        // Nobody watching, or the tuner is not in the rig: an MPM pass thirty times a second
        // for a needle that is not there is the definition of waste.
        if (getActiveEditor() == nullptr || ! linkWorks (params::rowTuner))
            return;

        if (const double sr = getSampleRate();
            sr > 0.0 && ! juce::approximatelyEqual (sr, tunerEar.preparedRate()))
            tunerEar.prepare (sr);

        tunerEar.update (tunerTap, juce::Time::getMillisecondCounter());
    }

    /** IS THIS LINK WORKING — the one question the chain asks, and the only shape of it.

        In the rig AND on. Out of the rig is nothing at all; STANDBY is here but not taking new
        signal. Both are ordinary parameters, so this reads like any other switch — no mirror to
        keep, nothing to normalise, and no way to express "not in the rig but playing". */
    bool linkWorks (params::ChainRow row) const noexcept
    {
        const auto* on = rowOn[(size_t) row];
        const auto* in = rowPresent[(size_t) row];

        return (on == nullptr || on->load() > 0.5f) && (in == nullptr || in->load() > 0.5f);
    }

    /** IN THE RIG, on its own — for the links that keep running while they stand by. An additive
        block out of the rig is unplugged: cleared, and costing nothing. In the rig it runs whether
        it is on or not, and `linkWorks` decides only whether it is being FED. */
    bool linkInRig (params::ChainRow row) const noexcept
    {
        const auto* in = rowPresent[(size_t) row];
        return in == nullptr || in->load() > 0.5f;
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
    // The list itself lives with the parameters now — see params::Stage. Pulled in whole so
    // every `nsStage[stBoost]` in this class still reads the way it always did.
    using Stage = params::Stage;
    using enum params::Stage;
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
    std::atomic<float>* stereoModeParam    = nullptr;
    float histWorst   = 0.0f;
    int   histSamples = 0;
    std::atomic<float>* limiterCeilParam   = nullptr;
    float lastOutGain = 1.0f;
    core::SoftLimiter limiter;

    core::CabinetIr cab;
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

    /** The two switches of every link, straight off `params::chainLinks` — plus the two ends,
        which are not rows in the strip yet but have had their switches since they were declared.
        Filled once in prepare; read on the audio thread like every other parameter. */
    std::atomic<float>* rowOn[params::numChainRows]      { };
    std::atomic<float>* rowPresent[params::numChainRows] { };

    std::atomic<float>* gateThresholdParam = nullptr;
    std::atomic<float>* gatePosParam       = nullptr;
    std::atomic<float>* gateDecayParam     = nullptr;

    std::atomic<float>* delaySyncParam    = nullptr;
    std::atomic<float>* delayTimeMsParam  = nullptr;
    std::atomic<float>* delayDivParam     = nullptr;
    std::atomic<float>* delayBpmParam     = nullptr;
    std::atomic<float>* delayRepeatsParam = nullptr;
    std::atomic<float>* delayDarkParam    = nullptr;
    std::atomic<float>* delayOffsetParam  = nullptr;
    std::atomic<float>* delayMixParam     = nullptr;

    std::atomic<float>* reverbTypeParam = nullptr;
    std::atomic<float>* reverbMixParam  = nullptr;
    std::atomic<float>* reverbDecayParam    = nullptr;
    std::atomic<float>* reverbPredelayParam = nullptr;
    std::atomic<float>* reverbHpfHzParam    = nullptr;

    std::atomic<float>* packCompParam    = nullptr;
    std::atomic<float>* boostGainParam  = nullptr;
    std::atomic<float>* preampGainParam = nullptr;
    juce::AudioBuffer<float> scopeDry;   // a block's input, kept for its pictures

    /** The crossfade of every link that REPLACES the signal, and the one buffer they share to do
        it: what the block was handed, kept only while a fade is actually running. Indexed by row,
        so a link asks for its own by name. */
    core::BypassFade         blockFade[params::numChainRows];
    juce::AudioBuffer<float> fadeDry;

    /** One per captured block: the delay a BYPASSED block still has to carry, so the chain does
        not arrive early the moment somebody stands a rate-matching model down. Only runs when the
        block is not working — while it is working the model carries its own latency and there is
        nothing to imitate. */
    core::BypassWire wire[2];

    /** Can a crossfade actually run this block? The buffer was sized in prepare, and a host may
        hand over a bigger block than it promised — copying into it on that block would walk off
        the end of the heap. Without room we simply do not blend: the switch lands hard, which is
        a click, and a click is a great deal better than a corrupted heap. */
    bool canFade (int numSamples, int numChannels) const noexcept
    {
        return numSamples <= fadeDry.getNumSamples() && numChannels <= fadeDry.getNumChannels();
    }

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
