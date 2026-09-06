// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

namespace orbitamp
{

/** THE GATE, whole, with no face of its own.

    It used to live inside the IN column: its threshold rode the same rail as the input volume, and
    a drag moved whichever of the two runners was nearer the grab. Two hands on one scale, told
    apart by proximity — the column's own header said so. So the gate moved out, and what it left
    behind is a column that does one thing.

    Its door is the strip's own arrow, and its controls are the menu that opens from it: stepped
    positions, a decay, where it mutes, and LEARN. That is not a lesser console than the caret was.
    A threshold is a decibel, and three named positions plus a measurement that sets one FOR you is
    a better way to reach one than aiming a runner at a moving meter.

    NOT a component. It owns no pixels — only the gate's parameters, its menu, and the measurement.

    LEARN is why this is a long-lived member of the editor rather than something made when a menu
    opens. It runs for three seconds after the click that started it, keeps a trace the big overlay
    draws from a POINTER into this object, and at the end writes a threshold AND switches the gate
    on. Every one of those outlives a popup; two of them outlive a patch, which is what `cancel()`
    is for. */
class GateConsole final : private juce::Timer
{
public:
    GateConsole (juce::AudioProcessorValueTreeState& state, const std::atomic<float>& keyDbSource)
        : keyDb (keyDbSource),
          presentP (*state.getParameter (params::gatePresent)),
          onP    (*state.getParameter (params::gateOn)),
          thP    (*state.getParameter (params::gateThreshold)),
          decayP (*state.getParameter (params::gateDecay)),
          posP   (*state.getParameter (params::gatePos))
    {
        onAtt    = std::make_unique<juce::ParameterAttachment> (onP,    [] (float) {});
        thAtt    = std::make_unique<juce::ParameterAttachment> (thP,    [] (float) {});
        decayAtt = std::make_unique<juce::ParameterAttachment> (decayP, [] (float) {});
        posAtt   = std::make_unique<juce::ParameterAttachment> (posP,   [] (float) {});
    }

    ~GateConsole() override
    {
        stopTimer();

        // WITHOUT THIS THE WEAK REFERENCE IS A LIE. `WeakReference::Master`'s destructor only
        // asserts (in debug); in a release build the shared holder keeps the dead pointer and
        // every `safe != nullptr` still answers true. The menu's guard is this line.
        masterReference.clear();
    }

    /** The big LEARN overlay's hooks: measurement started, measurement spoke its verdict. */
    std::function<void()>              onLearnBegin;
    std::function<void (juce::String)> onLearnDone;

    /** The overlay draws the measurement from HERE, by pointer — which is the whole reason this
        object outlives every popup and is declared before the overlay that reads it. */
    const std::vector<float>& learnTraceRef() const noexcept { return learnTrace; }

    static constexpr int learnTotalTicks = 90;   // 3 s at 30 Hz
    static constexpr int learnEdgeTicks  = 15;   // half a second each end, thrown away

    /** The threshold LEARN would set if it ended now — the overlay's live dashed line.
        -999 while the trusted window has heard nothing yet. */
    float learnPendingDb() const
    {
        if (! learning || learnPeak <= -119.0f)
            return -999.0f;

        return juce::jlimit (-80.0f, -10.0f, learnPeak + params::gateHysteresisDb);
    }

    bool isLearning() const noexcept { return learning; }

    /** STOP, and write nothing.

        A measurement takes three seconds, and three seconds is long enough to switch a register,
        load a preset, undo, or take the gate out of the rig. Finishing into whatever patch happens
        to be live by then would set a threshold nobody asked for and switch on a gate somebody had
        just switched off — and it would not look like a bug in the gate. It would look like the
        gate turning itself on by itself, three seconds after you looked away. */
    void cancel()
    {
        if (! learning)
            return;


        learning = false;
        stopTimer();

        if (onLearnDone != nullptr)
            onLearnDone ("CANCELLED");
    }

    /** The gate's whole console. `withVolume` is gone with the column it belonged to: the trim's
        RESET was the COLUMN's item, and the column has its own door now. */
    void showMenu (juce::Point<int> screenPos)
    {
        const bool  isOn  = onP.getValue() > 0.5f;
        const float th    = thP.convertFrom0to1 (thP.getValue());
        const bool  metal = decayP.getValue() > 0.5f;

        const auto matches = [&] (float t, bool m)
        {
            return isOn && std::abs (th - t) < 0.5f && metal == m;
        };

        juce::PopupMenu m;

        // The header carries the NUMBER, and that is not decoration. The threshold is continuous
        // and these positions are three points on it, so a value restored from a preset or drawn
        // by an automation lane can easily sit between them — with no item ticked and, without
        // this, nothing anywhere to say what it is.
        m.addSectionHeader (isOn ? "GATE  " + juce::String (juce::roundToInt (th)) + " DB"
                                 : juce::String ("GATE  OFF"));
        m.addItem (1, "OFF",         true, ! isOn);
        m.addItem (2, "SOFT   -60",  true, matches (-60.0f, false));
        m.addItem (3, "MEDIUM -50",  true, matches (-50.0f, false));
        m.addItem (4, "HARD   -40",  true, matches (-40.0f, true));
        m.addItem (5, "LEARN",       true, learning);

        juce::PopupMenu decay;
        decay.addItem (10, "NORMAL", true, ! metal);
        decay.addItem (11, "METAL",  true, metal);
        m.addSubMenu ("DECAY", decay);

        // WHERE the gate mutes. It always keys off the raw input; the VCA can stand at the front
        // or after the preamp.
        const bool preReverb = posP.getValue() > 0.5f;

        juce::PopupMenu where;
        where.addItem (12, params::gatePositions[0].toUpperCase(), true, ! preReverb);
        where.addItem (13, params::gatePositions[1].toUpperCase(), true, preReverb);
        m.addSubMenu ("MUTES AT", where);

        // The menu outlives the click that opened it, and this console dies with its window.
        // Same guard the gear's and the limiter's menus carry — a weak reference rather than a
        // SafePointer only because this has no pixels and is not a Component.
        m.showMenuAsync (juce::PopupMenu::Options()
                             .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                         [safe = juce::WeakReference<GateConsole> (this)] (int r)
                         {
                             if (safe != nullptr)
                                 safe->apply (r);
                         });
    }

private:
    void apply (int choice)
    {
        if (choice == 0)
            return;

        // OFF is a TOGGLE, not a one-way door: picking it on a silent gate turns the gate on —
        // the item must be un-pickable or the menu can only ever kill.
        if (choice == 1)
        {
            onAtt->setValueAsCompleteGesture (onP.getValue() > 0.5f ? 0.0f : 1.0f);
            return;
        }

        if (choice == 12 || choice == 13)
        {
            posAtt->setValueAsCompleteGesture (choice == 13 ? 1.0f : 0.0f);
            return;
        }

        if (choice == 10 || choice == 11)
        {
            decayAtt->setValueAsCompleteGesture (choice == 11 ? 1.0f : 0.0f);
            return;
        }

        if (choice == 5)
        {
            // Stay quiet: the fuse burns up the overlay's edge and the threshold lands the full
            // hysteresis above whatever floor was measured.
            learning   = true;
            learnTicks = 0;
            learnPeak  = -120.0f;
            learnTrace.clear();
            learnTrace.reserve ((size_t) learnTotalTicks);

            // What the patch looked like when the measurement began. If any of it moves while we
            // are counting, somebody else is driving — a register recalled, a preset loaded, an
            // undo, the gate taken out of the rig — and finishing would write into a patch that
            // is no longer the one that asked. Watched HERE rather than through a hook on the
            // history, because a hook is one slot and a host may open two editors.
            watchPresent = presentP.getValue();
            watchOn      = onP.getValue();
            watchTh      = thP.getValue();

            startTimerHz (30);

            if (onLearnBegin != nullptr)
                onLearnBegin();

            return;
        }

        onAtt->setValueAsCompleteGesture (1.0f);
        thAtt->setValueAsCompleteGesture (choice == 2 ? -60.0f : choice == 3 ? -50.0f : -40.0f);
        decayAtt->setValueAsCompleteGesture (choice == 4 ? 1.0f : 0.0f);   // HARD is the metal chop
    }

    /** Only while learning. A console with no face has nothing to repaint the rest of the time,
        and a timer running for nobody is the thing this whole rework keeps deleting. */
    void timerCallback() override
    {
        if (! learning)
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

        const float now = keyDb.load();

        ++learnTicks;
        learnTrace.push_back (now);   // the waveform on the overlay IS the progress

        // The window's edges are thrown away: the menu click itself, and the hand leaving the
        // mouse, are not the noise floor.
        if (learnTicks > learnEdgeTicks && learnTicks <= learnTotalTicks - learnEdgeTicks)
            learnPeak = juce::jmax (learnPeak, now);

        if (learnTicks < learnTotalTicks)
            return;

        learning = false;
        stopTimer();

        // Nothing arrived: a muted input teaches nothing.
        juce::String verdict = "NOTHING HEARD";

        if (learnPeak >= -75.0f)
        {
            const float th = juce::jlimit (-80.0f, -10.0f, learnPeak + params::gateHysteresisDb);
            thAtt->setValueAsCompleteGesture (th);
            onAtt->setValueAsCompleteGesture (1.0f);
            verdict = "SET " + juce::String (juce::roundToInt (th)) + " DB";
        }

        if (onLearnDone != nullptr)
            onLearnDone (verdict);
    }

    const std::atomic<float>& keyDb;
    juce::RangedAudioParameter& presentP;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& thP;
    juce::RangedAudioParameter& decayP;
    juce::RangedAudioParameter& posP;
    std::unique_ptr<juce::ParameterAttachment> onAtt, thAtt, decayAtt, posAtt;

    bool  learning   = false;
    int   learnTicks = 0;
    float learnPeak  = -120.0f;
    std::vector<float> learnTrace;
    float watchPresent = 0.0f, watchOn = 0.0f, watchTh = 0.0f;

    JUCE_DECLARE_WEAK_REFERENCEABLE (GateConsole)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateConsole)
};

} // namespace orbitamp
