// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IceGl.h"
#include "IceWorld.h"
#include "../../redpaw/view/Drive.h"

namespace orbitamp::frostpaw
{

/** THE ICE AS A DRIVE: IceWorld and IceGl behind the windscreen's face. A product passes one to the
    windscreen — `Windscreen (std::make_unique<frostpaw::IceDrive>())` — and the glass looks out on
    the ice moon; the 1993 landscape wears the ice's colours. */
class IceDrive final : public redpaw::Drive
{
public:
    IceWorld& ice() noexcept { return world; }

    const char* name() const noexcept override { return "Ice"; }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool on) override         { world.setTurbo (on); }
    void setParked (bool on) override        { world.setParked (on); }
    void setSteer (float amount) override    { world.setSteer (amount); }
    void step (double dt) override           { world.step (dt); }

    /** A still is the gear's own weather: parked, a drift of flakes and a standing rover. */
    void settle (int g, bool t, bool p) override
    {
        world.place (world.state().x, world.state().z, world.state().heading, g, t, p);
    }

    void placeAt (double x, double z, double heading) override
    {
        world.build();
        world.place (x, z, heading, world.gear(), world.isTurbo(), world.isParked());
    }

    void setClock (double seconds, double metresDriven) override { world.setClock (seconds, metresDriven); }

    float speed() const noexcept override                        { return world.state().speed; }
    float speedFor (int g, bool t) const noexcept override       { return IceWorld::speedFor (g, t); }
    redpaw::Mars1993::Look look1993() const noexcept override    { return redpaw::Mars1993::Look::ice; }

    bool create() override                                       { return gl.create(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }

private:
    IceWorld world;
    IceGl gl { world };
};

} // namespace orbitamp::frostpaw
