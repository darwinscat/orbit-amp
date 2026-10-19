// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../channels/Lineup.h"

namespace orbitamp::catpit
{

/** THE COCKPIT'S FOUR CHANNELS — the throttle's four detents, CLEAN, EDGE, CRUNCH and HI-GAIN — over the
    X88-IR's four packs, and what each opens on (channels/Lineup.h). The engine they are kept by is the
    family's (channels/Engine.h); this file is what makes it the cockpit's: the lineup, CHANNEL among
    the parameters, and its first start. */
const channels::Lineup& lineup();

} // namespace orbitamp::catpit
