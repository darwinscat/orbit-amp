// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHICH PRODUCT THIS BINARY IS. One processor wears several faces, each an amp of its own — its name,
    its settings, its presets — and all of them released together, as Darwin's Cat Amps, under the
    family's one version and one tag, vX.Y.Z. The target says which amp it is on the compiler's command
    line; a target that says nothing is OrbitAmp, which is how it has always been built.

      ORBITAMP_PRODUCT_NAME  the name it goes by — in its update check, and the folder its update badge
                             is remembered in
      ORBITAMP_VERSION       the running version — the family's (CMakeLists.txt's project())

    Every amp asks for the repository's LATEST release — the family's vX.Y.Z — as OrbitAmp always has:
    no tag prefix, since every release of the repository is every amp's. */
#ifndef ORBITAMP_PRODUCT_NAME
 #define ORBITAMP_PRODUCT_NAME "OrbitAmp"
#endif

namespace orbitamp::product
{
inline constexpr const char* name = ORBITAMP_PRODUCT_NAME;
} // namespace orbitamp::product
