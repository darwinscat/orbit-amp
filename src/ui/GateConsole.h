// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "GateLearner.h"

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
    is for. The measurement itself is GateLearner's (GateLearner.h) — the same one every face's GATE
    menu runs — and what is this console's is the words it is projected with. */
class GateConsole final
{
public:
    GateConsole (juce::AudioProcessorValueTreeState& state, const std::atomic<float>& keyDbSource)
        : learner (state, keyDbSource),
          onP    (*state.getParameter (params::gateOn)),
          thP    (*state.getParameter (params::gateThreshold)),
          decayP (*state.getParameter (params::gateDecay)),
          posP   (*state.getParameter (params::gatePos))
    {
        onAtt    = std::make_unique<juce::ParameterAttachment> (onP,    [] (float) {});
        thAtt    = std::make_unique<juce::ParameterAttachment> (thP,    [] (float) {});
        decayAtt = std::make_unique<juce::ParameterAttachment> (decayP, [] (float) {});
        posAtt   = std::make_unique<juce::ParameterAttachment> (posP,   [] (float) {});

        learner.onBegin = [this] { if (onLearnBegin != nullptr) onLearnBegin(); };
        learner.onDone  = [this] (const GateLearner::Take& take)
        {
            if (onLearnDone == nullptr)
                return;

            using Outcome = GateLearner::Outcome;
            onLearnDone (take.outcome == Outcome::set       ? "SET " + juce::String (juce::roundToInt (take.thresholdDb)) + " DB"
                       : take.outcome == Outcome::cancelled ? juce::String ("CANCELLED")
                                                            : juce::String ("NOTHING HEARD"));   // a muted input teaches nothing
        };
    }

    ~GateConsole()
    {
        // The menu's weak reference needs nothing here: `JUCE_DECLARE_WEAK_REFERENCEABLE` declares
        // a `WeakRefMaster` whose own destructor clears, and declares it last, so it is the first
        // member to go. (The bare `WeakReference::Master` is the one that only asserts — reading
        // that destructor instead of the macro is how a redundant clear got written here.)
    }

    /** The big LEARN overlay's hooks: measurement started, measurement spoke its verdict. */
    std::function<void()>              onLearnBegin;
    std::function<void (juce::String)> onLearnDone;

    /** The overlay draws the measurement from HERE, by pointer — which is the whole reason this
        object outlives every popup and is declared before the overlay that reads it. */
    const std::vector<float>& learnTraceRef() const noexcept { return learner.trace(); }

    static constexpr int learnTotalTicks = core::GateLearn::totalTicks;   // 3 s at 30 Hz
    static constexpr int learnEdgeTicks  = core::GateLearn::edgeTicks;    // half a second each end, thrown away

    /** The threshold LEARN would set if it ended now — the overlay's live dashed line.
        -999 while the trusted window has heard nothing yet. */
    float learnPendingDb() const { return learner.pendingDb(); }

    bool isLearning() const noexcept { return learner.isLearning(); }

    /** STOP, and write nothing (GateLearner::cancel says why). */
    void cancel() { learner.cancel(); }

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
        // OFF on top, ruled off, and only while there is something to switch off. No ON: a gate is
        // switched on by LEARN — three seconds of quiet set the threshold and arm it — or by one of
        // the three positions, each of which arms it too (Oleh, 24.10).
        if (isOn)
        {
            m.addItem (1, "OFF");
            m.addSeparator();
        }

        m.addItem (5, "LEARN",       true, learner.isLearning());
        m.addSeparator();
        m.addItem (2, "SOFT   -60",  true, matches (-60.0f, false));
        m.addItem (3, "MEDIUM -50",  true, matches (-50.0f, false));
        m.addItem (4, "HARD   -40",  true, matches (-40.0f, true));

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

        // OFF is offered only to a gate that is on: the way back on is LEARN or a position.
        if (choice == 1)
        {
            onAtt->setValueAsCompleteGesture (0.0f);
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
            learner.start();
            return;
        }

        onAtt->setValueAsCompleteGesture (1.0f);
        thAtt->setValueAsCompleteGesture (choice == 2 ? -60.0f : choice == 3 ? -50.0f : -40.0f);
        decayAtt->setValueAsCompleteGesture (choice == 4 ? 1.0f : 0.0f);   // HARD is the metal chop
    }

    GateLearner learner;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& thP;
    juce::RangedAudioParameter& decayP;
    juce::RangedAudioParameter& posP;
    std::unique_ptr<juce::ParameterAttachment> onAtt, thAtt, decayAtt, posAtt;

    JUCE_DECLARE_WEAK_REFERENCEABLE (GateConsole)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateConsole)
};

} // namespace orbitamp
