// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbitamp::titan
{

/** THE METHANE SEA, THE CPU'S HALF. A helicopter flying low over Titan's sea, seen through the whole
    canopy of its cabin: the sea is the plane y = 0 and the far shore a height function marched by the
    GPU, so nothing here has to keep the craft out of the ground — only the autopilot leans away from
    the side with more shore ahead.

    This side makes the two textures once (a four-channel periodic noise and the ripples' slope map, the
    same generators and seeds as the view's prototype, so the same coasts lie in the same places), finds
    the start (open water around, a shore across part of the view), and flies: integrates the craft in
    double precision, eases the gear, banks into turns, dips the nose with speed, bobs; sets out the
    nitrogen fields and the buoy gates ahead on the course; and keeps the canopy — methane drops landing,
    crawling up and out in the airflow, merging, leaving trails, drying, and three wipers, one on each
    pane, crossing every 60 seconds. The timed events — Saturn through the haze, the cryovolcano's
    eruptions, the lightning, the rotor's shadow — are pure functions of the clock.

    Every constant the renderer also needs is here, once: the shaders are written from these numbers.
    No JUCE in here. */
class TitanWorld final
{
public:
    static constexpr double pi = 3.14159265358979323846, deg = pi / 180;

    //==========================================================================
    /** THE GEARS ARE THE PRODUCT'S: as many as its lever has, at most four. The speeds go up from the
        first gear, whatever the count; TURBO is a dash at twice the gear's speed. */
    static constexpr int maxGears = 4;
    static constexpr std::array<double, maxGears> gearSpeed { 2.4, 5.0, 8.6, 13.5 };   // m/s, the first gear first
    static constexpr double turboFactor = 2.0;
    static constexpr double speedSeconds = 0.45, parkSeconds = 0.5, yawSeconds = 0.35, boostSeconds = 0.4;   // tau
    static constexpr double steerYawDegrees = 25, parkedYawDegrees = 8;
    static constexpr double camHeight = 2.6;           // the eye over the sea, metres
    static constexpr double topSpeed = 13.5;           // the fastest gear: the nose dips this far, then harder on a dash

    //==========================================================================
    /** THE VIEW: the cabin's whole canopy, 1672 x 418 of the cabin picture's design pixels (its top 418
        rows of 941), as many degrees a pixel as the rovers' 269.5-pixel windscreen, only taller. */
    static constexpr double cabinWidth = 1672, cabinHeight = 418;
    static constexpr double vfovHalfTan = 0.23086819112556312 * cabinHeight / 269.5;   // tan 13 deg, scaled
    static constexpr double shear = 0.1;                // the horizon 45 % from the top
    static constexpr double winAspect = cabinWidth / cabinHeight;
    static constexpr double rTitan = 2574700;

    /** The far shore: 1024 screen columns, five distance bands of 16 steps, 2..40 km. */
    static constexpr int profileCols = 1024, profileBands = 5, bandSteps = 16;
    static constexpr double profX0 = -1.3, profX1 = 1.3;    // screen x of the profile (the view is -1..1; a margin for reflections)
    static constexpr std::array<double, 5> bandMin { 2000, 3600, 6000, 10500, 18000 }, bandMax { 3600, 6000, 10500, 18000, 40000 };
    static constexpr double tanMin = -0.03, tanMax = 0.17;
    struct Land { double LA, LB, LC, duneP, C0, C1, near0, near1; double o0[2], o1[2], o2[2]; };
    static constexpr Land land { 46000, 13000, 5200, 1700, 0.555, 0.61, 2000, 3600, { 0.137, 0.713 }, { 0.581, 0.269 }, { 0.333, 0.917 } };
    static constexpr double duneAngle = 0.17;           // across the dunes; the crests run roughly along x

    struct Ripple { double L, ang, vel, rms; double o[2]; };
    static constexpr std::array<Ripple, 3> ripples { Ripple { 0.83, 0.35, 0.20, 0.030, { 0.11, 0.52 } },     // capillary ripples
                                                     Ripple { 2.9, -0.22, 0.42, 0.026, { 0.73, 0.18 } },     // wind ripples
                                                     Ripple { 9.7, 0.62, 0.85, 0.014, { 0.37, 0.88 } } };    // a long, low swell
    struct Drift { double L; double wind[2]; double off[2]; };
    static constexpr Drift mist { 900, { 0.7, 0.25 }, { 0.21, 0.64 } };
    static constexpr double mistDensity = 0.0034, mistScaleHeight = 6.0;
    static constexpr Drift cloud { 42000, { 6.0, 1.5 }, { 0.47, 0.05 } };
    static constexpr double cloudHeight = 12000, cloudAmp = 0.10;
    static constexpr Drift oil { 60, { 0.12, 0.05 }, { 0.62, 0.31 } };   // slicks: a 60 m noise tile
    static constexpr double magicCell = 1500, magicP = 0.22, magicT = 110;
    static constexpr double hazeL = 7000, landHazeL = 12000;            // air over the sea / toward the far shore
    static constexpr int dropCount = 2600, sprayCount = 1400;
    static constexpr double dropBox = 64, dropBoxHeight = 11, dropExposure = 1.0 / 70, dropAhead = 18;
    static constexpr double wind[2] { 0.45, 0.18 };
    static constexpr double blurSeconds = 0.7 / 60;    // the motion-blur window for the near sea
    static constexpr double sunAz = 26, sunEl = 9.5;   // a faint patch upper right of the starting heading
    struct Saturn { double az, el, radDeg, tiltDeg, open; };
    static constexpr Saturn saturn { -17, 6.0, 2.85, 13, 0.075 };   // ~11 Moons across
    static constexpr double volcanoAz = 38, volcanoD = 16000, volcanoH = 760;
    static constexpr double stormAz = -33;
    /** The main rotor above the canopy, in the cab's frame (x right, y up, +z back). */
    static constexpr double rotorHub[3] { 0, 1.25, 0.35 }, rotorRadius = 6.8, rotorTilt = 4 * deg;

    //==========================================================================
    /** THE GLASS is a plane in the cab, raked back 32 degrees from vertical, its lower edge 0.70 m ahead
        of the eye on the view's bottom ray. Glass coordinates, metres: s across, t up the slope from the
        lower edge of what the window shows. Everything on it lives there — drops, trails, flakes and the
        wipers — so it shrinks and flattens toward the top. */
    static constexpr double flakeRate = 1.0 / 50, filmRate = 1.0 / 300, flakeCell = 0.0085, fineCell = 0.0033;
    static constexpr double wipeEvery = 60, glassBeta = 32 * deg, glassZb = 0.70, bladeWidth = 1.2;   // the blades' width, times the rovers'
    static constexpr double strokeSeconds = 1.0, pauseSeconds = 0.3, wipeSeconds = 2 * strokeSeconds + pauseSeconds;
    static constexpr double fastStroke = 0.55, fastPause = 0.12, slowRest = 0.6;    // HI's quicker stroke; LO's rest between wipes
    static constexpr int maxDrops = 180, maxTrails = 900;
    static constexpr double trailLife = 3.5;
    static constexpr double glassYb = -(1 + shear) * vfovHalfTan * glassZb;

    /** A point of the window (ndc -1..1) -> where its ray meets the glass, (s, t) in metres. */
    static std::array<double, 2> glassST (double nx, double ny) noexcept;
    /** A point on the glass -> window uv, 0..1 from the bottom-left; th = the frame's horizontal half-tangent. */
    static std::array<double, 2> glassToUV (double s, double t, double th = vfovHalfTan * winAspect) noexcept;
    /** A point on the glass -> the cabin's pixels, x 0..1672, y 0..418 down. */
    static std::array<double, 2> cabinXY (double s, double t) noexcept;
    /** The window's half-width on the glass at t. */
    static double glassHalfW (double t) noexcept;
    /** The top of the glass the window shows, t. */
    static double glassTop() noexcept;

    /** A WIPER: the pivot (s, t), the stroke from th0 to th1 (radians, either way round), the blade from
        rIn to rOut, and its pane — the lines x = a + b y of the pane's two sides in the cabin's pixels,
        y down — so it is seen and wipes only there. One on each of the canopy's three panes, as long as
        its pane (cut at the posts' middles, x = 348 + 0.4 y and 1322 - 0.4 y): the centre one fans 180
        degrees from under the dash up to just below the overhead console, the side ones swing up from
        their outer foot to the post. Parked, all lie under the dash. */
    struct Wiper { double p[2]; double th0, th1, rIn, rOut; double pane[4]; };
    static const std::array<Wiper, 3>& wipers() noexcept;

    //==========================================================================
    /** With the product's gears, 1..maxGears. */
    explicit TitanWorld (int gears) noexcept;

    /** Makes the textures and finds the start, and puts the craft there if nothing has placed it yet.
        Once, tens of milliseconds. */
    void build();
    bool isBuilt() const noexcept { return built; }

    static constexpr int noiseSize = 256, rippleSize = 256;
    /** The noise: RGBA8, noiseSize^2, wrapping — R coastlines and slicks, G mist banks, coast detail,
        dune fields and film, B (ridged) hills and frost streaks, A detail, clouds, froth and vapour. */
    const std::vector<uint8_t>& noise() const noexcept { return noisePx; }
    /** The ripples' slope map: RG the slope x/z scaled to +-1, B slope^2 (so mips keep the roughness),
        A the height; RGBA8, rippleSize^2, wrapping. */
    const std::vector<uint8_t>& ripple() const noexcept { return ripplePx; }
    /** The per-axis RMS of the map's normalised slope: a layer's weight is its target RMS over this. */
    double rippleRms() const noexcept { return rmsAxis; }

    //==========================================================================
    /** The drive, the inputs. */
    void setGear (int g) noexcept;                       // 1..numGears()
    void setTurbo (bool on) noexcept { turbo = on; }
    /** PARK: a hover — the craft comes to a stand over the sea, the downwash rings out under it, the
        hand only turns the view in place. */
    void setParked (bool on) noexcept { parked = on; }
    void setSteer (float s) noexcept;                    // -1..1, held input
    /** THE WIPERS' SPEED, as the four keys under the radio set it: none of their own accord (a wipe
        running finishes), a wipe a minute, on and on with a rest between, on and on with a quicker
        stroke. */
    enum WiperSpeed { wipersOff, wipersInterval, wipersSlow, wipersFast };
    void setWipers (int speed) noexcept { wiperSpeed = speed; }
    int gear() const noexcept { return gearWanted; }
    bool isTurbo() const noexcept { return turbo; }
    bool isParked() const noexcept { return parked; }
    int numGears() const noexcept { return gears; }

    /** The speed the gear and TURBO ask for, before easing. */
    double speedFor (int gear, bool turbo) const noexcept;

    /** One step of the drive. */
    void step (double dt);

    /** Puts the craft somewhere with no history: speed and dash reached, the sea's events and the glass
        begun afresh from here, the flight evaluated directly. */
    void place (double x, double z, double heading, int gear, bool turbo, bool parked);
    /** Where it stands, as the drive would be once settled — the speed and the dash the gear asks for
        reached, the flight evaluated directly — keeping what is on the glass and out on the sea. For a
        still. */
    void settle (int gear, bool turbo, bool parked);
    /** The clock: the craft placed again where it is at `time`, so the events and the wipers count from
        there; `travel` is the metres driven, for the status. */
    void setClock (double time, double travel);
    /** A start with open water around, a shore across part of the view at 4–12 km, and open sea under the
        sun, the volcano and the storm: the same search as the prototype's, from the same seed. */
    std::array<double, 2> start() const noexcept { return startXZ; }

    /** The state a frame is a pure function of. */
    struct State
    {
        double x = 0, z = 0;          // metres
        double heading = 0;           // radians, 0 = -z
        double time = 0, travel = 0;  // seconds; metres flown
        double speed = gearSpeed[0];  // m/s, eased
        double boost = 0;             // 0..1: the TURBO dash, eased
        double down = 1;              // 0..1: the downwash on the liquid, strongest in a hover
        double rotorPhase = 0;        // radians: the rotor's glint
        double yawRate = 0;           // radians a second
        double camY = camHeight, pitch = 0, roll = 0;
        double foam = 0;              // 0..1: flying through a nitrogen field's spray
        double white = 0;             // 0..1: that spray on the glass
    };
    const State& state() const noexcept { return s; }

    /** Placed on the course: nitrogen fields and buoys, dropped once far behind. */
    struct Field { double x, z, r, born, seed; };
    struct Buoy { double x, z; int type; double phase; };   // type 0 red, 1 green (a gate), 2 an amber single
    const std::vector<Field>& fields() const noexcept { return fieldList; }
    const std::vector<Buoy>& buoys() const noexcept { return buoyList; }
    double fieldStrength (const Field& f) const noexcept;
    /** A field / a gate set out `ahead` metres on the heading now (for a scripted shot). */
    void addField (double ahead, double lateral);
    void addGate (double ahead);

    /** The glass: drops (s, t, radius in metres; age) and trails, and the wipers' timing. */
    struct Drop { double s, t, r, r0, age, life, ph, k, ls, lt; };
    struct Trail { double s0, t0, s1, t1, w, born; };
    const std::vector<Drop>& drops() const noexcept { return dropList; }
    const std::vector<Trail>& trails() const noexcept { return trailList; }
    /** The start of the current or last wipe, of the one before, and when deposits began; and each
        wipe's stroke — out, the pause at its far end; back as long as out. */
    double wipeCurrent() const noexcept { return wipeCur; }
    double wipePrevious() const noexcept { return wipePrev; }
    struct Stroke { double out, pause; double length() const noexcept { return 2 * out + pause; } };
    const Stroke& strokeCurrent() const noexcept { return strokeCur; }
    const Stroke& strokePrevious() const noexcept { return strokePrev; }
    double glassSince() const noexcept { return glassT0; }
    /** How far through its stroke every blade is, 0..1, at time t; false when parked. */
    bool wiperFraction (double t, double& f) const noexcept;
    /** A wipe now, unless one is running: at the speed set's stroke. */
    void startWipe() noexcept;
    double wipeDue() const noexcept;

    /** The timed events, pure functions of the time. */
    double saturnVisibility (double t) const noexcept;   // 0.62..1: never gone, rising out of the haze every 20–40 s
    double eruption (double t) const noexcept;           // 0..1
    struct Flash { double f, x, y; bool bolt; };
    Flash flash (double t) const noexcept;               // lightning in the storm
    std::array<double, 2> rotorShadow (double t) const noexcept;   // the band's place on the glass (m), its strength

private:
    bool built = false, placed = false;
    std::vector<uint8_t> noisePx, ripplePx;
    double rmsAxis = 1;
    std::array<double, 2> startXZ { 100000, 100000 };

    State s;
    const int gears;
    int gearWanted = 1;
    bool turbo = false, parked = false;
    float steer = 0.0f;
    double lastSteerTime = -10, meanderPhase = 0.7;

    struct Rng { uint32_t a; double next() noexcept; };
    Rng eventRng { 1 }, glassRng { 3 };
    std::vector<Field> fieldList;
    std::vector<Buoy> buoyList;
    double nextFieldT = 3, nextGateT = 7, nextBuoyT = 16;
    std::vector<Drop> dropList;
    std::vector<Trail> trailList;
    double dropAcc = 0, glassT0 = 0, wipeCur = -1e9, wipePrev = -1e9, nextWipe = 60;
    int wiperSpeed = wipersInterval;
    Stroke strokeCur { strokeSeconds, pauseSeconds }, strokePrev { strokeSeconds, pauseSeconds };

    /** A cached schedule of hashed cycle lengths: the cycle at a time without walking from 0 each frame. */
    struct Schedule
    {
        uint32_t seed; double base, spread, power;   // a cycle lasts base + spread * hash^power seconds
        mutable int k = 0; mutable double t0 = 0;
        struct At { int k; double a, L; };
        At at (double t) const noexcept;
    };
    Schedule saturnCycle { 0x51a7, 20, 20, 1 }, eruptCycle { 0x7e11, 34, 30, 1 }, shadowCycle { 0x5a5a, 8, 8, 1 }, flashCycle { 0x3f1a, 0.9, 4.6, 1.6 };

    double texS (int channel, double u, double v) const noexcept;   // bilinear on the noise's base level, as the GPU's
    double coast (double x, double z) const noexcept;
    double coastAt (double x, double z, double heading, double azDeg, double d) const noexcept;
    double landLean() const noexcept;
    void findStart();
    void resetEvents();
    void resetGlass();
    void stepEvents (double dt);
    void stepGlass (double dt);
    void spawnDrop (int kind);
};

} // namespace orbitamp::titan
