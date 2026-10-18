// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "TitanGl.h"
#include "TitanWorld.h"
#include "../../redpaw/view/Drive.h"

#include <cmath>
#include <string>

namespace orbitamp::titan
{

/** THE METHANE SEA AS A DRIVE: TitanWorld and TitanGl behind the windscreen's face. A product passes one
    to the windscreen — `Windscreen (std::make_unique<titan::TitanDrive> (gears))` — and the glass is the
    cabin's whole canopy, 1672 x 418, looking out over Titan's sea from a helicopter flying low; the 1993
    landscape wears Titan's colours. */
class TitanDrive final : public redpaw::Drive
{
public:
    /** In the product's gears, 1..TitanWorld::maxGears. */
    explicit TitanDrive (int gears) : world (gears) {}

    TitanWorld& titan() noexcept { return world; }

    const char* name() const noexcept override { return "Titan"; }
    int numGears() const noexcept override     { return world.numGears(); }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool on) override         { world.setTurbo (on); }
    void setParked (bool on) override        { world.setParked (on); }
    void setSteer (float amount) override    { world.setSteer (amount); }
    void setWipers (int speed) override      { world.setWipers (speed); }
    void step (double dt) override           { world.step (dt); }

    /** A still keeps what is on the glass and out on the sea; the flight is evaluated where it stands. */
    void settle (int g, bool t, bool p) override { world.build(); world.settle (g, t, p); }

    void placeAt (double x, double z, double heading) override
    {
        world.build();
        world.place (x, z, heading, world.gear(), world.isTurbo(), world.isParked());
    }

    void setClock (double seconds, double metresDriven) override { world.build(); world.setClock (seconds, metresDriven); }

    float speed() const noexcept override                        { return (float) world.state().speed; }
    float speedFor (int g, bool t) const noexcept override       { return (float) world.speedFor (g, t); }
    redpaw::Mars1993::Look look1993() const noexcept override    { return redpaw::Mars1993::Look::titan; }

    /** The buoys and the nitrogen fields out on the sea. */
    std::string landmarks (double metres) const override
    {
        static const char* const kinds[] { "buoy-red", "buoy-green", "buoy-amber" };
        const auto& st = world.state();
        std::string out;
        const auto line = [&] (const char* kind, double x, double z)
        {
            const double dx = x - st.x, dz = z - st.z, d = std::hypot (dx, dz);
            if (d <= metres)
                out += std::string (kind) + " " + std::to_string (x) + " " + std::to_string (z) + " " + std::to_string (dx) + " " + std::to_string (dz) + " " + std::to_string (d) + "\n";
        };
        for (const auto& b : world.buoys()) line (kinds[b.type], b.x, b.z);
        for (const auto& f : world.fields()) line ("field", f.x, f.z);
        return out;
    }

    bool create() override                                       { return gl.create(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }

private:
    TitanWorld world;
    TitanGl gl { world };
};

} // namespace orbitamp::titan
