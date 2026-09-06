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

// NOTHING about the chain lives here any more. Every link — the blocks, the two volume columns,
// the tuner — answers to its own two PARAMETERS, so what a player's rig is travels in the preset,
// the session, the registers and undo, and this file is left with what it always claimed to be:
// what the WINDOW shows, never what the sound is.

/** Which side of the curve the EQ console's row of hands sits on: under it (the default) or over
    it. Two blocks inside the console swap places and nothing else changes — the picture keeps its
    contents, the row keeps its arrangement, only the order changes.

    Over the curve puts every hand of the block together — the GAIN dial, the device's selectors,
    the tone knobs — and leaves the whole bottom of the block as one uninterrupted picture. Under
    it puts the result between the device and the hands that shape it. Both read; which one reads
    BETTER is a thing about eyes, so it is a switch. */
inline const juce::Identifier eqRowOnTop { "eq_row_on_top" };

/** How a link STANDING BY is shown: off (the default), its tile leaves the panel and the rest
    re-split, the way it always has; on, the tile keeps its place and goes dark.

    Purely the eye's business — the sound is the same either way, and the preset does not carry
    an opinion about it any more than it carries a colour. Removing gives the room to whoever is
    left; dimming keeps the panel still, so nothing under your hands moves while you A/B a block
    in and out. Neither is right, which is why it is a switch. */
inline const juce::Identifier dimStandby { "dim_standby" };

/** What an emptied row does to the WINDOW. On (the default): the window holds its height and
    whoever is left grows into the space — nothing on screen moves except the block that got
    taller. Off: the old way, the row collapses and the window shrinks with it.

    It matters more than it looks. A link's place in the rig is an automatable parameter, so with
    the window following the layout a host automation lane RESIZES THE PLUGIN WINDOW while you
    play. Holding the height takes that away entirely — but a lone block stretched over both rows
    may read as empty, which is why this is a switch and not a decision. */
inline const juce::Identifier growBlocks { "grow_blocks" };

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
