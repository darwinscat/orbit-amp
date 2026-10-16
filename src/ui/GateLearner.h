// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../core/GateLearn.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <vector>

namespace orbitamp
{

/** LEARN, RUNNING: the gate's measurement (core/GateLearn.h) on a clock, fed from the processor's gate
    key level, and written into the gate's own parameters when it is done — the threshold it found, and
    the gate switched on.

    NOT a component, and no menu: it owns no pixels and no words. OrbitAmp's GateConsole runs it from
    its menu and projects it on the big overlay; every face's console runs the same one from its GATE
    menu and shows it on the face's own glass. What each says about the outcome is its own — the
    overlay "SET -62 DB", a face "-62 dB" — so the outcome comes back as what happened and the number,
    not as words.

    IT OUTLIVES THE CLICK THAT STARTED IT. Three seconds is long enough to switch a register, load a
    preset, undo, or take the gate out of the rig; so it watches the gate's switches and threshold as
    they stood at the start, and if any of them moves it stops and writes nothing (cancel). Watched
    HERE rather than through a hook on the history, because a hook is one slot and a host may open two
    editors. */
class GateLearner final : private juce::Timer
{
public:
    GateLearner (juce::AudioProcessorValueTreeState& state, const std::atomic<float>& keyDbSource)
        : keyDb (keyDbSource),
          presentP (*state.getParameter (params::gatePresent)),
          onP      (*state.getParameter (params::gateOn)),
          thP      (*state.getParameter (params::gateThreshold))
    {
        onAtt = std::make_unique<juce::ParameterAttachment> (onP, [] (float) {});
        thAtt = std::make_unique<juce::ParameterAttachment> (thP, [] (float) {});
    }

    ~GateLearner() override { stopTimer(); }

    enum class Outcome { set, nothingHeard, cancelled };

    /** HOW A TAKE ENDED. A second measurement run in the same quiet seconds — a hum learner, say —
        would bring its answer back here, beside the threshold: written at the same moment, and
        thrown away by the same cancel. */
    struct Take
    {
        Outcome outcome;
        float thresholdDb;      // what was set; core::GateLearn::nothing unless `set`
    };

    /** Measurement started; measurement ended, however it ended. */
    std::function<void()>             onBegin;
    std::function<void (const Take&)> onDone;

    /** Stay quiet: three seconds from now the gate is set the full hysteresis above whatever floor was
        heard, and switched on. Picked again while running, it starts over. */
    void start()
    {
        take.begin();

        // What the patch looked like when the measurement began. If any of it moves while we are
        // counting, somebody else is driving — and finishing would write into a patch that is no
        // longer the one that asked.
        watchPresent = presentP.getValue();
        watchOn      = onP.getValue();
        watchTh      = thP.getValue();

        startTimerHz (core::GateLearn::ticksPerSecond);

        if (onBegin != nullptr)
            onBegin();
    }

    /** STOP, and write nothing. Finishing into whatever patch happens to be live by then would set a
        threshold nobody asked for and switch on a gate somebody had just switched off — and it would not
        look like a bug in the gate. It would look like the gate turning itself on by itself, three
        seconds after you looked away. */
    void cancel()
    {
        if (! take.isRunning())
            return;

        take.stop();
        stopTimer();

        if (onDone != nullptr)
            onDone ({ Outcome::cancelled, core::GateLearn::nothing });
    }

    bool isLearning() const noexcept { return take.isRunning(); }

    /** What was heard so far, a reading a tick — a picture draws from HERE, by reference. */
    const std::vector<float>& trace() const noexcept { return take.heard(); }

    /** The threshold the take would set if it ended now; core::GateLearn::nothing while there is none. */
    float pendingDb() const noexcept { return take.pendingDb(); }

private:
    /** Only while learning. Nothing to do the rest of the time, and a timer running for nobody is the
        thing this whole rework keeps deleting. */
    void timerCallback() override
    {
        if (! take.isRunning())
        {
            stopTimer();
            return;
        }

        // Somebody else moved the gate while we were counting. Stop, and write nothing.
        if (! juce::approximatelyEqual (presentP.getValue(), watchPresent)
            || ! juce::approximatelyEqual (onP.getValue(), watchOn)
            || ! juce::approximatelyEqual (thP.getValue(), watchTh))
        {
            cancel();
            return;
        }

        if (! take.hear (keyDb.load()))
            return;

        stopTimer();

        Take done { Outcome::nothingHeard, core::GateLearn::nothing };   // a muted input teaches nothing

        if (const float th = take.resultDb(); th > core::GateLearn::nothing)
        {
            thAtt->setValueAsCompleteGesture (th);
            onAtt->setValueAsCompleteGesture (1.0f);
            done = { Outcome::set, th };
        }

        if (onDone != nullptr)
            onDone (done);
    }

    const std::atomic<float>& keyDb;
    juce::RangedAudioParameter& presentP;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& thP;
    std::unique_ptr<juce::ParameterAttachment> onAtt, thAtt;

    core::GateLearn take { params::gateHysteresisDb };
    float watchPresent = 0.0f, watchOn = 0.0f, watchTh = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateLearner)
};

} // namespace orbitamp
