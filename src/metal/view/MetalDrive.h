// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MetalGl.h"
#include "MetalWorld.h"
#include "../../redpaw/view/Drive.h"
#include "../../worlds/Play.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::metal
{

/** THE METAL WORLD AS A DRIVE: MetalWorld and MetalGl behind the windscreen's face. A product passes one to the
    windscreen — `Windscreen (std::make_unique<metal::MetalDrive> (shippedArt()))` — and the view is the heavy rover's
    whole stage, 1672 x 941, in its cabin camera's projection as cabin.html hands it to the prototype: a 16 mm lens over
    the stage's width (the half-width's tangent 18/16 at any aspect, the centre where it is), the eye 2.6 m over the
    road, 5 degrees down (the world's own 2 and page.js's 3); the cabin's two plates drawn in the post pass while they
    are in (drawsCabin). The 1993 landscape wears Mars's look: the nearest of the five to a dark plain of rust-red
    rock and lava under a black sky (the ice, Titan's haze, the walker's forest and the sea are further off).

    The E530's lever: CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO, LEAD GAIN HI are the gears 1 to 4 (LEAD GAIN HI is the
    storm), PARK pulls onto the shoulder; no TURBO. The guitar heard is the attack answer: an onset in its level is one
    attack, the world's thunder (MetalWorld::hit).

    The world is made the first time it is needed, furnished with what the art's finds bring it (the dragon's head,
    the letters, the groves, the landmarks' tops; MetalGl::find, read here once) and settled then, as the prototype's
    view makes it: a gear or PARK asked for before that is what it is made in. */
class MetalDrive final : public redpaw::Drive
{
public:
    /** The cabin's projection, as cabin.html hands it to the prototype. */
    static constexpr double tanH = 18.0 / 16.0, shear = 0.0;
    static constexpr double pitchOffset = MetalWorld::pitchOffsetDefault;     // page.js's 3 degrees more down: 5 in all

    /** With the art (MetalGl::Art; none draws no cabin and only the procedural world). The art's finds and the night
        plate's window are looked at once here (glassAt): call on the message thread, before the windscreen. */
    explicit MetalDrive (MetalGl::Art art = {})
    {
        if (art.picture)
        {
            const auto dash = art.picture ("dashboard.webp");
            if (dash.valid())
                glass = MetalWorld::Glass::of (dash.rgba.data(), dash.width, dash.height, 4);
        }
        auto found = MetalGl::find (art);
        finds = found.finds;
        gl.setArt (std::move (art), std::move (found));
        gl.setLens (tanH, shear);
    }

    MetalWorld& metal() noexcept { made(); return world; }
    MetalGl& renderer() noexcept { return gl; }

    const char* name() const noexcept override { return "Metal"; }
    int numGears() const noexcept override     { return MetalWorld::numGears; }

    void setGear (int g) override            { world.setGear (std::clamp (g, 1, MetalWorld::numGears)); }
    void setTurbo (bool) override            {}    // the E530 has no TURBO
    void setSteer (float) override           {}    // the road steers
    void setPark (bool on) override          { world.setParked (on); }

    /** THE GUITAR, heard: the amp's input's peak since the last step, made a level between what counts as sounding
        and its top (worlds::Play's floorDb..topDb); a jump in it is an attack, as UnderwaterDrive hears one: over 0.28
        of the span, 0.14 s after the last, its strength the jump's. */
    void hear (float peak, float /*pick*/) override { if (lit) heard = std::max (heard, std::abs (peak)); }
    /** Not lit by the guitar: no answer at all, and one running is called off. */
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
        const bool changed = std::clamp (g, 1, MetalWorld::numGears) != world.gear();
        setGear (g);
        if (! world.isBuilt())
            made();
        else if (changed)
            world.settle();
        if (world.state().time <= 0)
            world.settleCamera();
    }

    /** A fresh drive from there: the world made again with no history, standing where the loop passes nearest (x, z)
        (the road sets the heading). */
    void placeAt (double x, double z, double /*heading*/) override
    {
        const int g = world.gear();
        const bool parked = world.isParked();
        world = MetalWorld();
        world.setGear (g); world.setParked (parked);
        made();
        const double L = MetalWorld::loopLength();
        double best = 0, bestD = 1e300;
        for (double sr = 0; sr < L; sr += 1.0)
        {
            const auto p = MetalWorld::roadPointAt (sr, 0);
            const double d = (p[0] - x) * (p[0] - x) + (p[1] - z) * (p[1] - z);
            if (d < bestD) { bestD = d; best = sr; }
        }
        world.startAt (best);
    }

    float speed() const noexcept override { return (float) world.state().speed; }
    float speedFor (int g, bool) const noexcept override { return (float) world.speedFor (g); }
    Ease ease() const noexcept override { return { MetalWorld::speedSeconds }; }
    redpaw::Mars1993::Look look1993() const noexcept override { return redpaw::Mars1993::Look::mars; }

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
    void setCabin (bool drawn) override                          { gl.setCabin (drawn); }
    /** The window, where the night plate's alpha is low; everywhere when no plate was given. Any thread. */
    bool glassAt (float x01, float y01) const override           { return glass.width == 0 || glass.open (x01, y01); }

private:
    static constexpr double onsetJump = 0.28, onsetGap = 0.14, onsetSeconds = 0.06;
    bool lit = false;
    float heard = 0.0f;
    double levelLP = 0.0, time = 0.0, lastOnset = -10.0;
    MetalWorld::Glass glass;            // made once, read only after
    MetalWorld::Finds finds;
    MetalWorld world;
    MetalGl gl { world };

    /** The world made, furnished and settled, once: what the prototype's view does as it starts. */
    void made()
    {
        if (world.isBuilt())
            return;
        world.build();
        world.furnish (finds);
        world.settle();
        world.setPitchOffset (pitchOffset);
    }
};

} // namespace orbitamp::metal
