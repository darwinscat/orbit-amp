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

    /** WHETHER ITS INPUT TRIM STANDS OVER THE MACHINE'S INPUT ZERO (prefs::inputZero): the engine plays the
        zero and the parameter added, the parameter being the hand's control — OrbitAmp's IN fader, whose 0
        is that zero, and a host's Input Trim with it. Off where the face folds the zero into the trim
        itself and the parameter IS what the engine plays: the cockpit's and the rovers' INPUT lever
        (face/Console.h). */
    bool inputTrimOverZero = false;
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
