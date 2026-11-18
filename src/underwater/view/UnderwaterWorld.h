// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
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
    /** THE THINGS ON THE FLOOR, as the shaders place them (rock.vert, worm.vert, flora.vert): one at most in each cell
        of a lattice snapped to the world, in a window of w x w cells ahead of the camera, kept or left empty by the
        cell's hash against the field's share (prob) times what the floor's textures make of the place, sized between
        smin and smax. The rocks, the spires, the worms, and the five flora (fan, bush, anemone, kelp, polyp). */
    enum FieldId { rockField, spireField, wormField, fanField, bushField, anemoneField, kelpField, polypField, numFields };
    struct FieldSpec { double cell; int w; double prob, smin, smax; };
    static constexpr std::array<FieldSpec, numFields> fieldSpecs { {
        { 3.0,  36, 0.16, 0.25, 1.3 },
        { 14.0, 16, 0.22, 2.5,  7.0 },
        { 1.1,  56, 0.35, 0.4,  2.8 },
        { 2.2,  36, 0.50, 0.4,  1.1 },
        { 2.6,  30, 0.40, 0.5,  1.3 },
        { 1.6,  40, 0.55, 0.35, 0.9 },
        { 1.0,  56, 0.40, 1.2,  3.0 },
        { 0.7,  64, 0.50, 0.15, 0.4 },
    } };
    /** A cell that may hold something: the shaders' cell (cij), where the thing stands (world xz, metres), and the
        floor's height there, the least and the most it can read however the GPU filters the textures; whether it is
        of the half that thins out far off at the foot (worms and flora, by the cell's hash: Quality::gone). */
    struct Cell { int32_t x = 0, z = 0; double cx = 0, cz = 0, lo = 0, hi = 0; bool thins = false; };
    /** Whether the shaders may draw something in this cell — false only where they leave it empty however the
        textures under it are filtered (each channel read as the least and the most of the texels round the sample,
        a texel further each way than the filter reaches) — and if so, the cell. */
    bool fieldCell (int id, int32_t x, int32_t z, Cell& out) const noexcept;
    /** How far a thing of a field reaches from where it stands, metres: across, under the floor and over it, at its
        largest and however it sways or curls (the shaders' bounds, with room to spare). */
    struct Reach { double across = 0, down = 0, up = 0; };
    static Reach fieldReach (int id) noexcept;
    /** The floor's height anywhere, the least and the most (floorH over the textures' extremes), metres. */
    std::array<double, 2> floorRange() const noexcept;

    /** THE CELLS OF A FIELD'S WINDOW THAT MAY HOLD SOMETHING: w x w from (bx, bz), row by row as the shaders number
        the window's instances, those fieldCell keeps. Each cell is worked out once while it stays in the window. */
    class FieldWindow
    {
    public:
        void update (const UnderwaterWorld& world, int id, int bx, int bz);
        const std::vector<Cell>& cells() const noexcept { return kept; }
        /** How many cells the last update worked out (the rest it had). */
        int worked() const noexcept { return workedOut; }
    private:
        struct Slot { int32_t x = 0, z = 0; bool known = false, holds = false; Cell cell; };
        std::vector<Slot> slots;
        std::vector<Cell> kept;
        int field = -1, baseX = 0, baseZ = 0, workedOut = 0;
    };

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
        int id = 0;                 // which it is, as made: the list is sorted far to near every frame
    };
    /** How far a jellyfish reaches from where it is, metres: its bell and its tendrils however they lie (the shaders'
        bell and tendrilPoint at their widest, the ribbons' width), so one farther than this from the view is not in it. */
    static double jellyReach (const Jelly& j) noexcept { return j.scale * (1.5 + 3.2 * j.tend); }

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
    /** The camera evaluated directly where the vehicle stands, as the first steps would leave it settled: its height
        over the floor, its pitch with the offset and the slope, the curious jellyfish looking where it looks. For a
        world that has not stepped yet (a poster, a site's first frame): unstepped, the camera is at height 0, under
        the floor, looking up at it. A world that has stepped keeps its own. */
    void settleCamera() noexcept;
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

    /** WHERE THE CABIN'S PANEL HIDES THE VIEW, AND WHERE IT LEAVES IT BARE: the dashboard's alpha in square blocks,
        each one COVERED — every pixel of it, and of a margin round it, opaque (255): the view under it is never seen,
        however the panel's picture or the view is filtered near it — OPEN — every pixel of it and of its margin 0 or
        1, under what the panel pass draws (0.004): the panel adds nothing there — or neither. Rows top first. */
    struct PanelMask
    {
        static constexpr uint8_t neither = 0, covered = 1, open = 2;
        int width = 0, height = 0;          // in blocks
        int block = 0, coverMargin = 0, openMargin = 0;     // in the picture's pixels
        int pictureWidth = 0, pictureHeight = 0;
        std::vector<uint8_t> cells;
        /** Made from straight RGBA rows, top row first; each margin counted in whole blocks, rounded up. */
        static PanelMask of (const uint8_t* rgba, int w, int h, int block, int coverMargin, int openMargin);
        uint8_t at (int bx, int by) const noexcept { return cells.empty() ? neither : cells[(size_t) (std::clamp (by, 0, height - 1) * width + std::clamp (bx, 0, width - 1))]; }
    };

    /** WHAT THE VIEW DRAWS LESS OF A RUNG BELOW THE TOP, as the windscreen's ladder steps the render scale down (1,
        0.75, 0.5): at the top everything; under it (level 1) the cabin's panel flat — the dashboard alone, unmoved by
        the water — the light shafts marched at the scene's pixels rather than the frame's, and the floor drawn after
        what stands on it; at the foot (level 2, 0.5 and under) besides: half the jellyfish, half the marine snow, the
        silt and the bubbles, half the worms and the flora beyond 25 m (shrinking away by 40 m), and the floor's
        lattice coarse beyond 45 m (its odd vertices sliding onto the even ones from 30 m). What goes fades out over
        fadeSeconds from the frame the rung changes, and back in as it climbs: never a jump; what goes far off goes
        from beyond the view inward. The first frame starts where its rung is. */
    struct Quality
    {
        static constexpr double fadeSeconds = 1.5;
        double flat = 0, thin = 0;      // 0..1: how flat the panel is drawn, how far the thinned jellyfish are gone
        int level = 0;
        bool started = false;
        static int levelOf (double scale) noexcept { return scale > 0.99 ? 0 : scale > 0.74 ? 1 : 2; }
        /** The jellyfish a thinned view leaves out, by which it is: every second one as made, so the half that goes
            is any half, near and far, and stays the same half as they move. */
        static bool thinned (int id) noexcept { return (id & 1) != 0; }
        /** Where what thins out with distance at the foot is gone by, metres from the camera across the floor: `at`
            once thin is 1, and 250 m further off for each whole of thin still to come — beyond the whole view while
            it is 0, so it comes in from out of sight. */
        double gone (double at) const noexcept { return at + (1 - thin) * 250.0; }
        /** Toward the rung of this scale, `seconds` after the last frame. */
        void follow (double scale, double seconds) noexcept
        {
            level = levelOf (scale);
            const double toFlat = level >= 1 ? 1 : 0, toThin = level >= 2 ? 1 : 0;
            const double d = started ? std::max (0.0, seconds) / fadeSeconds : 1.0;
            started = true;
            flat += std::clamp (toFlat - flat, -d, d);
            thin += std::clamp (toThin - thin, -d, d);
        }
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
