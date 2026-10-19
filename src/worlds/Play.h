// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cmath>

namespace orbitamp::worlds
{

/** THE GUITAR AS A WORLD HEARS IT: what the player is doing, made into two numbers a view can light things
    by. Fed the peak the amp's input had since the last step (linear, 1 = full scale; the level the IN meter
    shows, after the IN trim), once a frame, and stepped with the world:

      hit()    THE ATTACK. A note picked out of silence is a flash of 1, a note picked into a ringing one a
               smaller one, by how far it rose over what was sounding; it fades in about a third of a
               second. A long note held is no hit at all — the world answers the hand, not the loudness.
      glow()   THE PLAYING. The level itself, eased: up in a few tens of milliseconds, down in half a
               second — a soft light under the flashes while the guitar sounds, out when it stops.
      rise()   How far the last step rose over what was sounding, 0..1 of floorDb..topDb: for a world that
               answers only a hard attack — a loud chord out of quiet, not every note picked.

    All three 0..1, and 0 exactly in silence and after reset(), so a world that multiplies its light by
    (1 + k * hit()) draws the very frame it drew before it could hear. The same code steps it in the app
    and in the site's WebAssembly: the app feeds the processor's peak, the site its own audio's. No JUCE in
    here, no clock of its own: a frame-rate's dt in, the same answer at 30 frames a second as at 60. */
class Play
{
public:
    /** What counts as sounding at the amp's input — a guitar plays there at -30 to -6 dBFS, its hum and
        hiss sit at -60 to -80 (face/Monitor.h's quietDb) — and what is playing hard. */
    static constexpr float floorDb = -50.0f, topDb = -12.0f;
    /** The attack: the rise over what was sounding, in the 0..1 of floorDb..topDb, that is a full flash. */
    static constexpr float fullRise = 0.45f;
    /** How fast the flash fades, and how the level that a rise is judged against follows the playing. */
    static constexpr double hitSeconds = 0.32, heldSeconds = 0.12, fallSeconds = 0.6;
    /** How the glow follows the level: up, and down. */
    static constexpr double glowUpSeconds = 0.04, glowDownSeconds = 0.5;

    /** The input's peak since the last time, linear; the largest of several before a step is kept. */
    void hear (float peak) noexcept { heard = std::max (heard, std::abs (peak)); }

    /** One step of dt seconds: what was heard since the last one taken in. */
    void step (double dt) noexcept
    {
        const float db = 20.0f * std::log10 (std::max (heard, 1.0e-6f));
        const float level = std::clamp ((db - floorDb) / (topDb - floorDb), 0.0f, 1.0f);
        heard = 0.0f;
        if (dt <= 0.0)
            return;

        // The rise is judged against what was sounding: a follower that comes up in about a tenth of a
        // second and falls in more than half of one — a note picked into silence rises over nothing, one
        // picked into a ringing chord over the chord.
        rose = std::max (0.0f, level - held);
        const float rise = rose;
        held += (level - held) * ease (dt, level > held ? heldSeconds : fallSeconds);

        flash = std::max (flash * (float) std::exp (-dt / hitSeconds), std::min (1.0f, rise / fullRise));
        sustain += (level - sustain) * ease (dt, level > sustain ? glowUpSeconds : glowDownSeconds);
        if (flash < 1.0e-4f)   flash = 0.0f;       // out, not a last flicker forever
        if (sustain < 1.0e-4f) sustain = 0.0f;
    }

    float hit() const noexcept  { return flash; }
    float glow() const noexcept { return sustain; }
    float rise() const noexcept { return rose; }

    /** Silence, as before anything was heard: for a still, and for a world placed anew. */
    void reset() noexcept { heard = held = flash = sustain = rose = 0.0f; }

private:
    float heard = 0.0f, held = 0.0f, flash = 0.0f, sustain = 0.0f, rose = 0.0f;

    static float ease (double dt, double seconds) noexcept { return (float) (1.0 - std::exp (-dt / seconds)); }
};

} // namespace orbitamp::worlds
