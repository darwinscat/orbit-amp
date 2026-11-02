// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Parameters.h"
#include "PerProduct.h"
#include "core/PitchTracker.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

/** WHICH AMP A PROCESSOR IS. One processor wears several faces, each an amp of its own — its name, its
    settings, its presets, its face, its channels — and all of them released together, as Darwin's Cat
    Amps, under the family's one version and one tag, vX.Y.Z (ORBITAMP_VERSION, the family's, from
    CMakeLists.txt's project()).

    THE PRODUCT IS A VALUE, NOT A BUILD. A processor is made with its Product and keeps it; everything
    that is one amp's and not another's reads it there, handed down explicitly — the name its settings,
    its presets, its update check and its pictures go by, the list it reads in the factory folder, its
    face. There is no "current product" anywhere in a process: a host may run OrbitAmp and CatpitAmp side
    by side, out of one binary, and each processor is what it was made as.

    Each amp says what it is in a file of its own — OrbitAmp in OrbitAmpStart.cpp, OrbitBass in
    OrbitBassStart.cpp, the cockpit in catpit/CatpitLineup.cpp, each rover in its skin's file — and no two
    of them define anything under the same name, so every amp's sources link into one binary. Each amp's plug-in makes a processor of its
    own product (createPluginFilter, beside its Product, compiled into that plug-in alone); a gate makes
    whichever it plays.

    Every amp asks for the repository's LATEST release — the family's vX.Y.Z — as OrbitAmp always has:
    no tag prefix, since every release of the repository is every amp's. */
namespace orbitamp
{

class AmpProcessor;
struct ProductEngine;

namespace redpaw { struct Skin; }
namespace params { struct Opening; }

/** WHAT A PRODUCT'S CHAIN IS. The guitar's — gate, amp, delay, reverb, cabinet, limiter — is every amp's but
    OrbitBass's. The bass's is one signal from the input to the output and holds neither the delay nor the
    reverb (params::bassChainHolds); its blocks still to come — COMP, OCTAVE, FILTER, CHORUS, the DI beside
    the cabinet — have their places named in the chain (AmpProcessor::processChunk). */
enum class Chain { guitar, bass };

struct Product
{
    /** The name it goes by: its plug-in's, its settings' and presets' folder, its update check, its
        own list in the factory folder (device/FactorySet.h) and its pictures there, Faces/<name>. */
    const char* name;

    /** The face it opens. */
    juce::AudioProcessorEditor* (*makeEditor) (AmpProcessor&) = nullptr;

    /** What it does ONCE IN A PROCESS, before its first processor builds anything of its own — today,
        gather the player's IRs of before (gatherFormerIrs). A gate that must not reach the machine's real
        folders plays a product that has none. What the amps play out of the box is laid out by the
        family's installer, not by an amp (device/FactorySet.h). */
    void (*firstStart) (const Product&) = nullptr;

    /** Its own parameters, after the engine's, and its own half of the engine (ProductEngine). OrbitAmp
        adds no parameter and has no such half: it has neither. */
    void (*addParameters) (const Product&, juce::AudioProcessorValueTreeState::ParameterLayout&) = nullptr;
    std::unique_ptr<ProductEngine> (*makeEngine) (AmpProcessor&) = nullptr;

    /** The skin it wears, where it is a rover (redpaw/RedPawSkin.h): its channels, its picture, its world. */
    const redpaw::Skin* skin = nullptr;

    /** THE NOTE ITS STATES CARRIED BESIDE THE INPUT TRIM WHILE THE TRIM HELD THE PACK'S LEVEL. Up to 0.8.0
        the cockpit's and the rovers' face folded a machine setting, the lever's zero, into the trim itself
        (-9 dB unless the player chose another), and wrote down under this name which it had folded in
        (catpit_input_zero, rover_input_zero). That level is the pack's own now (`chain[].input_db`) and the
        trim is unity at the lever's middle, so a state that still carries this note is converted as it
        lands (AmpProcessor::takeFormerTrims). None for OrbitAmp, whose trim was never anything but its
        fader. */
    const char* formerInputZeroId = nullptr;

    /** WHERE A FRESH INSTANCE OF IT STANDS, where that is not where the engine's does: the cockpit and
        the rovers open on their first channel — its pack, its room, its delay — and say so here, so that
        each of those parameters declares that as its default (params::Opening). None for OrbitAmp, which
        opens where the engine does. */
    void (*opening) (const Product&, params::Opening&) = nullptr;

    /** WHETHER ITS TUNER HAS AN ON OF ITS OWN (`params::tunerOn`). The rovers' and the cockpit's do, in
        their menus. OrbitAmp's listens always: its face has no switch for it, and a tuner left off in
        an old session would be off with nothing anywhere to turn it back on. */
    bool tunerSwitch = true;

    /** ITS CHAIN (Chain, above): the guitar's unless it says otherwise. */
    Chain chain = Chain::guitar;

    /** A GUITAR CHAIN'S COMP, before the AMP (params::addGuitarCompParameters, which its `addParameters` adds): OrbitAmp's;
        the cockpit's and the rovers' faces have none. */
    bool guitarComp = false;

    /** THE LOWEST PITCH ITS TUNER LOOKS FOR, in Hz (core::PitchTracker::setLowestHz). Every guitar amp
        stays at the guitar's, which reaches a bass's low E and costs a guitar nothing more; OrbitBass
        reaches its low B and a drop-A's A0. */
    double tunerLowestHz = core::PitchTracker::guitarLowestHz;

    /** WHICH STANDARD EQ ITS AMP'S CONSOLE IS (params::EqStandard): the bass's on the bass's chain, the guitar's on
        every other. Read from the chain for now, and asked HERE and nowhere else — the console's face
        (EqSection::setStandard) and the processor's EQ (AmpProcessor::bassEq) both come to this one question — so the
        later step that lets a rig say its own standard (a word in its pack) replaces this one body and nothing else. */
    params::EqStandard eqStandard() const noexcept
    {
        return chain == Chain::bass ? params::EqStandard::bass : params::EqStandard::guitar;
    }

    /** A GUITAR AMP'S CABINETS AND A BASS AMP'S NEVER CROSS (the owner, 02.11): not the IRs the build carries,
        and not the player's own. Both are asked of the chain here, and nowhere else.

        Whether it plays the catalog's IR `number` (resources/cabinets.txt): a bass amp the bass's alone, a
        guitar amp every other — which of those its own list lays out is the list's to say (device/FactoryShelf.h). */
    bool playsCabIr (int number) const noexcept
    {
        return number >= 0 && number < (int) std::size (catalog::irs) && catalog::irs[number].bass == (chain == Chain::bass);
    }

    /** ...and the folder the player's own IRs lie in, under the family's (device/IrLibrary.h): `IRs` for every
        guitar amp, `Bass IRs` for every bass amp. */
    const char* irsFolder() const noexcept
    {
        return chain == Chain::bass ? "Bass IRs" : "IRs";
    }
};

/** EVERY AMP OF THE FAMILY, each defined in its own file (above). A binary links the ones it plays. */
namespace products
{
const Product& orbitAmp();
const Product& catpitAmp();
const Product& redPawAmp();
const Product& frostPawAmp();
const Product& amberPawAmp();
const Product& orbitBass();
} // namespace products

} // namespace orbitamp
