// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cmath>

namespace orbitamp::worlds
{

/** THE GUITAR AS A WORLD HEARS IT: what the player is doing, made into numbers a view can light things by. Fed
    once a frame what an Ear heard since the last one — the amp's input's peak (linear, 1 = full scale, the
    level the IN meter shows) and its pick (Ear::Heard::pick: how far the sound's top jumped, a power ratio) —
    and stepped with the world:

      hit()    THE STROKE. Every pick of a string is a flash, by how hard its click stood out of what was
               sounding — a stroke out of silence or a dug-in one full, a light one less — fading in about a
               third of a second; a riff flashes at every stroke, a note held does not flash at all: the world answers
               the hand, not the loudness. A click under the hiss is no stroke.
      glow()   THE PLAYING. The level itself, eased: up in a few tens of milliseconds, down in half a second — a
               soft light under the flashes while the guitar sounds, out when it stops.
      rise()   How far the level rose in the last step over what was sounding, 0..1 of floorDb..topDb: for a
               world that answers only a hard attack out of quiet — a loud chord after a rest.

    All three 0..1, and 0 exactly in silence and after reset(), so a world that multiplies its light by
    (1 + k * hit()) draws the very frame it drew before it could hear. The same code steps it in the app and in
    the site's WebAssembly. No JUCE in here, no clock of its own: a frame's dt in, the same answer at 30 frames
    a second as at 60. */
class Play
{
public:
    /** What counts as sounding at the amp's input — a guitar plays there at -30 to -6 dBFS, its hum and hiss sit
        at -60 to -80 (face/Monitor.h's quietDb) — and what is playing hard. */
    static constexpr float floorDb = -50.0f, topDb = -12.0f;
    /** A stroke: its click this far over what the top was is the first glimmer, this much further a full flash
        (Ear's pick, in dB); and the level a stroke has to be over the floor to be one at all. */
    static constexpr float pickFloorDb = 2.0f, pickSpanDb = 6.0f;
    static constexpr float audibleFrom = 0.05f, audibleSpan = 0.15f;
    /** How fast a flash fades; how the level a rise is judged against follows the playing; how the glow does. */
    static constexpr double hitSeconds = 0.32, heldSeconds = 0.12, fallSeconds = 0.6;
    static constexpr double glowUpSeconds = 0.04, glowDownSeconds = 0.5;

    /** What was heard since the last time: the largest of each of several before a step is kept. */
    void hear (float peak, float pick) noexcept
    {
        heard = std::max (heard, std::abs (peak));
        clicked = std::max (clicked, pick);
    }

    /** One step of dt seconds: what was heard since the last one taken in. */
    void step (double dt) noexcept
    {
        const float db = 20.0f * std::log10 (std::max (heard, 1.0e-6f));
        const float level = std::clamp ((db - floorDb) / (topDb - floorDb), 0.0f, 1.0f);
        const float pickDb = 10.0f * std::log10 (std::max (clicked, 1.0e-6f));
        heard = 0.0f; clicked = 0.0f;
        if (dt <= 0.0)
            return;

        // The stroke: the click's jump, heard only where the guitar is over the hiss.
        const float audible = std::clamp ((level - audibleFrom) / audibleSpan, 0.0f, 1.0f);
        const float stroke = std::clamp ((pickDb - pickFloorDb) / pickSpanDb, 0.0f, 1.0f) * audible;
        flash = std::max (flash * (float) std::exp (-dt / hitSeconds), stroke);

        // The rise is judged against what was sounding: a follower that comes up in about a tenth of a second
        // and falls in more than half of one — a chord out of a rest rises over nothing, one into a ringing
        // chord over the chord.
        rose = std::max (0.0f, level - held);
        held += (level - held) * ease (dt, level > held ? heldSeconds : fallSeconds);

        sustain += (level - sustain) * ease (dt, level > sustain ? glowUpSeconds : glowDownSeconds);
        if (flash < 1.0e-4f)   flash = 0.0f;       // out, not a last flicker forever
        if (sustain < 1.0e-4f) sustain = 0.0f;
    }

    float hit() const noexcept  { return flash; }
    float glow() const noexcept { return sustain; }
    float rise() const noexcept { return rose; }

    /** Silence, as before anything was heard: for a still, and for a world placed anew. */
    void reset() noexcept { heard = clicked = held = flash = sustain = rose = 0.0f; }

private:
    float heard = 0.0f, clicked = 0.0f, held = 0.0f, flash = 0.0f, sustain = 0.0f, rose = 0.0f;

    static float ease (double dt, double seconds) noexcept { return (float) (1.0 - std::exp (-dt / seconds)); }
};

} // namespace orbitamp::worlds
