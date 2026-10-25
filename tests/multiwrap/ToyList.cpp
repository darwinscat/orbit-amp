#include "ToyProcessors.h"
#include "ToyPluginList.h"

#include <cstring>

// The toy's ONE BINARY: each entry of its list gets its own processor, told apart by the entry the wrapper
// passes — nothing global says which plug-in is being made.
juce::AudioProcessor* createPluginFilter (const juce::PluginListEntry& plugin)
{
    if (std::strcmp (plugin.code, "Oamp") == 0)  return new multiwrap::Gain();
    if (std::strcmp (plugin.code, "Ctpt") == 0)  return new multiwrap::Tilt();
    if (std::strcmp (plugin.code, "Rdpw") == 0)  return new multiwrap::PhaseFlip();

    jassertfalse;   // an entry of the list with no processor here
    return nullptr;
}
