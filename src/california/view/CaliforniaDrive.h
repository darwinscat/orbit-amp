// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CaliforniaGl.h"
#include "CaliforniaWorld.h"
#include "../../redpaw/view/Drive.h"
#include "../../worlds/Play.h"

#include <algorithm>
#include <cmath>

namespace orbitamp::california
{

/** CALIFORNIA, 1967, AS A DRIVE: CaliforniaWorld and CaliforniaGl behind the windscreen's face. A product passes one
    to the windscreen — `Windscreen (std::make_unique<california::CaliforniaDrive> (art))` — and the view is the van
    cabin's whole stage, 1672 x 941, in its cabin camera's projection as cabin.html hands it to the prototype: a
    16 mm lens over the stage's width (the half-width's tangent 18/16 at any aspect), the centre at the stage's
    centre (no shear), the eye 1.9 m over the road, the world 5 degrees down (the render's 2, and 3 more). The cabin
    (the dashboard and the swinging peace pendant) is drawn over it as the last passes while its picture is in
    (drawsCabin); the 1993 landscape wears Mars's colours, the nearest of its looks to the golden hills.

    The Orange's lever: CLEAN and DIRTY are the gears 1 and 2 (no TURBO), and PARK pulls the van onto the shoulder
    and brings the monarchs to the glass. The guitar heard is the attack answer, as the sea's bass is: an onset in
    its level is a hit — the near poppies flare, petals and butterflies lift off, the pendant is kicked.

    No steering: the van drives itself round the loop. The world is made the first time it is needed and settled
    then, as the prototype's view makes it: a gear or PARK asked for before that is what it is made in. */
class CaliforniaDrive final : public redpaw::Drive
{
public:
    /** The cabin's projection, as cabin.html hands it to the prototype. */
    static constexpr double tanH = 18.0 / 16.0, shear = 0.0;
    static constexpr double pitchOffset = -3.0 * CaliforniaWorld::pi / 180;   // the render's 2 degrees down, made 5

    /** With the cabin's art (CaliforniaGl::Art; none draws no cabin and the finds as placeholders). The dashboard is
        looked at once here, for where its window is (glassAt): call on the message thread, before the windscreen. */
    explicit CaliforniaDrive (CaliforniaGl::Art art = {})
    {
        if (art.dashboard)
        {
            const auto dash = art.dashboard();
            if (dash.valid())
                glass = CaliforniaWorld::Glass::of (dash.rgba.data(), dash.width, dash.height, 4);
        }
        gl.setArt (std::move (art));
        gl.setLens (tanH, shear);
    }

    CaliforniaWorld& california() noexcept { return world; }

    const char* name() const noexcept override { return "California"; }
    int numGears() const noexcept override     { return CaliforniaWorld::numGears; }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool) override            {}    // the Orange has no TURBO
    void setSteer (float amount) override    { world.setSteer (amount); }
    void setPark (bool on) override          { world.setParked (on); }

    /** THE GUITAR, heard: the amp's input's peak since the last step, made a level between what counts as sounding
        and its top (worlds::Play's floorDb..topDb); a jump in it is an attack, as UnderwaterDrive hears one: over
        0.28 of the span, 0.14 s after the last, its strength the jump's. */
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
        const bool changed = std::clamp (g, 1, CaliforniaWorld::numGears) != world.gear();
        setGear (g);
        if (! world.isBuilt())
            made();
        else if (changed)
            world.settle();
        if (world.state().time <= 0)
            world.settleCamera();
    }

    /** The van put at the point of the loop nearest (x, z) — the loop's angle round its centre — on the lap it is
        on; the heading is the road's (the van drives itself one way round). */
    void placeAt (double x, double z, double /*heading*/) override
    {
        const double th = std::atan2 (z - CaliforniaWorld::loopCentre[1], x - CaliforniaWorld::loopCentre[0]);
        placeOnLoop (CaliforniaWorld::sOfTheta (th), -1);
    }
    /** The van put at arclength `sr` round the loop on lap `lap` (< 0: the lap it is on; the finds' places follow
        the lap), the world made first if it is not: the prototype's setAt (its ?s=&lap=), the gear kept. */
    void placeOnLoop (double sr, int lap)
    {
        made();
        world.setAt (sr, lap);
    }

    /** The world made and settled in the gear and PARK asked for, as the prototype's view makes it — the camera left
        where the world's first steps take it (no settleCamera): what a drive run on from here, as the prototype's
        ?t= runs it, starts from. */
    void make() { made(); }
    /** The prototype's ?off= (CaliforniaGl::setSkip): draws left out, for timing the rest. */
    void setSkip (const std::string& list) { gl.setSkip (list); }

    float speed() const noexcept override { return (float) world.state().speed; }
    float speedFor (int g, bool) const noexcept override
    {
        return (float) CaliforniaWorld::gearSpeed[(size_t) (std::clamp (g, 1, CaliforniaWorld::numGears) - 1)];
    }
    Ease ease() const noexcept override { return { CaliforniaWorld::speedSeconds }; }
    /** The 1993 look: Mars's, the nearest of Mars1993's to golden hills under an orange sun. */
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
    /** The window, where the dashboard's alpha is low; everywhere when no dashboard was given. Any thread. */
    bool glassAt (float x01, float y01) const override           { return glass.open (x01, y01); }

private:
    static constexpr double onsetJump = 0.28, onsetGap = 0.14, onsetSeconds = 0.06;
    bool lit = false;
    float heard = 0.0f;
    double levelLP = 0.0, time = 0.0, lastOnset = -10.0;
    CaliforniaWorld::Glass glass;       // made once, read only after
    CaliforniaWorld world;
    CaliforniaGl gl { world };

    /** The world made and settled, once: what the prototype's view does as it starts. */
    void made()
    {
        if (world.isBuilt())
            return;
        // build() makes the world afresh, as the prototype's makeWorld does — CLEAN, not parked — so the gear and
        // PARK asked for before are handed to it again, as the prototype's view hands them, and then settled.
        const int g = world.gear();
        const bool parked = world.isParked();
        world.build();
        world.setGear (g);
        world.setParked (parked);
        world.settle();
        world.setPitchOffset (pitchOffset);
    }
};

} // namespace orbitamp::california
