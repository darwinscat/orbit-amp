// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Product.h"

#include <felitronics/appkit/SettingsStore.h>

#include <juce_data_structures/juce_data_structures.h>

namespace orbitamp::prefs
{

/** The app-wide switches — what the WINDOW shows, never what the sound is. A preset carries none of
    these and the compare registers do not flip them: they are about this machine and this player,
    so they live in the product's settings store — one small JSON beside its Devices folder, shared
    by every instance of the product, locked against two of them writing at once. One for each product
    in a process (PerProduct): two amps in one host each keep their own. */
inline const felitronics::appkit::SettingsStore& store (const Product& p)
{
    static PerProduct<const felitronics::appkit::SettingsStore> stores;
    return stores.of (p.name, [&p] { return felitronics::appkit::SettingsStore ("Darwin's Cat", p.name); });
}

/** The update badge's own store — `update.settings`, beside the settings.json above.

    The switches above live in the family's JSON, written by appkit's SettingsStore; appkit's
    UpdateChecker persists the last seen release and the auto-check consent through a
    juce::PropertiesFile instead, so it gets its own small file in the SAME folder rather than a
    second copy of anything. (The family fix is teaching UpdateChecker the SettingsStore; until a
    second product wants it, this is one file.)

    Ownership follows OrbitCab's: the PROCESSOR holds it shared (of), so every instance of the product
    in a host process shares one object — no two racing on a save, and a badge stored by one instance
    is visible to the rest at once — made by the first and gone with the last, as a
    juce::SharedResourcePointer would be, but one for each product: two amps in one process each keep
    their own. The InterProcessLock serialises writes between host processes. Never a function-local
    static: that would outlive the MessageManager and be destroyed on library unload, which is not a
    place to be touching timers and files.

    Message thread only — a PropertiesFile is not thread-safe, and the checker touches it nowhere
    else (see UpdateChecker::Config::settings). */
class UpdateStore
{
public:
    explicit UpdateStore (const Product& p)
        : ipLock (juce::String (p.name) + ".update.settings")
    {
        juce::PropertiesFile::Options o;
        // The PRODUCT's folder, not the family's shared one: two faces of one processor are released
        // apart, and a newer OrbitAmp is no news for a cockpit. For OrbitAmp this is the folder it
        // has always been.
        o.applicationName     = "update";
        o.folderName          = "Darwin's Cat" + juce::String (juce::File::getSeparatorString()) + p.name;
        o.filenameSuffix      = "settings";
        o.osxLibrarySubFolder = "Application Support";
        o.processLock         = &ipLock;
        props.setStorageParameters (o);
    }

    juce::PropertiesFile* file() { return props.getUserSettings(); }

    /** The product's own, shared with every other processor of it that is there: the first makes it,
        the last to let go of it takes it away. */
    static std::shared_ptr<UpdateStore> of (const Product& p)
    {
        static std::mutex mutex;
        static std::map<std::string, std::weak_ptr<UpdateStore>> stores;

        const std::scoped_lock lock (mutex);
        auto& held = stores[p.name];
        auto store = held.lock();
        if (store == nullptr)
        {
            store = std::make_shared<UpdateStore> (p);
            held = store;
        }
        return store;
    }

private:
    juce::InterProcessLock    ipLock;
    juce::ApplicationProperties props;
};

inline bool getBool (const Product& p, const juce::Identifier& key, bool fallback)
{
    return (bool) store (p).get (key, fallback);
}

inline void setBool (const Product& p, const juce::Identifier& key, bool value)
{
    store (p).set (key, value);
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

/** The analysers — the consoles' ground, the cabinet's pair, the TONE tile's columns. One switch
    for all of them, read where a spectrum is about to be drawn: cached after the first look,
    because a paint routine must not open a file. Each cache here is the product's own (PerProduct). */
inline const juce::Identifier showSpectra { "show_spectra" };

inline std::atomic<bool>& spectraCache (const Product& p)
{
    static PerProduct<std::atomic<bool>> cached;
    return cached.of (p.name, [&p] { return getBool (p, showSpectra, true); });
}

inline bool spectraShown (const Product& p)               { return spectraCache (p).load (std::memory_order_relaxed); }
inline void setSpectraShown (const Product& p, bool shown) { spectraCache (p).store (shown, std::memory_order_relaxed); setBool (p, showSpectra, shown); }

/** The TUNER's LEVEL FLOOR — about this room and this guitar, not about a sound, so it lives on the
    machine like the switches above and a preset does not carry it. Read by the tuner thirty times a
    second, so cached like the spectra: a tick must not open a file.

    The quietest window that still counts as a note, in dBFS. -60 by default — low enough for a quiet
    guitar's decay, high enough that a pickup's hum and hiss with nobody playing do not read as a
    note. */
inline const juce::Identifier tunerFloorDb { "tuner_floor_db" };

inline constexpr int tunerFloors[]      = { -80, -70, -60, -50 };
inline constexpr int tunerFloorDefault  = -60;

/** A stored floor that is not one of the offered four — edited by hand, or from a build that
    offered others — lands on the nearest that is. */
inline int snapTunerFloor (int db)
{
    int best = tunerFloors[0];
    for (const int f : tunerFloors)
        if (std::abs (f - db) < std::abs (best - db))
            best = f;
    return best;
}

inline std::atomic<int>& tunerFloorCache (const Product& p)
{
    static PerProduct<std::atomic<int>> cached;
    return cached.of (p.name, [&p] { return snapTunerFloor ((int) store (p).get (tunerFloorDb, tunerFloorDefault)); });
}

inline int  tunerFloor (const Product& p)              { return tunerFloorCache (p).load (std::memory_order_relaxed); }
inline void setTunerFloor (const Product& p, int db)   { db = snapTunerFloor (db); tunerFloorCache (p).store (db, std::memory_order_relaxed); store (p).set (tunerFloorDb, db); }

/** The TUNER's REFERENCE — the pitch A4 is tuned to, in Hz. 440 wherever a guitar plays alone; a piano
    or an orchestra that will not retune, and a record that has drifted, are why a tuner lets it move.
    About the band and the room, like the floor above, so it lives on the machine with it and is read
    the same way. */
inline const juce::Identifier tunerReferenceHz { "tuner_reference_hz" };

inline constexpr int tunerReferences[]     = { 432, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445 };
inline constexpr int tunerReferenceDefault = 440;

inline int snapTunerReference (int hz)
{
    int best = tunerReferenceDefault;
    for (const int r : tunerReferences)
        if (std::abs (r - hz) < std::abs (best - hz))
            best = r;
    return best;
}

inline std::atomic<int>& tunerReferenceCache (const Product& p)
{
    static PerProduct<std::atomic<int>> cached;
    return cached.of (p.name, [&p] { return snapTunerReference ((int) store (p).get (tunerReferenceHz, tunerReferenceDefault)); });
}

inline int  tunerReference (const Product& p)              { return tunerReferenceCache (p).load (std::memory_order_relaxed); }
inline void setTunerReference (const Product& p, int hz)   { hz = snapTunerReference (hz); tunerReferenceCache (p).store (hz, std::memory_order_relaxed); store (p).set (tunerReferenceHz, hz); }

/** THE HUM AT THE INPUT — which mains the hum on this machine's input is made of: none to take off,
    50 Hz, 60 Hz, or AUTO, which listens and decides. It is about the wall socket, the cable and the
    pickups in this room, not about a sound, so it lives on the machine like the tuner's floor and a
    preset never carries it: a preset that switched it off would put the hum back on every machine it
    was loaded on. OFF on a machine that never chose: the stage still runs, in AUTO, bypassed, so the
    first On — the H key, a menu's switch, its LEARN — takes the hum off with what it already heard.
    The processor's audio thread reads it every block, so it is cached like the floor; the processor
    touches the cache in its constructor, so the first read, which opens the file, is never the audio
    thread's. Stored as a word, so the file says what it means; a word this build does not know reads
    as the default. The order is felitronics::dehum::Mains's, and the processor holds it to that. */
inline const juce::Identifier humMains { "hum_mains" };

enum class Hum { off, hz50, hz60, automatic };
inline constexpr Hum humDefault   = Hum::off;
inline constexpr Hum humFirstOn   = Hum::automatic;    // what OFF bypasses before anything else has been on

inline const char* humWord (Hum h)
{
    switch (h)
    {
        case Hum::off:       return "off";
        case Hum::hz50:      return "50";
        case Hum::hz60:      return "60";
        case Hum::automatic: break;
    }
    return "auto";
}

inline Hum humFromWord (const juce::String& word)
{
    for (const auto h : { Hum::off, Hum::hz50, Hum::hz60 })
        if (word == humWord (h))
            return h;
    return humDefault;
}

inline std::atomic<int>& humCache (const Product& p)
{
    static PerProduct<std::atomic<int>> cached;
    return cached.of (p.name, [&p] { return (int) humFromWord (store (p).get (humMains, humWord (humDefault)).toString()); });
}

/** THE LAST MODE THAT WAS NOT OFF — what OFF bypasses. OFF does not stop the stage: it goes on
    running in this mode, learning as it did, and only its output is set aside, so the way back is the
    hum taken off at once by what was already learned (the processor's removeHum). Kept on the machine
    beside the mode; AUTO when OFF is all this machine has ever chosen. */
inline const juce::Identifier humLastOnKey { "hum_last_on" };

inline std::atomic<int>& humLastOnCache (const Product& p)
{
    static PerProduct<std::atomic<int>> cached;
    return cached.of (p.name, [&p] {
        const auto h = humFromWord (store (p).get (humLastOnKey, humWord (humFirstOn)).toString());
        return (int) (h == Hum::off ? humFirstOn : h);
    });
}

inline Hum  hum (const Product& p)          { return (Hum) humCache (p).load (std::memory_order_relaxed); }
inline Hum  humLastOn (const Product& p)    { return (Hum) humLastOnCache (p).load (std::memory_order_relaxed); }
inline void setHum (const Product& p, Hum h)
{
    humCache (p).store ((int) h, std::memory_order_relaxed);
    store (p).set (humMains, humWord (h));
    if (h != Hum::off)
    {
        humLastOnCache (p).store ((int) h, std::memory_order_relaxed);
        store (p).set (humLastOnKey, humWord (h));
    }
}

/** The four the machine may hold. A menu offers them as a switch and three modes (HumMenu.h). */
inline constexpr Hum humChoices[] = { Hum::off, Hum::hz50, Hum::hz60, Hum::automatic };

/** A choice's line in a menu. AUTO's says where it stands while the stage runs it — `heard` is
    AmpProcessor::humHeard: the family it decided (50 or 60), 0 while it is still listening, -1 when
    the stage runs another mode. */
inline juce::String humLabel (Hum h, int heard)
{
    switch (h)
    {
        case Hum::off:       return "Off";
        case Hum::hz50:      return "50 Hz";
        case Hum::hz60:      return "60 Hz";
        case Hum::automatic: break;
    }
    const auto dash = juce::String::fromUTF8 ("Auto \xe2\x80\x94 ");
    return heard == 50 || heard == 60 ? dash + juce::String (heard) + " Hz"
         : heard == 0                 ? dash + "listening"
                                      : juce::String ("Auto");
}

/** THE QUICK A/B: OFF from any mode, and from OFF the mode it bypasses (humLastOn). */
inline Hum humFlip (const Product& p) { return hum (p) == Hum::off ? humLastOn (p) : Hum::off; }


/** What a face says for a moment after the flip — AUTO's state with it (`heard` as for humLabel). */
inline juce::String humTag (Hum h, int heard)
{
    const auto on = juce::String::fromUTF8 ("HUM ON \xe2\x80\x94 ");
    if (h == Hum::off)       return "HUM OFF";
    if (h != Hum::automatic) return on + humLabel (h, -1);
    return on + "Auto" + (heard == 50 || heard == 60 ? ", " + juce::String (heard) + " Hz"
                        : heard == 0                 ? juce::String (", listening")
                                                     : juce::String());
}

/** THE PLAYER'S IRS OF BEFORE ARE GATHERED: set once this product has copied the IRs it kept in its own
    folder into the family's (device::IrLibrary::gather), so it does so once on this machine — the
    plugin and the standalone alike, since they share this store. Not a switch anyone flips; the store
    is where the product remembers what it has done here. */
inline const juce::Identifier irsGathered { "irs_gathered" };

} // namespace orbitamp::prefs
