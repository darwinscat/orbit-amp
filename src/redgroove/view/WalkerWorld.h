// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbitamp::redgroove
{

/** THE WALKER'S WORLD, THE CPU'S HALF. The glowing mushroom forest on the twilight belt of a red dwarf's
    planet, seen from a walker's dome 6.6 m up: the heightfield (a 512 x 512 RGBA float texture of
    value-noise fractals, 2 m a texel, wrapping every 1024 m, with its box mips and a 64 x 64 detail
    texture), the forest's placement (three classes of mushrooms, each a cell hash over the world — knee-
    high caps on 2 m cells, the forest on 8 m cells, towers on 205 cells across the world), the meshes
    (a mushroom of unit height in three shapes, the lathe for the walker's limbs, the ground's polar
    grid), and the walk: the step phase psi and the heave, sway, roll and nod it makes, the tempo lock
    that pulls the footfalls onto the beat, the two front legs with their two-bone IK, a burst of spores
    and a footprint at every footfall, and the drive that turns the walker along the free way among the
    stalks and the towers (a fan of headings, each a path it can really walk, scored by its free run; a
    shorter stride when the best way runs short; keep-clear rings that give when it is held fast; dead
    ends remembered for three minutes).

    A port of the view's prototype, walker-world.js, matched to it number for number (the site runs the
    JS itself, the plug-in this): the same doubles where the JS works in doubles, the same f32 rounding
    where it stores into a Float32Array or calls Math.fround, the same 32-bit integer hash, and the JS's
    own Math functions (Js below). Build this file with -ffp-contract=off: a fused multiply-add rounds once
    where the JS rounds twice.
    Rendering is not in here. No JUCE in here. */
class WalkerWorld final
{
public:
    static constexpr double pi = 3.141592653589793;   // Math.PI

    static constexpr double worldPeriod = 8192.0;     // the walker's position wraps here (m)
    static constexpr int heightTex = 512;             // the heightfield: 2 m a texel, 8 periods per worldPeriod
    static constexpr double heightPeriod = 1024.0;
    static constexpr int detailTex = 64;

    // THE WALKER. The body frame: x right, y up, z ahead; the origin on the ground under the dome.
    static constexpr double domeHeight = 6.6;         // the eye in the dome over the ground (m)
    static constexpr std::array<double, 3> hip { 2.1, 3.0, 0.6 };   // the right front hip (the left is mirrored in x)
    static constexpr double femur = 3.8, tibia = 5.2; // the two bones (m)
    static constexpr double ankle = 0.5;              // the shin ends this far over the foot's sole
    static constexpr double footSide = 2.7, footAhead = 5.2;   // where a front foot rests at mid-stance
    static constexpr double stance = 0.62;            // the part of a leg's cycle spent on the ground
    static constexpr double footR = 0.95;             // the foot pad's radius
    static constexpr int numGears = 3;
    static constexpr std::array<double, numGears> gearRate { 0.62, 1.0, 1.5 };   // footfalls a second, gear 1 first
    static constexpr std::array<double, numGears> gearStep { 2.9, 3.9, 5.2 };    // metres a footfall
    static constexpr double turboRate = 1.35, turboStep = 1.5;                    // TURBO: a long striding run, x2 the speed
    static constexpr std::array<double, numGears> beatSteps { 0.5, 1.0, 2.0 };   // the tempo lock: footfalls a beat
    static constexpr double paceSeconds = 0.45, stormEaseSeconds = 0.6, sporeEaseSeconds = 0.7;
    static constexpr std::array<double, numGears> gearSpores { 0.24, 0.32, 0.42 };   // the drifting spore cloud, per gear
    static constexpr std::array<double, numGears> turboSpores { 0.72, 0.86, 1.0 };   // TURBO: the storm
    static constexpr std::array<double, numGears> turboStorm { 0.55, 0.78, 1.0 };
    static constexpr double breathAttack = 0.40, breathRelease = 1.10;   // the low note's envelope
    static constexpr double hitFlash = 0.25, hitGlow = 1.5;              // an attack: the flash and the slower glow (s)
    static constexpr double sunAzimuth = 24.0 * pi / 180, sunElevation = 5.0 * pi / 180;
    static constexpr int maxBursts = 24, burstParticles = 96;          // spore bursts alive at once, particles each
    static constexpr int maxPrints = 12;                               // footprints the knee-high caps remember

    // THE DRIVE: a fan of headings, each a path it can really walk, looked along for its free run.
    static constexpr double lookSeconds = 4.0, lookMin = 25.0, lookMax = 70.0;   // how far it looks
    static constexpr double lookReach = 84.0;          // the stalks gathered around the walker reach this far (m)
    static constexpr int maxDiscs = 1024;
    static constexpr int lookFan = 36;                 // headings each side, out to a half-turn (5 degrees apart)
    static constexpr double lookMargin = 0.8;          // the roomy way keeps this much more than a keep-clear ring (m)
    static constexpr double lookTouch = 0.02;          // this close to a keep-clear ring, it stands against it (m)
    static constexpr double lookGraze = 0.25;          // standing against one, a turn may graze this much closer (m)
    static constexpr double lookSpot = 0.5;            // a turn on the spot counts half a free turn
    static constexpr double lookHold = 0.15;           // what leaving the last aim costs, a half-turn away
    static constexpr double freeKeen = 0.5, handKeen = 1.5;   // what turning from the wanted heading costs
    static constexpr double aimSeconds = 1.5;          // it turns toward its aim at (the offset / this) rad/s ...
    static constexpr double yawMax = 40.0 * pi / 180;  // ... at most this
    static constexpr double brakeSeconds = 2.0, brakeFrom = 0.25, brakeMin = 0.35;   // a shorter stride, to 35 %
    static constexpr double squeezeHeld = 0.3, squeezeFree = 0.6, squeezeAfter = 1.0;
    static constexpr double squeezeRate = 0.5, squeezeMax = 2.0;   // the rings give 0.5 m/s, up to 2 m
    static constexpr int maxDeadEnds = 8;
    static constexpr double deadEndR = 12.0, deadEndSeconds = 180.0;
    static constexpr double deadEndTurn = 75 * pi / 180, deadEndGap = 4.0;

    // THE FOREST: three classes, each a cell hash over the world; sizes are the whole height (m).
    // w = the window of cells kept around the walker (the instance list), m = cells per worldPeriod.
    struct MushroomClass { double cell; int w; double prob, smin, smax; int segs, m; };
    static constexpr int numClasses = 3;
    static constexpr std::array<MushroomClass, numClasses> classes { {
        { 2.0,        48, 0.50, 0.25, 1.05, 10, 4096 },   // knee
        { 8.0,        28, 0.52, 2.0,  7.6,  18, 1024 },   // forest
        { 8192.0 / 205, 14, 0.50, 9.0, 24.0, 26, 205 },   // tower: 205 cells tile worldPeriod exactly
    } };
    static constexpr int variants = 3;
    static constexpr std::array<std::array<double, 3>, 4> palette { { { 0.10, 0.95, 0.72 }, { 0.22, 0.72, 1.00 },
                                                                     { 0.66, 0.36, 1.00 }, { 1.00, 0.30, 0.72 } } };   // teal, cyan, violet, magenta

    //==========================================================================
    /** The JS built-ins the world calls, as V8 computes them, so that a step here is the prototype's step to
        the last bit. sin, cos, exp, atan2 and acos are V8's fdlibm as it runs on x86-64 (V8's arm64 build
        contracts the same code into fused multiply-adds, which parts from these in the last bit of 0.05 -
        0.6 % of arguments); hypot is V8's Kahan sum of the squares over the largest, not the C library's;
        round takes halves toward +infinity. Math.pow is left to the C library: V8's own has changed between
        versions. The renderer's own JS-made shapes (the joints' profile) need these too. */
    struct Js
    {
        static double sin (double x) noexcept;
        static double cos (double x) noexcept;
        static double exp (double x) noexcept;
        static double atan2 (double y, double x) noexcept;
        static double acos (double x) noexcept;
        static double hypot (double x, double y) noexcept;
        static double hypot (double x, double y, double z) noexcept;
        static double round (double x) noexcept;
    };

    /** The hashes and noise the shaders carry too. */
    static uint32_t ih (uint32_t x) noexcept;
    /** A cell's hash, 0..1, rounded to f32 as the JS rounds it (the value returned as a double). */
    static double hcell (int32_t cx, int32_t cy, int32_t s) noexcept;

    /** The heightfield: RGBA float, R height (m), G grove density 0..1, B moss 0..1, A 1. */
    static std::vector<float> buildHeight();
    /** A 2 x 2 box mip chain of an RGBA float image of n x n, the finest (`px` itself) first. */
    static std::vector<std::vector<float>> mipChain (const std::vector<float>& px, int n);
    /** The detail texture: a bumpy sponge, its gradient in RG (128 = flat) and its value in B. RGBA8. */
    static std::vector<uint8_t> buildDetail();

    //==========================================================================
    /** THE MESHES. A mushroom of unit height, `segs` around: position 3, normal 3, uv 2, part 1 (0 stalk,
        1 cap top, 2 gills). A lathe: position 3, normal 3, uv 2. The polar grid: xz offsets. Indexed. */
    struct Mesh { std::vector<float> verts; std::vector<uint16_t> idx; };
    struct Profile { double capY, R; std::vector<std::array<double, 2>> top, gills; };
    static const Profile& profileOf (int variant) noexcept;
    static double stalkRadius (int variant, double t) noexcept;
    static Mesh mushroomMesh (int variant, int segs);
    /** A solid of revolution for the limbs: `profile` = (r, y) from the bottom, closed at both ends. */
    static Mesh lathe (const std::vector<std::array<double, 2>>& profile, int segs);
    /** The ground's polar lattice around the eye: rings from `nearR` to `farR`, `spokes` around, and the centre. */
    static Mesh polarGrid (int rings, int spokes, double nearR, double farR);

    /** The gait's shapes, per step: u = 0 at a footfall, 1 at the next. The rise between footfalls ... */
    static double stepRise (double u) noexcept;
    /** ... and the thud as the leg takes the weight: 0 at u = 0, 1 at u = 0.1, gone by u = 0.5. */
    static double stepThud (double u) noexcept;
    /** Two-bone IK: the knee for a hip h, a foot f, bones lf and lt, pulled toward `pole`. */
    static std::array<double, 3> kneeOf (const std::array<double, 3>& h, const std::array<double, 3>& f,
                                         double lf, double lt, const std::array<double, 3>& pole) noexcept;

    //==========================================================================
    /** A mushroom of the forest. */
    struct Mushroom
    {
        double x = 0, z = 0, size = 0;
        int variant = 0, hue = 0;
        double phase = 0, rot = 0, lean = 0, glow = 0, stalkR = 0, clear = 0, capR = 0, capY = 0;
    };

    /** A front leg: the foot's world position, the swing's ends, the hip and the knee (relative to the
        eye's xz, absolute y), and where the foot is drawn (absolute). */
    struct Leg
    {
        double side = 0, off = 0;
        std::array<double, 3> foot {}, from {}, to {};
        bool swing = false;
        double lift = 0;
        std::array<double, 3> hip {}, knee {}, pos {};
    };

    /** The state a frame is a pure function of; the JS world's `state`, field for field. */
    struct State
    {
        double x = 2033.0, z = 2125.0, heading = 0, heading0 = 0, pitch = 0, roll = 0, nod = 0, time = 0, travel = 0;
        double rate = gearRate[0], stepLen = gearStep[0], speed = gearRate[0] * gearStep[0], psi = 0;
        double heave = 0, sway = 0, eyeX = 2033.0, eyeZ = 2125.0, camY = 0, groundY = 0;
        double pace = gearRate[0] * gearStep[0];   // the speed it makes along its heading
        double brake = 1;                          // the stride the way ahead leaves
        double squeeze = 0;                        // how far the keep-clear rings give
        double spores = gearSpores[0], storm = 0, breath = 0, bass = 0, hit = 0, hitGlow = 0, beat = 0, bpm = 0;
        std::array<Leg, 2> legs {};
        std::array<float, maxBursts * 8> bursts {};   // x, y, z, t0, kind, strength, hue, radius — absolute xz
        int burstAt = 0;
        std::array<float, maxPrints * 4> prints {};   // x, z, t0, radius — absolute xz
        int printAt = 0;
        double footfall = 0;                          // the last footfall's time
    };

    WalkerWorld() noexcept;

    /** Where the walk starts, instead of the world's own start, and the heading it starts on (the sun's, 0,
        stays the one it drifts home to): call before build(). */
    void startAt (double x, double z, double heading = 0) noexcept;

    /** Makes the textures and stands the walker on the ground. Once, tens of milliseconds. */
    void build();
    bool isBuilt() const noexcept { return built; }
    /** The height texture's levels (heightTex^2 RGBA float first, down to 1 x 1) and the detail texture. */
    const std::vector<std::vector<float>>& heightMips() const noexcept { return mips; }
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }

    /** The pace, the stride, the spores and the storm where the gear and TURBO are taking them, at once; the
        breath where the held note is. */
    void settle() noexcept;
    /** One step of the walk. */
    void step (double dt);
    /** One attack of the bass, 0..1: a flash on the near caps and sparks of spores from the ten nearest. */
    void hit (double strength = 1);

    void setGear (int g) noexcept;                    // 1..3
    void setTurbo (bool on) noexcept { turbo = on; }
    void setSteer (double v) noexcept;                // -1..1, the hand on the glass
    void setBass (double v) noexcept;                 // 0..1, a held low note
    void setLevel (double v) noexcept;                // 0..1, the measured level: breath and onsets
    void setBpm (double v) noexcept;                  // 0 = its own pace, else the tempo lock
    int gear() const noexcept { return gearWanted; }
    bool isTurbo() const noexcept { return turbo; }

    const State& state() const noexcept { return s; }

    /** The ground's height at absolute world xz (0 before build()). */
    double groundH (double x, double z) const noexcept;
    /** The mushroom of class k in unwrapped cell (ci, cj), if the hash puts one there. */
    bool mushroomAt (int k, int ci, int cj, Mushroom& out) const noexcept;
    /** The instance lists of class k for the window whose base cell is (bx, bz): per variant, 8 floats an
        instance (x, z, size, rot, hue, phase, glow, lean). */
    void instancesOf (int k, int bx, int bz, std::array<std::vector<float>, variants>& lists) const;

private:
    bool built = false;
    std::vector<std::vector<float>> mips; // the heightfield is mips[0]
    std::vector<uint8_t> detailPx;

    State s;
    int gearWanted = 1;
    bool turbo = false;
    double steer = 0, bassIn = 0, levelIn = 0, levelLP = 0, lastOnset = -10;
    double yawRate = 0, lastSteerTime = -10, psiPrev = 0, aim = 0, heldFor = 0, freeFor = 0;
    bool started = false, squeezing = false;

    // The drive's scratch: the stalks around the walker as discs (x, z, keep-clear radius), and per near disc
    // its offset from a path's origin and the radii a path must keep.
    std::vector<double> discs, ox, oz, q2, capAt, gives, k0, k1;
    std::vector<int> nearIdx, touched;
    std::array<double, 2> ran {};
    std::array<double, 2 * lookFan> runs {}, roomy {}, wide {};
    int nDiscs = 0, nNear = 0, nTouch = 0;
    bool discsGathered = false;
    int discCi = 0, discCj = 0;
    std::array<double, maxDeadEnds * 3> deadEnds {};   // x, z, the time it was marked
    int deadEndAt = 0;
    double lastDeadEnd = -1e9;

    double tap (int ch, double px, double pz) const noexcept;
    double density (double x, double z) const noexcept;
    int g1() const noexcept;
    double rateFor() const noexcept;
    double stepFor() const noexcept;
    double sporesFor() const noexcept;
    double stormFor() const noexcept;

    void gatherDiscs();
    void nearDiscs (double far);
    void nearestCaps (int count, std::vector<Mushroom>& found) const;
    void prepFrom (double px, double pz, double graze, bool touch);
    void runsAlong (double h, double far);
    void turnsRound (int side, double rad);
    double wayAhead (double want, double keen);
    void pushBurst (double x, double y, double z, double kind, double strength, double hue, double radius) noexcept;
    void footfall (Leg& L) noexcept;
    void restSpot (const Leg& L, double ahead, std::array<double, 3>& out) const noexcept;
    void shiftWorld (double dx, double dz) noexcept;
};

} // namespace orbitamp::redgroove
