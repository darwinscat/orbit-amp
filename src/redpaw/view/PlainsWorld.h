// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbitamp::redpaw
{

/** THE PLAINS, THE CPU'S HALF. The landscape the GPU rasterizes is a height function of one wrapping
    1024 x 1024 four-channel plasma texture tapped at three incommensurate periods (7000 m, 1250 m,
    190 m) — mountains, a swell, hills, mesas, canyons, dunes and hashed craters — over a world that
    repeats every 665 km, so the drive is endless and exact. This side makes the textures once (the
    plasma, its two gradient textures and a detail texture), keeps a mirror of the height function
    for the camera, and drives: integrates the rover in double precision, eases the gear, steers,
    meanders when let go, and low-passes the camera's height and pitch so a climb never fills the
    windscreen with dirt.

    The height function here and the one in the shaders must agree, or the camera sinks into hills:
    the constants are the same numbers, written twice. No JUCE in here.

    THE RELICS: old machines left on the plains — a car-sized rover of six wheels on rocker-bogies
    with a mast and a power unit at the back, a knee-high rover under a flat solar deck, a lander open
    on its petals among its slack airbags, a rover with wing-like solar panels — one in about every
    other 532 m cell, by a hash, never on a mountain, a mesa or in a canyon: a world fact, so one passed
    is still there when the rover turns back, and rare enough that meeting one is an event. The drive
    is pushed around any relic ahead and never passes through one. */
class PlainsWorld final
{
public:
    static constexpr int texSize = 1024;         // the height texture, wrapping
    static constexpr int detailSize = 256;
    static constexpr double worldPeriod = 665000.0;   // metres: lcm of the layer periods and the cells
    static constexpr int numGears = 3;                // CLEAN, CRUNCH, HI-GAIN
    static constexpr std::array<float, numGears> gearSpeed { 2.4f, 6.2f, 13.5f };   // m/s, pending the owner's word
    static constexpr float turboFactor = 2.0f;        // TURBO doubles the gear's speed
    /** The dust TURBO raises on each gear, 0..1: a light haze with sparse specks, clearly more, the full
        head-on storm. Without TURBO the air is clear on every gear. */
    static constexpr std::array<float, numGears> turboDust { 0.25f, 0.6f, 1.0f };
    static constexpr double dustEaseSeconds = 0.7;    // tau: about 2 s to arrive or clear, never a jump
    static constexpr double parkSeconds = 0.5;        // tau: parked, the rover is standing in about 1.5 s
    static constexpr float parkedYawDegrees = 8.0f;   // parked, the hand turns the view in place, slowly

    /** The relics: one per hashed cell where the hash and the ground allow. */
    static constexpr double relicCell = 532.0;        // metres; WORLD / relicCell = 1250
    static constexpr float relicProbability = 0.65f;  // of a cell, before the ground's say: about 1.4 a km^2 stand, one met every 2-3 km
    static constexpr int relicWindow = 4;             // cells across the renderer's window (2.1 km, 1.7 of it ahead: beyond that a relic is a speck)
    static constexpr int numRelics = 4;               // the big rover, the small rover, the lander, the winged rover

    struct Relic
    {
        double x = 0, z = 0;       // where it stands, absolute world metres (unwrapped: the cell's own)
        float radius = 0;          // its footprint
        int variant = 0;
        int ci = 0, cj = 0;        // the unwrapped cell
    };

    /** Makes the textures. Once, tens of milliseconds. */
    void build();
    bool isBuilt() const noexcept { return built; }

    /** The height texture: RGBA float, level 0 at texSize^2, then its 2 x 2 box mips down to 1 x 1. */
    const std::vector<std::vector<float>>& heightMips() const noexcept { return mips; }
    /** The two gradient textures (dR dR dB dB) and (dG dG dA dA), RGBA8 texSize^2, and their scales. */
    const std::vector<uint8_t>& gradient1() const noexcept { return grad1; }
    const std::vector<uint8_t>& gradient2() const noexcept { return grad2; }
    std::array<float, 2> gradientScale1() const noexcept { return { gmax[0], gmax[1] }; }
    std::array<float, 2> gradientScale2() const noexcept { return { gmax[2], gmax[3] }; }
    /** The detail texture: gradient in RG, value in B, RGBA8 detailSize^2. */
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }

    //==========================================================================
    /** The drive, the inputs. */
    void setGear (int g) noexcept;                       // 1..3
    void setTurbo (bool on) noexcept { turbo = on; }
    /** PARK: the rover comes to a stand, the dust settles, the hand only turns the view in place. */
    void setParked (bool on) noexcept { parked = on; }
    void setSteer (float s) noexcept;                    // -1..1, held input
    int gear() const noexcept { return gearWanted; }
    bool isTurbo() const noexcept { return turbo; }
    bool isParked() const noexcept { return parked; }

    /** The speed the gear and TURBO ask for, before easing. */
    static float speedFor (int gear, bool turbo) noexcept;
    /** The dust the gear and TURBO ask for, before easing. */
    static float dustFor (int gear, bool turbo) noexcept;

    /** One step of the drive. */
    void step (double dt);

    /** Puts the rover somewhere with no history: speed and gear reached, camera and pitch evaluated
        directly. For a still, and for the start. */
    void place (double x, double z, double heading, int gear, bool turbo);

    /** The state a frame is a pure function of. */
    struct State
    {
        double x = 31500, z = 3000;      // metres, wrapped to the world
        double heading = 0.0;            // radians, 0 = -z
        double pitch = 0.0;              // radians, positive = nose up
        double camY = 0.0;               // metres, absolute
        double travel = 0.0;             // metres driven: only the dust streaks use it
        double time = 0.0;               // seconds: the moons drift by it
        float speed = gearSpeed[0];      // m/s, eased
        float storm = 0.0f;              // 0..1, eased: the dust, TURBO's and graded by the gear
    };
    const State& state() const noexcept { return s; }

    /** The height function, the mirror of the shaders': metres at absolute world xz. `dist` is the
        distance from the rover, which fades the dunes out. */
    float terrainHeight (double px, double pz, float dist) const noexcept;

    /** The relic of an unwrapped cell, if the hash puts one there and the ground takes it. */
    bool relicAt (int ci, int cj, Relic& out) const noexcept;
    /** Every relic within `cells` cells of (x, z). */
    void relicsNear (double x, double z, int cells, std::vector<Relic>& out) const;
    /** The renderer's instance lists: per variant, the unwrapped (i, j) of every relic in the
        relicWindow x relicWindow cells from (bx, bz), as float pairs. */
    void relicsInWindow (int bx, int bz, std::array<std::vector<float>, numRelics>& cells) const;

private:
    bool built = false;
    std::vector<float> height;            // level 0, RGBA
    std::vector<std::vector<float>> mips;
    std::vector<uint8_t> grad1, grad2, detailPx;
    std::array<float, 4> gmax { 1, 1, 1, 1 };

    State s;
    int gearWanted = 1;
    bool turbo = false, parked = false;
    float steer = 0.0f;
    double yawRate = 0.0, lastSteerTime = -10.0, meanderPhase = 0.0;
    std::vector<Relic> near;              // scratch for the step's two scans

    float tap (int channel, double u, double v) const noexcept;   // bilinear, wrapping, as GL_LINEAR + REPEAT
    double avoidRelics (double fx, double fz);
};

} // namespace orbitamp::redpaw
