// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CatpitHelm.h"
#include "../worlds/Play.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace orbitamp::catpit
{

/** THE FLIGHT the GPU's sky shows: the ship's speed, how far it has flown, the helm, and how far the turns
    have swept each layer of stars and each cloud — everything the sky computes on the CPU, a step a frame,
    to hand to its shader (SkyGl). No JUCE and no GL: the app's sky (GlSky) steps it on the GL thread with
    the frame's time, the site's (tools/worlds-wasm/sky.cpp) with the browser's.

    One thread's: the sky's that steps it. */
struct Flight
{
    // Ship speed at each detent. CLEAN and EDGE fly as the CPU sky does; from CRUNCH on this one goes
    // faster — CRUNCH a little past where HI-GAIN used to fly, HI-GAIN nearly twice that — and since a
    // star's streak is as long as the ship is fast, the two top detents smear the stars further too.
    static constexpr std::array<float, 4> detentSpeed { 0.05f, 0.16f, 1.8f, 3.0f };

    // A turn's sweep, in the stars' own cells a second at full lock: a slide at the cruise, four times
    // that at the top. A layer's cells look smaller the farther off it is, so the same sweep carries
    // the near stars across the glass much faster than the far ones. The clouds are swept the same
    // way, in their own measure, by this share of it.
    static constexpr float slideCruise = 3.0f, slidePerSpeed = 3.0f;
    static constexpr float cloudSlide = 0.08f;

    // THE GUITAR (worlds/Play.h): the stars brighten with each attack and the nebulae glow, a little while
    // it sounds — nothing of it in silence, so a silent sky is the sky it always was.
    static constexpr float starsOfHit = 0.8f, starsOfGlow = 0.2f;
    static constexpr float cloudsOfHit = 0.6f, cloudsOfGlow = 0.25f;

    float speed = detentSpeed[0];
    double travel = 0.0;
    Helm helm;
    float slide = 0.0f;             // the turn's sweep at the last step, cells a second
    // How far the turns have swept each layer of stars, and each cloud, since it last came up out of the
    // distance — and how deep each stood at the last step, to tell when it has.
    std::array<float, 4> drift {}, depth {};
    std::array<float, 2> cloudDrift {}, cloudPhase {};
    worlds::Play play;

    /** The guitar: the amp's input's peak since the last step. */
    void hear (float peak) noexcept { play.hear (peak); }
    /** How much brighter the stars shine and the nebulae glow for it: 0 in silence. */
    float starShine() const noexcept  { return starsOfHit * play.hit() + starsOfGlow * play.glow(); }
    float cloudShine() const noexcept { return cloudsOfHit * play.hit() + cloudsOfGlow * play.glow(); }

    /** The speed the throttle asks for: 0 = CLEAN .. 1 = HI-GAIN, continuous, between the detents' own. */
    static float speedFor (float throttle) noexcept
    {
        const float at = throttle * 3.0f;
        const int i = std::clamp ((int) at, 0, 2);
        return detentSpeed[(size_t) i] + (detentSpeed[(size_t) i + 1] - detentSpeed[(size_t) i]) * (at - (float) i);
    }

    /** `dt` seconds of flight with the throttle at `throttle` and the hand on the helm at `steer` (Helm). */
    void step (float throttle, float steer, float dt) noexcept
    {
        // The ship: the speed the lever asks for, reached the way a ship reaches it — slower up than down.
        const float want = speedFor (throttle);
        speed += (want - speed) * (1.0f - std::exp (-dt / (want > speed ? 0.9f : 0.5f)));
        travel += (double) (speed * dt);
        play.step (dt);

        // ...and the helm: the turn sweeps each layer and each cloud sideways, and what a layer or a cloud
        // was swept is forgotten when it comes up again out of the distance, where nothing of it shows —
        // so no sweep ever piles up. The depths are the shader's own.
        helm.step (steer, dt);
        slide = helm.turn * (slideCruise + slidePerSpeed * speed);
        const double flown = travelled();
        const auto fraction = [] (double v) { return (float) (v - std::floor (v)); };
        for (size_t l = 0; l < drift.size(); ++l)
        {
            const float z = fraction ((double) l * 0.25 + flown * 0.45);
            if (z < depth[l])
                drift[l] = 0.0f;
            depth[l] = z;
            drift[l] += slide * dt;
        }
        for (size_t c = 0; c < cloudDrift.size(); ++c)
        {
            const float phase = fraction (flown * 0.12 + (double) c * 0.5);
            if (phase < cloudPhase[c])
                cloudDrift[c] = 0.0f;
            cloudPhase[c] = phase;
            cloudDrift[c] += slide * cloudSlide * dt;
        }
    }

    /** At once at the speed the throttle asks for, as if the ship had flown at it all along — where a sky
        is first shown at a channel rather than spooling up to it. Nothing else moves. */
    void settle (float throttle) noexcept { speed = speedFor (throttle); }

    /** How far it has flown, as the shader reads it: the stars and the clouds repeat long before a
        thousand, and a float holds that to well under a pixel. */
    double travelled() const noexcept { return std::fmod (travel, 1000.0); }

    /** At speed the light ahead gathers into a glow: none below half a cell a second, all of it from 1.6. */
    float warp() const noexcept { return std::clamp ((speed - 0.5f) / 1.1f, 0.0f, 1.0f); }
};

} // namespace orbitamp::catpit
