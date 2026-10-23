// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace orbitamp
{

namespace
{
    /** OrbitAmp's first start in a process: the player's IRs it kept in its own folder are gathered into
        the family's (gatherFormerIrs). A gate that must not reach the machine's real folders plays
        OrbitAmp without it. */
    void firstStart (const Product& orbitAmp)
    {
        gatherFormerIrs (orbitAmp);
    }

    juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
    {
        return new AmpEditor (p);
    }
}

/** ORBITAMP (Product.h): the player of the whole library, in its own face (PluginEditor.h) — and the
    engine as it stands: no parameter of its own, no half of its own. Its tuner listens always. */
const Product& products::orbitAmp()
{
    static const Product orbitAmp = []
    {
        Product p { "OrbitAmp", makeEditor, firstStart };
        p.tunerSwitch = false;
        return p;
    }();
    return orbitAmp;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in OrbitAmp's own plug-in alone (CMakeLists.txt: add_amp): a host that opens it
// gets a processor of OrbitAmp. A gate, or a binary that plays several amps, makes its own.
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::orbitAmp());
}
#endif
