// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** THE STAGE A FACE IS DRAWN ON: the space its background render was composed in, 1672 x 941 — the
    cockpit's and every face after it — and the sizes type is set at in it. Each face lays its controls out
    in this space (catpit/CatpitLayout.h for the cockpit), and the window scales it as a whole. */
namespace orbitamp::face::stage
{

inline constexpr float width  = 1672.0f;
inline constexpr float height = 941.0f;

/** THE READING WIDTH: the window opens this wide, and every label is sized so that at THIS scale it is
    13 px. */
inline constexpr int   readingWidth   = 1024;
inline constexpr float readingScale   = (float) readingWidth / width;
/** The smallest the window may get: a little under the reading width, asked for. Pulled all the way in,
    a 13-px label is 12.2 px. */
inline constexpr int   minWindowWidth = 960;
inline constexpr float px13           = 13.0f / readingScale;   // a 13-px label, in this space
inline constexpr float px15           = 15.0f / readingScale;
inline constexpr float px16           = 16.0f / readingScale;
/** The one exception, asked for: the six keys under the tone knobs wear short names a size down. */
inline constexpr float px11           = 11.0f / readingScale;

} // namespace orbitamp::face::stage
