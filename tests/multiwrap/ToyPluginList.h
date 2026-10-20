#pragma once

// THE TOY BINARY'S PLUG-IN LIST — JUCE_PLUGIN_LIST_HEADER names this header (CMakeLists.txt), and the patched
// wrappers read it (juce_PluginList.h): the three amps under their real names, their real four-character codes
// beside the family's manufacturer code Dcat, and their real CLAP ids, so the class ids the toy's one VST3
// registers can be held against what each amp's own bundle shipped with. Plain data, nothing of JUCE: the VST3
// manifest helper compiles it too.
#include <juce_audio_plugin_client/juce_PluginList.h>

inline constexpr const char* multiwrapClapFeatures[] { "audio-effect", "guitar", "amplifier", nullptr };

inline constexpr juce::PluginListEntry jucePluginList[]
{
    { "OrbitAmp",  "Oamp", "com.darwinscat.orbitamp",  "Fx|Distortion", multiwrapClapFeatures },
    { "CatpitAmp", "Ctpt", "com.darwinscat.catpitamp", "Fx|Distortion", multiwrapClapFeatures },
    { "RedPawAmp", "Rdpw", "com.darwinscat.redpawamp", "Fx|Distortion", multiwrapClapFeatures },
};
