// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace orbitamp
{

namespace
{
    juce::AudioProcessorEditor* makeBassEditor (AmpProcessor& p)
    {
        return new AmpEditor (p);
    }

    /** WHERE A FRESH ORBITBASS STANDS, beyond the engine's: its chain's own (params::Opening) — no delay
        and no reverb, the cabinet's HPF on at 25 Hz, under a 5-string's low B. The gate stays off, as
        every amp's does, and so out of the strip until it is switched on; the LPF off, as the family has
        it. The pack it opens on is the first of its list, which is its only one for now (CMakeLists.txt:
        ORBITBASS_SET). */
    void bassOpening (const Product&, params::Opening& o)
    {
        o.delayOn  = false;
        o.reverbOn = false;
        o.cabHpfOn = true;
        o.cabHpfHz = 25.0f;
    }
}

/** ORBITBASS (Product.h): the bass's workstation — OrbitAmp's engine, frame and blocks over the bass's
    chain (Chain::bass): mono, no delay, no reverb, and the places of the blocks still to come. Its
    tuner listens always, as OrbitAmp's does, and down to a drop-A's A0. No first start: it has no
    player's files of before to gather. */
const Product& products::orbitBass()
{
    static const Product orbitBass = []
    {
        Product p { "OrbitBass", makeBassEditor };
        p.opening       = bassOpening;
        p.tunerSwitch   = false;
        p.chain         = Chain::bass;
        p.tunerLowestHz = core::PitchTracker::bassLowestHz;
        return p;
    }();
    return orbitBass;
}

} // namespace orbitamp

// THE PLUG-IN'S ENTRY, in OrbitBass's own plug-in alone (CMakeLists.txt: add_amp).
#if ORBITAMP_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor (orbitamp::products::orbitBass());
}
#endif
