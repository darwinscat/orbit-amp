// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Drive.h"
#include "PlainsGl.h"
#include "PlainsWorld.h"

#include <cmath>
#include <string>

namespace orbitamp::redpaw
{

/** THE PLAINS AS A DRIVE: PlainsWorld and PlainsGl behind the windscreen's face. What the windscreen
    was built around: `Windscreen (std::make_unique<PlainsDrive> (gears))`. */
class PlainsDrive final : public Drive
{
public:
    /** In the product's gears, 1..PlainsWorld::maxGears. */
    explicit PlainsDrive (int gears) : world (gears) {}

    PlainsWorld& plains() noexcept { return world; }

    const char* name() const noexcept override { return "Plains"; }
    int numGears() const noexcept override     { return world.numGears(); }

    void setGear (int g) override            { world.setGear (g); }
    void setTurbo (bool on) override         { world.setTurbo (on); }
    void setSteer (float amount) override    { world.setSteer (amount); }
    void hear (float peak) override          { world.hear (peak); }
    void step (double dt) override           { world.step (dt); }

    /** A still is clear air, as the drive would be once settled. */
    void settle (int g, bool t) override
    {
        world.place (world.state().x, world.state().z, world.state().heading, g, t);
    }

    void placeAt (double x, double z, double heading) override
    {
        world.build();
        world.place (x, z, heading, world.gear(), world.isTurbo());
    }

    float speed() const noexcept override                        { return world.state().speed; }
    float speedFor (int g, bool t) const noexcept override       { return world.speedFor (g, t); }
    Ease ease() const noexcept override                          { return { PlainsWorld::speedSeconds }; }
    Mars1993::Look look1993() const noexcept override            { return Mars1993::Look::mars; }

    std::string landmarks (double metres) const override
    {
        static const char* const kinds[] { "rover", "small-rover", "lander", "winged-rover" };
        std::vector<PlainsWorld::Relic> found;
        world.relicsNear (world.state().x, world.state().z, (int) std::ceil (metres / PlainsWorld::relicCell), found);
        std::string out;
        for (const auto& r : found)
        {
            const double dx = r.x - world.state().x, dz = r.z - world.state().z, d = std::hypot (dx, dz);
            if (d <= metres)
                out += std::string (kinds[r.variant]) + " " + std::to_string (r.x) + " " + std::to_string (r.z) + " " + std::to_string (dx) + " " + std::to_string (dz) + " " + std::to_string (d) + "\n";
        }
        return out;
    }

    bool create() override                                       { return gl.create(); }
    std::string error() const override                           { return gl.error(); }
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
