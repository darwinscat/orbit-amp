#include "ToyProcessors.h"

// The toy's OrbitAmp BUILT ALONE, as every amp is today: one plug-in, its ids from the target's own codes —
// the row that shows the patched wrappers, with no plug-in list, still give what the unpatched ones gave.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new multiwrap::Gain();
}
