// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PerProduct.h"

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

    Each amp says what it is in a file of its own — OrbitAmp in OrbitAmpStart.cpp, the cockpit in
    catpit/CatpitLineup.cpp, each rover in its skin's file — and no two of them define anything under the
    same name, so every amp's sources link into one binary. Each amp's plug-in makes a processor of its
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

    /** WHAT ITS BOOST IS, where it is not a switch of the pack it plays. The cockpit's TURBO pushes the
        capture — the preamp's input trim, at the push the settings give (channels::Channels::turboDb) —
        and the X88-IR's packs have no Boost of their own: true there, so the boost's level
        (params::boostLevel) is heard while TURBO is on. Everywhere else the boost is the playing pack's
        own switch of that name, and OrbitAmp's input trim pushed is only a trim. */
    bool boostIsPush = false;
};

/** EVERY AMP OF THE FAMILY, each defined in its own file (above). A binary links the ones it plays. */
namespace products
{
const Product& orbitAmp();
const Product& catpitAmp();
const Product& redPawAmp();
const Product& frostPawAmp();
const Product& amberPawAmp();
} // namespace products

} // namespace orbitamp
