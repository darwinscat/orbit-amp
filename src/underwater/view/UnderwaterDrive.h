// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "UnderwaterGl.h"
#include "UnderwaterWorld.h"
#include "../../redpaw/view/Drive.h"
#include "../../worlds/Play.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::underwater
{

/** THE SEA FLOOR AS A DRIVE: UnderwaterWorld and UnderwaterGl behind the windscreen's face. A product passes one to
    the windscreen — `Windscreen (std::make_unique<underwater::UnderwaterDrive> (art))` — and the view is the
    bathyscaphe's whole stage, 1672 x 941, in its cabin camera's projection (a 13 mm lens: the half-width's tangent
    18/13 at any aspect, the centre slid to -0.2339, the world 11 degrees down), with the cabin's panel drawn over it
    as the last pass while its pictures are in (drawsCabin); the 1993 landscape wears the sea's colours.

    The bass amp's lever: CLEAN, DIRT, DRIVE are the gears 1, 2, 3 (no TURBO: the bass has none), and PARK — the amp
    bypassed whole — stops the bathyscaphe and brings the near jellyfish to the glass. The bass heard is the attack
    answer: an onset in its level lights the living things, running out from the vehicle.

    The world is made the first time it is needed and settled then, as the prototype's view makes it: a gear or
    PARK asked for before that is what it is made in. */
class UnderwaterDrive final : public redpaw::Drive
{
public:
    /** The cabin's projection, as cabin.html hands it to the prototype. */
    static constexpr double tanH = 18.0 / 13.0, shear = -0.2339;
    static constexpr double pitchOffset = (-11.0 + 3.5) * UnderwaterWorld::pi / 180;   // the render's 3 degrees down, made 11

    /** With the cabin's art (UnderwaterGl::Art; none draws no cabin and no passing bathyscaphe). The dashboard is
        looked at once here, for where its window is (glassAt): call on the message thread, before the windscreen. */
    explicit UnderwaterDrive (UnderwaterGl::Art art = {})
    {
        if (art.dashboard)
        {
            const auto dash = art.dashboard();
            if (dash.valid())
                glass = UnderwaterWorld::Glass::of (dash.rgba.data(), dash.width, dash.height, 4);
        }
        gl.setArt (std::move (art));
        gl.setLens (tanH, shear);
    }

    UnderwaterWorld& sea() noexcept { return world; }

    const char* name() const noexcept override { return "Underwater"; }
    int numGears() const noexcept override     { return UnderwaterWorld::numGears; }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool) override            {}    // the bass has no TURBO
    void setSteer (float amount) override    { world.setSteer (amount); }
    void setPark (bool on) override          { world.setParked (on); }

    /** THE BASS, heard: the amp's input's peak since the last step, made a level between what counts as sounding
        and its top (worlds::Play's floorDb..topDb); a jump in it is an attack, as the walker hears one
        (WalkerWorld::setLevel): over 0.28 of the span, 0.14 s after the last, its strength the jump's. The pick,
        the Ear's click above 3 kHz, is not the bass's. */
    void hear (float peak, float /*pick*/) override { if (lit) heard = std::max (heard, std::abs (peak)); }
    /** Not lit by the bass: no answer at all, and one running is called off. */
    void setLit (bool on) override
    {
        if (lit && ! on)
            world.quiet();
        lit = on;
        if (! on) { heard = 0.0f; levelLP = 0.0; }
    }

    void step (double dt) override
    {
        made();
        if (lit && dt > 0)
        {
            const float db = 20.0f * std::log10 (std::max (heard, 1.0e-6f));
            const double level = std::clamp ((db - worlds::Play::floorDb) / (worlds::Play::topDb - worlds::Play::floorDb), 0.0f, 1.0f);
            const double jump = level - levelLP;
            if (jump > onsetJump && time - lastOnset > onsetGap) { lastOnset = time; world.hit (std::clamp (jump * 1.6, 0.3, 1.0)); }
            levelLP += (level - levelLP) * (1 - std::exp (-dt / onsetSeconds));
        }
        heard = 0.0f;
        time += dt;
        world.step (dt);
    }

    /** A gear or PARK changed is settled at once, as the prototype's view settles one; a still otherwise keeps the
        drive where it is. */
    void settle (int g, bool) override
    {
        const bool changed = std::clamp (g, 1, UnderwaterWorld::numGears) != world.gear();
        setGear (g);
        if (! world.isBuilt())
            made();
        else if (changed)
            world.settle();
    }

    /** A fresh drive from there: the world made again with no history, standing at (x, z) on that heading. */
    void placeAt (double x, double z, double heading) override
    {
        const int g = world.gear();
        const bool parked = world.isParked();
        world = UnderwaterWorld();
        world.setGear (g); world.setParked (parked);
        world.startAt (x, z, heading);
        made();
    }

    float speed() const noexcept override { return (float) world.state().speed; }
    float speedFor (int g, bool) const noexcept override { return (float) world.speedFor (g, false); }
    Ease ease() const noexcept override { return { UnderwaterWorld::speedSeconds }; }
    redpaw::Mars1993::Look look1993() const noexcept override { return redpaw::Mars1993::Look::underwater; }

    bool create() override                                       { made(); return gl.create(); }
    std::string error() const override                           { return gl.error(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }
    bool drawsCabin() const override                             { return gl.drawsCabin(); }
    /** The window, where the dashboard's alpha is low; everywhere when no dashboard was given. Any thread. */
    bool glassAt (float x01, float y01) const override           { return glass.open (x01, y01); }

private:
    static constexpr double onsetJump = 0.28, onsetGap = 0.14, onsetSeconds = 0.06;
    bool lit = false;
    float heard = 0.0f;
    double levelLP = 0.0, time = 0.0, lastOnset = -10.0;
    UnderwaterWorld::Glass glass;       // made once, read only after
    UnderwaterWorld world;
    UnderwaterGl gl { world };

    /** The world made and settled, once: what the prototype's view does as it starts. */
    void made()
    {
        if (world.isBuilt())
            return;
        world.build();
        world.settle();
        world.setPitchOffset (pitchOffset);
    }
};

} // namespace orbitamp::underwater
