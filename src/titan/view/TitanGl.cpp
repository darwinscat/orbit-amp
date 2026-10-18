// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "TitanGl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace orbitamp::titan
{

using namespace juce::gl;

namespace
{
    using W = TitanWorld;
    constexpr double pi = W::pi, deg = W::deg;

    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double clamp (double x, double a, double b) noexcept { return std::max (a, std::min (b, x)); }
    inline double wrap (double a, double m) noexcept { return a - std::floor (a / m) * m; }

    //==========================================================================
    // The shaders are written from the world's numbers: a constant is printed as the prototype printed
    // it, six decimals, so the two compile the same arithmetic.
    juce::String F (double v) { char b[48]; std::snprintf (b, sizeof b, "%.6f", v); return b; }
    using C3 = std::array<double, 3>;
    juce::String V3 (C3 a) { return "vec3(" + F (a[0]) + ", " + F (a[1]) + ", " + F (a[2]) + ")"; }

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
    const char* const glslHash = R"GLSL(
uint ih(uint x){ x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float h01(uint a, uint s){ return float(ih(a * 0x9E3779B1u ^ ih(s * 0x85EBCA77u + 0x632BE5ABu))) * (1.0 / 4294967296.0); }
float hcell(ivec2 c, uint s){ return float(ih(uint(c.x) * 0x9E3779B1u ^ ih(uint(c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float sq(float x){ return x * x; }
)GLSL";

    juce::String glslConst()
    {
        juce::String s;
        s << "const float TAN_MIN = " << F (W::tanMin) << ", TAN_MAX = " << F (W::tanMax) << ";\n"
          << "const float PROF_X0 = " << F (W::profX0) << ", PROF_X1 = " << F (W::profX1) << ";\n"
          << "const float BMIN[5] = float[5](" << F (W::bandMin[0]) << ", " << F (W::bandMin[1]) << ", " << F (W::bandMin[2]) << ", " << F (W::bandMin[3]) << ", " << F (W::bandMin[4]) << ");\n"
          << "const float BMAX[5] = float[5](" << F (W::bandMax[0]) << ", " << F (W::bandMax[1]) << ", " << F (W::bandMax[2]) << ", " << F (W::bandMax[3]) << ", " << F (W::bandMax[4]) << ");\n"
          << "const float HAZE_L = " << F (W::hazeL) << ", LAND_HAZE_L = " << F (W::landHazeL) << ";\n"
          << R"GLSL(vec2 rot(vec2 p, float c, float s){ return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }
vec2 rotT(vec2 p, float c, float s){ return vec2(c * p.x + s * p.y, -s * p.x + c * p.y); }
vec3 softclip(vec3 x){ return mix(x, 0.8 + 0.2 * (1.0 - exp(-(x - 0.8) / 0.2)), step(0.8, x)); }
vec3 toDisplay(vec3 c){ return pow(max(softclip(c), 0.0), vec3(1.0 / 2.2)); }
)GLSL";
        return s;
    }

    const char* const vsFull = R"GLSL(#version 150
void main(){ vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0); gl_Position = vec4(p, 0.0, 1.0); }
)GLSL";

    //==========================================================================
    // Pass 1: the shore profile, one texel per (screen column, distance band).
    juce::String fsProfile()
    {
        juce::String s ("#version 150\n");
        s << R"GLSL(uniform sampler2D uNoise;
uniform vec3 uHR, uHF; uniform float uTh, uCamY, uDuneOff;
uniform vec2 uLand0, uLand1, uLand2;
out vec4 o;
)GLSL" << glslConst()
          << "const float LA = " << F (W::land.LA) << ", LB = " << F (W::land.LB) << ", LC = " << F (W::land.LC) << ", DUNE_P = " << F (W::land.duneP) << ";\n"
          << "const float C0 = " << F (W::land.C0) << ", C1 = " << F (W::land.C1) << ", NEAR0 = " << F (W::land.near0) << ", NEAR1 = " << F (W::land.near1) << ";\n"
          << "const vec2 DUNE_N = vec2(" << F (std::sin (W::duneAngle)) << ", " << F (std::cos (W::duneAngle)) << ");\n"
          << "const float R_T = " << F (W::rTitan) << ";\n"
          << "const int BAND_STEPS = " << W::bandSteps << "; const float BAND_STEPS_F = " << F (W::bandSteps) << ", COLS_F = " << F (W::profileCols) << ";\n"
          << R"GLSL(// height of the land above the sea (m) at camera-relative xz p, D = its distance; duneW = share of dune field
float landH(vec2 p, float D, out float duneW){
  vec4 a = textureLod(uNoise, uLand0 + p / LA, 0.0);
  vec4 b = textureLod(uNoise, uLand1 + p / LB, 0.0);
  float n = 0.72 * a.r + 0.28 * b.g;
  float coast = smoothstep(C0, C1, n);
  duneW = 0.0;
  if (coast <= 0.0) return 0.0;
  vec4 c = textureLod(uNoise, uLand2 + p / LC, 0.0);
  float inland = smoothstep(C0 + 0.02, C1 + 0.12, n);
  duneW = smoothstep(0.42, 0.62, a.g) * inland;
  float hills = 25.0 + inland * (250.0 * c.r * c.r + 80.0 * c.b * c.b) + 60.0 * (b.a - 0.3);   // low, rounded
  float s = dot(p, DUNE_N) / DUNE_P + uDuneOff + (b.r - 0.5) * 1.4;
  float dune = pow(0.5 + 0.5 * sin(6.2831853 * s), 3.0) * 115.0;
  float h = coast * (mix(hills, 16.0 + 0.35 * hills, duneW) + dune * duneW);
  return h * smoothstep(NEAR0, NEAR1, D);
}
float bandMax(int k, vec2 dir, out float bestD, out float bestW){
  float best = -1.0; bestD = BMIN[k]; bestW = 0.0;
  float r = BMAX[k] / BMIN[k];
  for (int i = 0; i < BAND_STEPS; i++) {
    float D = BMIN[k] * pow(r, (float(i) + 0.5) / BAND_STEPS_F);
    float w; float h = landH(dir * D, D, w);
    float tn = (h - uCamY - D * D / (2.0 * R_T)) / D;
    if (tn > best) { best = tn; bestD = D; bestW = w; }
  }
  return best;
}
vec2 enc16(float v){ v = clamp(v, 0.0, 1.0) * 255.0; float hi = floor(v); return vec2(hi / 255.0, v - hi); }
void main(){
  int k = int(gl_FragCoord.y);
  float xs = mix(PROF_X0, PROF_X1, gl_FragCoord.x / COLS_F);
  vec2 dir = normalize(uHF.xz + (xs * uTh) * uHR.xz);
  float D = 0.0, w = 0.0, best = -1.0, u = 0.0;
  if (k < 5) { best = bandMax(k, dir, D, w); u = (D - BMIN[k]) / (BMAX[k] - BMIN[k]); }
  else { for (int j = 0; j < 5; j++) { float d2, w2; best = max(best, bandMax(j, dir, d2, w2)); } }
  o = vec4(enc16((best - TAN_MIN) / (TAN_MAX - TAN_MIN)), u, w);
}
)GLSL";
        return s;
    }

    //==========================================================================
    // Pass 2: the sky, the far events, the shore, the sea, the mist.
    juce::String fsScene (double rippleRms)
    {
        const auto& P = pal();
        juce::String s ("#version 150\n");
        s << R"GLSL(uniform vec2 uRes;
uniform vec3 uCamR, uCamU, uCamF, uHR, uHF;
uniform vec2 uTan; uniform float uShear, uCamY, uTime, uExposure;
uniform sampler2D uRipple, uNoise, uProfile;
uniform vec2 uRip0, uRip1, uRip2, uMotion, uFog, uCloud, uOil;
uniform vec4 uOilN;      // the oil film's 23-m detail and the TURBO streak's 19-m noise: their own wrapped offsets
uniform vec3 uSun, uSunR, uSunU;
uniform vec3 uHorizon, uZenith, uGlow, uMist, uLand, uBody;
uniform ivec2 uMagicBase; uniform vec2 uMagicFrac;
uniform vec3 uSat, uSatR, uSatU, uSatL; uniform float uSatVis;
uniform vec3 uVolcF, uVolcR; uniform float uVolcTop, uErupt;
uniform vec3 uStormF, uStormR; uniform float uFlash; uniform vec3 uFlashP;
uniform vec4 uField[2]; uniform vec2 uFieldSeed;
uniform float uWhite, uDown, uDash;
uniform vec2 uLamp;      // the searchlights, left and right, 0..1
uniform int uFlags;
out vec4 o;
)GLSL" << glslHash << glslConst()
          << "const float CORE_W2 = 0.0036, CORE_AMP = 1.1;               // the sun's core in the haze: sigma ~3.4 deg\n"
          << "const float MIST_L = " << F (W::mist.L) << ", MIST_DENS = " << F (W::mistDensity) << ", MIST_HS = " << F (W::mistScaleHeight) << ";\n"
          << "const float CLOUD_L = " << F (W::cloud.L) << ", CLOUD_H = " << F (W::cloudHeight) << ", CLOUD_AMP = " << F (W::cloudAmp) << ";\n"
          << "const float OIL_L = " << F (W::oil.L) << ";\n"
          << "const float MAGIC_CELL = " << F (W::magicCell) << ", MAGIC_P = " << F (W::magicP) << ", MAGIC_T = " << F (W::magicT) << ";\n"
          << R"GLSL(
// THE SEARCHLIGHTS, under the nose either side, aimed down at the sea ahead and a little out: where a lamp
// is (eye-relative), where its axis points, and how much of it lights a point in its cone, falling off
// with distance as light does.
const vec3 LAMP_C = vec3(1.0, 0.93, 0.80) * 1.2;
const float LAMP_SIDE = 1.6, LAMP_FWD = 1.2, LAMP_DROP = 1.4, AIM_SIDE = 5.5, AIM_FWD = 26.0, LAMP_COS_IN = 0.9986, LAMP_COS_OUT = 0.9925;
vec3 lampAt(int k){ float sd = k == 0 ? -1.0 : 1.0; return uHR * (sd * LAMP_SIDE) + uHF * LAMP_FWD + vec3(0.0, uCamY - LAMP_DROP, 0.0); }
vec3 lampAxis(int k){ float sd = k == 0 ? -1.0 : 1.0; vec3 aim = uHR * (sd * AIM_SIDE) + uHF * AIM_FWD; return normalize(vec3(aim.x, 0.0, aim.z) - lampAt(k)); }
float lampOn(vec3 q, int k, out vec3 l){
  vec3 d = q - lampAt(k); float r2 = max(dot(d, d), 4.0); l = d * inversesqrt(r2);
  return smoothstep(LAMP_COS_OUT, LAMP_COS_IN, dot(l, lampAxis(k))) * (AIM_FWD * AIM_FWD / r2);
}
)GLSL";
        for (int i = 0; i < 3; ++i)
        {
            const auto& r = W::ripples[(size_t) i];
            s << "const float RIP_L" << i << " = " << F (r.L) << ", RIP_C" << i << " = " << F (std::cos (r.ang)) << ", RIP_S" << i << " = " << F (std::sin (r.ang))
              << ", RIP_W" << i << " = " << F (r.rms / rippleRms) << ";   // layer weight = target RMS slope\n";
        }
        s << "const float SAT_RAD = " << F (std::tan (W::saturn.radDeg * deg)) << ", SAT_TC = " << F (std::cos (W::saturn.tiltDeg * deg)) << ", SAT_TS = " << F (std::sin (W::saturn.tiltDeg * deg)) << ", RING_OPEN = " << F (W::saturn.open) << ";\n"
          << "const vec3 FOAM = " << V3 (P.foam) << ", FOAM_SH = " << V3 (P.foamSh) << ", VAP_LIT = " << V3 (P.vapLit) << ", VAP_SH = " << V3 (P.vapSh) << ";\n"
          << "const vec3 STORM_D = " << V3 (P.stormD) << ", STORM_L = " << V3 (P.stormL) << ", FLASH = " << V3 (P.flash) << ";\n"
          << R"GLSL(
vec3 skyBase(vec3 d){
  float el = max(d.y, 0.0);
  vec3 c = mix(uHorizon, uZenith, pow(smoothstep(0.0, 0.45, el), 0.5));
  float cs = dot(d, uSun);
  c += uGlow * (0.5 * exp((cs - 1.0) * 5.0) + 0.55 * exp((cs - 1.0) * 30.0));   // aureole ~25 deg, ~10 deg
  return c;
}
// the sun's core as an anisotropic gaussian (elevation / azimuth widths squared, rad^2), energy kept
float sunCore(vec3 d, float wv2, float wh2){
  vec3 dd = d - uSun; float a = dot(dd, uSunU), b = dot(dd, uSunR);
  return exp(-0.5 * (a * a / wv2 + b * b / wh2)) * CORE_W2 / sqrt(wv2 * wh2);
}
// ---- Saturn, ghostly behind the haze: far half of the rings, the banded disc, the near half
vec3 saturnOver(vec3 col, vec3 d, float pxA){
  float fz = dot(d, uSat);
  if (fz < 0.985 || uSatVis < 0.002) return col;
  vec2 q = vec2(dot(d, uSatR), dot(d, uSatU)) / (fz * SAT_RAD);      // planet radii
  vec2 r = rot(q, SAT_TC, SAT_TS);                                     // x along the ring line
  float aa = pxA / SAT_RAD + 0.07;                                    // the haze blurs its edge
  float T = uSatVis * 0.55;                                            // never fully clear
  // the haze thins around it while it shows
  col *= 1.0 - 0.10 * T * exp(-dot(q, q) / 9.0);
  float er = length(vec2(r.x, r.y / RING_OPEN));
  float ringA = smoothstep(1.22, 1.30, er) * (1.0 - smoothstep(2.18, 2.27, er));
  ringA *= 1.0 - 0.8 * smoothstep(1.90, 1.93, er) * (1.0 - smoothstep(1.99, 2.02, er));   // the Cassini division
  ringA *= 0.5 * clamp(RING_OPEN * er * 2.0 / max(aa, 1e-4), 0.25, 1.0);                // thin line: no shimmer
  vec3 ringC = vec3(0.70, 0.62, 0.48) * (0.85 + 0.15 * sin(er * 31.0));
  if (r.y > 0.0) col = mix(col, ringC, ringA * T);                     // far half, behind the disc
  vec2 pq = vec2(r.x, r.y / 0.9); float pr = length(pq);
  float disc = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, pr);
  if (disc > 0.0) {
    float z = sqrt(max(1.0 - pr * pr, 0.0));
    float lat = pq.y;
    float band = 0.5 + 0.17 * sin(lat * 8.5 + 0.7) + 0.08 * sin(lat * 21.0 + 2.0);   // bands softened by the haze
    vec3 pc = mix(vec3(0.66, 0.56, 0.40), vec3(0.82, 0.74, 0.56), clamp(band, 0.0, 1.0));
    pc = mix(pc, vec3(0.55, 0.56, 0.62), smoothstep(0.75, 0.95, abs(lat)));   // greyer poles
    float lit = smoothstep(-0.15, 0.45, dot(vec3(pq, z), uSatL));
    vec3 planet = pc * (0.08 + 0.92 * lit) * (0.55 + 0.45 * z);
    planet *= 1.0 - 0.6 * exp(-sq((r.y + 0.16) / 0.05)) * lit;          // the rings' shadow band
    planet = mix(planet, col, 0.2);                                      // a little of the haze in front
    col = mix(col, planet, disc * T);
  }
  col += vec3(0.07, 0.056, 0.036) * (T * exp(-max(pr - 1.0, 0.0) * 5.0) * (1.0 - disc));   // a soft halo in the haze
  if (r.y <= 0.0) col = mix(col, ringC, ringA * T);                    // near half, in front
  return col;
}
// ---- the cryovolcano: a cone on the horizon and its vapour column (swells and spreads in an eruption)
vec3 volcanoOver(vec3 col, vec3 d, float pxT, vec3 hazeH){
  float fz = dot(d.xz, uVolcF.xz);
  if (fz < 0.93) return col;
  float X = dot(d, uVolcR) / fz, Y = d.y / fz;
  float top = uVolcTop;
  if (abs(X) > 0.42 || Y > top + 0.17 || Y < -0.004) return col;
  float hb = top - Y;
  if (hb > -0.003) {
    float rel = clamp(hb / top, 0.0, 1.0);
    float hw = 0.006 + hb / 0.58 * (1.0 + 1.1 * rel * rel);
    float a = (1.0 - smoothstep(hw - pxT, hw + pxT, abs(X))) * smoothstep(-pxT, pxT, hb);
    if (a > 0.0) {
      float streak = textureLod(uNoise, vec2(X / max(hw, 0.004) * 0.3 + 0.5, hb * 2.5 + 0.2), 0.0).b;
      vec3 cc = mix(uLand * 1.2, VAP_SH * 0.5, smoothstep(0.55, 0.8, streak) * (1.0 - 0.6 * rel));   // frost streaks
      cc *= 0.8 + 0.4 * smoothstep(-0.5, 1.0, X / max(hw, 0.004));                                  // lit from the right
      cc = mix(cc, hazeH, 1.0 - exp(-16000.0 / (LAND_HAZE_L * 1.6)));
      col = mix(col, cc, a);
    }
  }
  float yy = Y - top;
  if (yy > -0.006) {
    float e = uErupt;
    float Hc = mix(0.04, 0.092, e);
    float xc = 0.18 * yy + 1.4 * yy * yy;                                 // the wind bends it to the right
    float w = 0.005 + 0.004 * e + yy * (0.12 + 0.30 * e);
    float colm = exp(-sq((X - xc) / w)) * (1.0 - smoothstep(0.7 * Hc, 1.1 * Hc, yy));
    float capW = 0.02 + 0.2 * e;
    float cap = exp(-sq((yy - 0.92 * Hc) / (0.009 + 0.016 * e))) * exp(-sq((X - xc - 0.3 * capW) / capW)) * (0.2 + 0.8 * e);
    float dens = max(colm, cap) * smoothstep(-0.006, 0.002, yy);
    if (dens > 0.003) {
      vec4 n = textureLod(uNoise, vec2(X * 4.0 + 0.3, yy * 4.0 - uTime * (0.008 + 0.02 * e)), 1.5);
      vec4 n2 = textureLod(uNoise, vec2(X * 10.0 + 0.1, yy * 10.0 - uTime * (0.02 + 0.04 * e)), 2.5);
      dens *= 0.35 + 1.0 * n.a + 0.5 * (n2.a - 0.5);
      float a = clamp(dens * (0.55 + 0.4 * e), 0.0, 0.94);
      vec3 pc = mix(VAP_SH, VAP_LIT, clamp(0.25 + 0.9 * n2.a + 0.6 * (X - xc) / (w + capW * step(0.8 * Hc, yy)), 0.0, 1.0));
      pc *= 0.62;                                                        // lit only by the haze
      pc = mix(pc, hazeH, 0.3);
      col = mix(col, pc, a);
    }
  }
  return col;
}
float dome(float X, float c, float w, float h){ float q = (X - c) / w; return h * sqrt(max(1.0 - q * q, 0.0)); }
// ---- the storm far off: towers, an anvil, a rain curtain; lightning lights it and the haze around
vec3 stormOver(vec3 col, vec3 d, float pxT, vec3 hazeH){
  float fz = dot(d.xz, uStormF.xz);
  if (fz < 0.92) return col;
  float X = dot(d, uStormR) / fz, Y = d.y / fz;
  if (abs(X) > 0.42 || Y > 0.2 || Y < -0.004) return col;
  vec2 fd = vec2(X, Y) - uFlashP.xy;
  float fl = uFlash;
  if (abs(X) < 0.3 && Y < 0.13) {
    float top = max(max(dome(X, -0.075, 0.062, 0.060), dome(X, 0.02, 0.056, 0.090)),
                    max(dome(X, 0.115, 0.046, 0.050), dome(X, -0.17, 0.05, 0.034)));
    top = max(top, max(dome(X, 0.058, 0.032, 0.072), dome(X, -0.028, 0.03, 0.056)));
    vec4 n = textureLod(uNoise, vec2(X * 4.0 + 0.7, Y * 4.0 + 0.2), 1.0);
    vec4 n2 = textureLod(uNoise, vec2(X * 9.0 + 0.3, Y * 9.0 + 0.6), 2.0);
    top += 0.011 * (n.a - 0.5) + 0.008 * (n2.a - 0.5);                              // billowing tops
    float base = 0.022 + 0.008 * (n2.g - 0.5);
    float soft = 2.0 * pxT + 0.004;
    float body = smoothstep(top + soft, top - soft, Y) * smoothstep(base - 0.006, base + 0.006, Y);
    float ax = abs(X - 0.03);
    float anvTop = 0.104 + 0.002 * (n2.g - 0.5);                                   // flat on top, like a lid
    float anvBot = 0.084 + 0.16 * ax * ax + 0.003 * (n.a - 0.5);                   // its underside sags toward the tower
    float anv = smoothstep(anvBot - 0.003, anvBot + 0.003, Y) * (1.0 - smoothstep(anvTop - 0.002, anvTop + 0.002, Y)) * (1.0 - smoothstep(0.08, 0.13, ax));
    anv = max(anv, smoothstep(0.065, 0.085, Y) * (1.0 - smoothstep(0.03, 0.06, ax)) * (1.0 - smoothstep(anvTop - 0.002, anvTop + 0.002, Y)));   // the tower flares into it
    float cloud = clamp(max(body, 0.8 * anv), 0.0, 1.0);
    float shafts = 0.35 + 0.65 * textureLod(uNoise, vec2(X * 30.0, Y * 1.5 + 0.5), 0.5).g;
    float rain = (1.0 - smoothstep(base * 0.6, base, Y)) * (1.0 - smoothstep(0.05, 0.13, abs(X - 0.01))) * shafts;
    vec3 cc = mix(STORM_D, STORM_L, clamp((Y - base) / max(top - base, 0.01) * 0.9 + 0.7 * (n2.a - 0.5), 0.0, 1.0));
    cc += FLASH * fl * (3.2 * exp(-dot(fd, fd) / 0.0011) + 0.35);       // lit from inside
    cc = mix(cc, hazeH, 0.45);
    col = mix(col, cc * 0.8, rain * 0.5 * (1.0 - cloud));
    col = mix(col, cc, cloud * 0.9);
    if (uFlashP.z > 0.5 && fl > 0.25 && Y < uFlashP.y && Y > -0.002) {   // the bolt down to the sea: a jagged random walk
      float sY = Y / 0.0055; float i0 = floor(sY); float fY = sY - i0;
      uint seed = uint(uFlashP.x * 9973.0 + 5000.0);
      float j0 = h01(uint(i0 + 4.0) * 2u, seed) - 0.5, j1 = h01(uint(i0 + 5.0) * 2u, seed) - 0.5;
      float sY2 = Y / 0.0016; float k0 = floor(sY2);
      float jj = mix(h01(uint(k0 + 4.0) * 2u + 1u, seed) - 0.5, h01(uint(k0 + 5.0) * 2u + 1u, seed) - 0.5, sY2 - k0);
      float jag = mix(j0, j1, fY) * 0.009 + jj * 0.0025 + (Y - uFlashP.y) * 0.25 * (h01(5u, seed) - 0.5);
      col += FLASH * exp(-sq((X - uFlashP.x - jag) / (pxT * 0.8))) * fl * 3.0;
    }
  }
  col += FLASH * fl * (0.22 * exp(-dot(fd, fd) / 0.01) + 0.06 * exp(-dot(fd, fd) / 0.06));   // the haze lights up
  return col;
}
float dec16(vec2 t){ return (t.x * 255.0 + t.y) / 255.0; }
// the shore bands composited far to near over col; xs = screen column, e = elevation tangent, aa = edge width
vec3 landOver(vec3 col, float xs, float e, float aa, vec3 hazeH, inout float hitD){
  float u = (xs - PROF_X0) / (PROF_X1 - PROF_X0);
  if (u <= 0.0 || u >= 1.0) return col;
  float top = TAN_MIN + (TAN_MAX - TAN_MIN) * dec16(textureLod(uProfile, vec2(u, 5.5 / 6.0), 0.0).rg);
  if (e > top + aa) return col;
  for (int k = 4; k >= 0; k--) {
    vec4 t = textureLod(uProfile, vec2(u, (float(k) + 0.5) / 6.0), 0.0);
    float E = TAN_MIN + (TAN_MAX - TAN_MIN) * dec16(t.rg);
    float a = clamp((E - e) / aa + 0.5, 0.0, 1.0);
    if (a > 0.0) {
      float D = mix(BMIN[k], BMAX[k], t.b);
      float below = clamp((E - e) / max(E + 0.004, 0.004), 0.0, 1.0);    // 0 at the crest, 1 at the waterline
      vec3 lc = uLand * mix(1.1, 0.7, t.a) * mix(1.3, 0.85, sqrt(below));
      lc = mix(lc, hazeH, 1.0 - exp(-D / LAND_HAZE_L));
      col = mix(col, lc, a);
      hitD = mix(hitD, D, a);
    }
  }
  return col;
}
// everything above the sea in direction d (used for the sky and, mirrored, for the reflection)
vec3 skyScene(vec3 d, float pxT, float aaLand, vec3 hazeH, float corePlus, float gam, inout float hitD){
  vec3 c = skyBase(d) + uGlow * (CORE_AMP * sunCore(d, CORE_W2 + 2.0 * corePlus, CORE_W2 + 2.0 * corePlus * gam * gam));
  c = saturnOver(c, d, pxT + 1.4 * sqrt(corePlus));
  c = stormOver(c, d, pxT, hazeH);
  c = volcanoOver(c, d, pxT, hazeH);
  if ((uFlags & 1) != 0) {
    float e = d.y / length(d.xz);
    float xs = dot(d, uHR) / max(dot(d, uHF), 1e-3) / uTan.x;
    c = landOver(c, xs, e, aaLand, hazeH, hitD);
  }
  return c;
}
// one ripple layer: slope (world xz) and its sub-pixel variance, filtered by the pixel footprint and the motion
void ripLayer(vec2 off, float L, float c, float s, float w, vec2 p, vec2 px, vec2 py, inout vec2 sl, inout float var){
  vec2 uv = off + rot(p, c, s) / L;
  vec2 gx = rot(px, c, s) / L, gy = rot(py, c, s) / L, gm = rot(uMotion, c, s) / L;
  if (dot(gm, gm) > dot(gy, gy)) gy = gm;
  vec4 r = textureGrad(uRipple, uv, gx, gy);
  vec2 sv = (r.rg * 2.0 - 1.0) * w;
  sl += rotT(sv, c, s);
  var += max(r.b * 2.0 * w * w - dot(sv, sv), 0.0);
}
float mistPatch(vec2 xz, float lod){
  float n = textureLod(uNoise, uFog + xz / MIST_L, lod).g;
  return 0.03 + 1.2 * smoothstep(0.5, 0.78, n);
}
// optical depth of the mist layer between s0 and s1 along dir (closed form for an exponential height profile)
float mistSeg(vec3 dir, float s0, float s1, float pt){
  if (s1 <= s0) return 0.0;
  float y0 = dir.y * s0 + uCamY;
  float kk = dir.y * (s1 - s0) / MIST_HS;
  float g = abs(kk) < 1e-3 ? 1.0 - 0.5 * kk : (1.0 - exp(-kk)) / kk;
  return MIST_DENS * pt * exp(-max(y0, 0.0) / MIST_HS) * (s1 - s0) * g;
}
void main(){
  vec2 ndc = gl_FragCoord.xy / uRes * 2.0 - 1.0;
  vec3 dir = normalize(uCamF + (ndc.x * uTan.x) * uCamR + ((ndc.y - uShear) * uTan.y) * uCamU);
  vec3 ddx = dFdx(dir), ddy = dFdy(dir);
  float pxT = 2.0 * uTan.y / uRes.y;
  vec3 dH = normalize(vec3(dir.x, 0.0, dir.z));
  vec3 hazeH = skyBase(dH) + uGlow * (CORE_AMP * sunCore(dH, CORE_W2, CORE_W2));   // what distance fades to
  vec3 col; float tEnd = 6000.0;
  float foamM = 0.0;
  if (dir.y > -0.0012) {
    // ---- sky, Saturn, storm, volcano, the far shore
    float hd = tEnd;
    vec3 base = skyBase(dir);
    col = skyScene(dir, pxT, pxT, hazeH, 0.0, 0.0, hd);
    if ((uFlags & 16) != 0 && dir.y > 0.012) {   // faint high cloud streaks, stretched along x
      vec2 cuv = uCloud + (dir.xz / dir.y) * (CLOUD_H / CLOUD_L) * vec2(0.35, 1.0);
      float fp = pxT * CLOUD_H / (dir.y * dir.y) / CLOUD_L * 256.0;
      float n = textureLod(uNoise, cuv, clamp(log2(max(fp, 1.0)), 0.0, 7.0)).a;
      col += base * (smoothstep(0.02, 0.2, dir.y) * CLOUD_AMP * (n - 0.5) * 2.0);
    }
    tEnd = min(hd, tEnd);
  } else {
    // ---- the sea: an analytic plane, ripples from a slope map
    float t = uCamY / -dir.y;
    vec2 p = dir.xz * t;
    float ky = uCamY / (dir.y * dir.y);
    vec2 px = dir.xz * (ky * ddx.y) + ddx.xz * t, py = dir.xz * (ky * ddy.y) + ddy.xz * t;
    float fpm = max(length(px), length(py));                            // metres per pixel here
    float fpg = sqrt(length(px) * length(py));                          // the same, geometric mean (sharper at grazing)
    // oily slicks: calmer water with a thin-film sheen
    float slick = 0.0, film = 0.5, sheen = 0.0;
    if (t < 1500.0) {
      vec4 fo = textureLod(uNoise, uOil + p / OIL_L, log2(max(fpm / OIL_L * 256.0, 1.0)));
      slick = smoothstep(0.52, 0.7, fo.r) * (1.0 - smoothstep(500.0, 1500.0, t));
      if (t < 100.0) {
        float f2 = textureLod(uNoise, uOilN.xy + p / 23.0, log2(max(fpm / 23.0 * 256.0, 1.0)) + 1.5).g;
        film = fo.g * 1.6 + f2 * 1.2;
        sheen = slick * (1.0 - smoothstep(35.0, 100.0, t));
      }
    }
    // nitrogen "champagne": boiling froth with popping bubbles
    float bub = 0.0;
    if ((uFlags & 32) != 0) {
      for (int i = 0; i < 2; i++) {
        vec4 Fd = uField[i];
        if (Fd.w <= 0.001) continue;
        vec2 dq = p - Fd.xy;
        float dd = dot(dq, dq);
        if (dd > Fd.z * Fd.z * 1.7) continue;
        float sd = i == 0 ? uFieldSeed.x : uFieldSeed.y;
        float n = textureLod(uNoise, dq / 80.0 + vec2(sd, sd * 1.7), 0.0).g;
        float m = (1.0 - smoothstep(0.45, 1.0, sqrt(dd) / Fd.z + 0.6 * (n - 0.5))) * Fd.w;
        if (m <= 0.0) continue;
        float lodF = log2(max(sqrt(fpg * fpm) / 5.0 * 256.0, 1.0));
        float froth = 0.62 * textureLod(uNoise, dq / 5.0 + vec2(sd * 3.1, 0.0) + uTime * vec2(0.031, -0.023), lodF + 2.0).a
                    + 0.38 * textureLod(uNoise, dq / 1.9 + vec2(0.0, sd * 2.3) - uTime * vec2(0.02, 0.05), lodF + 3.2).g;
        float b = 0.0;
        if (fpm < 0.25) {                                               // resolved: bubbles swell and pop
          vec2 cq = dq / 0.75; vec2 ci = floor(cq); vec2 cf = cq - ci;
          ivec2 cc = ivec2(ci) + ivec2(i * 7919, 131);
          float h1 = hcell(cc, 91u), h2 = hcell(cc, 92u), h3 = hcell(cc, 93u);
          float ph = fract(uTime * (0.7 + 1.1 * h3) + h1);
          float br = 0.06 + 0.3 * ph;
          float dist = length(cf - vec2(0.2 + 0.6 * h1, 0.2 + 0.6 * h2));
          b = 0.6 * smoothstep(br + 0.14, br - 0.02, dist) * smoothstep(br - 0.26, br - 0.04, dist) * (1.0 - ph * ph) * (1.0 - smoothstep(0.08, 0.25, fpm));   // soft rims
        }
        float wide = 0.22 + 0.1 * clamp(lodF / 6.0, 0.0, 1.0);
        float f = m * (0.45 + 0.55 * smoothstep(0.5 - wide, 0.6 + wide, froth));
        foamM = max(foamM, f); bub = max(bub, b * m);
      }
    }
    // TURBO dash: the downwash tears the surface into a streak of foam ahead
    if (uDash > 0.01 && t < 70.0) {
      float fw = dot(p, uHF.xz), lat = dot(p, uHR.xz);
      vec4 nz = textureLod(uNoise, uOilN.zw + p / 19.0, log2(max(fpm / 19.0 * 256.0, 1.0)) + 1.0);
      float streak = (1.0 - smoothstep(0.6, 2.6, abs(lat) + 2.2 * (nz.g - 0.5))) * smoothstep(6.0, 12.0, fw) * (1.0 - smoothstep(20.0, 45.0, fw + 15.0 * (nz.a - 0.5)));
      foamM = max(foamM, uDash * streak * smoothstep(0.3, 0.75, nz.a + 0.25 * nz.g) * 0.7);
    }
    vec2 sl = vec2(0.0); float var = 0.0;
    ripLayer(uRip0, RIP_L0, RIP_C0, RIP_S0, RIP_W0 * (1.0 - 0.7 * slick), p, px, py, sl, var);
    ripLayer(uRip1, RIP_L1, RIP_C1, RIP_S1, RIP_W1 * (1.0 - 0.4 * slick), p, px, py, sl, var);
    ripLayer(uRip2, RIP_L2, RIP_C2, RIP_S2, RIP_W2, p, px, py, sl, var);
    // the rotor's downwash: rings of ripples running out from under the craft, strongest in a hover
    if (uDown > 0.01 && t < 90.0) {
      float rr = length(p); vec2 rd = p / max(rr, 0.01);
      float env = uDown * exp(-rr / 28.0) * (1.0 - smoothstep(0.12, 0.4, fpm));
      sl += rd * env * (0.075 * sin(rr * 3.9 - uTime * 5.5) + 0.04 * sin(rr * 1.7 - uTime * 3.1 + 1.3));
      var += env * 0.002;
    }
    // a "magic island": a rare, transient rougher, brighter patch on the surface
    float magic = 0.0;
    if ((uFlags & 8) != 0 && t < 5000.0) {
      vec2 f = uMagicFrac + p / MAGIC_CELL; vec2 fc = floor(f); vec2 lp = f - fc;
      ivec2 c = uMagicBase + ivec2(fc);
      if (hcell(c, 71u) < MAGIC_P) {
        vec2 cen = 0.3 + 0.4 * vec2(hcell(c, 72u), hcell(c, 73u));
        float an = 6.2831853 * hcell(c, 74u);
        vec2 q = rot(lp - cen, cos(an), sin(an)) * MAGIC_CELL;
        float rad = 70.0 + 110.0 * hcell(c, 75u);
        float nn = textureLod(uNoise, vec2(c) * 0.37 + q / 260.0, 0.0).a;
        float r = length(q / vec2(rad, rad * 0.42)) + 0.45 * (nn - 0.5);
        float life = smoothstep(0.35, 0.85, sin(6.2831853 * (uTime / MAGIC_T + hcell(c, 76u))));
        magic = (1.0 - smoothstep(0.55, 1.0, r)) * life;
      }
    }
    var += magic * 0.03 + foamM * 0.06;
    vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
    vec3 R = reflect(dir, n); R.y = max(abs(R.y), 0.0012);
    float gam = -dir.y;
    float hd = 0.0;
    vec3 refl = skyScene(R, pxT, pxT + 1.5 * sqrt(var), hazeH, var, gam, hd);
    float cosi = clamp(dot(-dir, n), 0.0, 1.0);
    // thin-film sheen: an oily rainbow over the orange reflection, strongest in the slicks
    vec3 irid = 0.5 + 0.5 * cos(6.2831853 * (film * 0.75 + uTime * 0.01 + vec3(0.0, 0.33, 0.67)));   // broad swirls, not flecks
    refl *= mix(vec3(1.0), 0.4 + 1.2 * irid, 0.45 * sheen);
    float Fr = 0.018 + 0.982 * pow(1.0 - cosi, 5.0);                   // liquid methane, n ~ 1.28
    col = uBody * (1.0 - Fr) + refl * (Fr * 0.62);                      // a dark liquid, a soft mirror
    col += magic * 0.07 * hazeH;
    // the froth sits on top: lit by the haze, violet in its hollows, bubble rims catch the light
    vec3 foamC = mix(FOAM_SH * 0.62, FOAM * 0.68, smoothstep(0.2, 0.9, foamM));
    col = mix(col, foamC, clamp(foamM * 1.1, 0.0, 0.92));
    col += FOAM * (bub * 0.6);
    // the searchlights' pools on the liquid: a little light in the body, the ripples glittering in it
    for (int k = 0; k < 2; k++) {
      float on = k == 0 ? uLamp.x : uLamp.y;
      if (on <= 0.001) continue;
      vec3 l; float lit = lampOn(vec3(p.x, 0.0, p.y), k, l);
      if (lit <= 0.0) continue;
      float spec = pow(max(dot(n, normalize(-l - dir)), 0.0), 220.0);
      col += LAMP_C * (on * lit) * (0.07 * (1.0 - Fr) + 2.6 * spec + 0.3 * foamM);
    }
    col = mix(col, hazeH, 1.0 - exp(-t / HAZE_L));
    tEnd = t;
  }
  // ---- low violet mist banks over the sea (and fizz vapour over a field)
  float pt0 = 0.0;
  vec3 mistC = mix(uMist, hazeH, 0.3);
  if ((uFlags & 2) != 0 && dir.y < 0.2) {
    float s0e = min(tEnd, 150.0), s1e = min(tEnd, 900.0), s2e = min(tEnd, 2500.0);
    pt0 = mistPatch(dir.xz * (0.4 * s0e), 0.0) * (1.0 - 0.7 * uDown);   // the downwash blows the near mist thin
    float pt1 = mistPatch(dir.xz * (150.0 + 0.4 * max(s1e - 150.0, 0.0)), 0.5);
    float pt2 = mistPatch(dir.xz * (900.0 + 0.4 * max(s2e - 900.0, 0.0)), 1.5);
    float tau = mistSeg(dir, 0.0, s0e, pt0) + mistSeg(dir, 150.0, s1e, pt1) + mistSeg(dir, 900.0, s2e, pt2);
    col = mix(mistC, col, exp(-tau));
    col = mix(col, mistC * 1.15, 0.22 * foamM);
  }
  // ---- the searchlights' beams in the haze: what each cone lights along the ray, out to the sea or 90 m
  if (uLamp.x + uLamp.y > 0.001) {
    float far = min(tEnd, 90.0), beam = 0.0;
    for (int k = 0; k < 2; k++) {
      float on = k == 0 ? uLamp.x : uLamp.y;
      if (on <= 0.001) continue;
      for (int i = 0; i < 10; i++) {
        float tq = far * (float(i) + 0.5) / 10.0;
        vec3 l; beam += on * lampOn(vec3(0.0, uCamY, 0.0) + dir * tq, k, l) * far / 10.0;
      }
    }
    col += LAMP_C * mix(hazeH, vec3(1.0), 0.5) * (beam * 0.0005);
  }
  // ---- spray on the glass while ploughing through froth
  if (uWhite > 0.003) {
    vec2 sp = gl_FragCoord.xy / uRes;
    float n = textureLod(uNoise, vec2(sp.x * 6.0, sp.y * 1.2 + uTime * 0.06), 0.0).a;
    float n2 = textureLod(uNoise, vec2(sp.x * 21.0 + 0.3, sp.y * 4.0 - uTime * 0.13), 0.0).g;
    float zone = smoothstep(0.5, 0.0, sp.y) * (0.6 + 0.4 * smoothstep(0.45, 0.0, min(sp.x, 1.0 - sp.x)));
    float blot = smoothstep(0.45, 0.7, n * 0.6 + n2 * 0.6);
    float w = clamp(uWhite * zone * (0.15 + 0.85 * blot), 0.0, 0.6);
    col = mix(col, mix(FOAM_SH, FOAM, 0.6) * 0.7, w);
  }
  vec3 cc = toDisplay(col * uExposure);
  cc += (h01(uint(gl_FragCoord.x) + uint(gl_FragCoord.y) * 8191u, 3u) - 0.5) / 255.0;   // no banding in the dark
  o = vec4(cc, 1.0);
}
)GLSL";
        return s;
    }

    //==========================================================================
    // Shared by the particle passes: a capsule sprite from a (where it was) to b (where it is), in pixels.
    const char* const glslCapsule = R"GLSL(
out vec2 vLoc; out float vLen; out float vRad; out float vSoft;
float capsule(vec2 a, vec2 b, float rad, int cv, vec2 res){
  vec2 ax = b - a; float len = length(ax);
  vec2 dn = len > 1e-3 ? ax / len : vec2(0.0, 1.0); vec2 nn = vec2(-dn.y, dn.x);
  float ext = rad + 1.0;
  bool head = (cv == 1 || cv == 2 || cv == 4);
  float side = (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0;
  vec2 q = (head ? b + dn * ext : a - dn * ext) + nn * (side * ext);
  gl_Position = vec4(q / res * 2.0 - 1.0, 0.0, 1.0);
  vLoc = vec2(head ? len + ext : -ext, side * ext); vLen = len; vRad = rad;
  return len;
}
)GLSL";

    juce::String fsCapsule()
    {
        juce::String s ("#version 150\n");
        s << R"GLSL(in vec3 vCol; in float vA; in vec2 vLoc; in float vLen; in float vRad; in float vSoft;
uniform float uExposure;
out vec4 o;
)GLSL" << glslHash << glslConst() << R"GLSL(
void main(){
  float dx = vLoc.x - clamp(vLoc.x, 0.0, vLen);
  float dist = length(vec2(dx, vLoc.y));
  float cover = mix(clamp(vRad + 0.5 - dist, 0.0, 1.0), sq(max(1.0 - dist / (vRad + 1.0), 0.0)), vSoft);   // drops crisp, mist soft
  float bead = 1.0 + 0.6 * (1.0 - vSoft) * (1.0 - smoothstep(0.0, vRad, dist));   // a bright core: a round drop
  float a = min(1.0, vA * cover);
  vec3 c = toDisplay(vCol * bead * uExposure);
  o = vec4(c * a, a);
}
)GLSL";
        return s;
    }

    // Pass 5: the drizzle — pea-sized drops drifting down like snow, world-anchored in a wrapping box ahead.
    juce::String vsDrizzle()
    {
        juce::String s ("#version 150\n");
        s << R"GLSL(uniform vec3 uCamR, uCamU, uCamF, uCamRp, uCamUp, uCamFp;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime, uExpo;
uniform vec2 uBoxBase, uBoxCen, uWind;
uniform vec3 uVel;
uniform vec3 uAmb;
uniform float uDown;
out vec3 vCol; out float vA;
)GLSL" << glslHash << glslCapsule
          << "const float B = " << F (W::dropBox) << ", HB = " << F (W::dropBoxHeight) << ";\n"
          << R"GLSL(vec2 toPx(vec3 v){ vec2 ndc = vec2(v.x / (v.z * uTan.x), v.y / (v.z * uTan.y) + uShear); return (ndc * 0.5 + 0.5) * uRes; }
void main(){
  int id = gl_VertexID / 6; int cv = gl_VertexID - id * 6;
  uint u = uint(id);
  float h1 = h01(u, 11u), h2 = h01(u, 12u), h3 = h01(u, 13u), h4 = h01(u, 14u), h5 = h01(u, 15u), h6 = h01(u, 16u);
  vec2 xz = mod(vec2(h1, h2) * B - uBoxBase, B) - 0.5 * B + uBoxCen;
  float vf = 0.85 + 0.55 * h5;                       // big drops in thick air and 1/7 g: they drift down
  float y = mod(h3 * HB - vf * uTime, HB) - uCamY;   // from the sea surface up
  float sw = 6.2831853 * h6, fq = 0.5 + 0.5 * h4;
  vec2 sway = 0.14 * vec2(sin(uTime * fq * 3.0 + sw), cos(uTime * fq * 2.3 + sw));
  vec2 swayV = 0.14 * vec2(cos(uTime * fq * 3.0 + sw) * fq * 3.0, -sin(uTime * fq * 2.3 + sw) * fq * 2.3);
  vec3 P = vec3(xz.x + sway.x, y, xz.y + sway.y);
  float hd = length(P.xz), push = uDown * max(0.0, 1.0 - hd / 9.0);            // the downwash blows the drops aside
  P.xz += (P.xz / max(hd, 0.1)) * (push * 2.2); P.y -= push * 1.2;
  vec3 Pp = P - (vec3(uWind.x + swayV.x, -vf, uWind.y + swayV.y) - uVel) * uExpo;
  vec3 v0 = vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF));
  vec3 v1 = vec3(dot(Pp, uCamRp), dot(Pp, uCamUp), dot(Pp, uCamFp));
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCol = vec3(0.0); vA = 0.0; vLoc = vec2(0.0); vLen = 0.0; vRad = 1.0; vSoft = 0.0;
  if (v0.z < 0.6 || v1.z < 0.4) return;
  float r = 0.0025 + 0.002 * h4;                      // 5–9 mm across: peas
  float focal = uRes.y / (2.0 * uTan.y);
  float rpx = r * focal / v0.z;
  float rad = max(rpx, 0.7);
  float len = capsule(toPx(v1), toPx(v0), rad, cv, uRes);
  float fadeZ = smoothstep(0.6, 2.0, v0.z) * (1.0 - smoothstep(30.0, 46.0, v0.z));
  vA = 0.75 * min(1.0, sq(rpx / rad) * 1.5) * fadeZ * mix(1.0, (2.0 * rad) / (2.0 * rad + len), 0.85);
  vCol = uAmb * 0.8;
  vCol = mix(vCol, uAmb, 1.0 - exp(-v0.z / 300.0));
}
)GLSL";
        return s;
    }

    // Pass 4: the downwash kicks the liquid up — a low ring of soft mist and flung droplets, ahead and around.
    juce::String vsSpray()
    {
        juce::String s ("#version 150\n");
        s << R"GLSL(uniform vec3 uCamR, uCamU, uCamF, uHR, uHF;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime, uSpeed, uDown, uFoam, uBoost;
uniform vec3 uAmb, uFoamC;
out vec3 vCol; out float vA;
)GLSL" << glslHash << glslCapsule << R"GLSL(
const float G = 1.35;                                // Titan's gravity
vec2 toPx(vec3 v){ vec2 ndc = vec2(v.x / (v.z * uTan.x), v.y / (v.z * uTan.y) + uShear); return (ndc * 0.5 + 0.5) * uRes; }
vec3 posAt(vec3 p0, vec3 v0, vec3 vt, float a, float k){ return p0 + vt * a + (v0 - vt) * ((1.0 - exp(-k * a)) / k); }
void main(){
  int id = gl_VertexID / 6; int cv = gl_VertexID - id * 6;
  uint u = uint(id);
  float h1 = h01(u, 21u), h2 = h01(u, 22u), h3 = h01(u, 23u), h4 = h01(u, 24u), h5 = h01(u, 25u), h6 = h01(u, 26u), h7 = h01(u, 27u), h8 = h01(u, 28u), h9 = h01(u, 29u);
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCol = vec3(0.0); vA = 0.0; vLoc = vec2(0.0); vLen = 0.0; vRad = 1.0; vSoft = 0.0;
  bool mist = h3 < 0.55;
  float amount = clamp(uDown * (mist ? 1.0 : 0.7) + uFoam * 0.9 + uBoost * 0.35, 0.0, 1.0);
  float gate = smoothstep(h9 - 0.08, h9, amount);    // more wash = more particles alive
  if (gate <= 0.0) return;
  float life = mist ? 2.4 + 2.2 * h1 : 1.4 + 1.2 * h1;
  float age = mod(uTime + h2 * life, life);
  // craft frame: x right, y up from the eye, z forward; kicked up off the liquid around the wash, mostly ahead
  float ang = (h5 - 0.5) * 2.8, r0 = mist ? 3.0 + 13.0 * h6 : 2.5 + 6.0 * h6;
  vec2 dir = vec2(sin(ang), cos(ang));
  vec3 p0 = vec3(dir.x * r0, -uCamY + 0.05, dir.y * r0);
  float sp = (mist ? 2.5 : 3.5) + 4.0 * h7;
  vec3 v0 = vec3(dir.x * sp, (mist ? 0.5 : 1.2) + (mist ? 1.0 : 2.4) * h8, dir.y * sp);
  float k = mist ? 1.8 : 0.9;                          // fine mist is held by the thick air
  vec3 vt = vec3(0.0, -G / k, -uSpeed);                // terminal: settles slowly, swept back by the air
  vec3 c = posAt(p0, v0, vt, age, k), cp = posAt(p0, v0, vt, max(age - 0.018, 0.0), k);
  if (c.y < -uCamY) return;                            // back in the sea
  vec3 P = uHR * c.x + vec3(0.0, c.y, 0.0) + uHF * c.z, Pp = uHR * cp.x + vec3(0.0, cp.y, 0.0) + uHF * cp.z;
  vec3 v0v = vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF)), v1v = vec3(dot(Pp, uCamR), dot(Pp, uCamU), dot(Pp, uCamF));
  if (v0v.z < 0.5 || v1v.z < 0.4) return;
  float r = mist ? 0.3 + 0.45 * h4 : 0.003 + 0.004 * h4;
  float focal = uRes.y / (2.0 * uTan.y);
  float rpx = r * focal / v0v.z;
  float rad = max(rpx, 0.7);
  float len = capsule(toPx(v1v), toPx(v0v), rad, cv, uRes);
  float fade = smoothstep(0.0, 0.3, age) * (1.0 - smoothstep(0.7, 1.0, age / life));
  vA = gate * fade * (mist ? 0.16 * smoothstep(2.0, 6.0, v0v.z) : 0.75 * min(1.0, sq(rpx / rad) * 1.5)) * mix(1.0, (2.0 * rad) / (2.0 * rad + len), 0.7);
  vCol = mix(uAmb * (mist ? 1.05 : 0.9), uFoamC * 0.9, clamp(uFoam, 0.0, 1.0));
  vSoft = mist ? 1.0 : 0.0;
}
)GLSL";
        return s;
    }

    // Pass 3: the beacon buoys — body, lamp glow, and the lamp's glitter column on the liquid.
    const char* const vsBuoy = R"GLSL(#version 150
uniform vec3 uCamR, uCamU, uCamF, uHR;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime;
uniform vec4 uBuoy[8]; uniform int uBuoyN;
out vec2 vUv; flat out int vPart; flat out int vType; out vec3 vCol; out float vA; out float vDist;
const float LAMP_H = 1.95;
vec3 toView(vec3 P){ return vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF)); }
vec2 toPx(vec3 v){ return (vec2(v.x / (v.z * uTan.x), v.y / (v.z * uTan.y) + uShear) * 0.5 + 0.5) * uRes; }
void main(){
  int q = gl_VertexID / 6, cv = gl_VertexID - q * 6;
  int bi = q / 3, part = q - bi * 3;
  vPart = part; vType = 0; vUv = vec2(0.0); vCol = vec3(0.0); vA = 0.0; vDist = 0.0;
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
  if (bi >= uBuoyN) return;
  vec4 Bu = uBuoy[bi];
  vType = int(Bu.z + 0.5);
  float bob = 0.07 * sin(uTime * 1.3 + Bu.w * 6.2832);
  vec3 base = vec3(Bu.x, -uCamY + bob, Bu.y);
  vec3 vb = toView(base);
  if (vb.z < 2.0) return;
  vDist = length(base);
  vec2 c = vec2((cv == 1 || cv == 2 || cv == 4) ? 1.0 : -1.0, (cv == 2 || cv == 4 || cv == 5) ? 1.0 : 0.0);
  float focal = uRes.y / (2.0 * uTan.y);
  float per = 2.2 + 1.2 * fract(Bu.w * 7.3), ph = mod(uTime + Bu.w * 9.0, per);
  float blink = smoothstep(0.0, 0.05, ph) * (1.0 - smoothstep(0.3, 0.5, ph));
  vec3 lampC = vType == 0 ? vec3(1.0, 0.12, 0.07) : (vType == 1 ? vec3(0.16, 1.0, 0.42) : vec3(1.0, 0.62, 0.12));
  float haze = exp(-vDist / 9000.0);
  vCol = lampC * haze;
  if (part == 0) {                                    // the body: a camera-facing card, shape drawn in the FS
    vec3 P = base + uHR * (c.x * 0.6) + vec3(0.0, -0.05 + c.y * 2.2, 0.0);
    vec2 s = toPx(toView(P));
    vec2 sc = toPx(vb);
    float minW = 1.6;                                 // keep far buoys at least a hair wide
    s.x = sc.x + sign(s.x - sc.x) * max(abs(s.x - sc.x), minW);
    gl_Position = vec4(s / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = c; vA = blink;
  } else if (part == 1) {                             // the lamp's glow, never smaller than a few pixels
    vec2 s = toPx(toView(base + vec3(0.0, LAMP_H, 0.0)));
    float r = max(1.4 * focal / vb.z, 6.0) * (0.55 + 0.9 * blink) + 2.0;
    gl_Position = vec4((s + vec2(c.x, c.y * 2.0 - 1.0) * r) / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = vec2(c.x, c.y * 2.0 - 1.0); vA = blink;
  } else {                                            // its reflection, smeared down the ripples toward us
    vec2 w = toPx(vb);
    vec2 m = toPx(toView(vec3(Bu.x, -uCamY - LAMP_H, Bu.y)));
    float spread = 0.06 / (2.0 * uTan.y / uRes.y);
    float r = max(0.8 * focal / vb.z, 3.0) * (0.6 + 0.6 * blink);
    float y0 = m.y - spread, y1 = w.y;
    float yy = mix(y0, y1, c.y);
    gl_Position = vec4(vec2(m.x + c.x * r, yy) / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = vec2(c.x, (yy - m.y) / spread); vA = blink;
  }
}
)GLSL";

    juce::String fsBuoy()
    {
        juce::String s ("#version 150\n");
        s << R"GLSL(in vec2 vUv; flat in int vPart; flat in int vType; in vec3 vCol; in float vA; in float vDist;
uniform float uTime, uExposure; uniform vec3 uAmb, uHaze;
out vec4 o;
)GLSL" << glslHash << glslConst() << R"GLSL(
void main(){
  if (vPart == 0) {
    float y = vUv.y * 2.2 - 0.05, x = abs(vUv.x) * 0.6;                // metres above the waterline / off the axis
    float aw = max(fwidth(x), 0.004), ah = max(fwidth(y), 0.004);
    float hull = (1.0 - smoothstep(-aw, aw, x - (0.38 - 0.12 * clamp(y / 0.6, 0.0, 1.0)))) * (1.0 - smoothstep(-ah, ah, y - 0.6)) * smoothstep(-ah, ah, y + 0.02);
    float mast = (1.0 - smoothstep(-aw, aw, x - (0.05 + 0.13 * clamp((1.72 - y) / 1.1, 0.0, 1.0)))) * smoothstep(-ah, ah, y - 0.58) * (1.0 - smoothstep(-ah, ah, y - 1.72));
    mast *= 0.55 + 0.45 * step(0.5, fract(y * 6.0 + x * 9.0));          // a lattice, not a pole
    float mark = 1.0 - smoothstep(-aw, aw, length(vec2(x, (y - 1.8) * 1.2)) - 0.12);
    float lamp = 1.0 - smoothstep(-aw, aw, length(vec2(x, y - 1.95)) - 0.075);
    float a = max(max(hull, mast), max(mark, lamp));
    vec3 paint = vType == 0 ? vec3(0.32, 0.05, 0.04) : (vType == 1 ? vec3(0.04, 0.2, 0.09) : vec3(0.36, 0.24, 0.04));
    vec3 c = paint * (0.35 + 0.65 * smoothstep(-0.1, 0.6, y)) + uAmb * 0.05 * smoothstep(0.2, 0.0, abs(vUv.x) - 0.6);
    c += vec3(0.25, 0.22, 0.24) * (1.0 - smoothstep(0.0, 0.05, abs(y))) * step(x, 0.42);   // a lick of froth at the waterline
    c = mix(c, vCol * 3.0 * vA + 0.05, lamp);
    c = mix(c, uHaze, 1.0 - exp(-vDist / 5000.0));
    vec3 d = toDisplay(c * uExposure);
    o = vec4(d * a, a);
  } else if (vPart == 1) {
    float r2 = dot(vUv, vUv);
    vec3 c = vCol * vA * (exp(-r2 * 10.0) * 1.6 + exp(-r2 * 3.0) * 0.3) * sq(max(1.0 - r2, 0.0));   // fades to nothing at the sprite's edge
    o = vec4(toDisplay(c * uExposure), 0.0);                            // additive
  } else {
    float across = exp(-vUv.x * vUv.x * 3.0) * sq(max(1.0 - vUv.x * vUv.x, 0.0));
    float along = exp(-vUv.y * vUv.y * 1.3) * smoothstep(-1.0, -0.6, vUv.y);
    float glit = h01(uint(gl_FragCoord.y * 0.5) + 77u, uint(uTime * 9.0)) * 1.4 + 0.15;
    vec3 c = vCol * vA * across * along * glit * 0.55;
    o = vec4(toDisplay(c * uExposure), 0.0);
  }
}
)GLSL";
        return s;
    }

    //==========================================================================
    // Pass 6: the canopy. Over the scene: the rotor, its shadow, the tholin film and flakes, the wipers.
    juce::String fsGlass()
    {
        const auto& wipers = W::wipers();
        const auto list = [&wipers] (auto&& one) { juce::String s; for (size_t i = 0; i < wipers.size(); ++i) s << (i ? ", " : "") << one (wipers[i]); return s; };
        juce::String s ("#version 150\n");
        s << R"GLSL(uniform sampler2D uNoise;
uniform vec2 uRes, uTan; uniform float uAspect, uTime;
uniform vec4 uWipe;      // start of the current / last wipe, start of the one before, when deposits began, stroke fraction 0..1 (< -9 = parked)
uniform vec4 uStroke;    // the current / last wipe's stroke out and its pause at the far end, the one before's (back as long as out)
uniform vec4 uRotor;     // glint phase (rad), disc tilt (rad), the shadow band's place on the glass (m), its strength
out vec4 o;
)GLSL" << glslHash
          << "const vec3 HUB = vec3(" << F (W::rotorHub[0]) << ", " << F (W::rotorHub[1]) << ", " << F (W::rotorHub[2]) << "); const float ROT_R = " << F (W::rotorRadius) << ";\n"
          << "const float WW = " << F (W::bladeWidth) << ";\n"
          << "// the wipers: pivot; stroke start, end, blade in, out; their pane's sides (x = a + b*y in the cabin's pixels)\n"
          << "const int NW = " << (int) wipers.size() << ";\n"
          << "const vec2 WP[NW] = vec2[NW](" << list ([] (const W::Wiper& w) { return "vec2(" + F (w.p[0]) + ", " + F (w.p[1]) + ")"; }) << ");\n"
          << "const vec4 WA[NW] = vec4[NW](" << list ([] (const W::Wiper& w) { return "vec4(" + F (w.th0) + ", " + F (w.th1) + ", " + F (w.rIn) + ", " + F (w.rOut) + ")"; }) << ");\n"
          << "const vec4 WM[NW] = vec4[NW](" << list ([] (const W::Wiper& w) { return "vec4(" + F (w.pane[0]) + ", " + F (w.pane[1]) + ", " + F (w.pane[2]) + ", " + F (w.pane[3]) + ")"; }) << ");\n"
          << "const vec2 CAB_PX = vec2(" << F (W::cabinWidth) << ", " << F (W::cabinHeight) << ");\n"
          << "const float SB = " << F (std::sin (W::glassBeta)) << ", CB = " << F (std::cos (W::glassBeta)) << ", YB = " << F (W::glassYb) << ", ZB = " << F (W::glassZb) << ", SHR = " << F (W::shear) << ";\n"
          << "const float FLAKE_RATE = " << F (W::flakeRate) << ", FILM_RATE = " << F (W::filmRate) << ", CELL = " << F (W::flakeCell) << ", FINE = " << F (W::fineCell) << ";\n"
          << R"GLSL(bool onPane(int i, vec2 X){ vec4 m = WM[i]; return X.x > m.x + m.y * X.y && X.x < m.z + m.w * X.y; }
float invSmooth(float y){ return 0.5 - sin(asin(clamp(1.0 - 2.0 * y, -1.0, 1.0)) / 3.0); }   // inverse of smoothstep
vec4 h4(ivec2 c, uint s){ uint h = ih(uint(c.x) * 0x9E3779B1u ^ ih(uint(c.y) * 0x85EBCA77u ^ s)); return vec4(uvec4(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u, h >> 24)) / 255.0; }   // four random bytes
float segDist(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }
// where this pixel's ray meets the glass: (s across, t up the slope), metres. Plane through (0, YB, -ZB), normal (0, SB, CB).
vec2 glassST(vec2 ndc){
  vec3 d = vec3(ndc.x * uTan.x, (ndc.y - SHR) * uTan.y, -1.0);
  vec3 P = d * ((YB * SB - ZB * CB) / (d.y * SB - CB));
  return vec2(P.x, (P.y - YB) * CB - (P.z + ZB) * SB);
}
// the main rotor above the canopy, seen from below through the glass's top: a translucent disc (blades are a blur),
// a ring where the tips run, one soft glint travelling slowly round the rim. Fixed to the cab, never strobing.
vec4 rotorLayer(vec2 ndc){
  vec3 d = vec3(ndc.x * uTan.x, (ndc.y - SHR) * uTan.y, -1.0);
  float ct = cos(uRotor.y), st = sin(uRotor.y);
  vec3 N = vec3(0.0, ct, -st);                                 // tilted forward: the front of the disc is lower
  float den = dot(d, N);
  if (den <= 1e-4) return vec4(0.0);
  vec3 q = d * (dot(HUB, N) / den) - HUB;
  float rho = length(q);
  if (rho > ROT_R + 0.4) return vec4(0.0);
  float phi = atan(q.x, dot(q, vec3(0.0, -st, -ct)));
  float disc = 1.0 - smoothstep(ROT_R - 0.4, ROT_R + 0.05, rho);
  float tips = exp(-sq((rho - (ROT_R - 0.3)) / 0.25));
  float a = (0.09 + 0.07 * tips) * disc;
  float dphi = mod(phi - uRotor.x + 1.5707963, 3.14159265) - 1.5707963;          // two glints, opposite each other
  float glint = exp(-sq(dphi / 0.38)) * smoothstep(ROT_R - 2.4, ROT_R - 0.3, rho) * disc;
  return vec4(vec3(0.02, 0.018, 0.016) * a + vec3(0.30, 0.24, 0.16) * (glint * 1.15), a);    // premultiplied; the glint adds
}
// one blade's arc (wiper i, on its own pane only): is g in it, and when did that blade last cross it (out with an
// easing, a pause, back)?
float arcTime(vec2 g, int i, bool pane, out bool inA, out float gen, out float rho, out float phi){
  vec4 a = WA[i];
  vec2 rp = g - WP[i]; rho = length(rp); phi = atan(rp.y, rp.x); gen = 0.0;
  float x = (phi - a.x) / (a.y - a.x);
  inA = pane && rho > a.z && rho < a.w && x > 0.0 && x < 1.0;
  if (!inA) return -1e9;
  x = invSmooth(x);
  float tOut = uWipe.x + x * uStroke.x, tBack = uWipe.x + uStroke.x + uStroke.y + (1.0 - x) * uStroke.x;
  if (uTime >= tBack) { gen = uWipe.x; return tBack; }
  if (uTime >= tOut) { gen = uWipe.x; return tOut; }
  gen = uWipe.y; return uWipe.y + uStroke.z + uStroke.w + (1.0 - x) * uStroke.z;
}
// the line of pushed muck at an arc's limit: its outer and inner radius and the far end of the stroke
float smearBand(int i, float rho, float phi, float aa){
  vec4 a = WA[i];
  float x = (phi - a.x) / (a.y - a.x);
  float dOut = rho - a.w, dIn = a.z - rho, dEnd = (x - 1.0) * abs(a.y - a.x) * rho;
  bool inAng = x > 0.0 && x < 1.0;
  float band = 0.0;
  if (inAng && dOut > 0.0) band = 1.0 - smoothstep(0.0, 0.007 + aa, dOut);
  if (inAng && dIn > 0.0) band = max(band, 1.0 - smoothstep(0.0, 0.005 + aa, dIn));
  if (rho > a.z && rho < a.w && dEnd > 0.0) band = max(band, 1.0 - smoothstep(0.0, 0.009 + aa, dEnd));
  return band;
}
// wiper i lying on the glass at stroke fraction f, on its pane only: the arm rides on the blade's inner half; a
// rubber blade with its leading edge lit
vec4 wiperOver(vec4 acc, vec2 g, int i, float f, float pane, float aa){
  vec4 a = WA[i]; vec2 piv = WP[i]; float ang = mix(a.x, a.y, f);
  vec2 dir = vec2(cos(ang), sin(ang)), nrm = vec2(-dir.y, dir.x) * sign(a.y - a.x);
  float da = segDist(g, piv, piv + dir * (0.5 * (a.z + a.w)) + nrm * (0.007 * WW));
  float arm = (1.0 - smoothstep(0.008 * WW - aa, 0.008 * WW + aa, da)) * pane;
  float db = segDist(g, piv + dir * (a.z - 0.01), piv + dir * (a.w + 0.01));
  float blade = (1.0 - smoothstep(0.010 * WW - aa, 0.010 * WW + aa, db)) * pane;
  vec3 bc = vec3(0.012, 0.011, 0.012) + vec3(0.07, 0.05, 0.035) * smoothstep(0.002 * WW, 0.008 * WW, dot(g - piv, nrm));
  acc = vec4(vec3(0.035, 0.03, 0.03) * arm, arm) + acc * (1.0 - arm);
  return vec4(bc * blade, blade) + acc * (1.0 - blade);
}
void main(){
  vec2 uv = gl_FragCoord.xy / uRes;
  vec2 g = glassST(uv * 2.0 - 1.0);
  float aa = 0.75 * length(fwidth(g));                       // one pixel, measured on the glass
  vec4 acc = vec4(0.0);                                       // premultiplied layers, blended over the scene
  vec2 p = vec2(uv.x * uAspect, uv.y);                       // window heights (for the edge weighting)
  vec2 X = vec2(uv.x, 1.0 - uv.y) * CAB_PX;                 // the cabin's pixels, y down (for the panes)
  float tw = uWipe.z, gen = 0.0, band = 0.0, bph = 0.0, brh = 0.0, panes[NW];
  bool inAny = false;
  for (int i = 0; i < NW; i++) {
    bool pane = onPane(i, X), inA; float gi, rh, ph;
    panes[i] = pane ? 1.0 : 0.0;
    float ai = arcTime(g, i, pane, inA, gi, rh, ph);
    if (ai > tw) { tw = ai; gen = gi; }
    inAny = inAny || inA;
    float b = pane ? smearBand(i, rh, ph, aa) : 0.0;
    if (b > band) { band = b; bph = ph; brh = rh; }
  }
  float age = max(uTime - tw, 0.0);
  // more settles toward the edges and corners, in drifts
  float ex = 1.0 - smoothstep(0.0, 0.6, min(p.x, uAspect - p.x)), ey = 1.0 - smoothstep(0.0, 0.32, min(p.y, 1.0 - p.y));
  float w = 0.22 + 0.78 * clamp(max(ex, ey) + 0.7 * ex * ey, 0.0, 1.0);
  w *= 0.35 + 1.3 * textureLod(uNoise, g * 1.15 + vec2(0.13, 0.71), 0.0).g;
  float D = age * FLAKE_RATE * w; D = 1.3 * (1.0 - exp(-D / 1.3));
  float film = age * FILM_RATE * w; film = 0.5 * (1.0 - exp(-film / 0.5));
  if (!inAny && uWipe.x > -1e8) {
    if (band > 0.0) {
      float pushed = clamp((uWipe.x - max(uWipe.y + uStroke.z * 2.0 + uStroke.w, uWipe.z)) * FLAKE_RATE * w * 1.4, 0.0, 0.7);
      pushed *= smoothstep(uWipe.x, uWipe.x + uStroke.x, uTime);
      float streak = 0.55 + 0.45 * textureLod(uNoise, vec2(bph * 4.0, brh * 30.0), 0.0).a;
      film = max(film, band * pushed * streak);
    }
  }
  acc = rotorLayer(uv * 2.0 - 1.0);                                                // the rotor, beyond the glass
  if (uRotor.w > 0.001) {                                                          // its shadow sweeping over the glass
    float sb = exp(-sq((g.x - uRotor.z - 0.35 * g.y) / 0.16)) * uRotor.w;
    acc = vec4(0.0, 0.0, 0.0, sb) + acc * (1.0 - sb);
  }
  acc = vec4(vec3(0.075, 0.042, 0.018) * film, 0.3 * film) + acc * (1.0 - 0.3 * film);   // a warm brown veil on the glass
  // tholin flakes, laid out on the glass (so they shrink and flatten toward the top): one per hashed cell once the
  // deposit passes the cell's threshold; a fine dust between
  if (D > 0.015) {
    ivec2 gg = ivec2(int(gen * 7.0), int(gen * 3.0));
    vec2 cq = g / CELL; vec2 ci = floor(cq); vec2 cf = cq - ci; ivec2 cc = ivec2(ci) + gg;
    vec4 ha = h4(cc, 201u);
    float h = ha.x * 1.35 + 0.04;
    if (h < D) {
      vec4 hb = h4(cc, 202u);
      vec2 ctr = 0.3 + 0.4 * ha.yz;
      float rad = 0.09 + 0.42 * ha.w * ha.w;                                      // many small, a few big
      float an = 6.2831853 * hb.x, ca = cos(an), sa = sin(an); vec2 q = cf - ctr; q = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
      q.y *= 1.0 + 0.9 * hb.y;                                                    // elongated
      float d0 = length(q) / rad;
      if (d0 < 1.7) {                                                             // only near the flake: the noise read
        vec2 nn = textureLod(uNoise, g * 88.0 + hb.zw * 7.0, 0.0).ag;
        float dd = d0 + 0.9 * (nn.x - 0.5) + 0.45 * (nn.y - 0.5);                // ragged, fluffy edges
        float a = (1.0 - smoothstep(0.6, 1.0, dd)) * smoothstep(h, h + 0.08, D) * (0.55 + 0.4 * fract(hb.x * 7.31));
        vec3 tint = mix(vec3(0.16, 0.09, 0.045), vec3(0.58, 0.34, 0.15), fract(hb.y * 5.17 + hb.z * 3.3));   // dark brown to rust and tan
        vec3 fc = tint + vec3(0.08, 0.045, 0.0) * smoothstep(0.3, 1.0, dd);
        float ka = a * 0.75;                                                      // a quarter of the light gets through
        acc = vec4(fc * a, ka) + acc * (1.0 - ka);
      }
    }
    vec2 fq = g / FINE; vec2 fi = floor(fq); vec4 hf = h4(ivec2(fi) + gg * 3, 211u);
    if (hf.x * 2.4 + 0.06 < D) {
      float dd = length(fq - fi - (0.3 + 0.4 * hf.yz)) / (0.12 + 0.16 * hf.w);
      float a = (1.0 - smoothstep(0.6, 1.0, dd)) * 0.55;
      acc = vec4(vec3(0.20, 0.12, 0.06) * a, a * 0.7) + acc * (1.0 - a * 0.7);
    }
  }
  if (uWipe.w > -9.0) for (int i = 0; i < NW; i++) acc = wiperOver(acc, g, i, uWipe.w, panes[i], aa);
  o = acc;
}
)GLSL";
        return s;
    }

    // The drops on the glass: lens sprites, six vertices a drop with its values on each — the centre uv,
    // the two radii in uv (an ellipse, flatter higher up) and the alpha.
    const char* const vsGlassDrop = R"GLSL(#version 150
in vec4 aDrop; in float aDropA;
uniform vec2 uRes;
out vec2 vQ; out vec2 vC; out float vR; out float vA;
void main(){
  int cv = gl_VertexID - (gl_VertexID / 6) * 6;
  vec2 c = vec2((cv == 1 || cv == 2 || cv == 4) ? 1.0 : -1.0, (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0);
  vec2 rpx = aDrop.zw * uRes;
  float m = 1.0 + 1.5 / max(min(rpx.x, rpx.y), 1.0);
  vQ = c * m; vC = aDrop.xy; vR = min(rpx.x, rpx.y); vA = aDropA;
  gl_Position = vec4((aDrop.xy + c * aDrop.zw * m) * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

    const char* const fsGlassDrop = R"GLSL(#version 150
in vec2 vQ; in vec2 vC; in float vR; in float vA;
uniform float uHorizonV;
uniform vec3 uSkyLo, uSkyHi, uSeaFar, uSeaNear, uMistC;   // display-space colours of the scene
out vec4 o;
void main(){
  float d2 = dot(vQ, vQ);
  if (d2 > 1.0 + 4.0 / max(vR, 1.0)) discard;
  float d = sqrt(d2);
  float edge = clamp((1.0 - d) * vR + 0.5, 0.0, 1.0);
  // a lens: what lies behind and around, shrunk and upside-down
  float sv = vC.y - vQ.y * 0.12 * (0.8 + 0.35 * d2);       // where this point of the lens looks: flipped
  float e = sv - uHorizonV;
  vec3 c = e > 0.0 ? mix(uSkyLo, uSkyHi, smoothstep(0.0, 0.45, e)) : mix(uSeaFar, uSeaNear, smoothstep(0.0, 0.5, -e));
  c = mix(c, uMistC, exp(-abs(e) / 0.02) * 0.6);
  c *= mix(1.1, 0.22, smoothstep(0.55, 1.0, d));                     // the dark rim
  vec2 hl = vQ - vec2(-0.3, 0.36);
  c += vec3(1.0, 0.93, 0.84) * (exp(-dot(hl, hl) * 38.0) * 0.85);     // the bright highlight
  vec2 cs = vQ - vec2(0.22, -0.5);
  c += vec3(1.0, 0.84, 0.6) * (exp(-dot(cs, cs) * 22.0) * 0.14);      // a soft caustic opposite
  float a = edge * vA;
  o = vec4(c * a, a);
}
)GLSL";

    // Thin wet trails behind crawling drops, six vertices a segment.
    const char* const vsGlassTrail = R"GLSL(#version 150
in vec4 aSeg; in vec2 aW;
uniform float uAspect; uniform vec2 uRes;
out float vS; out float vA; out vec2 vN;
void main(){
  vec2 k = vec2(uAspect, 1.0);
  vec2 a = aSeg.xy * k, b = aSeg.zw * k;
  vec2 d = b - a; float L = length(d); vec2 dn = L > 1e-6 ? d / L : vec2(0.0, 1.0); vec2 nn = vec2(-dn.y, dn.x);
  float w = max(aW.x, 0.8 / uRes.y) + 1.0 / uRes.y;
  int cv = gl_VertexID - (gl_VertexID / 6) * 6;
  float along = (cv == 1 || cv == 2 || cv == 4) ? 1.0 : 0.0, side = (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0;
  vec2 q = mix(a - dn * w * 0.5, b + dn * w * 0.5, along) + nn * (side * w);
  gl_Position = vec4((q / k) * 2.0 - 1.0, 0.0, 1.0);
  vS = side; vA = aW.y * min(1.0, aW.x * uRes.y / 0.8); vN = nn / k;
}
)GLSL";

    const char* const fsGlassTrail = R"GLSL(#version 150
in float vS; in float vA; in vec2 vN;
out vec4 o;
void main(){
  float a = vA * (1.0 - vS * vS);
  o = vec4(vec3(0.075, 0.062, 0.05) * a * (0.6 + 0.4 * vS), a * 0.06);   // a faint sheen, darker on one side
}
)GLSL";
}

//==============================================================================
int TitanGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool TitanGl::compile (Program& p, const juce::String& vs, const juce::String& fs, const char* what)
{
    auto s = std::make_unique<juce::OpenGLShaderProgram> (*juce::OpenGLContext::getCurrentContext());
    if (! s->addVertexShader (vs) || ! s->addFragmentShader (fs) || ! s->link())
    {
        DBG ("titan: the " << what << " shader did not build — " << s->getLastError());
        juce::ignoreUnused (what);
        return false;
    }
    p.id = s->getProgramID();
    p.program = std::move (s);
    return true;
}

bool TitanGl::create()
{
    if (juce::OpenGLContext::getCurrentContext() == nullptr)
        return false;
    world.build();

    if (! compile (profile, vsFull, fsProfile(), "profile")
        || ! compile (scene, vsFull, fsScene (world.rippleRms()), "scene")
        || ! compile (buoys, vsBuoy, fsBuoy(), "buoy")
        || ! compile (spray, vsSpray(), fsCapsule(), "spray")
        || ! compile (drizzle, vsDrizzle(), fsCapsule(), "drizzle")
        || ! compile (glass, vsFull, fsGlass(), "glass")
        || ! compile (glassDrops, vsGlassDrop, fsGlassDrop, "glass drop")
        || ! compile (glassTrails, vsGlassTrail, fsGlassTrail, "glass trail"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeBuffers();

    // The frame timer: a timer query where the driver has one. Asked once; an error means no.
    while (glGetError() != GL_NO_ERROR) {}
    if (glGenQueries != nullptr && glGetQueryObjectui64v != nullptr)
    {
        glGenQueries ((GLsizei) query.size(), query.data());
        glBeginQuery (GL_TIME_ELAPSED, query[0]);
        glEndQuery (GL_TIME_ELAPSED);
        queriesWork = glGetError() == GL_NO_ERROR;
        if (queriesWork)
        {
            GLuint64 ns = 0;
            glGetQueryObjectui64v (query[0], GL_QUERY_RESULT, &ns);   // drains the probe
        }
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
    const auto del = [] (juce::uint32& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (vboDrops, glDeleteBuffers); del (vboTrails, glDeleteBuffers);
    del (vaoEmpty, glDeleteVertexArrays); del (vaoDrops, glDeleteVertexArrays); del (vaoTrails, glDeleteVertexArrays);
    del (texNoise, glDeleteTextures); del (texRipple, glDeleteTextures); del (texProfile, glDeleteTextures); del (fboProfile, glDeleteFramebuffers);
    del (fboColour, glDeleteTextures); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &profile, &scene, &buoys, &spray, &drizzle, &glass, &glassDrops, &glassTrails }) { p->program.reset(); p->id = 0; }
}

void TitanGl::makeTextures()
{
    const auto rgba8 = [] (juce::uint32& id, int size, const std::vector<juce::uint8>& px)
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
    glGenVertexArrays (1, &vaoEmpty);   // the full-screen passes and the particles draw with no attributes; core needs a VAO bound all the same

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

void TitanGl::render (int w, int h, float scale, juce::uint32 target, int x, int y)
{
    if (scene.id == 0 || w <= 0 || h <= 0)
        return;
    const int rw = std::max (2, juce::roundToInt ((float) w * scale)), rh = std::max (2, juce::roundToInt ((float) h * scale));
    ensureFrame (rw, rh);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);

    // The timer: queries, read back a few frames late so nothing waits — or, without them, one frame
    // in sixty finished and clocked.
    const bool probe = ! queriesWork && (frameCounter % 60) == 0;
    ++frameCounter;
    if (queriesWork)
    {
        while (queriesPending > 0)
        {
            const auto oldest = query[(size_t) ((queryNext - queriesPending + (int) query.size()) % (int) query.size())];
            GLuint available = 0;
            glGetQueryObjectuiv (oldest, GL_QUERY_RESULT_AVAILABLE, &available);
            if (! available) break;
            GLuint64 ns = 0;
            glGetQueryObjectui64v (oldest, GL_QUERY_RESULT, &ns);
            noteCost ((float) ns * 1.0e-6f);
            --queriesPending;
        }
        if (queriesPending < (int) query.size())
        {
            glBeginQuery (GL_TIME_ELAPSED, query[(size_t) queryNext]);
            queryNext = (queryNext + 1) % (int) query.size();
            ++queriesPending;
        }
    }
    else if (probe)
    {
        glFinish();
        probeStart = juce::Time::getMillisecondCounterHiRes();
    }

    for (int i = 0; i < repeats; ++i)
        drawScene (rw, rh);

    if (queriesWork)
        glEndQuery (GL_TIME_ELAPSED);
    else if (probe)
    {
        glFinish();
        noteCost ((float) (juce::Time::getMillisecondCounterHiRes() - probeStart));
    }

    glBindFramebuffer (GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer (GL_DRAW_FRAMEBUFFER, target);
    glBlitFramebuffer (0, 0, rw, rh, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer (GL_FRAMEBUFFER, target);
}

bool TitanGl::readFrame (std::vector<juce::uint8>& rgba, int& w, int& h)
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
        glUniform2f (u ("uLamp"), (float) world.light (0), (float) world.light (1));
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

} // namespace orbitamp::titan
