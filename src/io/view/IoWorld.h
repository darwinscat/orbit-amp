// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orbitamp::io
{

/**
    THE IO WORLD'S CPU HALF: the surface of Jupiter's volcanic moon, the live view of the SansAmp RBI bass amp.

    A port of the view's prototype, io-world.js (all of it: the textures, the height mirror, the loop, the Sun from two
    knobs, the paterae, the plumes and the fountains, the flows baked into the feature map, the hardware and its
    stand-in meshes, the rocks, the drive, the knobs, the volcanoes' activity and its history, the dosimeter), and of
    what io-gl.js works out on the CPU for each frame (the surface's light, the skyline toward a low Sun, the lava's
    and the hardware's lights, the paterae's uniforms, the aurora, the plumes' bodies and the emitters, the instances
    of the hardware and the rocks: Frame), the rocks' mesh, and the readers of the finds' files (CBMESH1 meshes, the
    credit lines, the skylight's pit radius). Plain deterministic float and int functions, JUCE-free: the same state
    and time give the prototype's numbers, Math.fround where it rounds, half floats where it stores them, the same
    32-bit hashes and V8's own Math functions (IoJs.h). Matched number for number by tests/IoWorldTests.cpp against
    the prototype's own dump.

    THE LEVER: gears 1..2 are CRUISE SLOW and CRUISE FAST; PARK is setParked. There is no TURBO. hit (level) is one
    attack of the playing. The amp's knobs are the world's inputs (setKnob, each eased): DRIVE the time of day, BLEND
    Jupiter's phase (down to the eclipse at 0), PRESENCE the sulphur snow; the attacks wake the volcanoes.
*/
class IoWorld final
{
public:
    using V2 = std::array<double, 2>;
    using V3 = std::array<double, 3>;
    static constexpr double pi = 3.141592653589793, deg = pi / 180;

    //==========================================================================
    // THE DRIVE
    static constexpr int numGears = 2;
    static constexpr std::array<double, 2> gearSpeed { 4.5, 9.5 };            // m/s
    static constexpr double speedSeconds = 1.0, parkSeconds = 1.6, pullSeconds = 4.0, pullPerMs = 0.3;
    static constexpr double eye = 2.6, basePitch = -2.0 * pi / 180, lane = 1.9, parkLat = 5.4;
    static constexpr double driveSeconds = 1.4, blendSeconds = 1.6, presenceSeconds = 1.2;
    /** The volcanoes' activity from the hits (io-world.js ACT). */
    struct ActMap { double quiet, perHit, tau, cap, autoMin, autoMax; };
    static constexpr ActMap act { 0.18, 0.065, 10.0, 1.6, 0.25, 0.60 };
    /** The cabin's dosimeter (DOSE): its rest, three sines, the jitter, the kick's saturation and decay, the needle's spring. */
    struct DoseMap { double rest; std::array<std::array<double, 3>, 3> sines; double jitter, jitterTau, span, k, tau, w0, zeta, bounce; };
    static constexpr DoseMap dose { 0.13, { { { 0.009, 0.31, 0.0 }, { 0.005, 0.83, 1.7 }, { 0.003, 1.97, 0.6 } } }, 0.007, 0.12,
                                    0.80, 2.5, 0.5, 13.0, 0.5, 0.3 };

    // REAL IO
    static constexpr double ioR = 1821600.0, ioG = 1.796;
    static double jupRho() noexcept;                          // Jupiter's angular radius from Io (9.76 degrees)
    static constexpr double sunRho = 0.0510 * deg;

    static constexpr double worldPeriod = 48000.0;
    static constexpr int texSize = 256, detailSize = 256, noiseSize = 256, cellN = 256;
    static constexpr double l0 = 2400.0, l1 = 300.0, l2 = 48.0;
    static constexpr V2 off1 { 0.37, 0.61 }, off2 { 0.71, 0.13 };
    static constexpr V2 loopC { 24520.0, 24680.0 };
    static constexpr double loopRadius = 760.0;
    static constexpr std::array<double, 4> loopK { 2, 3, 5, 7 }, loopA { 200.0, 40.0, 22.0, 10.0 }, loopP { 2.8623, 2.1, 4.0, 1.3 };
    static constexpr int roadN = 256, loopN = 8192, rhN = 2048;
    static constexpr double roadSpan = 3200.0, roadHalf = 4.0;
    static constexpr int featN = 1024;
    static constexpr double featSpan = 4800.0, startAt0 = 0.0;
    static constexpr int maxCald = 12, histN = 256, histRows = 32;
    static constexpr double histDt = 0.25, structCell = 70.0;
    static constexpr int structWin = 40, numVariants = 10;
    static constexpr V2 roadOrg { loopC[0] - roadSpan / 2, loopC[1] - roadSpan / 2 }, featOrg { loopC[0] - featSpan / 2, loopC[1] - featSpan / 2 };
    static constexpr double pitR = 13.0, pitDepth = 15.0, rockCell = 4.5;
    static const std::array<const char*, numVariants> variants;    // mast, dish, module, lander, station, rover, base, tubebase, observatory, plant

    //==========================================================================
    // HASHES, THE TEXTURES (as the prototype makes them: the base in half floats, the rest RGBA8)
    static uint32_t ih (uint32_t x) noexcept;
    static double hcell (double cx, double cy, double s) noexcept;       // 0..1, an f32 value
    static double hs (double i, double s) noexcept;                      // 0..1, an f32 value
    static double toHalf (double v) noexcept;
    static std::vector<float> makeBase();       // RGBA16F 256^2: R the swell, G the mesas' field, B lumps, A colour regions
    static std::vector<uint8_t> makeDetail();   // RGBA8 256^2
    static std::vector<uint8_t> makeNoise();    // RGBA8 256^2: the sky's and Jupiter's noises
    static std::vector<uint8_t> makeCells();    // RGBA8 256^2: the lakes' crust and the flows' breakouts

    //==========================================================================
    // THE LOOP
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
    struct Near { double at = 0, d = 0; };
    static Near nearestOnLoop (double x, double z);
    struct Basis { V3 r, u, f; };
    static Basis cameraBasis (double heading, double pitch) noexcept;

    //==========================================================================
    // THE SKY: Jupiter fixed, the Sun from DRIVE (its height) and BLEND (its angle from Jupiter), Europa and Ganymede
    static V3 dirOf (double az, double el) noexcept;
    struct JupFrame { V3 J, R, U, axis; };
    static double jupEl() noexcept;
    static double jupAz();
    static const JupFrame& jupFrame();
    /** The Sun's map from the knobs (io-world.js SUN_MAP): the Sun's height at DRIVE 0 (top) and 1 (bottom, under the
        skyline), the DRIVE where it crosses the skyline (horizon: 0.65 since 20.11, the owner's "9 o'clock full day, 12 still
        day, 3 dark, 5 the darkest"; it was 0.5), its rate a unit of DRIVE there (dusk), and BLEND's eclipse and window. */
    struct SunMap { double top, bottom, horizon, dusk, deep, contact, win, winSep, softTop, softPull; V2 gate, night, adapt, cabin; double graze; };
    static constexpr SunMap sunMap { 17 * deg, 11 * deg, 0.65, 40 * deg, 4.6 * deg, 0.06, 0.35, 40 * deg, 20 * deg, 3 * deg,
                                     { -3.6 * deg, -2.6 * deg }, { -4 * deg, 7 * deg }, { -11 * deg, 7 * deg }, { -0.2 * deg, 5 * deg }, 4 * deg };
    /** The Sun: its look (sunLook) and its way (sunSolve), as the prototype's object has them. */
    struct Sun
    {
        double sep = 0, vis = 0; V3 hue {}; double light = 0, flat = 0, graze = 0, ring = 0, depth = 0, u = 0, eclAir = 0, eclExpo = 0, eclDark = 0;
        V3 dir {}; double el = 0, az = 0, eA = 0, gate = 0, night = 0, dark = 0, airglow = 0, expo = 1, cabin = 0;
    };
    /** The Sun's height over the skyline DRIVE asks (radians): a cubic each side of SunMap::horizon, flat at noon (DRIVE
        0) and at the deepest night (1), at the rate dusk where it crosses (C1). The day lasts to 0.65: 14.9 degrees at
        0.2, 5.9 at 0.5 (a low Sun, still day), 0 at 0.65, -6.3 at 0.8 (the full night), -11 at 1. With the horizon at 0.5
        it is the old map exactly. */
    static double sunHeightAt (double drive, const SunMap& map) noexcept;
    static double sunHeightAt (double drive) noexcept { return sunHeightAt (drive, sunMap); }
    static double sunSepAt (double blend) noexcept;
    static Sun sunLook (double sep, double late);
    static Sun sunSolve (double drive, double blend);
    /** The old path (the second DRIVE map, on no knob now: for a planetarium). */
    static double pathSepAt (double x);
    static Sun pathSun (double x);
    struct Moon { const char* name; V3 dir; double rho; V3 alb; };
    static const std::array<Moon, 2>& moons();

    //==========================================================================
    // THE VOLCANOES AND THE HARDWARE: the tables, and the places makeWorld works out from them
    struct Caldera
    {
        std::string name; double at = 0, d = 0, R = 0, W = 0, D = 0, rim = 0, lake = 0, th = 0; bool big = false, near = false, pit = false;
        int i = 0; double x = 0, z = 0, baseY = 0, out = 0, lakeR = 0;
    };
    struct Plume
    {
        std::string name; bool far = false; V2 farAt {}; double at = 0, d = 0, H = 0, th = 0; bool pele = false; double base = 0;
        std::optional<double> view;
        int i = 0; double x = 0, z = 0, y = 0; int row = 0; double ring = 0; Near near;
    };
    struct Fountain { int cald = 0; double toward = 0, H = 0, th = 0; int i = 0; double x = 0, z = 0, y = 0; int row = 0; };
    struct Flow { std::vector<V3> pts; double fresh = 0; bool fissure = false; };     // pts: x, z, width
    struct Structure
    {
        int variant = 0; double x = 0, z = 0, y = 0, yaw = 0, k = 1; V2 lean {}; bool onFlow = false, site = false;
        double ci = 0, cj = 0, seed = 0;
    };
    struct Site { double at = 0; std::vector<Structure> items; std::string name; };
    struct Station { std::string name; double at = 0, seen = 0, r = 0; std::vector<Structure> items; };
    struct Rock { double x = 0, z = 0, y = 0, yaw = 0, sx = 0, sy = 0, sz = 0, tint = 0; };
    struct Puff { double x = 0, y = 0, z = 0, t = 0, k = 0, R = 0; int seed = 0; };

    //==========================================================================
    IoWorld() noexcept = default;

    /** Makes the world: the textures, the road map, the paterae, the plumes, the flows and the feature map, the sites
        and the stations (makeWorld). Everything below reads the world built. */
    void build();
    bool isBuilt() const noexcept { return built; }

    const std::vector<float>& base() const noexcept { return basePx; }
    const std::vector<uint8_t>& detail() const noexcept { return detailPx; }
    const std::vector<uint8_t>& noise() const noexcept { return noisePx; }
    const std::vector<uint8_t>& cells() const noexcept { return cellsPx; }
    /** RGBA16F 256^2 over 3.2 km round the loop: R the signed distance from the centreline, G the road's height (half
        floats), B the patera's index + 1 whose reach the texel is in. */
    const std::vector<float>& roadMap() const noexcept { return roadPx; }
    /** RGBA8 1024^2 over 4.8 km: R the flows' signed distance (0.5 at the margin, +-24 m), G freshness, B red, A frost. */
    const std::vector<uint8_t>& featureMap() const noexcept { return featPx; }
    /** The emitters' activity history, R8 256 x 32 (a quarter second a texel, a row an emitter): uploaded each frame. */
    const std::vector<uint8_t>& history() const noexcept { return hist; }

    double tap (int ch, double u, double v) const noexcept;
    double plainH (double px, double pz) const noexcept;
    double roadHeight (double th) const noexcept;
    std::array<double, 3> roadDist (double x, double z) const noexcept;
    std::array<double, 3> calderaField (double px, double pz) const noexcept;
    V2 roadTap (double x, double z) const noexcept;
    std::array<double, 4> featTap (double x, double z) const noexcept;
    double terrainH (double px, double pz, double ad) const noexcept;
    double floorHeight (double px, double pz) const noexcept;
    double slopeAt (double x, double z, double r) const noexcept;

    const std::vector<Caldera>& calderas() const noexcept { return calderas_; }
    const std::vector<Plume>& plumes() const noexcept { return plumes_; }
    const std::vector<Fountain>& fountains() const noexcept { return fountains_; }
    const std::vector<Flow>& flows() const noexcept { return flows_; }
    const std::vector<Site>& sites() const noexcept { return sites_; }
    const std::vector<Station>& stations() const noexcept { return stations_; }
    /** The hashed single piece in an unwrapped 70 m cell, or none. */
    std::optional<Structure> structureAt (double ci, double cj) const;
    /** Every piece of hardware in the window round (x, z): the hashed singles, the sites, the stations. */
    std::vector<Structure> structuresNear (double x, double z, int cellsN = structWin) const;
    std::optional<Rock> rockAt (double ci, double cj) const;
    std::vector<Rock> rocksNear (double x, double z, double fx, double fz, double reach) const;
    /** The skylight's radius from its mesh's .txt (the pit in the ground follows). */
    void setPitRadius (double r) noexcept;

    static double plumeAct (const Plume& p, double b) noexcept;
    static double fountainAct (const Fountain& f, double b) noexcept;
    static double lakeAct (const Caldera& c, double b) noexcept;

    //==========================================================================
    /** THE DRIVE AND THE KNOBS, as the prototype's state has them. */
    struct State
    {
        double sr = 0; int lap = 0; double lat = lane, latV = 0, x = 0, z = 0, heading = 0, pitch = basePitch, camY = 0, travel = 0, time = 0;
        double speed = gearSpeed[0], park = 0, pitchOffset = 0, dt = 0; int gear = 1; bool parked = false;
        double drive = 0, blend = 1, presence = 0.2; Sun sun; double sunK = 1, dark = 0, jupLit = 0.9, cabinDay = 1;
        double act = IoWorld::act.quiet, actE = 0, actHold = 0; bool autoHits = false; double autoNext = 0; int autoN = 0;
        double hitT = 1e4, hitS = 0; int hits = 0; std::vector<Puff> puffs;
        double dose = IoWorld::dose.rest, doseV = 0, doseE = 0, doseJ = 0; int doseN = 0;
    };
    enum class Knob { drive, blend, presence };

    void step (double dt);
    void setGear (int g) noexcept;                      // 1..2
    void setParked (bool on) noexcept { parked = on; s.parked = parked; }
    void setKnob (Knob k, double v) noexcept;
    std::array<double, 3> knobs() const noexcept { return { want[0], want[1], want[2] }; }   // drive, blend, presence as asked
    /** Everything at its target at once (a page opening, a shot): no easing from the defaults. */
    void settle();
    /** Before the first step: the camera evaluated where the rover stands, as the steps would leave it settled. */
    void settleCamera() noexcept;
    /** The rover put at sr metres round the loop (the prototype's setAt; lap < 0 keeps the lap). */
    void startAt (double sr, int lap = -1);
    /** The volcanoes' activity set (0..1 on its own scale) and held as a floor the hits add to (hold false: it decays). */
    void setActivity (double a, bool hold = true);
    double activity() const noexcept { return s.act; }
    /** The auto-hits (an attack every 0.25-0.6 s: playing). */
    void setAuto (bool on) noexcept;
    bool isAuto() const noexcept { return s.autoHits; }
    /** One attack: the aurora, the dosimeter's kick, the volcanoes' activity, the nearest vents' puffs. */
    void hit (double level);
    /** An attack under way called off: the aurora, the puffs and the dosimeter's kick (the activity it added stays). */
    void quiet() noexcept;
    void setPitchOffset (double rad) noexcept;
    int gear() const noexcept { return gearWanted; }
    bool isParked() const noexcept { return parked; }
    double speedFor (int g) const noexcept;
    double speedOf() const noexcept { return speedFor (gearWanted); }
    const State& state() const noexcept { return s; }

    //==========================================================================
    /** THE JUMP LIST: everything on the loop the page lists, sorted round it. */
    struct Place { std::string group, name; double at = 0, seen = 0; };
    std::vector<Place> places() const;
    /** A place's jump: the rover put a few seconds before it (the view's jumpTo); s the metres shown, rounded. */
    struct Jump { double s = 0; int lap = 0; };
    Jump jumpTo (const Place& place);

    //==========================================================================
    /** THE SURFACE'S LIGHT (io-gl.js surfaceLight): the key's way and colour, uSunK, the ambient's way and colour. */
    struct Light { V3 dir {}, key {}; double k = 1; V3 adir {}, amb {}; };
    struct Ring { double level = 0, t = 0; };
    static Ring ringLevel (const Sun& sp);
    /** Jupiter's shine on the land (unexposed): as full as its phase, never under a fifth of the full Jupiter's (the owner,
        20.11: a little soft light stays at BLEND 0); the night's adaptation on it. Jupiter's own disc is not this. */
    static V3 jupShineOf (const Sun& sp);
    static Light surfaceLight (const Sun& sp);
    /** The skyline's elevation toward an azimuth from the eye (the ground out to the far cut, falling with the
        curvature, and the far massifs from the noise texture), and the Sun's disc and glare over it. */
    double skylineAt (double az, double camY, double x, double z) const;
    V2 sunOverSkyline (const Sun& sp, double camY, double x, double z) const;
    /** The dosimeter's needle on the dial (degrees counter-clockwise from 3 o'clock at the scale's start and end):
        its way on the plate, (cos a, -sin a), y down. */
    static V2 needle (double doseValue, double fromDeg = 210, double toDeg = -30) noexcept;
    /** The box round a plate's see-through pixels (fractions: x0, y0, x1, y1, y down), or none (the glass). */
    static std::optional<std::array<double, 4>> glassBounds (const uint8_t* rgba, int w, int h, int margin);
    /** A plate's ink round a point (io-gl.js setOverlay: the dosimeter's hub as the plate paints it): the mean of the
        straight RGBA pixels whose centres lie within r px of (x, y), as sRGB 0..1; none when no pixel does. */
    static std::optional<V3> inkAt (const uint8_t* rgba, int w, int h, double x, double y, double r);

    /** WHAT THE RENDERER UPLOADS FOR A FRAME (io-gl.js drawScene's CPU part, in its order): places relative to the
        camera's cell (ccx, ccz). Not const: the hardware's lists round the rover are rebuilt as it crosses a cell (the
        work lights read them as the frame before left them, as the prototype does). */
    struct Instances { int n = 0, nSh = 0; std::vector<float> data; };         // 8 floats each: place, yaw, scale, lean, seed
    struct Frame
    {
        double ccx = 0, ccz = 0; std::array<float, 2> camFrac {}; std::array<float, 3> camRel {};
        V2 off0 {}, off1 {}, off2 {}, fwd {}; double time = 0, hitAge = 0;                 // fwd: the rover's way (x, z)
        V3 sun {}, sunCol {}, jupSunCol {}, jupShine {}, airglow {}; double sunK = 0, dark = 0, air = 0, flat = 0, expo = 1;
        Light ground; std::array<double, 4> term {}; V3 sunUp {}, far {};
        double jupK = 1, sep = 0, jupT = 0, aurora = 0, flux = 0;
        std::array<float, 8> moon {}; std::array<float, 6> moonAlb {};      // Europa's and Ganymede's way and sin (radius); albedo
        std::array<float, 16> pl {}, plc {}; int plN = 0;                     // the lava's two lights: place, reach; colour, strength
        std::array<float, 24> sl {}; int slN = 0; V3 slc {}; std::array<double, 4> slBox {};   // the hardware's work lights
        V3 lavaEnv {};
        std::array<float, maxCald * 4> caldA {}, caldB {}, caldC {}; int caldN = 0;
        double snow = 0, travel = 0, speed = 0; V3 sunV {};
        std::array<Instances, numVariants> hardware;
        std::array<std::vector<float>, 2> rocks;                               // 8 floats each: the stones, the pebbles
        std::vector<std::array<float, 12>> volumes; int volumesInside = 0;     // the plumes' bodies, those round the eye first
        std::array<std::vector<std::array<float, 12>>, 3> emitters;            // the plumes', the fountains', the puffs' particles
    };
    Frame frame();
    /** The hardware round the rover by variant, as the last frame() left it (io-gl.js structs[v].list): what the
        finds' meshes are drawn from, one by one. */
    const std::array<std::vector<Structure>, numVariants>& hardwareNear() const noexcept { return near_; }
    static constexpr double farCut = 3400.0, sh0Half = 130.0, sh0Ahead = 110.0, rockReach = 85.0, sunI = 2.1;
    static constexpr int maxEmit = 24, maxRocks = 700;

    //==========================================================================
    // THE MESHES: the hardware's stand-ins (11 floats a vertex: position, normal, across, material, flag; y up) and the
    // rocks' (6 floats: position, normal)
    static std::vector<std::vector<float>> structureMeshes();
    static std::vector<float> rockMesh (bool fine);

    // THE FINDS' FILES (finds/<variant>3d/<name>.mesh + .txt): a mesh takes its variant's place everywhere
    struct MeshFile { bool ok = false; uint32_t nv = 0, ni = 0; size_t vertexAt = 16, indexAt = 0; };
    static MeshFile readMesh (const uint8_t* bytes, size_t size);
    /** The variant a finds folder fills (its name's start: mast, dish, ...), or -1. */
    static int roleOf (std::string_view folder);
    /** A .txt's credit line, or none for our own models (a line beginning "none"). */
    static std::optional<std::string> creditOf (std::string_view text);
    /** The skylight's pit radius in the lava-tube base's .txt ("pit 13.2" or "radius 13.2"), or none. */
    static std::optional<double> pitRadiusOf (std::string_view text);

private:
    bool built = false;
    std::vector<float> basePx, roadPx;
    std::vector<uint8_t> detailPx, noisePx, cellsPx, featPx, hist;
    std::vector<double> rhTable;
    std::vector<Caldera> calderas_;
    std::vector<Plume> plumes_;
    std::vector<Fountain> fountains_;
    std::vector<Flow> flows_;
    std::vector<Site> sites_;
    std::vector<Station> stations_;
    double pitAt = 0;
    mutable std::map<std::pair<double, double>, std::optional<Structure>> structCache;
    mutable std::map<std::pair<double, double>, std::optional<Rock>> rockCache;

    State s;
    std::array<double, 3> want { 0, 1, 0.2 };
    int gearWanted = 1;
    bool parked = false, wasParked = false;
    struct Pull { double t = 0, v0 = 0, lat0 = 0; int gear = 1; double T = 0; };
    std::optional<Pull> pull;
    int histSlot = -1;
    std::array<std::vector<Structure>, numVariants> near_;     // the hardware round the rover, by variant
    std::string nearKey;

    void place();
    void sky();
    void stepAct (double dt);
    double doseRest (double t) const;
    void stepDose (double dt);
    void writeHist (bool fill);
    void hitNow (double level);
    void makePlaces();
};

} // namespace orbitamp::io
