// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitStandalone.h"

#if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define CATPIT_STANDS_ALONE 1
#else
 #define CATPIT_STANDS_ALONE 0
#endif

namespace orbitamp::catpit::standalone
{

juce::AudioDeviceManager* devices()
{
   #if CATPIT_STANDS_ALONE
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        return &holder->deviceManager;
   #endif
    return nullptr;
}

void rememberDevices()
{
   #if CATPIT_STANDS_ALONE
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        holder->saveAudioDeviceState();
   #endif
}

void rememberWindowPosition ([[maybe_unused]] juce::Point<int> topLeft)
{
   #if CATPIT_STANDS_ALONE
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* settings = holder->settings.get())
        {
            settings->setValue ("windowX", topLeft.x);     // the names the standalone's window reads them by
            settings->setValue ("windowY", topLeft.y);
        }
   #endif
}

bool firstTimeHere ([[maybe_unused]] const char* key)
{
   #if CATPIT_STANDS_ALONE
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* settings = holder->settings.get())
            if (! settings->containsKey (key))
            {
                settings->setValue (key, true);
                return true;
            }
   #endif
    return false;
}

} // namespace orbitamp::catpit::standalone
