// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orbitamp::metal
{

/** THE METAL WORLD, THE CPU'S HALF: the ENGL E530's live view. A rogue planet's lava plain under the aurora, a closed
    loop road through it and its ranges, and on it a power-metal album cover: the skull gate and the tunnel behind it,
    the steel citadel and its Tesla towers, a wall of amplifier cabinets, a steel Stonehenge, a giant anvil, Darwin's
    Cat on its crag, pyro columns, lava fountains, ball lightning, a festival stage across the plain, the guitar groves,
    the burning letters and the steel dragons.

    A port of the view's prototype, metal-world.js (all of it: the textures, the height mirror, the loop, the things'
    places, the drive, the lightning, the dragons, the procedural meshes and the hill), with what metal-gl.js works out
    on the CPU for each frame (the fires it feeds the GPU, the ribbons of the lightning, the Tesla arcs and the
    searchlights) and its readers of the finds' files (CBMESH1 meshes, the dragon's CBANIM1 poses, the credit lines,
    the headstock boxes, guitars.txt and its groves, banners.txt, the letters' table and atlas). Matched to it number
    for number: the same doubles where the JS works in doubles, f32 where it stores into a Float32Array or calls
    Math.fround, half floats where it rounds to them, the same 32-bit hashes, and the JS's own Math functions (the
    walker's Js, V8's fdlibm). Build it with -ffp-contract=off: a fused multiply-add rounds once where the JS rounds
    twice.

    THE LEVER: gears 1..4 are CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO, LEAD GAIN HI (LEAD GAIN HI is the storm);
    PARK is setParked. There is no TURBO. hit (level) is one attack of the playing.

    Rendering is not in here. No JUCE in here. */
class MetalWorld final
{
public:
    using V2 = std::array<double, 2>;
    using V3 = std::array<double, 3>;
    static constexpr double pi = 3.141592653589793;   // Math.PI

    //==========================================================================
    // THE DRIVE (the E530's lever): PARK stops at the roadside; the four modes are four speeds, eased.
    static constexpr int numGears = 4;
    static constexpr std::array<const char*, 5> modeNames { "PARK", "CLEAN \xc2\xb7 GAIN LO", "CLEAN \xc2\xb7 GAIN HI",
                                                            "LEAD \xc2\xb7 GAIN LO", "LEAD \xc2\xb7 GAIN HI" };
    static constexpr std::array<double, numGears> gearSpeed { 5.0, 8.5, 12.5, 17.0 };       // m/s
    static constexpr std::array<double, numGears> stormOf { 0.0, 0.06, 0.25, 1.0 };         // only the last is THE storm
    static constexpr double speedSeconds = 0.8, parkSeconds = 1.5, stormSeconds = 0.9;
    static constexpr double pullSeconds = 4.5, pullPerMs = 0.3;                             // leaving PARK: a heavy pull-away
    static constexpr double eye = 2.6;                                                      // the eye over the road
    static constexpr double basePitch = -2.0 * pi / 180;
    static constexpr double lane = 1.9, parkLat = 5.8;
    static constexpr double pitchOffsetDefault = -3.0 * pi / 180;                           // page.js's: 3 degrees more down
    static constexpr double worldPeriod = 48000.0;
    static constexpr int texSize = 256, detailSize = 256, noiseSize = 256, cellN = 256;
    static constexpr double L0 = 2400.0, L1 = 300.0, L2 = 48.0;
    static constexpr V2 off1 { 0.37, 0.61 }, off2 { 0.71, 0.13 };
    static constexpr double ventCell = 48.0, ventProb = 0.22;
    static constexpr int ventM = 1000;
    static constexpr double crackPeriod = 3072.0;
    // THE ROAD (California's loop): its radius round a centre a function of the angle.
    static constexpr V2 loopC { 24520.0, 24680.0 };
    static constexpr double loopRadius = 760.0;
    static constexpr std::array<int, 4> loopK { 2, 3, 5, 7 };
    static constexpr std::array<double, 4> loopA { 200.0, 40.0, 22.0, 10.0 }, loopP { 2.8623, 2.1, 4.0, 1.3 };
    static constexpr int roadN = 256, loopN = 8192, rhN = 2048;
    static constexpr double roadSpan = 3200.0, roadHalf = 4.0, startBefore = 280.0;
    static constexpr V2 roadOrg { loopC[0] - roadSpan / 2, loopC[1] - roadSpan / 2 };

    // THE THINGS: fractions of the loop round from the skull gate (at 0); d the signed distance from the centreline.
    struct CitadelSpec { double at, d; V2 crag; int towers; };
    struct TeslaSpec { double at, d; int n; double step; };
    struct AmpsSpec { double at, d; int n, rows; double cab, deep; V2 cliff; double turn; };
    struct HengeSpec { double at, d; int n; double r; V2 rise; };
    struct CatSpec { double at, d; V2 crag; double h; };
    struct StageSpec { double at, d, w; };
    struct FossilSpec { double at, d, scale, raise; };
    struct WarriorSpec { double at, ahead, d; V2 rise; };
    struct AtSpec { double at, d; };
    struct PyroSpec { double at; int n; };
    struct DragonSpec { int n; double scale, vmax, swoopAt; V2 swoopEvery, hiccupEvery; double dark; V2 perchFor; double beat, tempo; };
    static constexpr double gateAt = 0.0;
    static constexpr CitadelSpec citadelSpec { 0.120, -118, { 62, 30 }, 5 };
    static constexpr TeslaSpec teslaSpec { 0.120, -46, 5, 26 };
    static constexpr AmpsSpec ampsSpec { 0.250, 46, 13, 3, 4.6, 2.6, { 24, 12 }, 55 };
    static constexpr HengeSpec hengeSpec { 0.372, -150, 10, 19, { 80, 11 } };
    static constexpr AtSpec anvilSpec { 0.400, 24 };
    static constexpr CatSpec catSpec { 0.592, 58, { 30, 14 }, 28 };
    static constexpr StageSpec stageSpec { 0.770, -320, 84 };
    static constexpr FossilSpec fossilSpec { 0.343, 19, 1.6, 3.5 };
    static constexpr double logoAt = 0.645;
    static constexpr WarriorSpec warriorSpec { 0.120, 78, -72, { 24, 7 } };
    static constexpr std::array<PyroSpec, 4> pyroSpecs { { { 0.046, 6 }, { 0.300, 6 }, { 0.548, 6 }, { 0.815, 6 } } };
    static constexpr double pyroStep = 24.0, pyroSide = 7.5;
    static constexpr std::array<AtSpec, 4> fountainSpecs { { { 0.190, -42 }, { 0.425, 40 }, { 0.672, -46 }, { 0.930, 44 } } };
    static constexpr std::array<double, 3> findSlots { 0.160, 0.520, 0.880 };
    static constexpr V2 findSwapFade { 60.0, 220.0 };
    static constexpr std::array<const char*, 5> findKinds { "tour bus", "cassette tape", "battle jacket", "guitar in the stone", "longship" };
    static constexpr DragonSpec dragonSpec { 3, 1.45, 40, 0.700, { 38, 70 }, { 7, 16 }, 16, { 24, 36 }, 3.2917, 2.0 };
    static constexpr V2 lightningEvery { 16, 34 }, lightningStormEvery { 2.2, 5.0 };
    static constexpr int numOrbs = 3;
    /** The owner's things along the road (drawn when their meshes come): role, label, where (at < 0: searched, the
        lava crawler's), how far off, turned toward the oncoming rover, along the road (parked with the traffic), the
        height asked (0: the file's own), its own paint kept. */
    struct LandmarkSpec { const char* role; const char* label; double at, d, turn; bool along; double height; bool paint; };
    static constexpr std::array<LandmarkSpec, 5> landmarkSpecs { {
        { "horns",   "the horns, a steel hand", 0.090, 15,   60, false, 0, false },
        { "mammoth", "mammoth skeleton",        0.215, -22,  70, false, 0, false },
        { "crawler", "lava crawler",            -1,    10.5, 0,  true,  0, true },
        { "brachy",  "Brachyceratops skeleton", 0.625, 20,   70, false, 0, false },
        { "guitars", "the giant guitars",       0.735, -24,  35, false, 0, false },
    } };
    /** The renderer's per-frame buffers (metal-gl.js): at most this many fires of 12 floats and ribbons of 16. */
    static constexpr int maxEmit = 96, maxRib = 2400;
    static constexpr double farCut = 1500.0;

    //==========================================================================
    /** The hashes the shaders carry too. hs and hcell take any number and wrap it to 32 bits as Math.imul does. */
    static uint32_t ih (uint32_t x) noexcept;
    static double hcell (double cx, double cy, double s) noexcept;       // 0..1, an f32 value
    static double hs (double i, double s) noexcept;                      // 0..1, an f32 value
    /** A number rounded to the nearest half float (what an RGBA16F texel holds). */
    static double toHalf (double v) noexcept;

    /** THE TEXTURES: the base RGBA16F 256 x 256 (R the swell, G the ranges' crests, B the lumps and the veins' warp, A the
        hot regions), half-float values held as floats; the detail (RGBA8: a gradient in RG, the value in B, a grain in
        A); the sky's noises (RGBA8); the veins' cells (RGBA8: a point, a hash). */
    static std::vector<float> makeBase();
    static std::vector<uint8_t> makeDetail();
    static std::vector<uint8_t> makeNoise();
    static std::vector<uint8_t> makeCells();

    //==========================================================================
    /** THE LOOP: its radius and the radius's derivative at an angle, a point on it, the arclength table (loopN + 1
        entries), its length, a length wrapped round it, the angle at an arclength, the tangent and the right there. */
    static V2 loopR (double th) noexcept;
    static V2 loopPoint (double th) noexcept;
    static const std::vector<double>& loopTable();
    static double loopLength();
    static double modL (double v);
    static double thetaAt (double s);
    static V2 tangentAt (double th) noexcept;
    static V2 rightAt (double th) noexcept;
    static V2 roadPointAt (double s, double d);
    static double headingAt (double s);
    /** The camera's right, up and forward for a heading and a pitch (positive = nose up), as metal-gl.js makes them. */
    struct Basis { V3 r, u, f; };
    static Basis cameraBasis (double heading, double pitch) noexcept;
    /** The road's edge studs, a whole number round the loop. */
    static int studs();
    /** A thing's front at (px, pz) by the road at arclength `at`: toward the road, turned `turn` degrees toward the
        oncoming rover. */
    static V2 frontAt (double px, double pz, double at, double turn);

    //==========================================================================
    /** A MESH: 7 floats a vertex (position, normal, material), y up, metres; triangles. The JS sends the indices as
        16 bits where the vertices fit (wide() false), else 32. */
    struct Mesh
    {
        std::vector<float> v;
        std::vector<uint32_t> idx;
        bool wide() const noexcept { return v.size() / 7 > 65535; }
    };
    /** The procedural meshes' builder (propBuilder): tubes, boxes, blobs, tori. */
    class Builder
    {
    public:
        std::vector<double> v;          // as the JS holds them until done()
        std::vector<uint32_t> idx;
        uint32_t vert (const V3& p, const V3& n, double m);
        void tube (const std::vector<V3>& pts, int sides, const std::vector<double>& radii, double m, bool cap = false, double rot = 0);
        void box (const V3& c, const V3& h, double m, double yaw = 0, double tilt = 0);
        void box (const V3& c, const V3& h, const std::array<double, 6>& m, double yaw = 0, double tilt = 0);
        void blob (const V3& c, const V3& r, double m, int rings = 8, int sides = 10);
        void torus (const V3& c, double R, double r, double m, int seg = 24, int sides = 8);
        Mesh done() const;
    private:
        void boxFaces (const V3& c, const V3& h, const double* m, bool perFace, double yaw, double tilt);
    };
    /** All the procedural meshes by name (propMeshes): gate, tower0..2, wall, wallb, tesla, cab, head, trilithon,
        trilithonF, altar, anvil, cat, stage, nozzle, pool, find0..find4. */
    static std::map<std::string, Mesh> propMeshes();

    //==========================================================================
    /** THE THINGS' PLACES, made by build(). */
    struct Crag { V2 a {}, b {}; double r = 0, h = 0; std::string name; };
    struct Prop { std::string mesh; double x = 0, y = 0, z = 0, yaw = 0, s = 1, p = 0; std::string name; int idx = 0; };
    struct Banner { double x = 0, y = 0, z = 0, yaw = 0, w = 0, h = 0, seed = 0; int cell = 0; };
    struct Target { double x = 0, y = 0, z = 0, w = 0; bool hit = false; int tesla = -1; };
    struct Perch { double x = 0, y = 0, z = 0, yaw = 0; std::string name; int tower = -1; };
    struct Spire { double x = 0, y = 0, z = 0; int prop = 0; };
    struct Gate { double at = 0, x = 0, z = 0, y = 0; int prop = 0; };
    struct Tower { double x = 0, y = 0, z = 0, h = 0, tip = 0; int prop = 0; };
    struct Citadel { double at = 0, x = 0, z = 0, y = 0; std::vector<Tower> towers; };
    struct Warrior { double at = 0, x = 0, y = 0, z = 0, yaw = 0; };
    struct Landmark { std::string role, label; double at = 0, d = 0, turn = 0; bool along = false; double height = 0; bool paint = false;
                      double x = 0, y = 0, z = 0, yaw = 0; bool came = false; };
    struct TeslaTower { double x = 0, y = 0, z = 0, top = 0; int prop = 0; };
    struct Amps { double at = 0, x = 0, z = 0, y = 0, fx = 0, fy = 0, fz = 0; };
    struct Spot { double at = 0, x = 0, z = 0, y = 0; };                                 // the henge's, the anvil's
    struct Cat { double at = 0, x = 0, z = 0, y = 0, nx = 0, nz = 0; };
    struct Stage { double at = 0, x = 0, z = 0, y = 0, nx = 0, nz = 0, yaw = 0; };
    struct Pyro { double x = 0, y = 0, z = 0, at = 0; int k = 0, i = 0; };
    struct Fountain { double x = 0, y = 0, z = 0, at = 0, next = 0, t = 1e4; int n = 0, i = 0; };
    struct Find { int kind = 0; double x = 0, z = 0, y = 0, yaw = 0, at = 0, d = 0; };
    struct Tunnel { double from = 0, to = 0, hw = 0; };
    /** A guitar grove: its place round the loop, its star's side, whether ours stand on both sides, its members (indices
        into guitars()). */
    struct Grove { int g = 0; double at = 0; bool both = false; int starSide = -1; std::vector<int> members; };
    struct GuitarSlot { int i = 0; double at = 0, x = 0, y = 0, z = 0, yaw = 0, k = 0; V2 lean {}; int grove = 0; bool star = false;
                        std::string name; int side = 0; double off = 0, along = 0; };
    /** A burning text in the air beside the road. */
    struct Letter { std::string name; double at = 0, x = 0, z = 0, y = 0, yaw = 0, nx = 0, nz = 0, hw = 0, hh = 0;
                    std::array<double, 4> cell {}; std::optional<std::array<double, 4>> colour; double seed = 0; bool big = false; };

    //==========================================================================
    /** THE LIGHTNING, THE TESLA ARCS, THE BALL LIGHTNING, THE FIRE. */
    struct Line { std::vector<V3> pts; double w = 1; };
    struct Bolt { double t = 0, k = 0; std::vector<Line> lines; V3 at {}; int n = 0; };
    struct Spark { double t = 0; V3 at {}; double k = 0; };
    struct TeslaArc { int i = 0; double t = 1e4, dur = 0.4; };
    struct Orb { int i = 0; double x = 0, y = 0, z = 0, age = 0, life = 1; double n = 0; double as = 0, lat = 0, up = 10, ph = 0; };

    /** THE DRAGONS. */
    enum class Mode { roam, swoop, toPerch, perch, clang, leave };
    enum class Kind { front, circle, park, prep };
    enum class Gesture { look, wings, shuffle, stretch };
    struct Path
    {
        std::vector<V3> pts;
        std::vector<double> ts;
        std::optional<V3> v0;
        std::vector<V3> tan;                // every point's tangent, or none (v0 and the neighbours)
        std::optional<double> ringAt;
    };
    struct Gest { Gesture kind = Gesture::look; double t = 0, dur = 0, a = 0, b = 0; };
    struct Pose { double hy = 0, hp = 0, wing = 0, pitch = 0, yaw = 0, side = 0; };
    struct Ring { V3 at {}, dir {}; };
    struct Clang { int spire = 0; double hitAt = 0; bool done = false; };
    struct Dragon
    {
        int i = 0;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        Mode mode = Mode::roam;
        double mt = 0;
        std::optional<Path> path;
        double as = 0, lat = 0, up = 60;
        bool front = true;
        Kind kind = Kind::front;
        double follow = 0.5, loopR = 30, loopW = 0.2, ph = 0, anchorT = 0, beat = 0, rate = 1;
        V3 U { 0, 1, 0 }, F { 0, 0, 1 };
        double rear = 0;
        std::optional<V2> head;
        std::optional<std::array<V3, 2>> takeoff;
        std::optional<Gest> gest;
        double nextGest = 0;
        Pose g;
        V3 headDir { 0, 0, 1 };
        double flameT = 1e4, hicT = 1e4, nextHic = 0, ringT = 1e4;
        std::optional<Ring> ring;
        std::optional<Perch> perch;       // where it sits or lands
        int perchOf = -1;                 // ...one of perches() (-1: none, or the ground in PARK)
        double perchT = 0;
        std::optional<Clang> clang;
        double tumble = 0, shake = 0;
        V3 mouth {};
        double nod = 0;
        V3 L { 1, 0, 0 };
    };
    /** The dragon's head in each pose of its wing-beat (from its CBANIM1): the mouth's tip and the head's middle, the
        file's frame, metres. */
    struct DragonHead { std::vector<V3> mouth, mid; };

    /** The state a frame is a pure function of; the JS world's `state`, field for field. */
    struct State
    {
        double sr = 0; int lap = 0;
        double lat = lane, latV = 0, x = 0, z = 0, heading = 0, pitch = basePitch, camY = 0, travel = 0, time = 0, aurora = 0;
        double speed = gearSpeed[0], storm = 0, calm = 0, park = 0, pitchOffset = 0, dt = 0;
        int gear = 1; bool parked = false;
        double hitT = 1e4, hitS = 0; int hits = 0;
        double flash = 0;
        std::array<double, 3> flashCol { 0.75, 0.8, 1.0 };
        double findFade = 1, beat = 0, stageHitT = 1e4;
    };

    //==========================================================================
    MetalWorld() noexcept = default;

    /** Makes the textures, the road map, the things' places, the groves; the drive 280 m before the skull gate. Once,
        about a tenth of a second. */
    void build();
    bool isBuilt() const noexcept { return built; }

    const std::vector<float>& base() const noexcept { return basePx; }
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }
    const std::vector<uint8_t>& noise() const noexcept { return noisePx; }
    const std::vector<uint8_t>& cells() const noexcept { return cellsPx; }
    /** The road map RGBA16F 256 x 256 over roadOrg + roadSpan: the signed distance from the centreline, the road's
        height, 0, 0 (half-float values). */
    const std::vector<float>& roadMap() const noexcept { return roadPx; }

    /** THE HEIGHT MIRROR (the shaders' floorH): the ground's height at absolute xz, the terrain without the road, how
        hot and how mountainous a place is, the road map's two values there. */
    double floorHeight (double px, double pz) const noexcept;
    double terrainH (double px, double pz) const noexcept;
    struct Probe { double hot = 0, mtn = 0; };
    Probe probe (double px, double pz) const noexcept;
    V2 roadTap (double x, double z) const noexcept;

    //==========================================================================
    /** One step of the drive and everything in the world. */
    void step (double dt);
    void setGear (int g) noexcept;                      // 1..4
    void setParked (bool on) noexcept { parked = on; s.parked = parked; }
    /** The speed, the storm and the calm where the gear (or PARK) is taking them, at once. */
    void settle();
    /** The camera evaluated where the rover stands: its height as the first step puts it, its pitch where the steps
        ease it to (the prototype lets the first steps do it, the pitch over a second). For a world that has not stepped
        yet (a poster, a first frame); a world that has stepped keeps its own. */
    void settleCamera() noexcept;
    /** The drive put at arclength `sr` (and on lap `lap`, if not negative): the dragons and the ball lightning anew. */
    void startAt (double sr, int lap = -1);
    /** The next rare find's approach, an arclength. */
    double nextFindAt() const;
    /** One attack of the playing, 0..1: the world's thunder. */
    void hit (double level);
    /** No answer at all: an attack's light called off (the pyro, the stage, the dragons' big flame). */
    void quiet() noexcept;
    /** A look down (negative) or up, radians, added to the pitch. */
    void setPitchOffset (double rad) noexcept;

    int gear() const noexcept { return gearWanted; }
    bool isParked() const noexcept { return parked; }
    double speedFor (int g) const noexcept;
    double speedOf() const noexcept { return speedFor (gearWanted); }
    const State& state() const noexcept { return s; }
    /** The cabin's flash plate's share (metal-gl.js's cabinFlash): the lightning light taken linearly, whole from 0.75. */
    double cabinFlash() const noexcept;

    //==========================================================================
    const std::vector<Crag>& crags() const noexcept { return crags_; }
    const std::vector<Prop>& props() const noexcept { return props_; }
    const std::vector<Banner>& banners() const noexcept { return banners_; }
    const std::vector<Target>& targets() const noexcept { return targets_; }
    const std::vector<Perch>& perches() const noexcept { return perches_; }
    const std::vector<Spire>& spires() const noexcept { return spires_; }
    /** Per spire the time since its clang (or since a strike near it). */
    const std::vector<double>& shakes() const noexcept { return shakes_; }
    const Gate& gate() const noexcept { return gate_; }
    const Citadel& citadel() const noexcept { return citadel_; }
    const Warrior& warrior() const noexcept { return warrior_; }
    const std::vector<Landmark>& landmarks() const noexcept { return landmarks_; }
    const std::vector<TeslaTower>& tesla() const noexcept { return tesla_; }
    const Amps& amps() const noexcept { return amps_; }
    const Spot& henge() const noexcept { return henge_; }
    const Spot& anvil() const noexcept { return anvil_; }
    const Cat& cat() const noexcept { return cat_; }
    const Stage& stage() const noexcept { return stage_; }
    const std::vector<Pyro>& pyro() const noexcept { return pyro_; }
    const std::vector<Fountain>& fountains() const noexcept { return fountains_; }
    const Tunnel& tunnel() const noexcept { return tunnel_; }
    const std::vector<Grove>& groves() const noexcept { return groves_; }
    const std::vector<GuitarSlot>& guitars() const noexcept { return guitarSlots; }
    const std::vector<Letter>& letters() const noexcept { return letters_; }
    /** The arclengths the burning letters and the groves keep away from. */
    const std::vector<double>& taken() const noexcept { return taken_; }
    /** The rare finds this lap: three of the five (lap L, slot k: kind (k + 3L) mod 5). */
    std::array<Find, 3> findsNow() const;
    /** THE HILL AND ITS TUNNEL behind the skull gate, relative to the gate's foot. */
    Mesh hillMesh() const;

    const std::vector<Bolt>& bolts() const noexcept { return bolts_; }
    const std::vector<Spark>& sparks() const noexcept { return sparks_; }
    const std::vector<Orb>& orbs() const noexcept { return orbs_; }
    const std::vector<Dragon>& dragons() const noexcept { return dragons_; }
    struct TeslaState { TeslaArc arc; double chain = 1e4; };
    TeslaState teslaState() const noexcept { return { teslaArc, teslaChain }; }
    /** A strike's light over time (its envelope). */
    static double boltI (double t) noexcept;
    /** A jagged line from a to b by midpoint displacement (depth levels), its jitter `rough` of its length. */
    static std::vector<V3> jag (const V3& a, const V3& b, int depth, double rough, double seed);

    //==========================================================================
    /** THE FIRES THIS FRAME (metal-gl.js's emitters): per fire 12 floats relative to the camera's cell (floor of its xz)
        — position, kind (0 pyro, 1 a dragon's flame, 2 its smoke ring, 3 a fountain, 4 a hiccup, 5 an orb, 7 a burning
        text), direction, size, age, seed, strength, duration — and the two lights near (xyz relative, strength). */
    struct Fires
    {
        std::vector<float> em;              // n x 12
        int n = 0;
        std::array<float, 8> lights {};
    };
    Fires fires() const;
    /** THE RIBBONS THIS FRAME: the lightning, the Tesla arcs, the searchlights (unless `beams` is off); 16 floats each
        — a, its width, b, its width, colour, strength, kind, the beam's piece (from, to), 0 — relative to the cell. */
    struct Ribbons
    {
        std::vector<float> rb;              // n x 16
        int n = 0;
    };
    Ribbons ribbons (bool beams = true) const;

    //==========================================================================
    /** EVERYTHING ON THE LOOP (the jump list), in the prototype's order. Groups: finds, big, road, signs, sky. */
    struct Place { std::string group, name; double at = 0, seen = 0; int lap = -1, kind = -1; };
    std::vector<Place> places() const;
    /** Where a jump to a place puts the drive (cabin's jumpTo): the arclength (rounded for the address) and the lap. */
    struct Jump { double at = 0; int lap = 0; };
    Jump jumpTo (const Place& place);

    //==========================================================================
    /** THE FINDS, as the world reads them (call build() first). The dragon's head for its fire's mouth. */
    void setDragonHead (const DragonHead& h) { dragonHead = h; }
    /** A burning text of the table: its name, its cell in the atlas (px), its size (m), its height over the ground, the
        logo once, its colours' cell. */
    struct LetterText { std::string name; std::array<double, 4> cell {}; V2 size {}; double up = 0; bool once = false;
                        std::optional<std::array<double, 4>> colour; };
    /** Every text twice a lap beside the road (the logo once), away from the groves' and the landmarks' approaches. */
    void buildLetters (const std::vector<LetterText>& table);
    /** Per guitar its grove and whether it is the grove's censored star; the slots, named as the caller names them. */
    struct GuitarItem { int grove = 0; bool star = false; };
    const std::vector<GuitarSlot>& placeGuitars (const std::vector<GuitarItem>& items);
    const std::vector<GuitarSlot>& placeGuitars (int n);
    void nameGuitar (int i, std::string name) { if (i >= 0 && i < (int) guitarSlots.size()) guitarSlots[(size_t) i].name = std::move (name); }
    /** A landmark's mesh came: drawn from now on, its top (m over its foot) a target for the lightning. */
    void landmarkCame (const std::string& role, double top);
    /** WHAT THE FINDS BRING THE WORLD, applied in one fixed order (the page applies them as its fetches come back, in
        no fixed order; this port fixes it so that the targets' order, which the lightning's pick reads, is the same on
        every run): the dragon's head, the letters' table, the guitars placed and named, then each landmark's top in
        the order given (the finds' folders by name). */
    struct Finds
    {
        std::optional<DragonHead> dragonHead;
        std::vector<LetterText> letters;
        std::vector<GuitarItem> guitars;
        std::vector<std::string> guitarNames;          // per guitar, the name it is shown with
        std::vector<std::pair<std::string, double>> landmarkTops;
    };
    void furnish (const Finds& f);
    /** The role a finds' folder plays, by its name (metal-gl.js's roleOf): horns, mammoth, brachy, guitars, crawler,
        warrior, gate, fossil, cat, find0..find4, anvil, dragonFile, or the name itself. */
    static std::string roleOf (std::string_view folder);

    // THE FINDS' FILES. Each reader takes the file's bytes or text.
    /** A CBMESH1 mesh: its counts and the least and the most z of its vertices (40 B a vertex from byte 16, then
        uint32 indices). Not ok when the magic or the size is wrong. */
    struct MeshFile { bool ok = false; uint32_t nv = 0, ni = 0; double zmin = 1e9, zmax = -1e9; size_t vertexAt = 16, indexAt = 0; };
    static MeshFile readMesh (const uint8_t* bytes, size_t size);
    /** The top a landmark's mesh gives it (its file's z at the most, scaled to the height asked), or none for a role
        that is no landmark or a mesh that is not one. */
    static std::optional<double> landmarkTop (const std::string& role, const MeshFile& m);
    /** The dragon's poses (CBANIM1): its head per pose, from the int16 positions; none when the file is wrong, its
        vertex count is not the mesh's (meshNv, 0: not checked) or it has no head. */
    static std::optional<DragonHead> readDragonHead (const uint8_t* bytes, size_t size, uint32_t meshNv = 0);
    /** The text after "credit" in a mesh's table, or nothing. */
    static std::optional<std::string> creditOf (std::string_view text);
    /** The headstock boxes of a guitars' table ("headstock <name> cx cy cz sx sy sz"). */
    struct Headstock { std::string name; V3 c {}, s {}; };
    static std::vector<Headstock> readHeadstocks (std::string_view text);
    /** guitars.txt: per guitar its name, its mesh's file (without .mesh), its headstock box (none: ours, never
        censored), its metallic paint. */
    struct GuitarEntry { std::string name, file; std::optional<Headstock> box; std::optional<V3> paint; };
    static std::vector<GuitarEntry> readGuitars (std::string_view text);
    /** The groves' guitars as the page deals them: in each one real type (the star, censored) among our four designs,
        turned round grove by grove. `chosen` indexes the set; empty when the set is. */
    struct GroveDeal { std::vector<int> chosen; std::vector<GuitarItem> items; std::vector<std::string> names; };   // names: as shown (_ a space)
    static GroveDeal dealGroves (const std::vector<GuitarEntry>& set);
    /** banners.txt: the cells of the banners' atlas (x y w h px), as fractions of its size: u0 v0 u1 v1. */
    static std::vector<std::array<double, 4>> readBanners (std::string_view text, double atlasW, double atlasH);
    /** A PNG's width and height from its header (0, 0 when it is not one). */
    static std::array<int, 2> pngSize (const uint8_t* bytes, size_t size);
    /** THE LETTERS (finds/letters/): letters.txt's rows ("name file.png W x H px N m wide") and the PNGs there (by
        name, in the folder's listing order, with their sizes) packed as the page packs them into one atlas, two columns
        of 1024 px, the word CENSORED's canvas (1024 x 220) among them; the table the world reads, the atlas's size, where
        each picture goes and the CENSORED cell. Empty when there is no table or no PNG. */
    struct Png { std::string file; int width = 0, height = 0; };
    struct LetterPiece { std::string name, file; std::array<double, 4> cell {}; };   // file empty: the CENSORED canvas
    struct LetterAtlas
    {
        int width = 0, height = 0;
        std::vector<LetterPiece> pieces;
        std::vector<LetterText> table;
        std::optional<std::array<double, 4>> censored;
    };
    static LetterAtlas packLetters (std::string_view lettersTxt, const std::vector<Png>& pngs);
    /** WHERE THE CABIN LEAVES THE VIEW BARE: the box round the plate's pixels with alpha under 255 (glassBounds),
        x0 y0 x1 y1 as fractions, a margin of pixels round it; none when the plate is opaque. */
    static std::optional<std::array<double, 4>> glassBounds (const uint8_t* rgba, int w, int h, int margin);
    /** Where the cabin's window is: the dashboard's alpha, every `step`-th pixel, for a press to know whether it fell
        on the open window (alpha low) or on the panel. */
    struct Glass
    {
        int width = 0, height = 0;
        std::vector<uint8_t> alpha;
        static Glass of (const uint8_t* rgba, int w, int h, int step);
        bool open (float x01, float y01) const noexcept;
    };

private:
    bool built = false;
    std::vector<float> basePx, roadPx;
    std::vector<uint8_t> detailPx, noisePx, cellsPx;
    std::vector<Crag> crags_;
    std::vector<double> rhTable;
    State s;
    int gearWanted = 1;
    bool parked = false, wasParked = false;
    struct Pull { double t = 0, v0 = 0, lat0 = 0; int gear = 1; double T = 0; };
    std::optional<Pull> pull;

    std::vector<Prop> props_;
    std::vector<Banner> banners_;
    std::vector<Target> targets_;
    std::vector<Perch> perches_;
    std::vector<Spire> spires_;
    std::vector<double> shakes_;
    Gate gate_;
    Citadel citadel_;
    Warrior warrior_;
    std::vector<double> taken_;
    std::vector<Landmark> landmarks_;
    std::vector<Grove> groves_;
    std::vector<GuitarSlot> guitarSlots;
    std::vector<TeslaTower> tesla_;
    Amps amps_;
    Spot henge_, anvil_;
    Cat cat_;
    Stage stage_;
    std::vector<Pyro> pyro_;
    std::vector<Fountain> fountains_;
    std::array<std::array<Find, 5>, 3> findPlaces {};
    std::vector<Letter> letters_;
    std::map<std::string, int> letterNames;
    Tunnel tunnel_;

    std::vector<Bolt> bolts_;
    int boltN = 0;
    double nextStrike = 8;
    std::vector<Spark> sparks_;
    TeslaArc teslaArc;
    double teslaNext = 2, teslaChain = 1e4;
    std::vector<Orb> orbs_;
    std::vector<Dragon> dragons_;
    struct DragonClock { double n = 0, sinceSeen = 0, nextSwoop = 20, swoopPass = -1, perchPass = -1, clangPass = -1; int called = -1; };
    DragonClock dc;
    std::optional<DragonHead> dragonHead;

    // The world's own helpers (metal-world.js's makeWorld closures).
    double tap (int ch, double u, double v) const noexcept;
    std::array<double, 4> tap4 (double u, double v) const noexcept;
    double cragH (double px, double pz) const noexcept;
    double roadHeight (double th) const noexcept;
    double footY (double px, double pz, double r) const noexcept;
    /** What a thing is placed with besides (add's `extra`): its height (else its foot's, the least of the ground
        round it within `foot`), its size, its glow phase (else its index's hash), its name. */
    struct Extra { std::optional<double> y; double foot = 2, s = 1; std::optional<double> p; std::string name; };
    Prop& add (const std::string& mesh, double x, double z, double yaw, const Extra& e);
    double crawlerSpot (double d) const;
    struct Tall { double x = 0, z = 0, y0 = 0, y1 = 0, r = 0; };
    int blocksView (double at, double px, double py, double pz, const Tall& o, double margin = 1.5) const;
    std::vector<Tall> tallThings() const;
    void makeGroves();
    bool inApproach (double a) const;
    Find placeFind (int slot, int kind) const;
    void place();
    void strike (const V3& target, double k);
    struct Seen { int tg = 0; double D = 0; };
    std::vector<Seen> targetsInView (double reach) const;
    void strikeSomewhere (double k, bool forceSteel);
    void respawnOrb (Orb& o, bool first);
    double dh (int k);
    void anchor (Dragon& u, Kind kind);
    V3 roamTarget (const Dragon& u, double t) const;
    void resetDragons();
    V3 P (double a, double lat, double up) const;
    void startSwoop (Dragon& u);
    void startPerch (Dragon& u, const Perch& spot, int index);
    void perchGestures (Dragon& u, double dt);
    V2 perchFacing (const Perch& q) const;
    void startClang (Dragon& u, int spire);
    void stepDragons (double dt);
    std::array<V3, 2> perchFrame (const Dragon& u) const;
    void orient (Dragon& u, const V3& pv, double dt);
};

} // namespace orbitamp::metal
