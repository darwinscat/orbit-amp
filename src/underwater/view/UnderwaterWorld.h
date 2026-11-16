// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace orbitamp::underwater
{

/** THE SEA FLOOR'S WORLD, THE CPU'S HALF. The bottom of an ocean on a planet around another star, seen from the
    bathyscaphe DC-744's sphere 1.7 m over the sand: the textures (a 256 x 256 RGBA float fbm of the swell, the
    mounds' mask, the dunes and the ridged mounds; the sand's ripples and grit as a detail normal map; the
    caustics' two Voronoi filament nets, their warp and the shafts' blobs), the meshes (a lumpy icosphere for the
    rocks and the spires, the worms' tube, the five flora, the jellyfish's bell grid and tendril ribbons), and the
    drive: the gears' crawl, the meander, the camera over the floor, PARK, the jellyfish (a hundred and twenty
    anchored in a box the camera wraps through, flinching, curious when parked), another bathyscaphe passing now
    and then, and an attack of the playing (the hit that lights the living things).

    A port of the view's prototype, sea-gl.js's makeWorld and the makers above it, matched to it number for number:
    the same doubles where the JS works in doubles, the same f32 rounding where it stores into a Float32Array or
    calls Math.fround, the same 32-bit integer hashes, and the JS's own Math functions (the walker's Js, V8's
    fdlibm). Build this file with -ffp-contract=off: a fused multiply-add rounds once where the JS rounds twice.

    Rendering is not in here. No JUCE in here. */
class UnderwaterWorld final
{
public:
    static constexpr double pi = 3.141592653589793;   // Math.PI

    // THE DRIVE.
    static constexpr int numGears = 3;
    static constexpr std::array<double, numGears> gearSpeed { 1.6, 3.6, 7.2 };   // m/s, a crawl along the bottom
    static constexpr double turboFactor = 2.0;         // the prototype's TURBO; the bass has none
    static constexpr double parkSeconds = 0.5, speedSeconds = 0.45;
    static constexpr double siltUp = 0.6, siltDown = 2.8;    // the silt cloud: raised fast, settles slowly
    static constexpr double flinchUp = 0.35, flinchDown = 3.0;
    static constexpr double curiousUp = 5.0, curiousDown = 2.0;
    static constexpr double thrustUp = 0.3, thrustDown = 0.4;
    static constexpr double camHeight = 1.7, parkedYawDegrees = 10.0;
    static constexpr double basePitch = -3.5 * pi / 180;
    static constexpr double worldPeriod = 30240.0;     // lcm of the layers' 420, 96, 27 m: the world wraps whole
    static constexpr int texSize = 256, detailSize = 256, causticSize = 256;
    static constexpr std::array<double, 3> start { 1200.0, 800.0, 0.35 };   // x, z, heading
    static constexpr double subAfter = 60.0, subFirst = 40.0;   // another bathyscaphe: the first, then a minute after the last
    static constexpr int jellyCount = 120;
    static constexpr double jellyBox = 120.0;
    static constexpr std::array<double, 3> snowBox { 44, 22, 44 }, siltBox { 0, 1.6, 16 }, bubbleBox { 0, 3.8, 12 };

    //==========================================================================
    /** The JS built-ins as the prototype's V8 computes them (WalkerWorld::Js), and the hashes the shaders carry. */
    static uint32_t ih (uint32_t x) noexcept;
    /** A cell's hash, 0..1, rounded to f32 as Math.fround rounds it (the value as a double). */
    static double hcell (int32_t cx, int32_t cy, int32_t s) noexcept;
    /** An index's hash under a seed, 0..1, f32 likewise. */
    static double hs (int32_t i, int32_t s) noexcept;

    /** The camera's right, up and forward for a heading and a pitch (positive = nose up). */
    using V3 = std::array<double, 3>;
    struct Basis { V3 r, u, f; };
    static Basis cameraBasis (double heading, double pitch) noexcept;
    static V3 normalised (const V3& v) noexcept;

    /** The textures: RGBA float (R swell, G mask, B dunes, A ridged mounds), the detail (RGBA8: the gradient in
        RG, the value in B), the caustics (RGBA8: two filament nets, a warp, the shafts' blobs) with the gain that
        makes the product of the nets average 1. */
    static std::vector<float> makeHeight();
    static std::vector<uint8_t> makeDetail();
    struct Caustic { std::vector<uint8_t> px; double gain = 1; };
    static Caustic makeCaustic();

    //==========================================================================
    /** THE MESHES, as the prototype makes them. A lumpy icosphere: 240 vertices of position + normal, flat faces. */
    static std::vector<float> icosphere (int seed);
    /** Indexed triangles over vertices of `stride` floats. */
    struct Mesh { std::vector<float> v; std::vector<uint16_t> idx; };
    /** A tube of `sides` around y, rings from y = 0 to 1: 3 floats a vertex. */
    static Mesh tubeMesh (int sides, int rings);
    /** The flora's five base meshes (fan, bush, anemone, kelp, polyp): 12 floats a vertex — position, normal,
        colour, (u, v), flag. */
    static std::array<Mesh, 5> floraMeshes();
    /** The bell's parameter grid (u around, v apex to rim): 2 floats a vertex. */
    static Mesh bellMesh (int segsU, int segsV);
    /** The tendrils: `count` ribbons of `segs` segments, vertices of (t, side, index). */
    static Mesh tendrilMesh (int count, int segs);

    //==========================================================================
    /** Another bathyscaphe, passing. */
    struct Sub
    {
        bool on = false;
        int n = -1;
        double x = 0, y = 0, z = 0, fx = 1, fz = 0, v = 0, v0 = 0, h = 0, t = 0, d = 1e9, vis = 0;
    };
    /** The state a frame is a pure function of; the JS world's `state`, field for field. */
    struct State
    {
        double x = start[0], z = start[1], heading = start[2], pitch = basePitch, camY = 0, travel = 0, time = 0;
        double speed = gearSpeed[0], silt = 0, flinch = 0, curious = 0, thrust = 0;
        std::array<double, 3> snowFlow {}, siltFlow {}, bubbleFlow {};
        double dt = 0, pitchOffset = 0;
        Sub sub;
        double lookHeading = start[2], hitT = 1e4, hitS = 0;
    };
    /** A jellyfish: its anchor and constants, and where the last updateJellies put it (relative to the camera). */
    struct Jelly
    {
        double ax = 0, az = 0, alt = 0, scale = 0;
        V3 col {}, glow {};
        double omega = 0, phase = 0;
        V3 axis {};
        double tend = 0, curiousW = 0;
        bool giant = false;
        double flinchK = 0, near = 0, side = 0, lift = 0, vx = 0, vz = 0;
        V3 rel {};
        double dist = 0;
        V3 outAxis { 0, 1, 0 };
    };

    UnderwaterWorld() noexcept = default;

    /** Where the drive starts, instead of the world's own start (call before build()). */
    void startAt (double x, double z, double heading) noexcept;
    /** Makes the textures and the jellyfish. Once, tens of milliseconds. */
    void build();
    bool isBuilt() const noexcept { return built; }

    const std::vector<float>& height() const noexcept { return heightPx; }
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }
    const std::vector<uint8_t>& caustic() const noexcept { return caustic_.px; }
    double causticGain() const noexcept { return caustic_.gain; }

    /** One step of the drive (the jellyfish are moved by updateJellies, once a frame drawn, as in the prototype). */
    void step (double dt);
    /** The jellyfish for this frame: positions relative to the camera, sorted far to near. */
    void updateJellies (double dt);
    const std::vector<Jelly>& jellies() const noexcept { return jelly; }

    /** The floor's height, the mirror of the shaders': metres at absolute world xz (0 before build()). */
    double floorHeight (double px, double pz) const noexcept;

    void setGear (int g) noexcept;                     // 1..3
    void setTurbo (bool on) noexcept { turbo = on; }
    void setParked (bool on) noexcept { parked = on; }
    void setSteer (double v) noexcept;                 // -1..1
    /** The speed where the gear (or PARK) is taking it, at once. */
    void settle() noexcept;
    /** When the next bathyscaphe comes, the world's time in seconds; one passing now is called off. */
    void setPassFirst (double atTime) noexcept { subNext = atTime; s.sub.on = false; }
    /** One attack of the playing, 0..1: the living things flash and glow, near ones first. */
    void hit (double level) noexcept;
    /** No answer at all: an attack's light called off (not lit by the playing). */
    void quiet() noexcept { s.hitT = 1e4; s.hitS = 0; }
    /** A look down (negative) or up, radians, added to the pitch. */
    void setPitchOffset (double rad) noexcept;

    int gear() const noexcept { return gearWanted; }
    bool isTurbo() const noexcept { return turbo; }
    bool isParked() const noexcept { return parked; }
    double speedFor (int g, bool t) const noexcept;
    const State& state() const noexcept { return s; }

    //==========================================================================
    /** THE PASSING BATHYSCAPHE'S SPRITES, as sprites.txt has them (plain text): the cell size and the scale, the
        atlas's grid, and per cell (heading) its lamps — the lens on the cell, the beam on the screen, how much it
        points at the viewer. */
    struct Lamp { double x = 0, y = 0, dx = 0, dy = 0, toward = 0; };
    struct Sprites
    {
        std::array<double, 2> cell { 1024, 576 };
        double scale = 0;                               // px per metre; 0 = the table said none
        std::array<int, 2> grid { 4, 4 };
        std::vector<std::vector<Lamp>> cells;
    };
    /** The prototype's reading of sprites.txt, line for line. */
    static Sprites parseSprites (std::string_view text);

    /** WHERE THE CABIN'S WINDOW IS: the dashboard's alpha, every `step`-th pixel, for a press to know whether it
        fell on the open window (alpha low) or on the panel's metal. */
    struct Glass
    {
        int width = 0, height = 0;
        std::vector<uint8_t> alpha;
        /** Made from straight RGBA rows, top row first, keeping every `step`-th pixel's alpha. */
        static Glass of (const uint8_t* rgba, int w, int h, int step);
        /** Whether (x, y), 0..1 from the top-left, is open window: true where nothing was made. */
        bool open (float x01, float y01) const noexcept;
    };

private:
    bool built = false;
    std::vector<float> heightPx;
    std::vector<uint8_t> detailPx;
    Caustic caustic_;
    State s;
    std::vector<Jelly> jelly;
    int gearWanted = 1;
    bool turbo = false, parked = false;
    double steer = 0, yawRate = 0, lastSteerTime = -10, subNext = subFirst;
    int subCount = 0;

    double tap (int ch, double u, double v) const noexcept;
    void makeJellies();
};

} // namespace orbitamp::underwater
