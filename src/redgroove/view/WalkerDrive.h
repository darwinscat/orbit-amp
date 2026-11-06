// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "WalkerGl.h"
#include "WalkerWorld.h"
#include "../../redpaw/view/Drive.h"
#include "../../worlds/Play.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::redgroove
{

/** THE WALKER IN THE GLOWING FOREST AS A DRIVE: WalkerWorld and WalkerGl behind the windscreen's face. A product
    passes one to the windscreen — `Windscreen (std::make_unique<redgroove::WalkerDrive> (gears))` — and the
    view is the dome's whole stage, the horizon where the cabin puts it; the 1993 landscape wears the forest's
    colours. The walker paces in its gears (1 to 3; RedGrooveBass has the first two) and, given the host's
    tempo, puts its footfalls on the beat.

    The world is made the first time it is needed and settled then, as the prototype's view makes it: a gear
    asked for before that is the gear it is made in. */
class WalkerDrive final : public redpaw::Drive
{
public:
    /** In the product's gears, 1..WalkerWorld::numGears; the horizon a fraction of the frame from its top. */
    explicit WalkerDrive (int gears, double horizon = 0.40)
        : gearCount (std::clamp (gears, 1, WalkerWorld::numGears))
    {
        gl.setHorizon (horizon);
    }

    WalkerWorld& walker() noexcept { return world; }

    const char* name() const noexcept override { return "Walker"; }
    int numGears() const noexcept override     { return gearCount; }

    void setGear (int g) override            { world.setGear (std::clamp (g, 1, gearCount)); }
    void setTurbo (bool on) override         { world.setTurbo (on); }
    void setSteer (float amount) override    { world.setSteer (amount); }
    void setTempo (double bpm) override      { world.setBpm (bpm); }

    /** THE BASS, heard: the amp's input's peak since the last step, made a level between what counts as sounding
        and its top (worlds::Play's floorDb..topDb), is the world's level input — the caps breathe with it, and a
        jump in it is an attack (WalkerWorld::setLevel). The pick, the Ear's click above 3 kHz, is not the bass's. */
    void hear (float peak, float /*pick*/) override { if (lit) heard = std::max (heard, std::abs (peak)); }
    /** Not lit by the bass: the level stays 0, and the forest is the forest as it always was. */
    void setLit (bool on) override { lit = on; if (! on) heard = 0.0f; }

    void step (double dt) override
    {
        made();
        const float db = 20.0f * std::log10 (std::max (heard, 1.0e-6f));
        world.setLevel (std::clamp ((db - worlds::Play::floorDb) / (worlds::Play::topDb - worlds::Play::floorDb), 0.0f, 1.0f));
        heard = 0.0f;
        world.step (dt);
    }

    /** A gear or TURBO changed is settled at once, as the prototype settles one: the pace, the stride, the spores
        and the storm where they are going. A still otherwise keeps the walk where it is, its spores and its sway. */
    void settle (int g, bool t) override
    {
        const bool changed = std::clamp (g, 1, gearCount) != world.gear() || t != world.isTurbo();
        setGear (g); setTurbo (t);
        if (! world.isBuilt())
            made();
        else if (changed)
            world.settle();
    }

    /** A fresh walk from there: the world made again with no history, standing at (x, z) on that heading. */
    void placeAt (double x, double z, double heading) override
    {
        const int g = world.gear();
        const bool t = world.isTurbo();
        const double bpm = world.state().bpm;
        world = WalkerWorld();
        world.setGear (g); world.setTurbo (t); world.setBpm (bpm);
        world.startAt (x, z, heading);
        made();
    }

    float speed() const noexcept override { return (float) world.state().speed; }
    float speedFor (int g, bool t) const noexcept override
    {
        const auto i = (size_t) (std::clamp (g, 1, gearCount) - 1);
        const double bpm = world.state().bpm;
        const double rate = bpm > 0 ? bpm / 60 * WalkerWorld::beatSteps[i] : WalkerWorld::gearRate[i] * (t ? WalkerWorld::turboRate : 1);
        return (float) (rate * WalkerWorld::gearStep[i] * (t ? WalkerWorld::turboStep : 1));
    }
    Ease ease() const noexcept override                          { return { WalkerWorld::paceSeconds }; }
    redpaw::Mars1993::Look look1993() const noexcept override    { return redpaw::Mars1993::Look::walker; }

    bool create() override                                       { made(); return gl.create(); }
    std::string error() const override                           { return gl.error(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }

private:
    const int gearCount;
    bool lit = false;
    float heard = 0.0f;
    WalkerWorld world;
    WalkerGl gl { world };

    /** The world made and settled, once: what the prototype's view does as it starts. */
    void made()
    {
        if (world.isBuilt())
            return;
        world.build();
        world.settle();
    }
};

} // namespace orbitamp::redgroove
