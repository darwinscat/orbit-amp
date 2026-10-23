// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../core/HumLearnTake.h"
#include "HumMenu.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <vector>

namespace orbitamp
{

/** THE HUM'S LEARN, RUNNING: its take (core/HumLearnTake.h) on a clock, fed the input's level for its
    picture. Its own, apart from the gate's LEARN (GateLearner.h): it teaches the hum stage, and only
    the hum stage. Picked with HUM OFF, it switches the hum on first, in the mode OFF bypasses, as
    picking a mode there does — a learn of a stage nobody hears would teach nothing anyone could judge.

    NOT a component and no menu, like the gate's: each surface starts it from the HUM part of its menus
    and shows it where it shows the gate's — a face on its glass, OrbitAmp on its overlay — with the
    words `words` gives. A surface that runs both makes sure only one runs at a time.

    Message thread only. */
class HumLearner final : private juce::Timer
{
public:
    /** `product`'s: the mode it learns for is the product's (prefs::hum). */
    HumLearner (const Product& product, core::HumLearnLink& link, const std::atomic<float>& keyDbSource,
                const std::atomic<float>& quietWidthHz)
        : ownProduct (product), take (link), keyDb (keyDbSource), quietHz (quietWidthHz) {}

    ~HumLearner() override { stopTimer(); }

    enum class Outcome { done, cancelled };

    struct Take
    {
        Outcome outcome;
        core::HumLearnLink::Answer hum {};  // `verdict` unanswered when the stage never answered
        int modeFamily = 0;                 // the mode in force at the end: 50 or 60, 0 for AUTO
    };

    std::function<void()>             onBegin;
    std::function<void (const Take&)> onDone;

    /** Stay quiet: the stage learns the hum anew. Picked again while running, it starts over. */
    void start()
    {
        if (take.isRunning())
            take.stop();
        if (prefs::hum (ownProduct) == prefs::Hum::off)
            prefs::setHum (ownProduct, prefs::humFlip (ownProduct));

        take.begin (quietHz.load (std::memory_order_relaxed));
        startTimerHz (core::HumLearnTake::ticksPerSecond);
        if (onBegin != nullptr)
            onBegin();
    }

    /** STOP: the stage goes back to the model it had. */
    void cancel()
    {
        if (! take.isRunning())
            return;
        take.stop();
        stopTimer();
        if (onDone != nullptr)
            onDone ({ Outcome::cancelled });
    }

    bool isLearning() const noexcept { return take.isRunning(); }
    const std::vector<float>& trace() const noexcept { return take.heard(); }
    int ticksPlanned() const noexcept { return take.ticksPlanned(); }

    /** The verdict, as the glass and the overlay put it, each in its own case: what the stage now takes
        off and how loud; what it heard when the mode in force is the other mains; that there was none;
        or that the quiet was not one. */
    static juce::String words (const Take& t)
    {
        if (t.outcome == Outcome::cancelled)
            return "CANCELLED";

        const auto hz = [] (int family) { return juce::String (family) + " Hz"; };
        switch (t.hum.verdict)
        {
            case core::HumLearnLink::learned:
                if (t.hum.kept)
                    return "HUM " + hz (t.hum.family) + " " + juce::String (juce::roundToInt (t.hum.humDb)) + " dB";
                if (t.modeFamily != 0 && t.modeFamily != t.hum.family)
                    return "HUM " + hz (t.hum.family) + " heard, set to " + hz (t.modeFamily);
                return "no hum";
            case core::HumLearnLink::nothingHeard: return "no hum";
            case core::HumLearnLink::disturbed:    return "DISTURBED";
            case core::HumLearnLink::tooShort:     return "TOO SHORT";
            default:                               return "CANCELLED";
        }
    }

private:
    const Product& ownProduct;
    void timerCallback() override
    {
        if (! take.hear (keyDb.load (std::memory_order_relaxed)))
            return;

        stopTimer();
        Take done { Outcome::done, take.answer(),
                    prefs::hum (ownProduct) == prefs::Hum::hz50 ? 50 : prefs::hum (ownProduct) == prefs::Hum::hz60 ? 60 : 0 };
        if (done.hum.verdict == core::HumLearnLink::unanswered)
            done.outcome = Outcome::cancelled;
        if (onDone != nullptr)
            onDone (done);
    }

    core::HumLearnTake take;
    const std::atomic<float>& keyDb;
    const std::atomic<float>& quietHz;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumLearner)
};

} // namespace orbitamp
