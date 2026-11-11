// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <panel/Panel.h>
#include <synth/CabSynth.h>
#include <synth/State.h>

#include <optional>
#include <string>

namespace orbitamp::device
{

/** WHOSE CABINET A STATE IS: the bass's or the guitar's, two builders of cab-ir-generator (Orbit Bass Cab Builder,
    Orbit Guitar Cab Builder), each its own table and its own panel's catalog. Told by the box, never by the amp: the
    two builders' boxes share no name ("8x10-classic", "4x12-closed"), so a state says whose it is wherever it comes
    from — a preset, a session, a link — and one binary may hold amps of both, side by side (Product.h). */
enum class CabKind { none, bass, guitar };

CabKind kindOf (const std::string& box);

/** The table a kind's synthesis reads — the bass's cabinets.txt, the guitar's (both carried in the binary). */
const cabir::synth::Table& tableOf (CabKind);

/** The panel's catalog of a kind: what its window draws and its states are known by. */
const cabir::Panel::Catalog& catalogOf (CabKind);

/** A STATE FROM ITS TEXT, as cabir::knownState reads one, but against its own builder's catalog rather than whichever
    the panel holds: none where the text is none, names a box neither builder has, or a mic its builder has not. */
std::optional<cabir::State> knownCabinet (const std::string& toml);

/** The panel set to draw `kind`'s boxes (cabir::Panel::useCatalog) — before a window of it opens; none leaves it. */
void drawCabinetsOf (CabKind);

} // namespace orbitamp::device
