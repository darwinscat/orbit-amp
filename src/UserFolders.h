// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

namespace orbitamp
{

/** THE PLAYER'S OWN FOLDERS, as JUCE finds them — read here by everything that keeps or finds the player's files: the
    family's folder (DeviceLibrary::familyRoot), the presets, the settings, the GPU's log, the cab builders' saves.

    A gate runs in a home of its own (tools/run-gates.sh). On a Mac and on Linux JUCE finds these folders through HOME,
    which the runner points there. Windows finds AppData and Documents without asking the environment, so a program
    built as a gate (ORBITAMP_GATE_OWN_HOME, CMakeLists.txt: THE GATES) reads them on Windows from the home the runner
    names, ORBITAMP_GATE_HOME, laid out as a Windows profile is: Documents, AppData/Roaming. An amp is never built so. */
inline juce::File userFolder (juce::File::SpecialLocationType type)
{
   #if defined (ORBITAMP_GATE_OWN_HOME) && JUCE_WINDOWS
    if (const auto home = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_GATE_HOME", {}); home.isNotEmpty())
    {
        switch (type)
        {
            case juce::File::userHomeDirectory:            return juce::File (home);
            case juce::File::userDocumentsDirectory:       return juce::File (home).getChildFile ("Documents");
            case juce::File::userApplicationDataDirectory: return juce::File (home).getChildFile ("AppData").getChildFile ("Roaming");
            default:                                       break;
        }
    }
   #endif

    return juce::File::getSpecialLocation (type);
}

/** Where an app keeps the player's data: the user's application data, which on the Mac JUCE gives as bare ~/Library —
    the step down into Application Support is made here, or the folder lands one level too high and nothing a player
    drops in is ever found. */
inline juce::File userAppData()
{
    auto dir = userFolder (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif

    return dir;
}

} // namespace orbitamp
