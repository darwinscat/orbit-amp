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

} // namespace orbitamp::prefs
