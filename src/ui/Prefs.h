// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/appkit/SettingsStore.h>

namespace orbitamp::prefs
{

/** The app-wide switches — what the WINDOW shows, never what the sound is. A preset carries none of
    these and the compare registers do not flip them: they are about this machine and this player,
    so they live in the family's settings store — one small JSON beside the Devices folder, shared
    by every instance, locked against two of them writing at once. */
inline const felitronics::appkit::SettingsStore& store()
{
    static const felitronics::appkit::SettingsStore s ("Darwin's Cat", "OrbitAmp");
    return s;
}

inline bool getBool (const juce::Identifier& key, bool fallback)
{
    return (bool) store().get (key, fallback);
}

inline void setBool (const juce::Identifier& key, bool value)
{
    store().set (key, value);
}

/** The two TEMPORARY strips under the footer — the audition player and the glyph review — off
    unless this player asked for them. */
inline const juce::Identifier showDemo   { "show_demo_player" };
inline const juce::Identifier showGlyphs { "show_device_glyphs" };

/** Which blocks stand on the panel — the LAYOUT popup's six switches. A hidden block is GONE,
    not dimmed: its row re-splits evenly among whoever remains and its power parameter is put out
    with it, because a hidden block must not colour the sound. The core four ship shown; the
    power amp (no pack ships yet) and the delay (an echo is a choice, not a starting point)
    ship hidden. */
inline const juce::Identifier showBoost  { "show_boost" };
inline const juce::Identifier showPreamp { "show_preamp" };
inline const juce::Identifier showDelay  { "show_delay" };
inline const juce::Identifier showReverb { "show_reverb" };
inline const juce::Identifier showPower  { "show_power_amp" };
inline const juce::Identifier showCab    { "show_cab" };

/** The side columns — the IN rail with the gate's hand, the OUT rail with the master's. The
    strip's end caps toggle them. Hiding one is about the INSTRUMENTS, not the sound: the gate
    and the limiter keep working as set (a safety that dies with its meter is no safety), but
    the column's TRIM returns to unity — a hidden hand must not keep pressing. */
inline const juce::Identifier showInCol  { "show_in_column" };
inline const juce::Identifier showOutCol { "show_out_column" };

/** The tuner's needle — the whole row under the panel now: hidden, the row collapses and the
    window follows. The guards' lights and menus live in the strip's arrows. */
inline const juce::Identifier showTuner { "show_tuner" };

/** The analysers — the consoles' ground, the cabinet's pair, the TONE tile's columns. One switch
    for all of them, read where a spectrum is about to be drawn: cached after the first look,
    because a paint routine must not open a file. */
inline const juce::Identifier showSpectra { "show_spectra" };

inline std::atomic<bool>& spectraCache()
{
    static std::atomic<bool> cached { getBool (showSpectra, true) };
    return cached;
}

inline bool spectraShown()               { return spectraCache().load (std::memory_order_relaxed); }
inline void setSpectraShown (bool shown) { spectraCache().store (shown, std::memory_order_relaxed); setBool (showSpectra, shown); }

} // namespace orbitamp::prefs
