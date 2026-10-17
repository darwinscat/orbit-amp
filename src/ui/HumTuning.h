// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Prefs.h"

#include <array>
#include <cmath>

namespace orbitamp::prefs::humtune
{

/** HOW HARD THE HUM STAGE WORKS — TEMPORARY, for tuning by ear. Three of the stage's numbers, put in
    the HUM part of every menu while the right ones are found by listening, and taken out again once
    they are: the notch's width while the strings sound, its width while the input is quiet, and the
    highest harmonic it takes off. The machine's, like the mode, and like the mode read by the audio
    thread from a cached atomic (the processor's removeHum hands them to the stage when they change).
    The widths default to the stage's own; the reach defaults to 2 kHz, and at 1.2 kHz it is the stage
    as it ran before there was anything to tune, exactly — so an A/B against an earlier build means
    what it seems to. */
inline constexpr float playWidths[]  = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };          // Hz, while playing
inline constexpr float quietWidths[] = { 1.0f, 3.0f, 6.0f };                       // Hz, while quiet
inline constexpr float upTos[]       = { 1200.0f, 2000.0f, 3200.0f, 4800.0f, 6400.0f };   // Hz

inline constexpr float playDefault  = 0.5f;
inline constexpr float quietDefault = 3.0f;
inline constexpr float upToDefault  = 2000.0f;
inline constexpr float upToOwn      = 1200.0f;     // the stage's own 24 harmonics, at 50 Hz

inline const juce::Identifier playKey  { "hum_width_playing" };
inline const juce::Identifier quietKey { "hum_width_quiet" };
inline const juce::Identifier upToKey  { "hum_up_to" };

/** The offered value nearest `v` — a stored one this build does not offer lands on it. */
template <size_t N>
float snap (const float (&offered)[N], float v)
{
    float best = offered[0];
    for (const float o : offered)
        if (std::abs (o - v) < std::abs (best - v))
            best = o;
    return best;
}

inline std::atomic<float>& playCache()
{
    static std::atomic<float> cached { snap (playWidths, (float) (double) store().get (playKey, playDefault)) };
    return cached;
}

inline std::atomic<float>& quietCache()
{
    static std::atomic<float> cached { snap (quietWidths, (float) (double) store().get (quietKey, quietDefault)) };
    return cached;
}

inline std::atomic<float>& upToCache()
{
    static std::atomic<float> cached { snap (upTos, (float) (double) store().get (upToKey, upToDefault)) };
    return cached;
}

inline void setPlay (float v)  { v = snap (playWidths, v);  playCache().store (v, std::memory_order_relaxed);  store().set (playKey, v); }
inline void setQuiet (float v) { v = snap (quietWidths, v); quietCache().store (v, std::memory_order_relaxed); store().set (quietKey, v); }
inline void setUpTo (float v)  { v = snap (upTos, v);       upToCache().store (v, std::memory_order_relaxed);  store().set (upToKey, v); }

/** A number as a person writes it: no trailing zeros, no bare point. */
inline juce::String plain (float v)
{
    auto s = juce::String (v, 2);
    while (s.endsWithChar ('0'))
        s = s.dropLastCharacters (1);
    return s.endsWithChar ('.') ? s.dropLastCharacters (1) : s;
}

inline juce::String hz (float v)  { return plain (v) + " Hz"; }
inline juce::String kHz (float v) { return plain (v / 1000.0f) + " kHz"; }

/** One of the three, as a menu offers it: its name, the values, the one in force, how a value reads,
    and how it is chosen. */
struct Knob
{
    const char*  name;
    const float* values;
    int          count;
    float        now;
    juce::String (*say) (float);
    void         (*choose) (float);
};

inline std::array<Knob, 3> knobs()
{
    return { { { "Width playing", playWidths,  (int) std::size (playWidths),  playCache().load(),  hz,  setPlay },
               { "Width quiet",   quietWidths, (int) std::size (quietWidths), quietCache().load(), hz,  setQuiet },
               { "Up to",         upTos,       (int) std::size (upTos),       upToCache().load(),  kHz, setUpTo } } };
}

} // namespace orbitamp::prefs::humtune
