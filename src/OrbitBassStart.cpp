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

    /** WHERE A FRESH ORBITBASS STANDS, beyond the engine's: the engine's own (params::Opening). Its DELAY and REVERB
        are out of the strip (params::delayPresent, reverbPresent) — not in its sound or on its face until a player adds
        them — and their switches ON, so one added plays at once. The gate stays off, as every amp's does. Its cuts are
        its own stage, not the cabinet's (params::bassHpf: the HPF at 5, a low B whole; the LPF off), so the cabinet's
        own stay off and its IR plays as it was shot. The pack it opens on is the first of its list, which is its only
        one for now (CMakeLists.txt: ORBITBASS_SET). */
    void bassOpening (const Product&, params::Opening&) {}

    /** Its own parameters, after the engine's: COMP, BLEND, DI/CAB and its cuts. */
    void bassParameters (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        params::addBassParameters (layout);
    }
}

/** ORBITBASS (Product.h): the bass's workstation — OrbitAmp's engine, frame and blocks over the bass's
    chain (Chain::bass): mono to its CHORUS; COMP, the AMP's BLEND, DI/CAB and its own cuts, and the
    places of the blocks still to come. Its
    tuner listens always, as OrbitAmp's does, and down to a drop-A's A0. No first start: it has no
    player's files of before to gather. */
const Product& products::orbitBass()
{
    static const Product orbitBass = []
    {
        Product p { "OrbitBass", makeBassEditor };
        p.opening       = bassOpening;
        p.addParameters = bassParameters;
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
