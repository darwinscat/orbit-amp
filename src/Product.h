// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHICH PRODUCT THIS BINARY IS. One processor wears two faces, and the two are released apart: each
    has a version of its own and tags of its own in the one repository. The target says which it is on
    the compiler's command line; a target that says nothing is OrbitAmp, which is how it has always
    been built.

      ORBITAMP_PRODUCT_NAME        the name it goes by — in its update check, and the folder its
                                   update badge is remembered in
      ORBITAMP_RELEASE_TAG_PREFIX  what its release tags wear before the version: nothing for
                                   OrbitAmp ("v0.8.0"), "catpit-" for the cockpit ("catpit-v0.1.0")
      ORBITAMP_VERSION             the running version — that product's, not the project's */
#ifndef ORBITAMP_PRODUCT_NAME
 #define ORBITAMP_PRODUCT_NAME "OrbitAmp"
#endif

#ifndef ORBITAMP_RELEASE_TAG_PREFIX
 #define ORBITAMP_RELEASE_TAG_PREFIX ""
#endif

namespace orbitamp::product
{
inline constexpr const char* name      = ORBITAMP_PRODUCT_NAME;
inline constexpr const char* tagPrefix = ORBITAMP_RELEASE_TAG_PREFIX;
} // namespace orbitamp::product
