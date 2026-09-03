// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/appkit/SettingsStore.h>

#include <juce_data_structures/juce_data_structures.h>

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

/** The update badge's own store — `update.settings`, beside the settings.json above.

    The switches above live in the family's JSON, written by appkit's SettingsStore; appkit's
    UpdateChecker persists the last seen release and the auto-check consent through a
    juce::PropertiesFile instead, so it gets its own small file in the SAME folder rather than a
    second copy of anything. (The family fix is teaching UpdateChecker the SettingsStore; until a
    second product wants it, this is one file.)

    Ownership follows OrbitCab's: the PROCESSOR holds it through a juce::SharedResourcePointer, so
    every instance in a host process shares one object — no two racing on a save, and a badge stored
    by one instance is visible to the rest at once. The InterProcessLock serialises writes between
    host processes. Never a function-local static: that would outlive the MessageManager and be
    destroyed on library unload, which is not a place to be touching timers and files.

    Message thread only — a PropertiesFile is not thread-safe, and the checker touches it nowhere
    else (see UpdateChecker::Config::settings). */
class UpdateStore
{
public:
    UpdateStore()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "update";
        o.folderName          = "Darwin's Cat" + juce::String (juce::File::getSeparatorString()) + "OrbitAmp";
        o.filenameSuffix      = "settings";
        o.osxLibrarySubFolder = "Application Support";
        o.processLock         = &ipLock;
        props.setStorageParameters (o);
    }

    juce::PropertiesFile* file() { return props.getUserSettings(); }

private:
    juce::InterProcessLock    ipLock { "OrbitAmp.update.settings" };
    juce::ApplicationProperties props;
};

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

// The blocks' presence needs no prefs any more: a block's `*_on` PARAMETER is its presence —
// the strip writes it, the panel follows it, and the save, the history, and the registers all
// carry it. Only the instruments below keep machine-level switches.

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
