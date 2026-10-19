// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "TitanGl.h"

#include "../../shaders/Shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace orbitamp::amberpaw
{

using namespace orbitamp::gpu;

namespace
{
    using W = TitanWorld;
    constexpr double pi = W::pi, deg = W::deg;

    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double clamp (double x, double a, double b) noexcept { return std::max (a, std::min (b, x)); }

    // THE GUITAR (worlds/Play.h): the searchlights flare with each attack and a little while it sounds, the
    // buoys' lamps light with it between their own blinks (and the storm strikes, TitanWorld::flash) —
    // nothing of it in silence, so a silent frame is the frame it always was.
    constexpr double searchlightOfHit = 0.7, searchlightOfGlow = 0.2;
    constexpr float buoyOfHit = 0.85f, buoyOfGlow = 0.25f;
    inline double wrap (double a, double m) noexcept { return a - std::floor (a / m) * m; }

    /** To the nearest, a half to the even one, as JUCE's roundToInt does it: the frame's size at a render scale. */
    inline int roundToInt (float x) noexcept { return (int) std::lrint ((double) x); }

    //==========================================================================
    // The shaders are written from the world's numbers: a constant is printed as the prototype printed
    // it, six decimals, so the two compile the same arithmetic.
    std::string F (double v) { char b[48]; std::snprintf (b, sizeof b, "%.6f", v); return b; }
    using C3 = std::array<double, 3>;
    std::string V3 (C3 a) { return "vec3(" + F (a[0]) + ", " + F (a[1]) + ", " + F (a[2]) + ")"; }

    // The palette: picked as sRGB, used linear — amber-brown haze, violet shadows and fog.
    C3 lin (double r, double g, double b) { return { std::pow (r / 255, 2.2), std::pow (g / 255, 2.2), std::pow (b / 255, 2.2) }; }
    struct Palette
    {
        C3 horizon = lin (206, 138, 64);   // the haze at the horizon: amber
        C3 zenith  = lin (62, 36, 18);     // higher up it darkens to brown
        C3 glow    = lin (140, 98, 50);    // the sun patch, added
        C3 mist    = lin (166, 140, 178);  // low mist banks: violet-grey
        C3 land    = lin (34, 22, 38);     // dark organic sand and hills, violet in shadow
        C3 body    = lin (10, 6, 13);      // the liquid itself
        C3 amb     = lin (214, 172, 136);  // what a drizzle drop / spray shows
        C3 foam    = lin (214, 196, 186);  // nitrogen froth, lit by the haze
        C3 foamSh  = lin (120, 104, 140);  // its violet shadows
        C3 vapLit  = lin (214, 204, 218);  // cryovolcano vapour, lit
        C3 vapSh   = lin (112, 98, 132);   //   in shadow
        C3 stormD  = lin (60, 52, 74);     // storm towers
        C3 stormL  = lin (124, 110, 124);
        C3 flash   = lin (226, 214, 255);  // lightning
    };
    const Palette& pal() { static const Palette p; return p; }
    // What a drop on the glass shows, upside down: the scene's own colours, display space (read off the rendered frame).
    constexpr float lensSkyLo[3] { 0.80f, 0.56f, 0.33f }, lensSkyHi[3] { 0.60f, 0.38f, 0.20f }, lensSeaFar[3] { 0.52f, 0.37f, 0.26f }, lensSeaNear[3] { 0.27f, 0.18f, 0.12f }, lensMist[3] { 0.72f, 0.62f, 0.70f };

    C3 dirFromAzEl (double az, double el) { const double a = az * deg, e = el * deg; return { std::sin (a) * std::cos (e), std::sin (e), -std::cos (a) * std::cos (e) }; }
    C3 cross (C3 a, C3 b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    C3 norm3 (C3 a) { const double l = std::sqrt (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); return { a[0] / l, a[1] / l, a[2] / l }; }
    C3 hdir (double az) { return { std::sin (az * deg), 0, -std::cos (az * deg) }; }
    C3 hright (double az) { return { std::cos (az * deg), 0, std::sin (az * deg) }; }

    /** The sky's fixed frame: the sun, Saturn and the light on it. */
    struct Sky
    {
        C3 sun, sunR, sunU, sat, satR, satU, satL;
        Sky()
        {
            sun = dirFromAzEl (W::sunAz, W::sunEl);
            sunR = norm3 (cross ({ 0, 1, 0 }, sun)); sunU = norm3 (cross (sun, sunR));
            sat = dirFromAzEl (W::saturn.az, W::saturn.el);
            satR = norm3 (cross (sat, { 0, 1, 0 })); satU = norm3 (cross (satR, sat));
            const C3 L = norm3 ({ 0.62, 0.22, 0.75 });   // lit from the right, gibbous (in the ring frame)
            const double c = std::cos (W::saturn.tiltDeg * deg), s = std::sin (W::saturn.tiltDeg * deg);
            satL = { c * L[0] - s * L[1], s * L[0] + c * L[1], L[2] };
        }
    };
    const Sky& sky() { static const Sky s; return s; }

    /** The camera: heading, then pitch, then roll. */
    struct Basis { C3 r, u, f; };
    Basis cameraBasis (double h, double p, double r)
    {
        const C3 f0 { std::sin (h), 0, -std::cos (h) }, r0 { std::cos (h), 0, std::sin (h) };
        const double cp = std::cos (p), sp = std::sin (p);
        const C3 f { f0[0] * cp, sp, f0[2] * cp }, u { -f0[0] * sp, cp, -f0[2] * sp };
        const double cr = std::cos (r), sr = std::sin (r);
        return { { r0[0] * cr + u[0] * sr, r0[1] * cr + u[1] * sr, r0[2] * cr + u[2] * sr },
                 { u[0] * cr - r0[0] * sr, u[1] * cr - r0[1] * sr, u[2] * cr - r0[2] * sr }, f };
    }

    //==========================================================================
    // THE SHADERS are files in resources/shaders/titan (src/shaders/Shaders.h), written without the
    // #version, which glsl() puts in front of them — the app's or the web's (src/shaders/Gpu.h). What a file asks for by name — `#include <world>` — is
    // written here, from the world's numbers. titan/full.vert is the full-screen triangle the profile, the
    // scene and the canopy draw over; titan/buoy.vert is pass 3's, the beacon buoys — body, lamp glow, and
    // the lamp's glitter column on the liquid. The drops on the glass (titan/glass-drop.*) are lens
    // sprites, six vertices a drop with its values on each — the centre uv, the two radii in uv (an
    // ellipse, flatter higher up) and the alpha; the thin wet trails behind crawling drops
    // (titan/glass-trail.*) are six vertices a segment.
    std::string glsl (const char* path, std::initializer_list<std::pair<const char*, std::string>> numbers = {})
    {
        std::vector<shaders::Written> written;
        for (const auto& [name, text] : numbers)
            written.push_back ({ name, text });
        return gpu::header() + shaders::source (path, written);
    }

    /** Shader text written a piece at a time: words, numbers F() has printed, and whole numbers in decimal. */
    struct Text
    {
        std::string s;
        Text& operator<< (const std::string& t) { s += t; return *this; }
        Text& operator<< (const char* t)        { s += t; return *this; }
        Text& operator<< (int v)                { s += std::to_string (v); return *this; }
        operator const std::string&() const noexcept { return s; }
    };

    // The world's numbers, for every pass that draws in its light: `<world>`, with titan/util.glsl after it.
    std::string worldNumbers()
    {
        Text s;
        s << "const float TAN_MIN = " << F (W::tanMin) << ", TAN_MAX = " << F (W::tanMax) << ";\n"
          << "const float PROF_X0 = " << F (W::profX0) << ", PROF_X1 = " << F (W::profX1) << ";\n"
          << "const float BMIN[5] = float[5](" << F (W::bandMin[0]) << ", " << F (W::bandMin[1]) << ", " << F (W::bandMin[2]) << ", " << F (W::bandMin[3]) << ", " << F (W::bandMin[4]) << ");\n"
          << "const float BMAX[5] = float[5](" << F (W::bandMax[0]) << ", " << F (W::bandMax[1]) << ", " << F (W::bandMax[2]) << ", " << F (W::bandMax[3]) << ", " << F (W::bandMax[4]) << ");\n"
          << "const float HAZE_L = " << F (W::hazeL) << ", LAND_HAZE_L = " << F (W::landHazeL) << ";\n";
        return s;
    }

    //==========================================================================
    // Pass 1: the shore profile, one texel per (screen column, distance band).
    std::string fsProfile()
    {
        Text s;
        s << "const float LA = " << F (W::land.LA) << ", LB = " << F (W::land.LB) << ", LC = " << F (W::land.LC) << ", DUNE_P = " << F (W::land.duneP) << ";\n"
          << "const float C0 = " << F (W::land.C0) << ", C1 = " << F (W::land.C1) << ", NEAR0 = " << F (W::land.near0) << ", NEAR1 = " << F (W::land.near1) << ";\n"
          << "const vec2 DUNE_N = vec2(" << F (std::sin (W::duneAngle)) << ", " << F (std::cos (W::duneAngle)) << ");\n"
          << "const float R_T = " << F (W::rTitan) << ";\n"
          << "const int BAND_STEPS = " << W::bandSteps << "; const float BAND_STEPS_F = " << F (W::bandSteps) << ", COLS_F = " << F (W::profileCols) << ";\n";
        return glsl ("titan/profile.frag", { { "world", worldNumbers() }, { "profile", s } });
    }

    //==========================================================================
    // Pass 2: the sky, the far events, the shore, the sea, the mist.
    std::string fsScene (double rippleRms)
    {
        const auto& P = pal();
        Text s, ripples;
        s << "const float CORE_W2 = 0.0036, CORE_AMP = 1.1;               // the sun's core in the haze: sigma ~3.4 deg\n"
          << "const float MIST_L = " << F (W::mist.L) << ", MIST_DENS = " << F (W::mistDensity) << ", MIST_HS = " << F (W::mistScaleHeight) << ";\n"
          << "const float CLOUD_L = " << F (W::cloud.L) << ", CLOUD_H = " << F (W::cloudHeight) << ", CLOUD_AMP = " << F (W::cloudAmp) << ";\n"
          << "const float OIL_L = " << F (W::oil.L) << ";\n"
          << "const float MAGIC_CELL = " << F (W::magicCell) << ", MAGIC_P = " << F (W::magicP) << ", MAGIC_T = " << F (W::magicT) << ";\n";
        for (int i = 0; i < 3; ++i)
        {
            const auto& r = W::ripples[(size_t) i];
            ripples << "const float RIP_L" << i << " = " << F (r.L) << ", RIP_C" << i << " = " << F (std::cos (r.ang)) << ", RIP_S" << i << " = " << F (std::sin (r.ang))
                    << ", RIP_W" << i << " = " << F (r.rms / rippleRms) << ";   // layer weight = target RMS slope\n";
        }
        ripples << "const float SAT_RAD = " << F (std::tan (W::saturn.radDeg * deg)) << ", SAT_TC = " << F (std::cos (W::saturn.tiltDeg * deg)) << ", SAT_TS = " << F (std::sin (W::saturn.tiltDeg * deg)) << ", RING_OPEN = " << F (W::saturn.open) << ";\n"
                << "const vec3 FOAM = " << V3 (P.foam) << ", FOAM_SH = " << V3 (P.foamSh) << ", VAP_LIT = " << V3 (P.vapLit) << ", VAP_SH = " << V3 (P.vapSh) << ";\n"
                << "const vec3 STORM_D = " << V3 (P.stormD) << ", STORM_L = " << V3 (P.stormL) << ", FLASH = " << V3 (P.flash) << ";\n";
        return glsl ("titan/scene.frag", { { "world", worldNumbers() }, { "scene", s }, { "ripples", ripples } });
    }

    //==========================================================================
    // The particle passes draw a capsule sprite from a (where it was) to b (where it is), in pixels:
    // titan/capsule.glsl, shared by the two.
    std::string fsCapsule()
    {
        return glsl ("titan/capsule.frag", { { "world", worldNumbers() } });
    }

    // Pass 5: the drizzle — pea-sized drops drifting down like snow, world-anchored in a wrapping box ahead.
    std::string vsDrizzle()
    {
        Text s;
        s << "const float B = " << F (W::dropBox) << ", HB = " << F (W::dropBoxHeight) << ";\n";
        return glsl ("titan/drizzle.vert", { { "drizzle", s } });
    }

    // Pass 4: the downwash kicks the liquid up — a low ring of soft mist and flung droplets, ahead and around.
    std::string vsSpray()
    {
        return glsl ("titan/spray.vert");
    }

    // Pass 3: the beacon buoys — body, lamp glow, and the lamp's glitter column on the liquid.
    std::string fsBuoy()
    {
        return glsl ("titan/buoy.frag", { { "world", worldNumbers() } });
    }

    //==========================================================================
    // Pass 6: the canopy. Over the scene: the rotor, its shadow, the tholin film and flakes, the wipers.
    std::string fsGlass()
    {
        const auto& wipers = W::wipers();
        const auto list = [&wipers] (auto&& one) { Text s; for (size_t i = 0; i < wipers.size(); ++i) s << (i ? ", " : "") << one (wipers[i]); return s.s; };
        Text s;
        s << "const vec3 HUB = vec3(" << F (W::rotorHub[0]) << ", " << F (W::rotorHub[1]) << ", " << F (W::rotorHub[2]) << "); const float ROT_R = " << F (W::rotorRadius) << ";\n"
          << "const float WW = " << F (W::bladeWidth) << ";\n"
          << "// the wipers: pivot; stroke start, end, blade in, out; their pane's sides (x = a + b*y in the cabin's pixels)\n"
          << "const int NW = " << (int) wipers.size() << ";\n"
          << "const vec2 WP[NW] = vec2[NW](" << list ([] (const W::Wiper& w) { return "vec2(" + F (w.p[0]) + ", " + F (w.p[1]) + ")"; }) << ");\n"
          << "const vec4 WA[NW] = vec4[NW](" << list ([] (const W::Wiper& w) { return "vec4(" + F (w.th0) + ", " + F (w.th1) + ", " + F (w.rIn) + ", " + F (w.rOut) + ")"; }) << ");\n"
          << "const vec4 WM[NW] = vec4[NW](" << list ([] (const W::Wiper& w) { return "vec4(" + F (w.pane[0]) + ", " + F (w.pane[1]) + ", " + F (w.pane[2]) + ", " + F (w.pane[3]) + ")"; }) << ");\n"
          << "const vec2 CAB_PX = vec2(" << F (W::cabinWidth) << ", " << F (W::cabinHeight) << ");\n"
          << "const float SB = " << F (std::sin (W::glassBeta)) << ", CB = " << F (std::cos (W::glassBeta)) << ", YB = " << F (W::glassYb) << ", ZB = " << F (W::glassZb) << ", SHR = " << F (W::shear) << ";\n"
          << "const float FLAKE_RATE = " << F (W::flakeRate) << ", FILM_RATE = " << F (W::filmRate) << ", CELL = " << F (W::flakeCell) << ", FINE = " << F (W::fineCell) << ";\n";
        return glsl ("titan/glass.frag", { { "glass", s } });
    }
}

//==============================================================================
int TitanGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool TitanGl::compile (Program& p, const std::string& vs, const std::string& fs, const char* what)
{
    std::string log;
    p.id = gpu::program (vs, fs, log);
    if (p.id == 0)
    {
        trouble = std::string ("the ") + what + " shader did not build — " + log;
        gpu::note ("titan: " + trouble);
        return false;
    }
    return true;
}

bool TitanGl::create()
{
    trouble.clear();
    if (! gpu::current())
    {
        trouble = "no GL context was current to build it in";
        gpu::note ("titan: " + trouble);
        return false;
    }
    world.build();

    if (! compile (profile, glsl ("titan/full.vert"), fsProfile(), "profile")
        || ! compile (scene, glsl ("titan/full.vert"), fsScene (world.rippleRms()), "scene")
        || ! compile (buoys, glsl ("titan/buoy.vert"), fsBuoy(), "buoy")
        || ! compile (spray, vsSpray(), fsCapsule(), "spray")
        || ! compile (drizzle, vsDrizzle(), fsCapsule(), "drizzle")
        || ! compile (glass, glsl ("titan/full.vert"), fsGlass(), "glass")
        || ! compile (glassDrops, glsl ("titan/glass-drop.vert"), glsl ("titan/glass-drop.frag"), "glass drop")
        || ! compile (glassTrails, glsl ("titan/glass-trail.vert"), glsl ("titan/glass-trail.frag"), "glass trail"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeBuffers();

    // The frame timer: a timer query where the driver has one. Asked once; an error means no.
    while (glGetError() != GL_NO_ERROR) {}
    if (gpu::timerQueries())
    {
        glGenQueries ((GLsizei) query.size(), query.data());
        gpu::beginTimer (query[0]);
        gpu::endTimer();
        queriesWork = glGetError() == GL_NO_ERROR;
        if (queriesWork)
            gpu::timerNanoseconds (query[0]);   // drains the probe
        else
        {
            glDeleteQueries ((GLsizei) query.size(), query.data());
            query.fill (0);
        }
    }
    while (glGetError() != GL_NO_ERROR) {}
    return true;
}

void TitanGl::destroy()
{
    const auto del = [] (std::uint32_t& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (vboDrops, glDeleteBuffers); del (vboTrails, glDeleteBuffers);
    del (vaoEmpty, glDeleteVertexArrays); del (vboEmpty, glDeleteBuffers); del (vaoDrops, glDeleteVertexArrays); del (vaoTrails, glDeleteVertexArrays);
    del (texNoise, glDeleteTextures); del (texRipple, glDeleteTextures); del (texProfile, glDeleteTextures); del (fboProfile, glDeleteFramebuffers);
    del (fboColour, glDeleteTextures); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &profile, &scene, &buoys, &spray, &drizzle, &glass, &glassDrops, &glassTrails }) { if (p->id != 0) glDeleteProgram (p->id); p->id = 0; }
}

void TitanGl::makeTextures()
{
    const auto rgba8 = [] (std::uint32_t& id, int size, const std::vector<std::uint8_t>& px)
    {
        glGenTextures (1, &id);
        glBindTexture (GL_TEXTURE_2D, id);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    };
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    rgba8 (texNoise, TitanWorld::noiseSize, world.noise());
    rgba8 (texRipple, TitanWorld::rippleSize, world.ripple());
    // The ripples are read at grazing angles: anisotropic filtering, 4x, where the driver has it.
    while (glGetError() != GL_NO_ERROR) {}
    GLfloat maxAniso = 0;
    glGetFloatv (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (glGetError() == GL_NO_ERROR && maxAniso >= 1)
        glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min (4.0f, maxAniso));
    while (glGetError() != GL_NO_ERROR) {}

    // The profile target: 1024 x (5 + 1), RGBA8, linear along the columns.
    glGenTextures (1, &texProfile);
    glBindTexture (GL_TEXTURE_2D, texProfile);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, TitanWorld::profileCols, TitanWorld::profileBands + 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);
    glGenFramebuffers (1, &fboProfile);
    glBindFramebuffer (GL_FRAMEBUFFER, fboProfile);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texProfile, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
}

void TitanGl::makeBuffers()
{
    vaoEmpty = gpu::attributeless (vboEmpty);   // the full-screen passes and the particles draw with no attributes (gpu::attributeless)

    dropData.assign ((size_t) TitanWorld::maxDrops * 6 * 5, 0.0f);
    glGenVertexArrays (1, &vaoDrops);
    glBindVertexArray (vaoDrops);
    glGenBuffers (1, &vboDrops);
    glBindBuffer (GL_ARRAY_BUFFER, vboDrops);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (dropData.size() * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
    {
        const auto l = glGetAttribLocation (glassDrops.id, "aDrop"), la = glGetAttribLocation (glassDrops.id, "aDropA");
        if (l >= 0) { glEnableVertexAttribArray ((GLuint) l); glVertexAttribPointer ((GLuint) l, 4, GL_FLOAT, GL_FALSE, 20, nullptr); }
        if (la >= 0) { glEnableVertexAttribArray ((GLuint) la); glVertexAttribPointer ((GLuint) la, 1, GL_FLOAT, GL_FALSE, 20, (const void*) 16); }
    }

    trailData.assign ((size_t) TitanWorld::maxTrails * 6 * 6, 0.0f);
    glGenVertexArrays (1, &vaoTrails);
    glBindVertexArray (vaoTrails);
    glGenBuffers (1, &vboTrails);
    glBindBuffer (GL_ARRAY_BUFFER, vboTrails);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (trailData.size() * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
    {
        const auto l = glGetAttribLocation (glassTrails.id, "aSeg"), lw = glGetAttribLocation (glassTrails.id, "aW");
        if (l >= 0) { glEnableVertexAttribArray ((GLuint) l); glVertexAttribPointer ((GLuint) l, 4, GL_FLOAT, GL_FALSE, 24, nullptr); }
        if (lw >= 0) { glEnableVertexAttribArray ((GLuint) lw); glVertexAttribPointer ((GLuint) lw, 2, GL_FLOAT, GL_FALSE, 24, (const void*) 16); }
    }
    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void TitanGl::ensureFrame (int w, int h)
{
    if (fbo != 0 && w == fbW && h == fbH)
        return;
    if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); }
    glBindTexture (GL_TEXTURE_2D, fboColour);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
    fbW = w; fbH = h;
}

void TitanGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void TitanGl::render (int w, int h, float scale, std::uint32_t target, int x, int y)
{
    if (scene.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, roundToInt ((float) w * scale)), rh = std::max (2, roundToInt ((float) h * scale));
    ensureFrame (rw, rh);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);

    // The timer: queries, read back a few frames late so nothing waits — or, without them, one frame
    // in sixty finished and clocked. On the web, neither (gpu::timerQueries).
    const bool probe = ! gpu::web && ! queriesWork && (frameCounter % 60) == 0;
    ++frameCounter;
    if (queriesWork)
    {
        while (queriesPending > 0)
        {
            const auto oldest = query[(size_t) ((queryNext - queriesPending + (int) query.size()) % (int) query.size())];
            GLuint available = 0;
            glGetQueryObjectuiv (oldest, GL_QUERY_RESULT_AVAILABLE, &available);
            if (! available) break;
            noteCost ((float) gpu::timerNanoseconds (oldest) * 1.0e-6f);
            --queriesPending;
        }
        if (queriesPending < (int) query.size())
        {
            gpu::beginTimer (query[(size_t) queryNext]);
            queryNext = (queryNext + 1) % (int) query.size();
            ++queriesPending;
        }
    }
    else if (probe)
    {
        glFinish();
        probeStart = gpu::milliseconds();
    }

    for (int i = 0; i < repeats; ++i)
        drawScene (rw, rh);

    if (queriesWork)
        gpu::endTimer();
    else if (probe)
    {
        glFinish();
        noteCost ((float) (gpu::milliseconds() - probeStart));
    }

    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer (0, 0, rw, rh, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer (GL_FRAMEBUFFER, target);
}

bool TitanGl::readFrame (std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (fbo == 0)
        return false;
    rgba.resize ((size_t) fbW * (size_t) fbH * 4);
    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glPixelStorei (GL_PACK_ALIGNMENT, 1);
    glReadPixels (0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    w = fbW; h = fbH;
    return true;
}

//==============================================================================
void TitanGl::drawScene (int rw, int rh)
{
    const auto& s = world.state();
    const auto& P = pal();
    const auto& K = sky();
    const double tv = W::vfovHalfTan, aspect = (double) rw / rh, th = tv * aspect;
    const Basis B = cameraBasis (s.heading, s.pitch, s.roll);
    const Basis Bp = cameraBasis (s.heading - s.yawRate * W::dropExposure, s.pitch, s.roll);
    const C3 hr { std::cos (s.heading), 0, std::sin (s.heading) }, hf { std::sin (s.heading), 0, -std::cos (s.heading) };
    const double cx = s.x, cz = s.z, t = s.time;
    const float tf = (float) t;
    const double satVis = world.saturnVisibility (t), erupt = world.eruption (t);
    const auto lf = world.flash (t);
    constexpr int flags = 1 | 2 | 8 | 16 | 32;     // land, mist, magic, clouds, fields

    const auto v3 = [] (int loc, C3 a) { glUniform3f (loc, (float) a[0], (float) a[1], (float) a[2]); };
    const auto v2 = [] (int loc, double a, double b) { glUniform2f (loc, (float) a, (float) b); };

    glDisable (GL_DEPTH_TEST); glDisable (GL_CULL_FACE); glDisable (GL_BLEND);
    glBindVertexArray (vaoEmpty);

    // 1. The shore profile.
    {
        glBindFramebuffer (GL_FRAMEBUFFER, fboProfile);
        glViewport (0, 0, TitanWorld::profileCols, TitanWorld::profileBands + 1);
        glUseProgram (profile.id);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texNoise); glUniform1i (profile.uniform ("uNoise"), 0);
        glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, 0);
        v3 (profile.uniform ("uHR"), hr); v3 (profile.uniform ("uHF"), hf);
        glUniform1f (profile.uniform ("uTh"), (float) th); glUniform1f (profile.uniform ("uCamY"), (float) s.camY);
        const auto& L = W::land;
        v2 (profile.uniform ("uLand0"), fract (cx / L.LA + L.o0[0]), fract (cz / L.LA + L.o0[1]));
        v2 (profile.uniform ("uLand1"), fract (cx / L.LB + L.o1[0]), fract (cz / L.LB + L.o1[1]));
        v2 (profile.uniform ("uLand2"), fract (cx / L.LC + L.o2[0]), fract (cz / L.LC + L.o2[1]));
        glUniform1f (profile.uniform ("uDuneOff"), (float) fract ((cx * std::sin (W::duneAngle) + cz * std::cos (W::duneAngle)) / L.duneP));
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    }

    // 2. The sky, the far events, the shore, the sea, the mist.
    glViewport (0, 0, rw, rh);
    glUseProgram (scene.id);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texNoise);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texRipple);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texProfile);
    {
        const auto& p = scene;
        const auto u = [&p] (const char* n) { return p.uniform (n); };
        glUniform1i (u ("uNoise"), 0); glUniform1i (u ("uRipple"), 1); glUniform1i (u ("uProfile"), 2);
        glUniform2f (u ("uRes"), (float) rw, (float) rh);
        v3 (u ("uCamR"), B.r); v3 (u ("uCamU"), B.u); v3 (u ("uCamF"), B.f); v3 (u ("uHR"), hr); v3 (u ("uHF"), hf);
        v2 (u ("uTan"), th, tv); glUniform1f (u ("uShear"), (float) W::shear); glUniform1f (u ("uCamY"), (float) s.camY);
        glUniform1f (u ("uTime"), tf); glUniform1f (u ("uExposure"), 1.0f);
        for (int i = 0; i < 3; ++i)
        {
            const auto& L = W::ripples[(size_t) i];
            const double c = std::cos (L.ang), sn = std::sin (L.ang), rx = c * cx - sn * cz, rz = sn * cx + c * cz;
            const char* names[] { "uRip0", "uRip1", "uRip2" };
            v2 (u (names[i]), fract ((rx - L.vel * t) / L.L + L.o[0]), fract (rz / L.L + L.o[1]));
        }
        v2 (u ("uMotion"), hf[0] * s.speed * W::blurSeconds, hf[2] * s.speed * W::blurSeconds);
        v2 (u ("uFog"), fract ((cx - W::mist.wind[0] * t) / W::mist.L + W::mist.off[0]), fract ((cz - W::mist.wind[1] * t) / W::mist.L + W::mist.off[1]));
        v2 (u ("uCloud"), fract ((cx - W::cloud.wind[0] * t) * 0.35 / W::cloud.L + W::cloud.off[0]), fract ((cz - W::cloud.wind[1] * t) / W::cloud.L + W::cloud.off[1]));
        v2 (u ("uOil"), fract ((cx - W::oil.wind[0] * t) / W::oil.L + W::oil.off[0]), fract ((cz - W::oil.wind[1] * t) / W::oil.L + W::oil.off[1]));
        // Each texture scale wraps on its own period: a wrapped offset times a factor jumps every time it wraps.
        const double ox = cx - W::oil.wind[0] * t, oz = cz - W::oil.wind[1] * t;
        glUniform4f (u ("uOilN"), (float) fract (ox / 23 + 0.37), (float) fract (oz / 23 + 0.81), (float) fract (ox / 19 + 0.13), (float) fract (oz / 19 + 0.59));
        v3 (u ("uSun"), K.sun); v3 (u ("uSunR"), K.sunR); v3 (u ("uSunU"), K.sunU);
        v3 (u ("uHorizon"), P.horizon); v3 (u ("uZenith"), P.zenith); v3 (u ("uGlow"), P.glow); v3 (u ("uMist"), P.mist); v3 (u ("uLand"), P.land); v3 (u ("uBody"), P.body);
        const double mbx = std::floor (cx / W::magicCell), mbz = std::floor (cz / W::magicCell);
        glUniform2i (u ("uMagicBase"), (int) mbx, (int) mbz); v2 (u ("uMagicFrac"), cx / W::magicCell - mbx, cz / W::magicCell - mbz);
        v3 (u ("uSat"), K.sat); v3 (u ("uSatR"), K.satR); v3 (u ("uSatU"), K.satU); v3 (u ("uSatL"), K.satL); glUniform1f (u ("uSatVis"), (float) satVis);
        v3 (u ("uVolcF"), hdir (W::volcanoAz)); v3 (u ("uVolcR"), hright (W::volcanoAz));
        glUniform1f (u ("uVolcTop"), (float) ((W::volcanoH - s.camY - W::volcanoD * W::volcanoD / (2 * W::rTitan)) / W::volcanoD)); glUniform1f (u ("uErupt"), (float) erupt);
        v3 (u ("uStormF"), hdir (W::stormAz)); v3 (u ("uStormR"), hright (W::stormAz)); glUniform1f (u ("uFlash"), (float) lf.f);
        glUniform3f (u ("uFlashP"), (float) lf.x, (float) lf.y, lf.bolt ? 1.0f : 0.0f);
        float fieldBuf[8] {};
        const auto& fl = world.fields();
        for (size_t i = 0; i < std::min<size_t> (2, fl.size()); ++i)
        {
            fieldBuf[i * 4] = (float) (fl[i].x - cx); fieldBuf[i * 4 + 1] = (float) (fl[i].z - cz);
            fieldBuf[i * 4 + 2] = (float) fl[i].r; fieldBuf[i * 4 + 3] = (float) world.fieldStrength (fl[i]);
        }
        glUniform4fv (u ("uField"), 2, fieldBuf);
        v2 (u ("uFieldSeed"), fl.size() > 0 ? fl[0].seed : 0.0, fl.size() > 1 ? fl[1].seed : 0.0);
        glUniform1f (u ("uWhite"), (float) s.white); glUniform1f (u ("uDown"), (float) s.down); glUniform1f (u ("uDash"), (float) (s.boost * clamp (s.speed / 20, 0, 1)));
        glUniform1i (u ("uFlags"), flags);
        const double flare = 1.0 + searchlightOfHit * world.playing().hit() + searchlightOfGlow * world.playing().glow();
        glUniform2f (u ("uLamp"), (float) (world.light (0) * flare), (float) (world.light (1) * flare));
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // Premultiplied, alpha 0 = additive; the frame's own alpha stays 1.
    glEnable (GL_BLEND); glBlendFuncSeparate (GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);

    // 3. The buoys.
    const auto& bl = world.buoys();
    const int nb = (int) std::min<size_t> (8, bl.size());
    if (nb > 0)
    {
        const auto& p = buoys;
        const auto u = [&p] (const char* n) { return p.uniform (n); };
        glUseProgram (p.id);
        v3 (u ("uCamR"), B.r); v3 (u ("uCamU"), B.u); v3 (u ("uCamF"), B.f); v3 (u ("uHR"), hr);
        v2 (u ("uTan"), th, tv); glUniform2f (u ("uRes"), (float) rw, (float) rh); glUniform1f (u ("uShear"), (float) W::shear); glUniform1f (u ("uCamY"), (float) s.camY);
        glUniform1f (u ("uTime"), tf); glUniform1f (u ("uExposure"), 1.0f); v3 (u ("uAmb"), P.amb); v3 (u ("uHaze"), P.horizon);
        float buoyBuf[32] {};
        for (int i = 0; i < nb; ++i)
        {
            const auto& o = bl[(size_t) i];
            buoyBuf[i * 4] = (float) (o.x - cx); buoyBuf[i * 4 + 1] = (float) (o.z - cz); buoyBuf[i * 4 + 2] = (float) o.type; buoyBuf[i * 4 + 3] = (float) o.phase;
        }
        glUniform4fv (u ("uBuoy"), 8, buoyBuf); glUniform1i (u ("uBuoyN"), nb);
        glUniform1f (u ("uPlay"), std::min (1.0f, buoyOfHit * world.playing().hit() + buoyOfGlow * world.playing().glow()));
        glDrawArrays (GL_TRIANGLES, 0, nb * 3 * 6);
    }

    // 4. The spray.
    if (s.down + s.foam + s.boost > 0.01)
    {
        const auto& p = spray;
        const auto u = [&p] (const char* n) { return p.uniform (n); };
        glUseProgram (p.id);
        v3 (u ("uCamR"), B.r); v3 (u ("uCamU"), B.u); v3 (u ("uCamF"), B.f); v3 (u ("uHR"), hr); v3 (u ("uHF"), hf);
        v2 (u ("uTan"), th, tv); glUniform2f (u ("uRes"), (float) rw, (float) rh); glUniform1f (u ("uShear"), (float) W::shear); glUniform1f (u ("uCamY"), (float) s.camY);
        glUniform1f (u ("uTime"), tf); glUniform1f (u ("uSpeed"), (float) s.speed); glUniform1f (u ("uDown"), (float) s.down); glUniform1f (u ("uFoam"), (float) s.foam); glUniform1f (u ("uExposure"), 1.0f);
        glUniform1f (u ("uBoost"), (float) s.boost); v3 (u ("uAmb"), P.amb); v3 (u ("uFoamC"), P.foam);
        glDrawArrays (GL_TRIANGLES, 0, W::sprayCount * 6);
    }

    // 5. The drizzle.
    {
        const auto& p = drizzle;
        const auto u = [&p] (const char* n) { return p.uniform (n); };
        glUseProgram (p.id);
        v3 (u ("uCamR"), B.r); v3 (u ("uCamU"), B.u); v3 (u ("uCamF"), B.f); v3 (u ("uCamRp"), Bp.r); v3 (u ("uCamUp"), Bp.u); v3 (u ("uCamFp"), Bp.f);
        v2 (u ("uTan"), th, tv); glUniform2f (u ("uRes"), (float) rw, (float) rh); glUniform1f (u ("uShear"), (float) W::shear);
        glUniform1f (u ("uCamY"), (float) s.camY); glUniform1f (u ("uTime"), tf); glUniform1f (u ("uExpo"), (float) W::dropExposure); glUniform1f (u ("uExposure"), 1.0f);
        const double bc[2] { hf[0] * W::dropAhead, hf[2] * W::dropAhead };
        v2 (u ("uBoxBase"), wrap (cx + bc[0] - W::wind[0] * t, W::dropBox), wrap (cz + bc[1] - W::wind[1] * t, W::dropBox));
        v2 (u ("uBoxCen"), bc[0], bc[1]); v2 (u ("uWind"), W::wind[0], W::wind[1]);
        glUniform3f (u ("uVel"), (float) (hf[0] * s.speed), 0.0f, (float) (hf[2] * s.speed));
        v3 (u ("uAmb"), P.amb); glUniform1f (u ("uDown"), (float) s.down);
        glDrawArrays (GL_TRIANGLES, 0, W::dropCount * 6);
    }

    // 6. The canopy: the rotor, the film and flakes, the wipers; then the trails and the drops.
    {
        double wf = 0;
        const bool wiping = world.wiperFraction (t, wf);
        const auto rs = world.rotorShadow (t);
        const auto& p = glass;
        const auto u = [&p] (const char* n) { return p.uniform (n); };
        glUseProgram (p.id);
        glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texNoise);
        glUniform1i (u ("uNoise"), 0);
        glUniform2f (u ("uRes"), (float) rw, (float) rh); glUniform1f (u ("uAspect"), (float) aspect); glUniform1f (u ("uTime"), tf);
        v2 (u ("uTan"), tv * aspect, tv);
        glUniform4f (u ("uWipe"), (float) world.wipeCurrent(), (float) world.wipePrevious(), (float) world.glassSince(), wiping ? (float) wf : -10.0f);
        glUniform4f (u ("uStroke"), (float) world.strokeCurrent().out, (float) world.strokeCurrent().pause, (float) world.strokePrevious().out, (float) world.strokePrevious().pause);
        glUniform4f (u ("uRotor"), (float) std::fmod (s.rotorPhase, 2 * pi), (float) (W::rotorTilt + 0.03 * s.speed / W::topSpeed + 0.05 * s.boost), (float) rs[0], (float) rs[1]);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }
    const auto& trails = world.trails();
    const int nt = (int) std::min<size_t> ((size_t) W::maxTrails, trails.size());
    if (nt > 0)
    {
        for (int i = 0; i < nt; ++i)
        {
            const auto& q = trails[(size_t) i];
            const auto u0 = W::glassToUV (q.s0, q.t0, th), u1 = W::glassToUV (q.s1, q.t1, th);
            const float v[6] { (float) u0[0], (float) u0[1], (float) u1[0], (float) u1[1],
                               (float) (q.w / (2 * tv * (W::glassZb + q.t0 * std::sin (W::glassBeta)))),   // metres -> window heights there
                               (float) (1 - (t - q.born) / W::trailLife) };
            for (int k = 0; k < 6; ++k) std::copy (v, v + 6, trailData.begin() + (long) ((i * 6 + k) * 6));
        }
        glUseProgram (glassTrails.id); glBindVertexArray (vaoTrails);
        glBindBuffer (GL_ARRAY_BUFFER, vboTrails);
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) ((size_t) nt * 36 * sizeof (float)), trailData.data());
        glUniform2f (glassTrails.uniform ("uRes"), (float) rw, (float) rh); glUniform1f (glassTrails.uniform ("uAspect"), (float) aspect);
        glDrawArrays (GL_TRIANGLES, 0, nt * 6);
    }
    const auto& drops = world.drops();
    const int nd = (int) std::min<size_t> ((size_t) W::maxDrops, drops.size());
    if (nd > 0)
    {
        for (int i = 0; i < nd; ++i)
        {
            const auto& d = drops[(size_t) i];
            const auto c = W::glassToUV (d.s, d.t, th), ux = W::glassToUV (d.s + d.r, d.t, th), uy = W::glassToUV (d.s, d.t + d.r, th);
            const float v[5] { (float) c[0], (float) c[1], (float) std::abs (ux[0] - c[0]), (float) std::abs (uy[1] - c[1]), (float) std::min (1.0, d.age / 0.12) };
            for (int k = 0; k < 6; ++k) std::copy (v, v + 5, dropData.begin() + (long) ((i * 6 + k) * 5));
        }
        glUseProgram (glassDrops.id); glBindVertexArray (vaoDrops);
        glBindBuffer (GL_ARRAY_BUFFER, vboDrops);
        glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) ((size_t) nd * 30 * sizeof (float)), dropData.data());
        const auto u = [this] (const char* n) { return glassDrops.uniform (n); };
        glUniform2f (u ("uRes"), (float) rw, (float) rh);
        glUniform1f (u ("uHorizonV"), (float) (0.5 + 0.5 * (W::shear - std::tan (s.pitch) / tv)));
        glUniform3fv (u ("uSkyLo"), 1, lensSkyLo); glUniform3fv (u ("uSkyHi"), 1, lensSkyHi); glUniform3fv (u ("uSeaFar"), 1, lensSeaFar);
        glUniform3fv (u ("uSeaNear"), 1, lensSeaNear); glUniform3fv (u ("uMistC"), 1, lensMist);
        glDrawArrays (GL_TRIANGLES, 0, nd * 6);
    }
    glDisable (GL_BLEND);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::amberpaw
