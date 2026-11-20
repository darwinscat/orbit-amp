// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../../underwater/view/UnderwaterWorld.h"     // the cabin's Glass, PanelMask and the Quality ladder, shared

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace orbitamp::california
{

/** CALIFORNIA, 1967, THE CPU'S HALF. The live view out of a hippie van's windshield on a country road looping between
    golden hills of orange poppies at golden hour: the textures (the hills' fbm, the grass's detail, the clouds' noise;
    the road map and the land map baked once over the loop's ground), the loop and its arclength table, the drive (two
    gears, PARK on the shoulder, the heavy old van's pull-away), the peace pendant's swing, the butterflies (on the
    flowers, and on the glass in PARK), the oaks, the poles, the signs and the rhyme boards, the barns, the giants,
    the chalk on the road, the windpumps, the fences and their flags, the water tower, the radio telescopes, the rare
    finds lap by lap, the flying saucers, the yellow submarine, a flock of birds, the shadow map round the van, and the
    meshes made in code. Plus the readers of the files the world takes in (the sprites' and the signs' tables, the
    flags and the chalk, the pendant's box, the meshes' CBMESH1 and their .txt).

    A port of the view's prototype, california-gl.js's makeWorld and the makers round it, matched to it number for
    number: the same doubles where the JS works in doubles, the same f32 rounding where it stores into a Float32Array,
    the same half floats where it rounds to them, the same 32-bit integer hashes, and the JS's own Math functions (the
    walker's Js, V8's fdlibm). Build this file with -ffp-contract=off: a fused multiply-add rounds once where the JS
    rounds twice. No steering (the loop drives itself) and no TURBO.

    Rendering is not in here. No JUCE in here. */
class CaliforniaWorld final
{
public:
    static constexpr double pi = 3.141592653589793;   // Math.PI
    using V2 = std::array<double, 2>;
    using V3 = std::array<double, 3>;
    using V4 = std::array<double, 4>;

    // THE DRIVE.
    static constexpr int numGears = 2;                                  // CLEAN, DIRTY
    static constexpr std::array<double, numGears> gearSpeed { 6.0, 14.0 };
    static constexpr double speedSeconds = 0.45, parkSeconds = 1.1, pullSeconds = 3.5, pullPerMs = 0.35;
    static constexpr double eye = 1.9, basePitch = -2.0 * pi / 180, lane = 1.65, parkLat = 4.3;
    static constexpr double worldPeriod = 92400.0;
    static constexpr int texSize = 256, detailSize = 256, noiseSize = 256;
    static constexpr double L0 = 2100.0, L1 = 330.0, L2 = 48.0, hillA = 70.0, medA = 16.0, fineA = 0.9;
    // THE LOOP: its radius round the centre a function of the angle; arclength from a table of loopN steps.
    static constexpr V2 loopCentre { 46200.0, 46200.0 };
    static constexpr double loopRadius = 760.0, startS = 2000.0;
    static constexpr int loopN = 8192;
    static constexpr int roadN = 256;                                    // the road and land maps: 256 x 256 texels...
    static constexpr double roadSpan = 3200.0;                           // ...over the loop's ground
    static constexpr V2 roadOrigin { loopCentre[0] - roadSpan / 2, loopCentre[1] - roadSpan / 2 };
    static constexpr double sunElev = 8.0 * pi / 180, sunAz = 8.0 * pi / 180, windSpeed = 2.4;
    static constexpr double pendPeriod = 0.9, pendZeta = 0.06, pendZetaParked = 0.13, gravity = 9.81;
    // The rare finds' places (fractions of the loop), the fade at the loop's start where they change.
    static constexpr std::array<double, 3> findSlots { 0.133, 0.467, 0.80 };
    static constexpr V2 findSwapFade { 60.0, 220.0 };
    static constexpr double monoThick = 3.3, monoWide = 13.3, monoHigh = 30.0;   // the monolith, 1 : 4 : 9
    static constexpr double pumpOmega = 0.9;
    static constexpr double flockEvery = 70.0, flockFirst = 12.0, flockLife = 34.0;
    static constexpr int beamPetals = 48;
    static constexpr double saucerScale = 1.5, subScale = 4.0, subSeconds = 30.0;
    // What the renderer draws, as the prototype sizes it.
    static constexpr double tanH = 18.0 / 16.0, zNear = 0.5, zFar = 2600.0, farCut = 1500.0, hazeK = 1.0 / 2600.0;
    struct Level { double cell; int n; };
    static constexpr std::array<Level, 3> levels { { { 2.0, 96 }, { 6.0, 96 }, { 24.0, 96 } } };
    struct FloraSpec { int kind; double cell; int w; double ahead, density, smin, smax; std::array<double, 4> far; };
    static constexpr std::array<FloraSpec, 3> flora { {
        { 0, 0.9, 72, 20, 0.92, 0.26, 0.42, { 28, 40, 20, 30 } },     // poppies
        { 1, 1.5, 52, 24, 0.60, 0.45, 0.75, { 32, 46, 22, 32 } },     // lupine
        { 2, 0.9, 64, 18, 0.75, 0.40, 0.75, { 20, 30, 14, 22 } },     // tufts
    } };
    static constexpr double oakCell = 28.0, oakNearDistance = 140.0, oakMidDistance = 420.0;
    static constexpr int oakW = 84;
    static constexpr double poleStep = 42.0, poleSide = 7.0, poleAhead = 1300.0;
    static constexpr int bfCount = 44, bfBurst = 5;
    static constexpr double bfBox = 70.0;
    static constexpr int pollenCount = 1400, fluffCount = 360, petalSlots = 4, petals = 150, birds = 11;
    static constexpr V3 pollenBox { 36, 7, 36 }, fluffBox { 40, 9, 40 };
    static constexpr int shN = 256, shStep = 8, shCoarse = 4;           // the shadow map: 384 m, toroidal
    static constexpr double shTexel = 1.5, shAhead = 100.0;
    static constexpr std::array<double, 3> rungs { 1.0, 0.75, 0.5 };
    /** Where the pole's three wires hang (local, before the height's scale): the crossarm's ends and the top. */
    static constexpr std::array<V3, 3> wireAt { { { -0.85, 8.52, 0 }, { 0.85, 8.52, 0 }, { 0, 9.1, 0 } } };
    /** The butterflies on the glass (PARK): the spot (px of the 1672 x 941 stage), the half span, the turn, when it lands. */
    struct GlassSpot { double x, y, r, a, at; };
    static constexpr std::array<GlassSpot, 5> glassSpots { {
        { 430, 255, 64, -0.35, 1.2 }, { 650, 470, 54, 0.28, 3.4 }, { 1190, 235, 70, 0.12, 5.6 }, { 1395, 430, 58, -0.22, 8.5 },
        { 1050, 545, 50, 0.42, 12.0 } } };
    static constexpr V2 stage { 1672, 941 };

    //==========================================================================
    /** The hashes the shaders carry too (as underwater's). */
    static uint32_t ih (uint32_t x) noexcept;
    static double hcell (int32_t cx, int32_t cy, int32_t s) noexcept;     // 0..1, f32-rounded
    static double hs (int32_t i, int32_t s) noexcept;                     // 0..1, f32-rounded
    /** A value rounded to the nearest half float (what an RGBA16F texel holds), as a double. */
    static double toHalf (double v) noexcept;
    /** The camera's right, up and forward for a heading and a pitch (positive = nose up). */
    struct Basis { V3 r, u, f; };
    static Basis cameraBasis (double heading, double pitch) noexcept;
    static V3 normalised (const V3& v) noexcept;
    /** A mesh's lean [x, z] (radians toward that way) as a 3 x 3 rotation times k, column-major (the prototype's tiltOf). */
    static std::array<float, 9> tiltOf (V2 lean, double k = 1) noexcept;

    /** The textures: RGBA (half-float values in floats: R the hills, G the groves, B the swells, A the bumps), the
        detail (RGBA8: the strokes' gradient in RG, their value in B, the grain in A), the noise (RGBA8: the clouds,
        their detail, a warp, the far ridges' profiles). */
    static std::vector<float> makeHeight();
    static std::vector<uint8_t> makeDetail();
    static std::vector<uint8_t> makeNoise();

    //==========================================================================
    /** THE LOOP (static: it is the same for every world). */
    struct Radius { double R = 0, dR = 0; };
    static Radius loopR (double th) noexcept;
    static V2 loopPoint (double th) noexcept;
    /** The arclength table: entry i the length from angle 0 to angle i / loopN of a turn (loopN + 1 entries). */
    static const std::vector<double>& loopTable();
    static double loopLength();
    static double modL (double v);
    static double thetaAt (double s);
    static V2 tangentAt (double th) noexcept;
    static V2 rightAt (double th) noexcept;
    static double sOfTheta (double th);
    static V2 roadPointAt (double s, double d);
    static int polesN();                       // a whole number of poles round the loop...
    static double poleDs();                    // ...this far apart
    static int dashesN();                      // the centre line's dashes round the loop
    static V3 sunDir() noexcept;               // toward the sun
    static V2 wind() noexcept;                 // the breeze, m/s (x, z)

    //==========================================================================
    /** THE MESHES, as the prototype makes them: indexed, 16-bit, `stride` floats a vertex. */
    struct Mesh { std::vector<float> v; std::vector<uint16_t> idx; };
    struct Icosphere { std::vector<V3> V; std::vector<std::array<int, 3>> F; };
    static Icosphere icosphere (int sub);
    static Mesh oakMesh (int detail);          // 7 floats: position, normal, part
    static Mesh poleMesh();
    static Mesh pumpTowerMesh();
    static Mesh pumpFanMesh();
    static std::array<Mesh, 3> floraMeshes();  // 13 floats: position, normal, colour, (u, v), flag, sway
    static Mesh butterflyMesh();               // 6 floats: position, the wing's pattern point, part

    //==========================================================================
    /** THE STATE a frame is a function of: the JS world's `state`, field for field. */
    struct Pendulum { double th = 0, thV = 0, ph = 0, phV = 0; };
    struct Burst { double t = 1e4, x = 0, z = 0, fx = 0, fz = -1, level = 0; int seed = 0; };
    struct Flock { bool on = false; int n = -1; double x = 0, y = 0, z = 0, fx = 1, fz = 0, t = 0, vis = 0; };
    /** A butterfly on the glass: 0 away, 1 waiting, 2 arriving, 3 on its spot, 4 flying off; screen xy (NDC). */
    struct GlassFly { int st = 0; double t = 0, x = 0, y = 0, k = 0, open = 1, a = 0, rot = 0, fx = 0, fy = 0, fk = 0; };
    struct State
    {
        double sr = startS;
        int lap = 0;
        double lat = lane, latV = 0, latA = 0, x = 0, z = 0, heading = 0, pitch = basePitch, camY = 0, travel = 0;
        double time = 0, speed = gearSpeed[0], aFwd = 0, aLeft = 0, dt = 0, pitchOffset = 0, park = 0, parkT = 0;
        double hitT = 1e4, hitS = 0;
        int hits = 0;
        double startleT = 1e4, startleS = 0;
        Pendulum pend;
        V3 pollenFlow {}, fluffFlow {};
        std::array<Burst, petalSlots> bursts {};
        double findFade = 0;
        Flock flock;
        std::array<GlassFly, glassSpots.size()> glass {};
    };
    /** A butterfly: its constants, and where the last step put it (relative to the camera's xz, height over the eye). */
    struct Butterfly
    {
        double ax = 0, az = 0, alt = 0, scale = 0, A = 0, w1 = 0, w2 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, rate = 0, glideW = 0, phase = 0;
        V3 rel {};
        double yaw = 0, flap = 0, pitch = 0, vis = 0, dist = 0;
    };
    /** A flying saucer. */
    enum class Mode { roam, swoop, pair };
    enum class Anchor { front, side, park, prep };
    struct Swoop { std::array<V3, 5> pts {}; std::array<double, 5> ts {}; V3 v0 {}; };
    struct Saucer
    {
        int i = 0;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, up = 60;
        bool front = true;
        double ph = 0, loopR = 25, loopW = 0.3;
        Mode mode = Mode::roam;
        double mt = 0;
        std::optional<Swoop> path;
        V2 lean {};
        double yaw = 0, beam = 0;
        bool beamOn = false;
        double beamT = 0, anchorT = 0, as = 0, lat = 0, follow = 0;
        Anchor kind = Anchor::front;
    };
    /** The yellow submarine, crossing. */
    struct Sub
    {
        bool on = false;
        double t = 0, as = 0;
        int from = -1;
        double up = 120, yb = 0, x = 0, y = 0, z = 0, yaw = 0;
        V2 lean {};
        double fade = 0;
        int pass = -1, k = 0;
        double lift = 1;
    };

    //==========================================================================
    /** THE THINGS ON THE LOOP. */
    struct Oak { double x = 0, z = 0, y = 0, R = 0, s = 0, yaw = 0, tint = 0, vis = 1; };
    struct Pole { int k = 0; double x = 0, z = 0, y = 0, h = 0, yaw = 0, lean = 0; V2 side {}; };
    /** A rare find: 0 the peace sign of flowers (u, v: its axes on the slope; R its radius; k its stretch up the
        slope), 1 the Feynman van, 2 the monolith (nx, nz: its front). */
    struct Find { int kind = 0; double x = 0, z = 0, y = 0; V3 u {}, v {}; double R = 0, k = 0, nx = 0, nz = 0; };
    /** A box of the signs, the fences or a placeholder (world xz absolute, y up): part 0 wood, 1 a board with a face,
        2 a banner's or a flag's cloth, 3 a placeholder; rect the atlas's uv (x < 0: none) or a placeholder's colour;
        ph the sprite a placeholder stands in for ("" for none). */
    struct Box { double x = 0, y = 0, z = 0, yaw = 0, hx = 0, hy = 0, hz = 0; int part = 0; V4 rect { -1, 0, 0, 0 }; double vis = 0, flutter = 0; std::string ph; };
    struct Pump { double x = 0, z = 0, y = 0, vis = 0, phase = 0; };
    struct Placeholder { double w = 0, h = 0, d = 0, up = 0; V3 col {}; };
    /** A thing by the road, on a hill or off it: where it stands and which way its front looks. */
    struct SpritePlace
    {
        std::string name, label, mesh, group;
        double at = 0;
        bool hill = false;
        double x = 0, z = 0, y = 0, nx = 0, nz = 0, pull = 0;
        Placeholder ph;
        double vis = 0, scale = 1;
        bool paint = false;
        double seen = 0;
        bool hasPart = false;
        V2 part {};
        bool noBox = false, noJump = false;
    };
    struct Dish { double x = 0, z = 0, y = 0, nx = 0, nz = 0; };
    struct Chalk { double x = 0, z = 0, tx = 0, tz = 0, hw = 0, hl = 0; V4 cell {}; double at = 0, d = 0; std::string name; };
    /** An entry of the jump list: lap and kind only for the finds (-1 for the rest). */
    struct Place { std::string group, name; double at = 0, seen = 0; int lap = -1, kind = -1; };

    //==========================================================================
    /** THE FILES THE WORLD READS, as the prototype's view reads them. */
    /** A sprite table (sprites.txt): the cell size, px per metre (0: none said), the atlas grid, the anchor, the cells. */
    struct SpriteTable { V2 cell { 512, 288 }; double scale = 0; std::array<int, 2> grid { 8, 4 }; V2 anchor { 256, 144 }; int count = 0; };
    static SpriteTable parseSpriteTable (std::string_view text);
    /** The pendant's box on the stage (x0 y0 x1 y1, px) and its pivot; none unless both are there. */
    struct PendantBox { V4 box {}; V2 pivot {}; };
    static std::optional<PendantBox> parsePendantBox (std::string_view text);
    /** The signs' table (signs.txt). */
    struct Sign { std::string name, shape, post; V4 cell {}; V2 size {}; double bottom = 0; };
    static std::vector<Sign> parseSigns (std::string_view text);
    /** The flags' cells (flags.txt) as uv of an atlas of w x h px. */
    static std::vector<V4> parseFlagCells (std::string_view text, int atlasW, int atlasH);
    /** The chalk's table (chalk.txt), cells as uv of an atlas of w x h px, sizes in metres (along, across). */
    struct ChalkDrawing { std::string name; bool optional = false; V4 cell {}; V2 size {}; };
    static std::vector<ChalkDrawing> parseChalk (std::string_view text, int atlasW, int atlasH);
    /** A mesh's .txt: its textures by row (texture <n> <file>; "" where none), its censored headstocks' boxes. */
    struct Headstock { std::string name; V3 c {}, s {}; };
    struct MeshInfo { std::vector<std::string> textures; std::vector<Headstock> censor; };
    static MeshInfo parseMeshInfo (std::string_view text);
    /** A CBMESH1 file: its 40-byte vertices as they are, its 32-bit indices; with parts (ranges of x, the file's
        metres), the triangles sorted by their centre's x into them, each a range of the indices. None if the file is
        not one or is short. */
    struct CbMesh
    {
        uint32_t vertices = 0, indices = 0;
        std::vector<uint8_t> vertexBytes;
        std::vector<uint32_t> idx;
        struct Part { V2 range {}; uint32_t first = 0, count = 0; double cx = 0; };
        std::vector<Part> parts;
    };
    static std::optional<CbMesh> parseCbMesh (const uint8_t* data, size_t size, const std::vector<V2>& parts = {});
    /** WHERE THE CABIN LEAVES THE VIEW BARE: the box round every pixel of the dashboard that is not opaque, grown by
        `margin` px, as fractions of the picture (left, top, right, bottom); none if it is opaque everywhere. */
    static std::optional<V4> glassBounds (const uint8_t* rgba, int w, int h, int margin);
    /** The window's mask for a press, the panel's cover, the quality ladder: the sea's, the same for every cabin. */
    using Glass = underwater::UnderwaterWorld::Glass;
    using PanelMask = underwater::UnderwaterWorld::PanelMask;
    using Quality = underwater::UnderwaterWorld::Quality;
    /** A shadow map's rectangle of world texels (as updateShadow lists them) as up to four pieces of the toroidal
        texture: x, y, w, h each. */
    static std::vector<std::array<int, 4>> shadowPieces (const std::array<int, 4>& rect);

    //==========================================================================
    CaliforniaWorld() noexcept;
    ~CaliforniaWorld();

    /** Where the drive starts (call before build()): metres round the loop, a lap (the finds' places follow it). */
    void startAt (double sr, int lap) noexcept { startSr = sr; startLap = lap; started = true; }
    /** Makes the textures, the maps and everything on the loop. Once, tens of milliseconds. */
    void build();
    bool isBuilt() const noexcept { return built; }

    const std::vector<float>& height() const noexcept { return heightPx; }
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }
    const std::vector<uint8_t>& noise() const noexcept { return noisePx; }
    const std::vector<float>& roadMap() const noexcept { return roadPx; }
    const std::vector<float>& landMap() const noexcept { return landPx; }

    /** One step of the drive, the pendant, the butterflies, the saucers, the submarine, the flock. */
    void step (double dt);
    void setGear (int g) noexcept;                     // 1..2
    void setParked (bool on) noexcept { parked = on; }
    void setSteer (double) noexcept {}                 // no steering: the van keeps to its lane round the loop
    /** A look down (negative) or up, radians, added to the pitch (clamped to +-0.5). */
    void setPitchOffset (double rad) noexcept;
    /** The speed where the gear (or PARK) is taking it, at once (and on the shoulder, if parked). */
    void settle() noexcept;
    /** The camera evaluated where the van stands, as the first steps would leave it settled: its height over the road
        and its pitch with the offset and the grade. For a world that has not stepped yet (a poster, a first frame). */
    void settleCamera() noexcept;
    /** Puts the van at arclength sr on lap `lap` (< 0: the lap it is on). */
    void setAt (double sr, int lap = -1);
    /** The arclength 350 m before the next find ahead (FIND). */
    double nextFindAt() const;
    /** Puts the van a few seconds before a place of the jump list comes into view, keeping the gear; a find's lap with
        it. Returns where (whole metres) and the lap. */
    struct Jump { double s = 0; int lap = 0; };
    Jump jumpTo (const Place& p);
    /** One attack of the playing, 0..1: a flash and a glow running out, petals, butterflies starting up, the pendant kicked. */
    void hit (double level) noexcept;
    /** No answer at all: an attack's light, petals and startle called off. */
    void quiet() noexcept;

    int gear() const noexcept { return gearWanted; }
    bool isParked() const noexcept { return parked; }
    double speedOf() const noexcept { return speedFor (gearWanted); }
    const State& state() const noexcept { return s; }
    const std::vector<Butterfly>& butterflies() const noexcept { return bf; }
    const std::array<Saucer, 2>& saucers() const noexcept { return sau; }
    /** Seconds since a saucer was last in the front view. */
    double saucerSeen() const noexcept { return sc.sinceSeen; }
    const Sub& sub() const noexcept { return yellow; }

    /** The ground's height, the mirror of the shaders' floorH: metres at absolute world xz (0 before build()). */
    double floorHeight (double px, double pz) const noexcept;
    /** The sun's visibility past the hills at absolute xz (the road map's B). */
    double sunAt (double x, double z) const noexcept;
    /** The far ridges' crest at world azimuth az (turns), layer k: the tangent of its elevation over the eye at alt. */
    double ridgeTan (double az, int k, double alt) const noexcept;

    /** The oaks in the window round the camera (84 x 84 cells, worked out again when the camera crosses a cell). */
    const std::vector<Oak>& listOaks();
    /** The poles from two behind the van to 1.3 km ahead, in order. */
    std::vector<Pole> listPoles();
    Pole poleAt (int k);
    /** Keeps the shadow map's window round the camera (its centre 100 m ahead), drawing the strips that came in (each
        listed in shadowUpdates()); returns the window's centre (world metres). */
    V2 updateShadow();
    const std::vector<uint8_t>& shadowPixels() const noexcept { return shPix; }
    const std::vector<std::array<int, 4>>& shadowUpdates() const noexcept { return shUpdates; }
    void clearShadowUpdates() noexcept { shUpdates.clear(); }
    /** The window's corner, world texel indices (none before the first updateShadow). */
    std::optional<std::array<int, 2>> shadowCorner() const noexcept { return shO; }
    /** The light at the van (1 lit .. 0 in a shadow): the dashboard and the pendant dim in a pole's or an oak's shadow. */
    double lightAtVan();

    /** The rare finds this lap: one of each kind, the kinds turning round the three places lap by lap. */
    std::array<Find, 3> findsNow();
    /** The signs' boxes from their table, with the atlas's size (0: no atlas yet, the faces placeholders). */
    void buildSigns (const std::vector<Sign>& table, int atlasW, int atlasH);
    const std::vector<Box>& signBoxes() const noexcept { return boxes; }
    const std::vector<Pump>& pumps() const noexcept { return pumpList; }
    const std::vector<Box>& flags() const noexcept { return flagList; }
    /** The flags' cloth from flags.txt: each flag a cell in turn. */
    void setFlagCells (const std::vector<V4>& cells);
    const std::vector<SpritePlace>& spritePlaces() const noexcept { return spriteList; }
    const std::vector<Dish>& dishes() const noexcept { return dishList; }
    const std::vector<Chalk>& chalks() const noexcept { return chalkList; }
    /** The chalk from its table; wink: the optional ones too (on in the app, off on the site). */
    void placeChalk (const std::vector<ChalkDrawing>& table, bool wink = true);
    /** A sprite's own picture or mesh has come: its placeholder box goes. */
    void spriteCame (const std::string& name);
    /** EVERYTHING ON THE LOOP (the jump list), the finds by lap first, then all by the way round. */
    std::vector<Place> places() const;

private:
    struct SignPost { std::string key; double at = 0, x = 0, z = 0, y = 0, nx = 0, nz = 0, yaw = 0, rx = 0, rz = 0, vis = 0; bool banner = false; V2 r {}; };
    struct Pull { double t = 0, v0 = 0, lat0 = 0; int gear = 1; double T = 0; };
    struct Pair { double as = 0, lat = 0, up = 0, x = 0, y = 0, z = 0, rx = 1, rz = 0; int beamer = 0; };
    struct SaucerControl
    {
        int n = 0;
        double sinceSeen = 0, nextSwoop = 0, nextPair = 0, nextBeam = 0;
        int swoopPass = -1, called = -1;
        std::optional<Pair> pair;
    };
    struct Caster { double ax, az, bx, bz, r, dark, soft; };

    bool built = false, started = false;
    double startSr = startS;
    int startLap = 0;
    std::vector<float> heightPx, roadPx, landPx;
    std::vector<uint8_t> detailPx, noisePx;
    State s;
    int gearWanted = 1;
    bool parked = false, wasParked = false;
    double flockNext = flockFirst;
    int flockCount = 0;
    std::optional<Pull> pull;
    double prevSpeed = gearSpeed[0];
    std::vector<Butterfly> bf;
    std::array<Saucer, 2> sau;
    SaucerControl sc;
    Sub yellow;
    std::optional<std::array<std::array<Find, 3>, 3>> findPlaces;
    std::unordered_map<int64_t, std::optional<Oak>> oakCache;
    std::vector<Oak> oaks;
    std::optional<std::array<int64_t, 2>> oakBase;
    std::unordered_map<int, Pole> poleCache;
    std::vector<std::pair<std::string, std::vector<SignPost>>> signPosts;
    std::vector<std::pair<std::string, std::string>> signNames;
    std::vector<Box> boxes, fenceBoxes, flagList;
    std::vector<Pump> pumpList;
    std::vector<SpritePlace> spriteList;
    std::vector<std::string> spritesIn;
    std::vector<Dish> dishList;
    std::vector<Chalk> chalkList;
    std::vector<uint8_t> shPix;
    std::optional<std::array<int, 2>> shO;
    std::vector<std::array<int, 4>> shUpdates;
    bool shadowAll = false;

    double speedFor (int g) const noexcept;
    double tap (int ch, double u, double v) const noexcept;
    double noiseTap (int ch, double u, double v) const noexcept;
    double roadHeight (double th, double R) const noexcept;
    V3 roadDist (double x, double z) const noexcept;
    V3 roadTap (double x, double z) const noexcept;
    void place() noexcept;
    void stepButterflies (double dt);
    void stepGlass (double dt);
    void glassPark (bool on);
    Find placeFind (int slot, int kind);
    bool oakNear (double x, double z, double r);
    const std::optional<Oak>& oakAt (int64_t ci, int64_t cj);
    Pole pole (int k);
    std::vector<Box> placeholderBoxes() const;
    double sh (int k);
    void anchor (Saucer& u, Anchor kind);
    V3 roamTarget (const Saucer& u, double t) const;
    void resetSaucers();
    void startSwoop (Saucer& u);
    void stepSaucers (double dt);
    void stepSub (double dt);
    std::vector<Caster> casters (double x0, double z0, double x1, double z1) const;
    void rasterRect (int i0, int j0, int i1, int j1);
};

} // namespace orbitamp::california
