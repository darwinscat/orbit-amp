// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Drive.h"
#include "PlainsGl.h"
#include "PlainsWorld.h"

namespace orbitamp::redpaw
{

/** THE PLAINS AS A DRIVE: PlainsWorld and PlainsGl behind the windscreen's face. What the windscreen
    was built around, and what it hosts when given nothing else. */
class PlainsDrive final : public Drive
{
public:
    PlainsWorld& plains() noexcept { return world; }

    const char* name() const noexcept override { return "Plains"; }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool on) override         { world.setTurbo (on); }
    void setParked (bool on) override        { world.setParked (on); }
    void setSteer (float amount) override    { world.setSteer (amount); }
    void step (double dt) override           { world.step (dt); }

    /** A still is clear air, as the drive would be once settled: the Plains' place takes no PARK. */
    void settle (int g, bool t, bool) override
    {
        world.place (world.state().x, world.state().z, world.state().heading, g, t);
    }

    void placeAt (double x, double z, double heading) override
    {
        world.build();
        world.place (x, z, heading, world.gear(), world.isTurbo());
    }

    float speed() const noexcept override                        { return world.state().speed; }
    float speedFor (int g, bool t) const noexcept override       { return PlainsWorld::speedFor (g, t); }
    Mars1993::Look look1993() const noexcept override            { return Mars1993::Look::mars; }

    bool create() override                                       { return gl.create(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }

private:
    PlainsWorld world;
    PlainsGl gl { world };
};

} // namespace orbitamp::redpaw
