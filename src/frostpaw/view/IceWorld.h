// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbitamp::frostpaw
{

/** THE ICE, THE CPU'S HALF. An ice moon's crust: a height function of one wrapping 1024 x 1024
    four-channel plasma texture tapped at four periods (7000 m, 1250 m, 190 m, 50 m) — a swell, rolling
    snow slopes, broad uplands, frost dunes near the rover — plus what is not in a texture: two families
    of LINEAE (long, gently bent double ridges, each line hashed for its height and width, fading in and
    out along its length), CHAOS terrain (hashed 14 m grids of tilted ice blocks inside patches of the
    1250 m layer, within 900 m), and a snow drift at the foot of every STEEL STRUCTURE (one per hashed
    70 m cell, in groups and singly). The world repeats every 665 km, so the drive is endless and exact.

    This side makes the textures once (the plasma, its two gradient textures and a wind-stretched
    detail texture), keeps a mirror of the height function for the camera and the structures' feet,
    enumerates the steel for the renderer's instance lists, and drives: integrates the rover in double
    precision, eases the gear, steers, meanders when let go, leans away from broken ice, is pushed
    around any structure ahead and never passes through one, and low-passes the camera's height and
    pitch. The snow is the drive's weather: none in first gear, a light fall and more up the gears,
    TURBO up to the blizzard, a drift of flakes when parked.

    The height function here and the one in the shaders must agree, or the camera sinks into the
    slopes: the constants are the same numbers, written twice. The shaders' height also falls away
    with the planet's curvature, which is camera-relative and 0 at the rover, so it is not here.
    No JUCE in here. */
class IceWorld final
{
public:
    static constexpr int texSize = 1024;         // the height texture, wrapping
    static constexpr int detailSize = 256;
    static constexpr double worldPeriod = 665000.0;   // metres: lcm of the layer periods and the cells
    /** THE GEARS ARE THE PRODUCT'S: as many as its lever has, at most three. The speeds go up from the
        first gear, whatever the count; the weather comes down from the top one. */
    static constexpr int maxGears = 3;
    static constexpr std::array<float, maxGears> gearSpeed { 2.4f, 6.2f, 13.5f };   // m/s, the first gear first
    static constexpr float turboFactor = 2.0f;        // TURBO doubles the gear's speed
    /** The snow each gear drives through, 0..1 — on three gears clear air, a light fall, more; and with
        TURBO, up to the blizzard. Graded down from the top: a product of fewer gears has the top ones,
        so its top gear with TURBO is always the blizzard. Parked, a light drift of flakes. */
    static constexpr std::array<float, maxGears> gearSnow { 0.0f, 0.25f, 0.45f };
    static constexpr std::array<float, maxGears> turboSnow { 0.45f, 0.72f, 1.0f };
    static constexpr float parkedSnow = 0.12f;
    static constexpr double snowEaseSeconds = 0.7;    // tau: about 2 s to thicken or clear, never a jump
    static constexpr double parkSeconds = 0.5;        // tau: parked, the rover is standing in about 1.5 s
    static constexpr double speedSeconds = 0.45;      // tau: the gear's speed reached
    static constexpr float parkedYawDegrees = 8.0f;   // parked, the hand turns the view in place, slowly
    static constexpr float steerYawDegrees = 25.0f;   // the hand's full lock, degrees a second

    /** The steel: one structure per hashed cell, a 280 m super-cell deciding a group or sparse ground. */
    static constexpr double structCell = 70.0;
    static constexpr int structWindow = 48;           // cells across the renderer's window (3.4 km)
    static constexpr float structProbIn = 0.30f, structProbOut = 0.035f;
    static constexpr int numVariants = 4;             // a lattice mast, a gantry, a dish on a tripod, a frame module
    static constexpr double chaosCell = 14.0;         // the ice blocks' grid
    static constexpr int snowCount = 2600;            // flakes in the camera-space box

    struct Structure
    {
        double x = 0, z = 0;       // the foot, absolute world metres (unwrapped: the cell's own)
        float radius = 0, scale = 1;
        int variant = 0;
        int ci = 0, cj = 0;        // the unwrapped cell
    };

    /** With the product's gears, 1..maxGears. */
    explicit IceWorld (int gears) noexcept;

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
    /** The detail texture: gradient in RG, value in B, RGBA8 detailSize^2; stretched along x, so its
        ripples run across the wind. */
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }

    //==========================================================================
    /** The drive, the inputs. */
    void setGear (int g) noexcept;                       // 1..numGears()
    void setTurbo (bool on) noexcept { turbo = on; }
    /** PARK: the rover comes to a stand, the snow thins to a drift, the hand only turns the view in place. */
    void setParked (bool on) noexcept { parked = on; }
    void setSteer (float s) noexcept;                    // -1..1, held input
    int gear() const noexcept { return gearWanted; }
    bool isTurbo() const noexcept { return turbo; }
    bool isParked() const noexcept { return parked; }
    int numGears() const noexcept { return gears; }

    /** The speed the gear and TURBO ask for, before easing. */
    float speedFor (int gear, bool turbo) const noexcept;
    /** The snow the gear, TURBO and PARK ask for, before easing. */
    float snowFor (int gear, bool turbo, bool parked) const noexcept;

    /** One step of the drive. */
    void step (double dt);

    /** Puts the rover somewhere with no history: speed, snow and gear reached, camera and pitch
        evaluated directly. For a still, and for the start. */
    void place (double x, double z, double heading, int gear, bool turbo, bool parked = false);

    /** The speed and the snow where the gear, TURBO and PARK are taking them, at once rather than eased to: the
        rover standing, or at its gear's speed, wherever it is. The site's view starts so, and is put so when
        asked for a gear or PARK "settled". */
    void settle() noexcept;

    /** The clock, for a still: `time` turns Jupiter and moves Io, `travel` sets the flakes' phase. */
    void setClock (double time, double travel) noexcept { s.time = time; s.travel = travel; }

    /** The state a frame is a pure function of. */
    struct State
    {
        double x = 31500, z = 3000;      // metres, wrapped to the world
        double heading = 0.0;            // radians, 0 = -z
        double pitch = 0.0;              // radians, positive = nose up
        double camY = 0.0;               // metres, absolute
        double travel = 0.0;             // metres driven: only the snow's depth cycles by it
        double time = 0.0;               // seconds: Jupiter turns, Io circles, the flakes fall by it
        float speed = gearSpeed[0];      // m/s, eased
        float snow = 0.0f;               // 0..1, eased: the fall, graded by the gear, TURBO's blizzard
        float avoidYaw = 0.0f;           // radians a second the steel ahead is pushing the heading by
    };
    const State& state() const noexcept { return s; }

    /** The height function, the mirror of the shaders': metres at absolute world xz. `dist` is the
        distance from the rover, which fades the dunes, the blocks and the drifts out. */
    float terrainHeight (double px, double pz, float dist) const noexcept;
    /** The chaos terrain's mask at absolute world xz, 0..1. */
    float chaosMask (double px, double pz) const noexcept;

    /** The structure of an unwrapped 70 m cell, if the hash puts one there — chaos or not. */
    bool structureAt (int ci, int cj, Structure& out) const noexcept;
    /** Every structure within `cells` cells of (x, z) that stands, i.e. not in chaos terrain. */
    void structuresNear (double x, double z, int cells, std::vector<Structure>& out) const;
    /** The renderer's instance lists: per variant, the unwrapped (i, j) of every standing structure
        in the structWindow x structWindow cells from (bx, bz), as float pairs. */
    void structuresInWindow (int bx, int bz, std::array<std::vector<float>, numVariants>& cells) const;

private:
    bool built = false;
    std::vector<float> height;            // level 0, RGBA
    std::vector<std::vector<float>> mips;
    std::vector<uint8_t> grad1, grad2, detailPx;
    std::array<float, 4> gmax { 1, 1, 1, 1 };

    State s;
    const int gears;
    int gearWanted = 1;
    bool turbo = false, parked = false;
    float steer = 0.0f;
    double yawRate = 0.0, lastSteerTime = -10.0, meanderPhase = 0.0;
    std::vector<Structure> near;          // scratch for the step's two scans

    float tap (int channel, double u, double v) const noexcept;   // bilinear, wrapping, as GL_LINEAR + REPEAT
    float structDrift (double px, double pz, float chaos) const noexcept;
    double avoidStructures (double fx, double fz);
};

} // namespace orbitamp::frostpaw
