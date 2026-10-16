// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <atomic>

namespace orbitamp::core
{

/** THE HUM'S LEARN, BETWEEN A WINDOW AND THE AUDIO THREAD. The hum stage (felitronics::dehum) learns
    only where it runs: beginLearn and endLearn belong to the audio thread, and nothing in the stage is
    atomic. So a window ASKS — begin, end and keep, end and throw away — and the processor serves the
    ask at the start of its next block; what the learn found comes back the same way, field by field,
    and `answers` counts the answers given. Nothing here locks and nothing allocates, on either side.

    What the audio thread also says every block, for a take to plan by: whether a mode other than OFF
    is in force (only then is there a hum learn to run), whether the stage is taking a hum off right now
    (a learn begun now starts from a model it has, and settles sooner), and how long the learn now
    running has run — the clock the take reads its ticks against, since the stage's answer is in its
    own time, not the window's.

    One processor owns one; JUCE-free, like the gate's measurement it serves (GateLearn.h). */
struct HumLearnLink
{
    enum Ask : int { none, begin, keep, drop };

    /** How the stage judged a learn — felitronics::dehum::HumRemover::LearnVerdict, in its order (the
        processor holds it to that) — or no answer at all. */
    enum Verdict : int { unanswered = -1, learned, disturbed, nothingHeard, tooShort, notLearning };

    struct Answer
    {
        int   verdict      = unanswered;
        bool  kept         = false;    // the stage now runs what was learned
        int   family       = 0;        // what was heard: 50 or 60, 0 for none
        float humDb        = -200.0f;  // the hum learned, dB re a full-scale sine
        float settledAtMs  = -1.0f;    // when the stage's settled window opened, ms into the learn (-1: never)
        float floorAfterDb = -200.0f;  // the peak of what it let through over that window, dBFS
    };

    // The window's to write, the audio thread's to take.
    std::atomic<int> ask { none };

    // The audio thread's to write, every block.
    std::atomic<bool>  on      { false };
    std::atomic<bool>  running { false };
    std::atomic<float> learnMs { -1.0f };   // -1 while no learn runs

    // The answer: the fields first, then `answers` — released, so a window that sees the count move
    // (acquire) reads the fields that came with it.
    std::atomic<int>      verdict     { unanswered };
    std::atomic<bool>     kept        { false };
    std::atomic<int>      family      { 0 };
    std::atomic<float>    humDb       { -200.0f };
    std::atomic<float>    settledAtMs { -1.0f };
    std::atomic<float>    floorAfterDb { -200.0f };
    std::atomic<unsigned> answers     { 0 };

    void tell (const Answer& a) noexcept
    {
        verdict.store (a.verdict, std::memory_order_relaxed);
        kept.store (a.kept, std::memory_order_relaxed);
        family.store (a.family, std::memory_order_relaxed);
        humDb.store (a.humDb, std::memory_order_relaxed);
        settledAtMs.store (a.settledAtMs, std::memory_order_relaxed);
        floorAfterDb.store (a.floorAfterDb, std::memory_order_relaxed);
        answers.fetch_add (1, std::memory_order_release);
    }

    Answer answer() const noexcept
    {
        return { verdict.load (std::memory_order_relaxed), kept.load (std::memory_order_relaxed),
                 family.load (std::memory_order_relaxed), humDb.load (std::memory_order_relaxed),
                 settledAtMs.load (std::memory_order_relaxed), floorAfterDb.load (std::memory_order_relaxed) };
    }
};

} // namespace orbitamp::core
