// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/limiter/TruePeakLimiter.h>

namespace orbitamp::core::door
{

/** THE SAFETY AT THE DOOR, as the chain sets it up — one place, so the processor and its gates build
    the same limiter.

    felitronics::limiter::TruePeakLimiter: it sees each peak a millisecond before it arrives and takes
    the gain down on a four-times grid, so a peak is never cut flat at the ceiling — the instant-attack
    follower it replaced did exactly that, on every new peak, and crackled on a dense channel held
    against its lid. Four times over a short kernel: 63 samples late at 48 kHz, the kernel's droop
    above anything a guitar cabinet lets through. */
inline felitronics::limiter::TruePeakLimiterConfig config()
{
    felitronics::limiter::TruePeakLimiterConfig c;
    c.lookaheadMs      = 1.0;
    c.oversampleFactor = 4;
    c.tapsPerPhase     = 16;
    return c;
}

/** HOW FAR UNDER THE PLAYER'S CEILING THE GRID'S LIES. The limiter bounds every sample on its grid; the
    signal rebuilt from that grid can stand up to about a decibel higher where the gain moves hard
    (the module's own characterisation: ~0.9 dB for dense, hard-limited material at 4×, a tenth of that
    for dark material such as a cabinet's). The ceiling the player sets is the one that leaves the box,
    so the grid's is set this much under it. */
inline constexpr double derateDb = 1.0;

inline constexpr double releaseMs = 60.0;      // as the follower it replaced let go

/** The limiter's parameters for a ceiling the player set, in dBFS. */
inline felitronics::limiter::TruePeakLimiterParams params (double ceilingDb)
{
    felitronics::limiter::TruePeakLimiterParams p;
    p.ceilingDbTp = ceilingDb - derateDb;
    p.releaseMs   = releaseMs;
    return p;
}

} // namespace orbitamp::core::door
