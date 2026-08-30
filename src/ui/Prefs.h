#pragma once

#include "../device/DeviceLibrary.h"

#include <juce_data_structures/juce_data_structures.h>

namespace orbitamp::prefs
{

/** The app-wide switches — what the WINDOW shows, never what the sound is. A preset carries none of
    these and the compare registers do not flip them: they are about this machine and this player,
    so they live in one small file beside the Devices folder, shared by every instance. */
inline juce::File file()
{
    return device::DeviceLibrary::appDataRoot().getChildFile ("settings.xml");
}

inline bool getBool (const juce::String& key, bool fallback)
{
    juce::PropertiesFile props (file(), {});
    return props.getBoolValue (key, fallback);
}

inline void setBool (const juce::String& key, bool value)
{
    file().getParentDirectory().createDirectory();
    juce::PropertiesFile props (file(), {});
    props.setValue (key, value);
    props.saveIfNeeded();
}

/** The two TEMPORARY strips under the footer — the audition player and the glyph review — off
    unless this player asked for them. */
inline constexpr const char* showDemo   = "show_demo_player";
inline constexpr const char* showGlyphs = "show_device_glyphs";

} // namespace orbitamp::prefs
