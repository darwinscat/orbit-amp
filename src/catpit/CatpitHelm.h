// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <cmath>
#include <numbers>

namespace orbitamp::catpit
{

/** THE HELM: the hand's steering (face::Steering, -1 full left .. 1 full right) made into what the ship
    does with it, for both skies alike. A hand is jerky and a ship is not: the hand is smoothed first, the
    turn follows that, and the bank follows the turn a moment later, as a ship heels over only once it has
    begun to come round. Held full over, the turn is nine tenths of the way there in about 0.6 s and the
    bank in about 0.9 s. Let go, and the turn unwinds and the ship levels out — the bank under a twentieth
    of its lean in about a second, without a snap at either end. There is no heading to hold in space:
    the ship simply straightens.

    What the turn and the bank show, the skies share:
    - the BANK rolls the sky in the glass about the point the ship is headed for — counter-clockwise for a
      turn to the right, as from a cockpit heeled over to the right — up to `maxRoll`;
    - the TURN moves that point, which the stars stream out of, toward the turn — up to `maxFocus` of the
      windshield's height — and sweeps the whole stream, stars and clouds, the other way: each sky at its
      own pace, quicker the faster the ship flies, the nearer stars quicker than the far ones.

    No JUCE: the site's sky, in WebAssembly, steps the same helm (CatpitFlight.h). One thread's: the sky
    that steps it. */
struct Helm
{
    static constexpr float maxRoll  = 11.0f * std::numbers::pi_v<float> / 180.0f;
    static constexpr float maxFocus = 0.75f;

    float hand = 0.0f;           // the hand, smoothed
    float turn = 0.0f;           // how hard the ship is coming round, -1..1
    float bank = 0.0f;           // ...and how far it has heeled over, -1..1

    void step (float steer, float dt) noexcept
    {
        const auto ease = [dt] (float& v, float to, float seconds) { v += (to - v) * (1.0f - std::exp (-dt / seconds)); };
        ease (hand, steer, 0.15f);
        ease (turn, hand, 0.15f);
        ease (bank, turn, 0.2f);
    }

    /** The roll, radians: positive, the ship heeled over to the right. */
    float roll() const noexcept { return bank * maxRoll; }

    /** Where the point the stars stream out of stands, in windshield heights right of its middle. */
    float focus() const noexcept { return turn * maxFocus; }
};

} // namespace orbitamp::catpit
