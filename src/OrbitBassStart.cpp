// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "core/CapturedBlock.h"
#include "device/BassCabinets.h"
#include "device/DeviceLibrary.h"

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
        its own stage, not the cabinet's (params::bassHpf: the HPF at 5, a low B whole; the LPF at FINGERS), so the
        cabinet's own stay off and its IR plays as it was shot. The pack it opens on is the first of its list, which is
        its only one (CMakeLists.txt: ORBITBASS_SET). */
    void bassOpening (const Product&, params::Opening&) {}

    /** WHETHER THE PACK A FRESH ORBITBASS OPENS ON — the first of its list — HAS SWITCHES OF ITS TONE, the SGT-DI's
        ULTRA LO and ULTRA HI: tones of positions that carry bands (core::CapturedBlock::switchCarriesBands), no sweep.
        Its face then stands them where the cuts' switches were, STR 4 / 5 and FING / SLAP one line each, as the walker
        has them (EqSection::layOutRow) — and the cuts always in. */
    bool opensOnToneSwitches (const Product& product)
    {
        const auto packs = device::DeviceLibrary::scan (product.name, device::DeviceLibrary::ampSlot);
        if (packs.isEmpty())
            return false;
        for (const auto& stage : packs.getReference (0).rig.chain)
        {
            if (stage.kind != namz::rig::StageKind::Nam)
                continue;
            for (const auto& tone : stage.tone)
                if (tone.sweep <= 0 && core::CapturedBlock::switchCarriesBands (tone))
                    return true;
            break;
        }
        return false;
    }

    /** THE CABINET A FRESH ONE OPENS ON, and INIT: the bass's dozen's first, 1: 8x10 STACK (the owner, 06.11), drawn —
        not no cabinet at all, whatever the player keeps in `Bass IRs`. A session or a preset keeps what it saved. */
    juce::String freshCabinet()
    {
        const auto& dozen = device::BassCabinets::dozen();
        return dozen.empty() ? juce::String() : AmpProcessor::cabSynthFrom (dozen.front().state);
    }

    /** Its own parameters, after the engine's: COMP, BLEND, DI/CAB and its cuts — both cuts IN where its pack has
        switches of its tone (the owner, 03.11: the cuts' switches give their place to the pack's, the cuts always in),
        and COMP's MIX at three quarters, as the walker holds it (the owner, 10.11).
        Declared so, a fresh one plays them in with no window open, INIT and a host's reset bring them back in, and the
        face coming up finds them so and moves nothing. */
    void bassParameters (const Product& product, juce::AudioProcessorValueTreeState::ParameterLayout& layout)
    {
        params::Opening opening;
        opening.compAmount = 0.75f;
        opening.bassLpfOn = opensOnToneSwitches (product);   // the HPF opens in on every bass
        params::addBassParameters (layout, opening);
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
        p.freshCabinet  = freshCabinet;
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
