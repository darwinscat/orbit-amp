// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IceGl.h"

#include <cmath>

namespace orbitamp::frostpaw
{

using namespace juce::gl;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double cell0 = 1.0;             // metres, the finest level
    constexpr double fwdShift = 0.4;          // the levels pushed ahead of the rover, of their half-extent
    constexpr double vfovHalfTan = 0.23086819;  // tan 13 deg: vfov 26 deg, hfov follows from the aspect
    constexpr float shear = 0.2f;             // the horizon at NDC y = +0.2: 40 % from the top
    constexpr double jupiterRadiusDegrees = 9.0;   // angular radius: 18 degrees across
    constexpr double ioRadiusDegrees = 0.32;
    constexpr double skyClock = 2 * pi * 2000;     // seconds: Jupiter's turn (0.0045 rad/s) and Io's lap (0.011) both whole in it
    constexpr double snowClock = 36.0 * 2000;      // metres: the flakes' 36 m depth cycle, whole in it

    // Shared by the terrain's two shaders and the steel's vertex shader: the height function. HTEX is
    // textureLod (.., 0) in a vertex shader and texture() in a fragment shader.
    const char* const glslCommon = R"GLSL(
uniform sampler2D uHeight;
uniform vec2 uCamCell;      // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;      // the camera's xz minus uCamCell
uniform vec2 uTexOff0, uTexOff1, uTexOff2, uTexOff3;  // fract (uCamCell / L_i) (+ the fixed layer offsets), from doubles
uniform vec4 uLinOff;       // (sOffA, tOffA, sOffB, tOffB): the lineae coordinates of uCamCell, wrapped, metres
uniform ivec2 uLinBase;     // the line index of uCamCell per family
uniform vec2 uChaosOff; uniform ivec2 uChaosBase;   // fract / floor of uCamCell / the chaos cell
uniform vec2 uStructOff; uniform ivec2 uStructBase; // fract / floor of uCamCell / the structure cell
const float L0 = 7000.0, L1 = 1250.0, L2 = 190.0, L3 = 50.0;
const float CHB = 14.0; const int CHM = 47500;      // the chaos cell, WORLD / CHB
const float SC = 70.0; const int SM = 9500;         // the structure cell, WORLD / SC
const float PROB_IN = 0.3, PROB_OUT = 0.035;
const float CURV_R = 1560000.0;                     // the moon's radius: the ground falls away as dist^2 / 2R
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }

// The lineae: two families of long, gently bent double ridges, each line with its own height and width,
// fading in and out along its length. sv = the signed coordinate across the lines (m), tv = along (m).
struct Linea { float h; float ds; float w; float sg; float amp; float dAmp; float H; float x; };   // for H and its gradient
Linea lineaFam (vec2 pr, float warp, vec2 d, vec2 pp, float P, float Lw, float sOff, float tOff, int base, uint seed) {
  Linea o;
  float sv = sOff + dot (pr, d) + warp;
  float fi = floor (sv / P); int i = base + int (fi);
  o.ds = sv - (fi + 0.5) * P;
  float r1 = hcell (ivec2 (i, 7), seed), r2 = hcell (ivec2 (i, 8), seed), r3 = hcell (ivec2 (i, 9), seed);
  o.H = 3.0 + 8.0 * r1 * r1; o.w = 9.0 + 9.0 * r2; o.sg = 0.55 * o.w;
  float tv = tOff + dot (pr, pp);
  float ph = tv / Lw * 6.2831853 + r3 * 6.2831853;
  o.x = 0.5 + 0.5 * sin (ph);
  o.amp = smoothstep (0.15, 0.55, o.x);
  o.dAmp = 0.5 * cos (ph) * (6.2831853 / Lw);     // d(x)/d(tv); times dss (0.15, 0.55, x) outside
  float e1 = exp (-sq ((o.ds - o.w) / o.sg)), e2 = exp (-sq ((o.ds + o.w) / o.sg)), e3 = exp (-sq (o.ds / (0.7 * o.sg)));
  o.h = o.amp * o.H * (e1 + e2 - 0.45 * e3);
  return o;
}
float lineae (vec2 pr, float bb) {
  float warp = (bb - 0.5) * 160.0;
  Linea A = lineaFam (pr, warp, vec2 (0.904752, 0.425939), vec2 (-0.425939, 0.904752), 1100.0, 1600.0, uLinOff.x, uLinOff.y, uLinBase.x, 101u);
  Linea B = lineaFam (pr, warp, vec2 (-0.370181, 0.928960), vec2 (-0.928960, -0.370181), 1700.0, 2300.0, uLinOff.z, uLinOff.w, uLinBase.y, 202u);
  return A.h + B.h;
}
// The chaos terrain: tilted ice blocks on a hashed 14 m grid, inside patches of the 1250 m layer.
float chaosMaskOf (vec4 b) { return smoothstep (0.66, 0.74, b.a); }
float chaos (vec2 pr, float mask, vec4 c) {
  if (mask < 0.005) return 0.0;
  vec2 q = uChaosOff + pr / CHB; vec2 fc = floor (q); vec2 f = q - fc;
  ivec2 cc = (uChaosBase + ivec2 (fc) + CHM * 64) % CHM;
  float ex = step (hcell (cc, 31u), 0.78);
  float hgt = 0.6 + 3.6 * sq (hcell (cc, 32u));
  vec2 tilt = (vec2 (hcell (cc, 33u), hcell (cc, 34u)) - 0.5) * 0.45;
  float m = 0.08 + 0.10 * hcell (cc, 35u);
  float mx = min (f.x, 1.0 - f.x), my = min (f.y, 1.0 - f.y);
  float pl = smoothstep (m, m + 0.16, mx) * smoothstep (m, m + 0.16, my);
  float top = hgt + dot (tilt, (f - 0.5) * CHB);
  return mask * (ex * pl * top + (c.g - 0.5) * 1.2 - 0.8);
}
// The steel: one structure per hashed 70 m cell, in groups (a 280 m super-cell decides dense or sparse).
bool structCell (ivec2 cij, out vec2 footCell, out float rad, out int variant, out float scl, out float ang) {
  ivec2 c = (cij + SM * 64) % SM;
  ivec2 g = (cij + SM * 64) / 4;
  float prob = (hcell (g, 47u) < 0.3) ? PROB_IN : PROB_OUT;
  float r0 = hcell (c, 41u);
  footCell = vec2 (0.0); rad = 0.0; variant = 0; scl = 1.0; ang = 0.0;
  if (r0 > prob) return false;
  variant = int (hcell (c, 42u) * 4.0);
  footCell = 0.22 + 0.56 * vec2 (hcell (c, 43u), hcell (c, 44u));
  scl = 0.8 + 0.5 * hcell (c, 45u); ang = 6.2831853 * hcell (c, 46u);
  rad = (variant == 0 ? 2.4 : variant == 1 ? 6.0 : variant == 2 ? 2.8 : 3.4) * scl;
  return true;
}
// The snow drift at a structure's foot (height) and its contact shadow (0..1), from the same lookup.
vec2 structFoot (vec2 pr) {
  vec2 q = uStructOff + pr / SC; vec2 fc = floor (q);
  vec2 fcell; float rad; int v; float scl, ang;
  if (! structCell (uStructBase + ivec2 (fc), fcell, rad, v, scl, ang)) return vec2 (0.0);
  vec2 footRel = (fc + fcell - uStructOff) * SC;
  float r2 = dot (pr - footRel, pr - footRel);
  float drift = 0.55 * scl * exp (-r2 / (2.0 * sq (rad + 1.5)));
  float shadow = 0.45 * exp (-r2 / (2.0 * sq (rad * 0.75)));
  return vec2 (drift, shadow);
}
float terrainH (vec2 pr, out vec4 a, out vec4 b, out vec4 c) {   // pr = world xz relative to uCamCell, metres
  a = HTEX (uTexOff0 + pr / L0);
  b = HTEX (uTexOff1 + pr / L1);
  c = HTEX (uTexOff2 + pr / L2);
  vec2 rel = pr - uCamFrac; float d2 = dot (rel, rel); float dist = sqrt (d2);
  float swell = (a.a - 0.5) * 70.0;
  float hills = (b.r - 0.5) * 30.0 * (0.3 + 0.7 * a.g);
  float ridges = (a.r - 0.5) * 24.0 * smoothstep (0.55, 0.8, a.b);      // broad snowy uplands here and there
  float fade = 1.0 - smoothstep (600.0, 2000.0, dist);
  float dune = ((c.r - 0.5) * 1.6 + (c.b - 0.5) * 0.6) * fade;
  float fade3 = 1.0 - smoothstep (250.0, 600.0, dist);
  if (fade3 > 0.0) { vec4 d = HTEX (uTexOff3 + pr / L3); dune += ((d.r - 0.5) * 1.1 + (d.b - 0.5) * 0.9 + (d.g - 0.5) * 0.4) * fade3; }
  float cm = chaosMaskOf (b) * (1.0 - smoothstep (700.0, 900.0, dist));
  float h = swell + hills + ridges + dune + lineae (pr, b.b) - d2 / (2.0 * CURV_R);
  if (dist < 900.0) h += chaos (pr, cm, c);
  if (dist < 400.0) h += structFoot (pr).x * (1.0 - cm) * (1.0 - smoothstep (300.0, 400.0, dist));
  return h;
}
float terrainH (vec2 pr) { vec4 a, b, c; return terrainH (pr, a, b, c); }
)GLSL";

    // The analytic world-space gradient of terrainH from the gradient textures: the per-pixel normal.
    // uGrad1 = (dR/du, dR/dv, dB/du, dB/dv) and uGrad2 = (dG/du, dG/dv, dA/du, dA/dv) per uv unit, as
    // 0.5 + 0.5 * g / gmax in RGBA8; uGS1 = (gmaxR, gmaxB), uGS2 = (gmaxG, gmaxA).
    const char* const glslGrad = R"GLSL(
uniform sampler2D uGrad1, uGrad2; uniform vec2 uGS1, uGS2;
float dss (float e0, float e1, float x) { float t = clamp ((x - e0) / (e1 - e0), 0.0, 1.0); return 6.0 * t * (1.0 - t) / (e1 - e0); }
vec2 lineaFamGrad (vec2 pr, float warp, vec2 gwarp, vec2 d, vec2 pp, float P, float Lw, float sOff, float tOff, int base, uint seed, out float tint) {
  Linea o = lineaFam (pr, warp, d, pp, P, Lw, sOff, tOff, base, seed);
  vec2 dsdp = d + gwarp;
  float e1 = exp (-sq ((o.ds - o.w) / o.sg)), e2 = exp (-sq ((o.ds + o.w) / o.sg)), e3 = exp (-sq (o.ds / (0.7 * o.sg)));
  float de = e1 * (-2.0 * (o.ds - o.w) / sq (o.sg)) + e2 * (-2.0 * (o.ds + o.w) / sq (o.sg)) - 0.45 * e3 * (-2.0 * o.ds / sq (0.7 * o.sg));
  tint = o.amp * (0.45 * (e1 + e2) + e3);
  vec2 dAmp = dss (0.15, 0.55, o.x) * o.dAmp * pp;
  return o.H * (dAmp * (e1 + e2 - 0.45 * e3) + o.amp * de * dsdp);
}
vec2 lineaeGrad (vec2 pr, float bb, vec2 gBb, out float tint) {
  float warp = (bb - 0.5) * 160.0; vec2 gwarp = 160.0 * gBb; float t1, t2;
  vec2 g = lineaFamGrad (pr, warp, gwarp, vec2 (0.904752, 0.425939), vec2 (-0.425939, 0.904752), 1100.0, 1600.0, uLinOff.x, uLinOff.y, uLinBase.x, 101u, t1)
         + lineaFamGrad (pr, warp, gwarp, vec2 (-0.370181, 0.928960), vec2 (-0.928960, -0.370181), 1700.0, 2300.0, uLinOff.z, uLinOff.w, uLinBase.y, 202u, t2);
  tint = max (t1, t2);
  return g;
}
vec2 chaosGrad (vec2 pr, float mask, vec2 dmask, vec4 c, vec2 gCg, out float blockEdge, out float blockTop) {
  blockEdge = 0.0; blockTop = 0.0;
  if (mask < 0.005) return vec2 (0.0);
  vec2 q = uChaosOff + pr / CHB; vec2 fc = floor (q); vec2 f = q - fc;
  ivec2 cc = (uChaosBase + ivec2 (fc) + CHM * 64) % CHM;
  float ex = step (hcell (cc, 31u), 0.78);
  float hgt = 0.6 + 3.6 * sq (hcell (cc, 32u));
  vec2 tilt = (vec2 (hcell (cc, 33u), hcell (cc, 34u)) - 0.5) * 0.45;
  float m = 0.08 + 0.10 * hcell (cc, 35u);
  float mx = min (f.x, 1.0 - f.x), my = min (f.y, 1.0 - f.y);
  float sx = smoothstep (m, m + 0.16, mx), sy = smoothstep (m, m + 0.16, my);
  vec2 dpl = vec2 (dss (m, m + 0.16, mx) * (f.x < 0.5 ? 1.0 : -1.0) * sy, dss (m, m + 0.16, my) * (f.y < 0.5 ? 1.0 : -1.0) * sx) / CHB;
  float pl = sx * sy;
  float top = hgt + dot (tilt, (f - 0.5) * CHB);
  float h = ex * pl * top + (c.g - 0.5) * 1.2 - 0.8;
  blockEdge = mask * ex * (1.0 - pl) * step (0.02, pl);
  blockTop = mask * ex * pl;
  return mask * (ex * (dpl * top + pl * tilt) + 1.2 * gCg) + dmask * h;
}
vec2 structFootGrad (vec2 pr, out float shadow) {
  vec2 q = uStructOff + pr / SC; vec2 fc = floor (q);
  vec2 fcell; float rad; int v; float scl, ang; shadow = 0.0;
  if (! structCell (uStructBase + ivec2 (fc), fcell, rad, v, scl, ang)) return vec2 (0.0);
  vec2 footRel = (fc + fcell - uStructOff) * SC; vec2 dv = pr - footRel;
  float r2 = dot (dv, dv); float s2 = sq (rad + 1.5);
  float drift = 0.55 * scl * exp (-r2 / (2.0 * s2));
  shadow = 0.45 * exp (-r2 / (2.0 * sq (rad * 0.75)));
  return -drift * dv / s2;
}
vec2 terrainGrad (vec2 pr, vec4 a, vec4 b, vec4 c, out float lineaTint, out float chaosM, out float blockEdge, out float blockTop, out float shadow) {
  vec2 uv0 = uTexOff0 + pr / L0, uv1 = uTexOff1 + pr / L1, uv2 = uTexOff2 + pr / L2;
  vec4 s1 = vec4 (uGS1.x, uGS1.x, uGS1.y, uGS1.y), s2 = vec4 (uGS2.x, uGS2.x, uGS2.y, uGS2.y);
  vec4 g1a = (texture (uGrad1, uv0) * 2.0 - 1.0) * s1 / L0, g2a = (texture (uGrad2, uv0) * 2.0 - 1.0) * s2 / L0;
  vec4 g1b = (texture (uGrad1, uv1) * 2.0 - 1.0) * s1 / L1, g2b = (texture (uGrad2, uv1) * 2.0 - 1.0) * s2 / L1;
  vec4 g1c = (texture (uGrad1, uv2) * 2.0 - 1.0) * s1 / L2, g2c = (texture (uGrad2, uv2) * 2.0 - 1.0) * s2 / L2;
  vec2 gAr = g1a.xy, gAb = g1a.zw, gAg = g2a.xy, gAa = g2a.zw;
  vec2 gBr = g1b.xy, gBb = g1b.zw, gBa = g2b.zw;
  vec2 gCr = g1c.xy, gCb = g1c.zw, gCg = g2c.xy;
  vec2 rel = pr - uCamFrac; float dist = length (rel);
  vec2 g = 70.0 * gAa;
  g += 30.0 * (gBr * (0.3 + 0.7 * a.g) + (b.r - 0.5) * 0.7 * gAg);
  float up = smoothstep (0.55, 0.8, a.b);
  g += 24.0 * (gAr * up + (a.r - 0.5) * dss (0.55, 0.8, a.b) * gAb);
  float fade = 1.0 - smoothstep (600.0, 2000.0, dist);
  g += fade * (1.6 * gCr + 0.6 * gCb);
  float fade3 = 1.0 - smoothstep (250.0, 600.0, dist);
  if (fade3 > 0.0) { vec2 uv3 = uTexOff3 + pr / L3;
    vec4 g1d = (texture (uGrad1, uv3) * 2.0 - 1.0) * s1 / L3, g2d = (texture (uGrad2, uv3) * 2.0 - 1.0) * s2 / L3;
    g += fade3 * (1.1 * g1d.xy + 0.9 * g1d.zw + 0.4 * g2d.xy); }
  lineaTint = 0.0; blockEdge = 0.0; blockTop = 0.0; shadow = 0.0;
  float cfade = 1.0 - smoothstep (700.0, 900.0, dist);
  chaosM = chaosMaskOf (b) * cfade;
  g += lineaeGrad (pr, b.b, gBb, lineaTint);
  if (dist < 900.0) {
    vec2 dcm = dss (0.66, 0.74, b.a) * gBa * cfade - chaosMaskOf (b) * dss (700.0, 900.0, dist) * rel / max (dist, 1.0);
    g += chaosGrad (pr, chaosM, dcm, c, gCg, blockEdge, blockTop);
    if (dist < 400.0) { vec2 gs = structFootGrad (pr, shadow); float sf = 1.0 - smoothstep (300.0, 400.0, dist);
      g += gs * (1.0 - chaosM) * sf; shadow *= (1.0 - chaosM) * sf; } }
  g -= rel / CURV_R;
  return g;
}
)GLSL";

    const char* const glslLight = R"GLSL(
uniform vec3 uSun, uJup, uHorizon, uMist;
uniform float uHazeK, uSnow, uSunInt;
vec3 applyHaze (vec3 col, float dist, vec3 V) {
  float f = 1.0 - exp (-dist * uHazeK);
  f = max (f, smoothstep (6500.0, 8000.0, dist));
  return mix (col, uHorizon, f);
}
// Snow and ice: the sun (warm white, wrapped a little), the blue skylight off the ice itself, Jupiter's shine.
vec3 shadeSnow (vec3 alb, vec3 n) {
  float ndl = dot (n, uSun);
  float diff = max (ndl, 0.0) * 0.9 + 0.12 * smoothstep (-0.35, 0.35, ndl);
  vec3 sunC = vec3 (1.0, 0.96, 0.90) * uSunInt;
  vec3 amb = vec3 (0.26, 0.42, 0.74) * (0.32 + 0.12 * uSnow) * (0.55 + 0.45 * n.y);
  vec3 jup = vec3 (0.60, 0.48, 0.38) * 0.18 * max (dot (n, uJup), 0.0);
  return alb * (amb + jup + sunC * diff);
}
)GLSL";

    const char* const vsTerrainBody = R"GLSL(
in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize, uMorphOn;
uniform mat4 uViewProj;
out vec3 vPos; out vec4 vA, vB, vC;
void main() {
  vec2 pr = uLvlOrigin + aGrid * uCellSize;
  float h = terrainH (pr, vA, vB, vC);
  float b = max (abs (aGrid.x - 64.0), abs (aGrid.y - 64.0));
  float alpha = clamp ((b - 48.0) / 15.0, 0.0, 1.0) * uMorphOn;
  if (alpha > 0.0) {
    vec2 d = mod (aGrid, 2.0) * uCellSize;
    float hc = 0.5 * (terrainH (pr - d) + terrainH (pr + d));   // the height on the coarser lattice
    h = mix (h, hc, alpha);
  }
  vPos = vec3 (pr.x, h, pr.y);
  gl_Position = uViewProj * vec4 (vPos, 1.0);
}
)GLSL";

    const char* const fsTerrainBody = R"GLSL(
uniform sampler2D uDetail;
uniform vec3 uCamRel;
uniform vec2 uDetOff0, uDetOff1, uDetOff2;
in vec3 vPos; in vec4 vA, vB, vC;
out vec4 fragColor;
void main() {
  vec3 rel = vPos - uCamRel; float dist = length (rel); vec3 V = rel / dist;
  float lineaTint, chaosM, blockEdge, blockTop, shadow;
  vec2 g = terrainGrad (vPos.xz, vA, vB, vC, lineaTint, chaosM, blockEdge, blockTop, shadow);
  vec3 n = normalize (vec3 (-g.x, 1.0, -g.y));
  vec3 d1 = texture (uDetail, uDetOff0 + vPos.xz / 3.1).rgb * 2.0 - 1.0;     // wind ripples, sastrugi
  vec3 d2 = texture (uDetail, uDetOff1 + vPos.xz / 0.45).rgb * 2.0 - 1.0;    // crust grain
  float df1 = exp (-dist / 160.0), df2 = exp (-dist / 35.0);
  vec3 nd = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.55 * df1 + vec3 (d2.x, 0.0, d2.y) * 0.16 * df2);
  float slope = 1.0 - nd.y;
  // The albedo: snow, blue ice on the steep faces and the block edges, the reddish-brown salts of the lineae.
  vec3 snow = vec3 (0.93, 0.95, 0.99);
  vec3 ice = vec3 (0.72, 0.84, 0.97);
  vec3 salt = vec3 (0.58, 0.40, 0.29);
  vec3 alb = mix (snow, ice, smoothstep (0.08, 0.35, slope));
  alb = mix (alb, ice, blockEdge * 0.8);
  float saltM = lineaTint * (0.55 + 0.45 * vB.g) * smoothstep (0.1, 0.5, lineaTint);
  alb = mix (alb, salt, clamp (saltM, 0.0, 0.7));
  alb = mix (alb, vec3 (0.80, 0.86, 0.95), 0.5 * chaosM * (1.0 - blockTop));   // the rubble between the blocks, greyer
  alb *= 1.0 + 0.07 * d1.z * df1 + 0.06 * d2.z * df2;
  alb *= 0.96 + 0.08 * vB.g;
  vec3 col = shadeSnow (alb, nd);
  // The sparkle: a micro-facet from the fine crust grain, hashed per 12 cm of world so it twinkles as the rover moves.
  if (dist < 160.0) {
    vec3 d3 = texture (uDetail, uDetOff2 + vPos.xz / 0.13).rgb * 2.0 - 1.0;
    vec3 nm = normalize (nd + vec3 (d3.x, 0.0, d3.y) * 1.6);
    vec3 H = normalize (uSun - V);
    float sp = pow (max (dot (nm, H), 0.0), 240.0);
    ivec2 gc = ivec2 (floor ((vPos.xz + uCamCell) * 8.0));
    float gate = step (0.955, hcell (gc, 71u));
    col += vec3 (1.0, 0.98, 0.95) * sp * gate * 2.2 * exp (-dist / 70.0) * max (dot (nd, uSun), 0.0) * uSunInt;
  }
  col = mix (col, col * vec3 (0.50, 0.62, 0.85), shadow);
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
)GLSL";

    // The steel: instanced bar meshes, one draw per variant; aCell is the instance's unwrapped 70 m cell.
    const char* const vsSteelBody = R"GLSL(
in vec3 aPos; in vec3 aNrm; in vec3 aExp; in float aLod; in vec2 aCell;
uniform mat4 uViewProj;
uniform int uVariant;
uniform vec3 uCamRel; uniform float uPxExpand;   // metres of bar width per metre of distance that one pixel covers
out vec3 vN; out vec3 vRel; out vec3 vWorld; out float vSeed;
void main() {
  ivec2 cij = ivec2 (aCell);
  vec2 fcell; float rad; int variant; float scl, ang;
  bool ok = structCell (cij, fcell, rad, variant, scl, ang);
  vec2 pr = (vec2 (cij) + fcell) * SC - uCamCell;
  if (! ok || variant != uVariant) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vWorld = vec3 (0); vSeed = 0.0; return; }
  vec4 b = HTEX (uTexOff1 + pr / L1);
  if (chaosMaskOf (b) > 0.02) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vWorld = vec3 (0); vSeed = 0.0; return; }
  float gh = terrainH (pr);
  float dist = length (vec3 (pr.x, gh, pr.y) - uCamRel);
  float ca = cos (ang), sa = sin (ang);
  vec3 lp = aPos * scl;
  // Thin bars kept at least ~1.3 px wide: expanded across the bar with distance; the detail bars vanish far away.
  float extra = max (0.0, uPxExpand * dist * 1.3 - 0.22 * scl) * 0.5;
  lp += aExp * extra;
  if (aLod > 0.5 && dist > 900.0) lp = vec3 (0.0, 0.0, 0.0);
  vec3 ln = aNrm;
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.15, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vWorld = vec3 (wp.x + uCamCell.x, wp.y, wp.z + uCamCell.y); vSeed = hcell (cij, 48u);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
)GLSL";

    const char* const fsSteelBody = R"GLSL(
uniform sampler2D uDetail; uniform float uJupCos;
in vec3 vN; in vec3 vRel; in vec3 vWorld; in float vSeed;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 R = reflect (V, n);
  // The environment a brushed surface sees: the black sky, Jupiter as a soft warm blob, the bright ice below, the mist band.
  float jb = smoothstep (uJupCos - 0.12, uJupCos + 0.02, dot (R, uJup));
  vec3 skyE = vec3 (0.01, 0.015, 0.03) + vec3 (0.78, 0.64, 0.50) * 0.75 * jb;
  vec3 iceE = vec3 (0.82, 0.88, 0.97) * (0.7 + 0.3 * max (-R.y, 0.0));
  vec3 env = mix (iceE, skyE, smoothstep (-0.04, 0.06, R.y));
  env = mix (env, uMist, 0.5 * exp (-abs (R.y) / 0.05));
  float cosT = max (dot (n, -V), 0.0);
  float fres = 0.56 + 0.44 * pow (1.0 - cosT, 5.0);
  vec3 H = normalize (uSun - V);
  float ndh = max (dot (n, H), 0.0);
  vec3 sunC = vec3 (1.0, 0.96, 0.90) * uSunInt;
  vec3 col = env * fres * 0.85
           + sunC * (pow (ndh, 110.0) * 1.5 + pow (ndh, 10.0) * 0.12) * step (0.0, dot (n, uSun))
           + vec3 (0.30, 0.44, 0.70) * 0.10 * (0.5 + 0.5 * n.y) + sunC * 0.10 * max (dot (n, uSun), 0.0) * vec3 (0.5, 0.5, 0.52);
  // The brush grain: a faint streaky modulation.
  float grain = texture (uDetail, vWorld.xz * 0.9 + vWorld.y * vec2 (0.0, 2.7)).b;
  col *= 0.9 + 0.2 * grain;
  // Frost on the upper surfaces, patchy.
  float fr = smoothstep (0.35, 0.8, n.y) * smoothstep (0.25, 0.65, texture (uDetail, vWorld.xz * 0.35 + vSeed).b + 0.25 * n.y);
  vec3 snowC = shadeSnow (vec3 (0.93, 0.95, 0.99), n);
  col = mix (col, snowC, fr);
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
)GLSL";

    // The sky: stars, Jupiter, Io, the sun, a thin mist band at the horizon.
    const char* const vsSky = R"GLSL(#version 150
out vec2 vNdc;
void main() {
  vec2 p = vec2 ((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vNdc = p; gl_Position = vec4 (p, 1.0, 1.0);
}
)GLSL";

    const char* const fsSkyBody = R"GLSL(
uniform sampler2D uDetail;
uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uTime, uPxAng;
uniform vec3 uJupR, uJupU, uJupAxis, uIo; uniform float uJupSin, uJupCos, uIoSin;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }
in vec2 vNdc;
out vec4 fragColor;
vec3 jupiterColor (float lat, float lon) {
  // Bands by latitude, warped by a little noise along the longitude; the Great Red Spot off the equator.
  float nz = texture (uDetail, vec2 (lon * 0.30, lat * 0.9)).b - 0.5;
  float nz2 = texture (uDetail, vec2 (lon * 0.9 + 0.3, lat * 2.6)).b - 0.5;
  float la = lat + 0.045 * nz + 0.012 * nz2;
  float band = 0.5 + 0.5 * sin (la * 14.0 + 0.6);
  float band2 = 0.5 + 0.5 * sin (la * 31.0 + 1.7);
  vec3 zone = vec3 (0.95, 0.90, 0.80), belt = vec3 (0.56, 0.36, 0.24);
  float bm = smoothstep (0.2, 0.8, band) * 0.7 + 0.3 * band2;
  bm = mix (bm, 0.85, smoothstep (0.08, 0.0, abs (la)));                   // the bright equatorial zone
  vec3 c = mix (belt, zone, bm);
  c = mix (c, vec3 (0.74, 0.62, 0.50), 0.5 * smoothstep (0.35, 0.45, abs (la + 0.12)) * (1.0 - smoothstep (0.45, 0.6, abs (la + 0.12))));   // a dark belt pair
  c = mix (c, vec3 (0.78, 0.70, 0.58), smoothstep (0.9, 1.3, abs (la)));     // paler, bluer poles
  c = mix (c, vec3 (0.62, 0.60, 0.68), smoothstep (1.15, 1.45, abs (la)));
  c *= 0.94 + 0.12 * nz2;
  float e = sq ((lon - 0.35) / 0.21) + sq ((la - 0.36) / 0.085);
  float spot = smoothstep (1.0, 0.55, e);
  c = mix (c, vec3 (0.80, 0.48, 0.36), spot * 0.85);
  c = mix (c, vec3 (0.94, 0.84, 0.70), smoothstep (1.5, 1.0, e) * (1.0 - spot) * 0.6);   // the pale collar
  return c;
}
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  vec3 col = vec3 (0.0);
  // The stars: hashed cells in (azimuth, elevation); steady, no twinkle (no air).
  vec2 ae = vec2 (atan (dir.x, -dir.z) * 70.0, asin (clamp (dir.y, -1.0, 1.0)) * 70.0);
  vec2 cf = floor (ae); vec2 f = ae - cf; ivec2 ci = ivec2 (cf);
  float r0 = hcell (ci, 3u);
  if (r0 < 0.16) {
    vec2 sc = vec2 (hcell (ci, 4u), hcell (ci, 5u));
    float dpx = length (f - sc) / 70.0 / uPxAng;       // the distance in pixels
    float mag = hcell (ci, 6u); float br = 0.25 + 1.3 * mag * mag * mag;
    float st = exp (-dpx * dpx * 0.9) * br;
    vec3 sc3 = mix (vec3 (0.75, 0.85, 1.0), vec3 (1.0, 0.92, 0.80), hcell (ci, 7u));
    col += sc3 * st * smoothstep (-0.01, 0.03, dir.y);
  }
  // The sun: a small bright disc with a touch of glare.
  float s = dot (dir, uSun);
  col += vec3 (1.0, 0.97, 0.92) * uSunInt * (smoothstep (0.999970, 0.999985, s) * 2.0 + 0.5 * pow (max (s, 0.0), 1200.0) + 0.05 * pow (max (s, 0.0), 40.0));
  // Jupiter: a shaded sphere in a fixed world direction.
  float cj = dot (dir, uJup);
  if (cj > uJupCos - 0.002) {
    float u = dot (dir, uJupR) / uJupSin, v = dot (dir, uJupU) / uJupSin;
    float rr = u * u + v * v;
    float edge = 1.0 - smoothstep (1.0 - 2.5 * uPxAng / uJupSin, 1.0, rr);
    if (edge > 0.0) {
      float z = sqrt (max (1.0 - rr, 0.0));
      vec3 n = u * uJupR + v * uJupU - z * uJup;      // the surface normal, toward us at the centre
      float lat = asin (clamp (dot (n, uJupAxis), -1.0, 1.0));
      vec3 ax = normalize (cross (uJupAxis, uJup)); vec3 az = cross (uJupAxis, ax);
      float lon = atan (dot (n, ax), dot (n, az)) + uTime * 0.0045;
      vec3 jc = jupiterColor (lat, lon);
      float l = dot (n, uSun);
      float lit = smoothstep (-0.06, 0.34, l) * (0.9 + 0.1 * max (l, 0.0));
      float limb = 0.55 + 0.45 * z;
      vec3 jcol = jc * (lit * 1.25 * limb + 0.045);
      col = mix (col, jcol, edge);
    }
  }
  // Io: a small yellowish disc, lit by the same sun.
  float cio = dot (dir, uIo);
  if (cio > 0.9995) {
    vec3 ir = normalize (cross (uIo, vec3 (0.0, 1.0, 0.0))); vec3 iu = cross (ir, uIo);
    float u = dot (dir, ir) / uIoSin, v = dot (dir, iu) / uIoSin; float rr = u * u + v * v;
    float edge = 1.0 - smoothstep (1.0 - 3.0 * uPxAng / uIoSin, 1.0 + 1.0 * uPxAng / uIoSin, rr);
    float z = sqrt (max (1.0 - rr, 0.0)); vec3 n = u * ir + v * iu - z * uIo;
    float lit = smoothstep (-0.05, 0.3, dot (n, uSun));
    col = mix (col, vec3 (0.88, 0.80, 0.52) * (lit * 1.0 + 0.02), edge);
  }
  // The ground mist at the horizon: thin, cold, more of it when it snows; below the horizon the haze's colour.
  float mist = exp (-max (dir.y, 0.0) / 0.024) * (0.6 + 0.4 * uSnow);
  col = mix (col, uMist, mist * smoothstep (-0.02, 0.0, dir.y));
  col = mix (col, uHorizon, smoothstep (0.004, -0.01, dir.y));
  fragColor = vec4 (col, 1.0);
}
)GLSL";

    // The snow: streak quads in camera space, radiating from the vanishing point.
    const char* const vsSnow = R"GLSL(#version 150
uniform mat4 uProj; uniform vec2 uResolution; uniform float uTravel, uTime, uSpeed, uSnow, uCount;
out float vA; out vec2 vUv;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u), rs = hs (pi, 5u);
  const float range = 36.0, fall = 1.1;
  float z = fract (rz - (uTravel + uTime * 0.35) / range) * range + 0.7;        // metres ahead, flowing toward the glass
  float sway = 0.4 * sin (uTime * (0.6 + rw) + rw * 20.0);
  float y = fract (ry - uTime * fall / 12.0) * 12.0 - 6.5;
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 22.0 * (0.35 + 0.65 * z / range) + sway, y, -z);
  // The streak: where this flake was ~1/45 s ago (it came toward us at the rover's speed, and fell).
  float expo = 0.022;
  vec3 P2 = P + vec3 (0.0, fall * expo, min (uSpeed * expo, z - 0.4));
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uResolution * 0.5;
  vec2 pa = na * hres, pb = nb * hres;
  vec2 dpx = pb - pa; float L = length (dpx);
  float size = (0.012 + 0.03 * rs * rs) / z * hres.y / 0.23087;   // the flake's radius in pixels
  size = max (size, 0.9);
  vec2 dirpx = (L > 1e-3) ? dpx / L : vec2 (1.0, 0.0);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  vec2 ppx = mix (pa - dirpx * size, pb + dirpx * size, corner.x) + perp * (corner.y - 0.5) * 2.0 * size;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  float fadeIn = clamp ((uSnow * uCount - float (pi)) / (0.12 * uCount), 0.0, 1.0);
  float thin = min (1.0, (2.0 * size) / max (L + 2.0 * size, 1.0)) * 0.6 + 0.4;   // long streaks are fainter
  vA = fadeIn * (0.45 + 0.45 * rw) * smoothstep (0.7, 2.0, z) * (0.35 + 0.65 * smoothstep (range, 6.0, z)) * thin;
  vUv = corner;
}
)GLSL";

    const char* const fsSnow = R"GLSL(#version 150
in float vA; in vec2 vUv;
out vec4 fragColor;
void main() {
  float a = vA * pow (1.0 - abs (vUv.y * 2.0 - 1.0), 1.4) * smoothstep (0.0, 0.25, vUv.x) * smoothstep (1.0, 0.75, vUv.x);
  fragColor = vec4 (vec3 (0.95, 0.97, 1.0), a);
}
)GLSL";

    const juce::String vsTerrainSrc = juce::String ("#version 150\n#define HTEX(uv) textureLod(uHeight, uv, 0.0)\n") + glslCommon + vsTerrainBody;
    const juce::String fsTerrainSrc = juce::String ("#version 150\n#define HTEX(uv) texture(uHeight, uv)\n") + glslCommon + glslGrad + glslLight + fsTerrainBody;
    const juce::String vsSteelSrc   = juce::String ("#version 150\n#define HTEX(uv) textureLod(uHeight, uv, 0.0)\n") + glslCommon + vsSteelBody;
    const juce::String fsSteelSrc   = juce::String ("#version 150\n") + glslLight + fsSteelBody;
    const juce::String fsSkySrc     = juce::String ("#version 150\n") + glslLight + fsSkyBody;

    //==========================================================================
    /** THE STEEL'S GEOMETRY: bars are boxes with flat normals. A vertex is position (3), normal (3),
        its direction across the bar (3, for the widening with distance) and a LOD flag (1): ten floats.
        The lumps are the same every time: no generator, only arithmetic. */
    struct V3 { double x, y, z; };
    inline V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    inline V3 operator* (V3 a, double k) { return { a.x * k, a.y * k, a.z * k }; }
    inline V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline V3 norm (V3 a) { const double l = std::sqrt (a.x * a.x + a.y * a.y + a.z * a.z); return l > 0 ? a * (1 / l) : a; }

    struct MeshBuilder
    {
        std::vector<float> v;

        void tri (V3 A, V3 B, V3 C, V3 n, V3 ea, V3 eb, V3 ec, float lod)
        {
            for (const auto& [P, E] : { std::pair { A, ea }, std::pair { B, eb }, std::pair { C, ec } })
                for (double d : { P.x, P.y, P.z, n.x, n.y, n.z, E.x, E.y, E.z, (double) lod })
                    v.push_back ((float) d);
        }

        /** A square-section bar of thickness t along a -> b. */
        void bar (V3 a, V3 b, double t, float lod = 0)
        {
            const V3 u = norm (b - a);
            const V3 ref = std::abs (u.y) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
            const V3 vv = norm (cross (u, ref)), w = cross (u, vv);
            const double h = t / 2, r2 = 0.70710678118654752;
            V3 C[8], E[8];
            int k = 0;
            for (int end = 0; end < 2; ++end)
                for (double sv : { -1.0, 1.0 })
                    for (double sw : { -1.0, 1.0 })
                    {
                        C[k] = (end ? b : a) + vv * (sv * h) + w * (sw * h);
                        E[k] = (vv * sv + w * sw) * r2;
                        ++k;
                    }
            const auto quad = [&] (int i0, int i1, int i2, int i3, V3 n)
            {
                tri (C[i0], C[i1], C[i2], n, E[i0], E[i1], E[i2], lod);
                tri (C[i0], C[i2], C[i3], n, E[i0], E[i2], E[i3], lod);
            };
            quad (2, 3, 7, 6, vv); quad (0, 4, 5, 1, vv * -1.0);     // +v / -v faces
            quad (1, 5, 7, 3, w);  quad (0, 2, 6, 4, w * -1.0);      // +w / -w
            quad (4, 6, 7, 5, u);  quad (0, 1, 3, 2, u * -1.0);      // the ends
        }

        /** A paraboloid shell of radius R and depth, both faces, segs around. */
        void dish (V3 c, V3 axis, double R, double depth, int segs)
        {
            const V3 u = axis;
            const V3 ref = std::abs (u.y) < 0.9 ? V3 { 0, 1, 0 } : V3 { 1, 0, 0 };
            const V3 vv = norm (cross (u, ref)), w = cross (u, vv);
            const auto pt = [&] (double r, double ang) { const double d = depth * (r / R) * (r / R); return c + vv * (r * std::cos (ang)) + w * (r * std::sin (ang)) + u * d; };
            const double rings[] { 0, R * 0.5, R };
            const V3 z { 0, 0, 0 };
            for (int s = 0; s < segs; ++s)
            {
                const double a0 = s / (double) segs * pi * 2, a1 = (s + 1) / (double) segs * pi * 2;
                for (int r = 0; r < 2; ++r)
                {
                    const V3 P0 = pt (rings[r], a0), P1 = pt (rings[r + 1], a0), P2 = pt (rings[r + 1], a1), P3 = pt (rings[r], a1);
                    const V3 n = norm (cross (P1 - P0, P2 - P0));
                    tri (P0, P1, P2, n, z, z, z, 0); if (r > 0) tri (P0, P2, P3, n, z, z, z, 0);
                    const V3 nn = n * -1.0;
                    tri (P0, P2, P1, nn, z, z, z, 0); if (r > 0) tri (P0, P3, P2, nn, z, z, z, 0);
                }
            }
        }

        /** A vertical prism (the tank), segs sides, with its lid. */
        void prism (V3 c, double R, double h, int segs, float lod)
        {
            const V3 z { 0, 0, 0 };
            for (int s = 0; s < segs; ++s)
            {
                const double a0 = s / (double) segs * pi * 2, a1 = (s + 1) / (double) segs * pi * 2;
                const V3 P0 { c.x + R * std::cos (a0), c.y, c.z + R * std::sin (a0) }, P1 { c.x + R * std::cos (a1), c.y, c.z + R * std::sin (a1) };
                const V3 Q0 { P0.x, c.y + h, P0.z }, Q1 { P1.x, c.y + h, P1.z };
                const double am = (a0 + a1) / 2; const V3 n { std::cos (am), 0, std::sin (am) };
                tri (P0, P1, Q1, n, z, z, z, lod); tri (P0, Q1, Q0, n, z, z, z, lod);
                const V3 T { c.x, c.y + h, c.z }; tri (Q0, Q1, T, { 0, 1, 0 }, z, z, z, lod);
            }
        }
    };

    /** The four variants: a 24 m lattice mast, an 11 m gantry, a dish on a tripod, a frame module with a tank. */
    std::array<std::vector<float>, 4> buildSteelMeshes()
    {
        std::array<std::vector<float>, 4> out;
        const std::pair<double, double> corners[] { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        const std::pair<std::pair<double, double>, std::pair<double, double>> faces[] { { { -1, -1 }, { 1, -1 } }, { { 1, -1 }, { 1, 1 } }, { { 1, 1 }, { -1, 1 } }, { { -1, 1 }, { -1, -1 } } };
        {   // 0: the lattice mast — 4 tapering legs, rings and X-braces on every face, a whip and a small dish
            MeshBuilder m; const double H = 24, b0 = 1.6, b1 = 0.55; const int nl = 5;
            const auto at = [&] (double h, double sx, double sz) { const double b = b0 + (b1 - b0) * (h / H); return V3 { sx * b, h, sz * b }; };
            for (const auto& [sx, sz] : corners) m.bar (at (0, sx, sz), at (H, sx, sz), 0.26);
            for (int l = 0; l <= nl; ++l)
            {
                const double h = H * l / nl;
                for (const auto& [p, q] : faces) m.bar (at (h, p.first, p.second), at (h, q.first, q.second), 0.14, (l == 0 || l == nl) ? 0.0f : 1.0f);
                if (l < nl)
                {
                    const double h2 = H * (l + 1) / nl;
                    for (const auto& [p, q] : faces) { m.bar (at (h, p.first, p.second), at (h2, q.first, q.second), 0.10, 1); m.bar (at (h2, p.first, p.second), at (h, q.first, q.second), 0.10, 1); }
                }
            }
            m.bar ({ 0, H, 0 }, { 0, H + 4.5, 0 }, 0.16); m.bar ({ -0.9, H + 2.2, 0 }, { 0.9, H + 2.2, 0 }, 0.10, 1);
            m.dish ({ 0.0, H * 0.72, b0 * 0.9 }, { 0, 0.25, 1 }, 1.3, 0.35, 10);   // the axis as given, a shade over unit: the dish is that much deeper
            out[0] = std::move (m.v);
        }
        {   // 1: the gantry — two A-frames and a trussed beam between them, a hook
            MeshBuilder m; const double W = 11, H = 9;
            for (double x : { -W / 2, W / 2 }) { m.bar ({ x, 0, -2.2 }, { x, H, 0 }, 0.30); m.bar ({ x, 0, 2.2 }, { x, H, 0 }, 0.30); m.bar ({ x, H * 0.45, -1.2 }, { x, H * 0.45, 1.2 }, 0.14, 1); }
            m.bar ({ -W / 2, H, -0.5 }, { W / 2, H, -0.5 }, 0.28); m.bar ({ -W / 2, H, 0.5 }, { W / 2, H, 0.5 }, 0.28);
            m.bar ({ -W / 2, H - 1.3, 0 }, { W / 2, H - 1.3, 0 }, 0.22);
            for (int i = 0; i < 6; ++i)
            {
                const double x0 = -W / 2 + W * i / 6, x1 = -W / 2 + W * (i + 1) / 6;
                m.bar ({ x0, H, (i & 1) ? 0.5 : -0.5 }, { x1, H - 1.3, 0 }, 0.10, 1); m.bar ({ x0, H - 1.3, 0 }, { x1, H, (i & 1) ? -0.5 : 0.5 }, 0.10, 1);
            }
            m.bar ({ 1.5, H - 1.3, 0 }, { 1.5, H - 4.2, 0 }, 0.12, 1); m.bar ({ 0.6, H - 4.2, 0 }, { 2.4, H - 4.2, 0 }, 0.18, 1);
            out[1] = std::move (m.v);
        }
        {   // 2: a dish on a tripod, a feed strut
            MeshBuilder m; const double H = 6;
            for (int k = 0; k < 3; ++k)
            {
                const double a = k / 3.0 * pi * 2, a2 = (k + 1) / 3.0 * pi * 2;
                m.bar ({ 2.2 * std::cos (a), 0, 2.2 * std::sin (a) }, { 0, H, 0 }, 0.22);
                m.bar ({ 1.1 * std::cos (a), H / 2, 1.1 * std::sin (a) }, { 1.1 * std::cos (a2), H / 2, 1.1 * std::sin (a2) }, 0.10, 1);
            }
            m.bar ({ 0, H, 0 }, { 0, H + 1.4, 0 }, 0.22);
            const V3 ax { 0, 0.55, 0.835 }; m.dish ({ 0, H + 1.4, 0 }, ax, 3.2, 0.9, 12);
            m.bar ({ 0, H + 1.4, 0 }, { ax.x * 2.4, H + 1.4 + ax.y * 2.4, ax.z * 2.4 }, 0.10, 1);
            out[2] = std::move (m.v);
        }
        {   // 3: the frame module — an open cube frame on short legs, a tank inside, a boom
            MeshBuilder m; const double S = 4.2, H0 = 1.2, Ht = 4.0;
            const auto c = [&] (double sx, double h, double sz) { return V3 { sx * S / 2, h, sz * S / 2 }; };
            for (const auto& [sx, sz] : corners) m.bar (c (sx, 0, sz), c (sx, H0 + Ht, sz), 0.22);
            for (double h : { H0, H0 + Ht }) for (const auto& [p, q] : faces) m.bar (c (p.first, h, p.second), c (q.first, h, q.second), 0.18);
            m.bar (c (-1, H0, -1), c (1, H0 + Ht, -1), 0.10, 1); m.bar (c (1, H0, 1), c (-1, H0 + Ht, 1), 0.10, 1);
            m.prism ({ 0, H0 + 0.1, 0 }, 1.3, Ht - 0.4, 8, 0);
            m.bar ({ S / 2, H0 + Ht, 0 }, { S / 2 + 2.0, H0 + Ht + 1.5, 0 }, 0.12, 1);
            out[3] = std::move (m.v);
        }
        return out;
    }

    inline double fract (double x) noexcept { return x - std::floor (x); }

    using Mat4 = std::array<float, 16>;   // column-major
    Mat4 multiply (const Mat4& a, const Mat4& b)
    {
        Mat4 o {};
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { float s = 0; for (int k = 0; k < 4; ++k) s += a[(size_t) (k * 4 + r)] * b[(size_t) (c * 4 + k)]; o[(size_t) (c * 4 + r)] = s; }
        return o;
    }
    struct Basis { float r[3], u[3], f[3]; };
    Basis cameraBasis (double heading, double pitch)   // right, up, forward (world); pitch positive = nose up
    {
        const double cp = std::cos (pitch), sp = std::sin (pitch);
        const double f0x = std::sin (heading), f0z = -std::cos (heading);
        Basis b;
        b.r[0] = (float) std::cos (heading); b.r[1] = 0; b.r[2] = (float) std::sin (heading);
        b.f[0] = (float) (f0x * cp); b.f[1] = (float) sp; b.f[2] = (float) (f0z * cp);
        b.u[0] = (float) (-f0x * sp); b.u[1] = (float) cp; b.u[2] = (float) (-f0z * sp);
        return b;
    }

    /** The sky's fixed frame: the sun, Jupiter, and Jupiter's right, up and axis as seen from here. */
    struct SkyFrame
    {
        float sun[3], jup[3], jupR[3], jupU[3], jupAxis[3];
        float jupSin, jupCos, ioSin;
    };
    SkyFrame skyFrame()
    {
        const auto dir = [] (double azDeg, double elDeg) { const double az = azDeg * pi / 180, el = elDeg * pi / 180; return V3 { std::sin (az) * std::cos (el), std::sin (el), -std::cos (az) * std::cos (el) }; };
        const V3 sun = dir (-118, 16);     // low, to the left of heading 0 (azimuth from -z, positive = right)
        const V3 jup = dir (27, 4.0);      // huge, low, right of heading 0 — a fixed world direction
        const V3 R = norm (cross ({ 0, 1, 0 }, jup)), U = norm (cross (jup, R));
        const V3 axis = norm (U * 0.96 + R * 0.12 - jup * 0.25);   // tilted a little toward us
        SkyFrame f;
        const auto put = [] (float* o, V3 a) { o[0] = (float) a.x; o[1] = (float) a.y; o[2] = (float) a.z; };
        put (f.sun, sun); put (f.jup, jup); put (f.jupR, R); put (f.jupU, U); put (f.jupAxis, axis);
        f.jupSin = (float) std::sin (jupiterRadiusDegrees * pi / 180); f.jupCos = (float) std::cos (jupiterRadiusDegrees * pi / 180);
        f.ioSin = (float) std::sin (ioRadiusDegrees * pi / 180);
        return f;
    }
    /** Io circles Jupiter slowly, out to ~2.3 Jupiter radii from the disc's edge. */
    void ioDirection (const SkyFrame& f, double time, float* out)
    {
        const double a = time * 0.011 + 2.2, R = f.jupSin * 3.3;
        const V3 J { f.jup[0], f.jup[1], f.jup[2] }, JR { f.jupR[0], f.jupR[1], f.jupR[2] }, JU { f.jupU[0], f.jupU[1], f.jupU[2] };
        const V3 io = norm (J + JR * (std::cos (a) * R) + JU * (std::sin (a) * R * 0.2));
        out[0] = (float) io.x; out[1] = (float) io.y; out[2] = (float) io.z;
    }
}

//==============================================================================
int IceGl::Program::uniform (const char* name) const
{
    return glGetUniformLocation (id, name);
}

bool IceGl::compile (Program& p, const char* vs, const char* fs, const char* what)
{
    auto s = std::make_unique<juce::OpenGLShaderProgram> (*juce::OpenGLContext::getCurrentContext());
    if (! s->addVertexShader (vs) || ! s->addFragmentShader (fs) || ! s->link())
    {
        DBG ("frostpaw: the " << what << " shader did not build — " << s->getLastError());
        juce::ignoreUnused (what);
        return false;
    }
    p.id = s->getProgramID();
    p.program = std::move (s);
    return true;
}

bool IceGl::create()
{
    if (juce::OpenGLContext::getCurrentContext() == nullptr)
        return false;
    world.build();

    if (! compile (terrain, vsTerrainSrc.toRawUTF8(), fsTerrainSrc.toRawUTF8(), "terrain")
        || ! compile (steel, vsSteelSrc.toRawUTF8(), fsSteelSrc.toRawUTF8(), "steel")
        || ! compile (sky, vsSky, fsSkySrc.toRawUTF8(), "sky")
        || ! compile (snow, vsSnow, fsSnow, "snow"))
    {
        destroy();
        return false;
    }
    makeTextures();
    makeGeometry();

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

void IceGl::destroy()
{
    for (auto& l : level) { if (l.indexBuffer != 0) glDeleteBuffers (1, &l.indexBuffer); l = {}; }
    const auto del = [] (juce::uint32& id, void (*f) (GLsizei, const GLuint*)) { if (id != 0) f (1, &id); id = 0; };
    del (vboGrid, glDeleteBuffers); del (vboSteel, glDeleteBuffers);
    for (auto& b : instanceBuffer) del (b, glDeleteBuffers);
    instanceCount.fill (0); instanceBx = instanceBz = 0x7fffffff;
    del (vaoTerrain, glDeleteVertexArrays); del (vaoSteel, glDeleteVertexArrays); del (vaoEmpty, glDeleteVertexArrays);
    del (texHeight, glDeleteTextures); del (texGrad1, glDeleteTextures); del (texGrad2, glDeleteTextures); del (texDetail, glDeleteTextures);
    del (fboColour, glDeleteTextures); del (fboDepth, glDeleteRenderbuffers); del (fbo, glDeleteFramebuffers);
    fbW = fbH = 0;
    if (query[0] != 0) glDeleteQueries ((GLsizei) query.size(), query.data());
    query.fill (0);
    queriesWork = false; queriesPending = 0; queryNext = 0;
    for (auto* p : { &terrain, &steel, &sky, &snow }) { p->program.reset(); p->id = 0; }
}

void IceGl::makeTextures()
{
    const int n = IceWorld::texSize;
    glGenTextures (1, &texHeight);
    glBindTexture (GL_TEXTURE_2D, texHeight);
    {
        int size = n, lvl = 0;
        for (const auto& mip : world.heightMips())
        {
            glTexImage2D (GL_TEXTURE_2D, lvl++, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT, mip.data());
            size >>= 1;
        }
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, lvl - 1);
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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
    rgba8 (texGrad1, n, world.gradient1());
    rgba8 (texGrad2, n, world.gradient2());
    rgba8 (texDetail, IceWorld::detailSize, world.detail());
    glBindTexture (GL_TEXTURE_2D, 0);
}

void IceGl::makeGeometry()
{
    // The grid: (N+1)^2 vertices of (i, j); one full index buffer for level 0, a ring buffer per other level.
    glGenVertexArrays (1, &vaoTerrain);
    glBindVertexArray (vaoTerrain);
    {
        std::vector<float> g ((size_t) (N + 1) * (N + 1) * 2);
        for (int j = 0; j <= N; ++j) for (int i = 0; i <= N; ++i) { g[(size_t) ((j * (N + 1) + i) * 2)] = (float) i; g[(size_t) ((j * (N + 1) + i) * 2 + 1)] = (float) j; }
        glGenBuffers (1, &vboGrid);
        glBindBuffer (GL_ARRAY_BUFFER, vboGrid);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (g.size() * sizeof (float)), g.data(), GL_STATIC_DRAW);
        const auto loc = (GLuint) glGetAttribLocation (terrain.id, "aGrid");
        glEnableVertexAttribArray (loc);
        glVertexAttribPointer (loc, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    fullIndices.resize ((size_t) N * N * 6);   // per cell (j-major): two triangles, the diagonal (i,j)-(i+1,j+1)
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
        {
            const size_t o = (size_t) (j * N + i) * 6; const juce::uint16 v = (juce::uint16) (j * (N + 1) + i);
            fullIndices[o] = v; fullIndices[o + 1] = (juce::uint16) (v + 1); fullIndices[o + 2] = (juce::uint16) (v + N + 2);
            fullIndices[o + 3] = v; fullIndices[o + 4] = (juce::uint16) (v + N + 2); fullIndices[o + 5] = (juce::uint16) (v + N + 1);
        }
    ringIndices.resize (fullIndices.size());
    for (int k = 0; k < levels; ++k)
    {
        auto& L = level[(size_t) k];
        glGenBuffers (1, &L.indexBuffer);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (fullIndices.size() * sizeof (juce::uint16)), fullIndices.data(), k == 0 ? GL_STATIC_DRAW : GL_DYNAMIC_DRAW);
        L.count = (int) fullIndices.size();
        L.cell = cell0 * std::pow (2.0, k);
        L.holeX = L.holeY = -1;
    }
    glBindVertexArray (0);

    // The steel: the four meshes in one buffer, the instance cells in a small dynamic buffer per variant.
    glGenVertexArrays (1, &vaoSteel);
    glBindVertexArray (vaoSteel);
    {
        auto meshes = buildSteelMeshes();
        std::vector<float> all;
        for (int v = 0; v < IceWorld::numVariants; ++v)
        {
            steelRange[(size_t) v] = { (int) (all.size() / 10), (int) (meshes[(size_t) v].size() / 10) };
            all.insert (all.end(), meshes[(size_t) v].begin(), meshes[(size_t) v].end());
        }
        glGenBuffers (1, &vboSteel);
        glBindBuffer (GL_ARRAY_BUFFER, vboSteel);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (all.size() * sizeof (float)), all.data(), GL_STATIC_DRAW);
        const auto lp = (GLuint) glGetAttribLocation (steel.id, "aPos"), ln = (GLuint) glGetAttribLocation (steel.id, "aNrm");
        const auto le = (GLuint) glGetAttribLocation (steel.id, "aExp"), ll = (GLuint) glGetAttribLocation (steel.id, "aLod");
        glEnableVertexAttribArray (lp); glVertexAttribPointer (lp, 3, GL_FLOAT, GL_FALSE, 40, nullptr);
        glEnableVertexAttribArray (ln); glVertexAttribPointer (ln, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 12);
        glEnableVertexAttribArray (le); glVertexAttribPointer (le, 3, GL_FLOAT, GL_FALSE, 40, (const void*) 24);
        glEnableVertexAttribArray (ll); glVertexAttribPointer (ll, 1, GL_FLOAT, GL_FALSE, 40, (const void*) 36);
        locCell = glGetAttribLocation (steel.id, "aCell");
        instancing = glVertexAttribDivisor != nullptr && locCell >= 0;
        for (auto& b : instanceBuffer)
        {
            glGenBuffers (1, &b);
            glBindBuffer (GL_ARRAY_BUFFER, b);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (IceWorld::structWindow * IceWorld::structWindow * 2 * sizeof (float)), nullptr, GL_DYNAMIC_DRAW);
        }
        instanceCount.fill (0); instanceBx = instanceBz = 0x7fffffff;
    }
    glBindVertexArray (0);
    glGenVertexArrays (1, &vaoEmpty);   // the sky and the snow draw with no attributes; core needs a VAO bound all the same
    glBindBuffer (GL_ARRAY_BUFFER, 0);
}

void IceGl::rebuildRing (Level& L, int hx, int hy)   // leaves out the N/2 x N/2 cells at [hx, hx + N/2) x [hy, hy + N/2)
{
    size_t n = 0; const int H = N / 2;
    const auto put = [&] (size_t from, size_t to) { std::copy (fullIndices.begin() + (long) from, fullIndices.begin() + (long) to, ringIndices.begin() + (long) n); n += to - from; };
    for (int j = 0; j < N; ++j)
    {
        if (j < hy || j >= hy + H) put ((size_t) j * N * 6, (size_t) (j + 1) * N * 6);
        else { put ((size_t) j * N * 6, (size_t) (j * N + hx) * 6); put ((size_t) (j * N + hx + H) * 6, (size_t) (j + 1) * N * 6); }
    }
    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
    glBufferSubData (GL_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptr) (n * sizeof (juce::uint16)), ringIndices.data());
    L.count = (int) n; L.holeX = hx; L.holeY = hy;
}

void IceGl::ensureFrame (int w, int h)
{
    if (fbo != 0 && w == fbW && h == fbH)
        return;
    if (fbo == 0) { glGenFramebuffers (1, &fbo); glGenTextures (1, &fboColour); glGenRenderbuffers (1, &fboDepth); }
    glBindTexture (GL_TEXTURE_2D, fboColour);
    glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture (GL_TEXTURE_2D, 0);
    glBindRenderbuffer (GL_RENDERBUFFER, fboDepth);
    glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer (GL_RENDERBUFFER, 0);
    glBindFramebuffer (GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboColour, 0);
    glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fboDepth);
    fbW = w; fbH = h;
}

void IceGl::noteCost (float ms)
{
    gpuMs = gpuMs < 0 ? ms : gpuMs + (ms - gpuMs) * 0.1f;
}

void IceGl::render (int w, int h, float scale, juce::uint32 target, int x, int y)
{
    if (terrain.id == 0 || w <= 0 || h <= 0)
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

bool IceGl::readFrame (std::vector<juce::uint8>& rgba, int& w, int& h)
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

void IceGl::drawScene (int rw, int rh)
{
    const auto& s = world.state();
    // The snow: the world eases it, graded by the gear, TURBO's blizzard at 1. The air thickens and
    // whitens with it, the sun dims, the mist band grows.
    const float snowAmount = s.snow;
    const auto mix3 = [] (std::array<float, 3> a, std::array<float, 3> b, float t) { return std::array<float, 3> { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t }; };
    const auto horizon = mix3 ({ 0.74f, 0.80f, 0.90f }, { 0.84f, 0.87f, 0.92f }, snowAmount);     // what distance fades to: the cold haze
    const auto mist    = mix3 ({ 0.52f, 0.61f, 0.76f }, { 0.70f, 0.76f, 0.86f }, snowAmount);     // the thin band on the horizon
    const float hazeK = (1.0f / 3200.0f) * (1 + 6.0f * snowAmount);
    const float sunInt = 2.0f - 0.8f * snowAmount;
    static const SkyFrame skyF = skyFrame();
    const double skyTime = std::fmod (s.time, skyClock);
    float io[3]; ioDirection (skyF, skyTime, io);

    // The frame of reference: the integer cell at the rover; every shader position is relative to it.
    const double ccx = std::floor (s.x), ccz = std::floor (s.z);
    const float camFrac[2] { (float) (s.x - ccx), (float) (s.z - ccz) };
    const double fx = std::sin (s.heading), fz = -std::cos (s.heading);

    // The projection: off-centre, the horizon at NDC y = +shear; near 0.5 m, far 40 km.
    const double aspect = (double) rw / rh;
    const double tv = vfovHalfTan, th = tv * aspect, zn = 0.5, zf = 40000.0;
    Mat4 P {};
    P[0] = (float) (1 / th); P[5] = (float) (1 / tv); P[10] = (float) (-(zf + zn) / (zf - zn)); P[11] = -1; P[14] = (float) (-2 * zf * zn / (zf - zn));
    P[9] = -shear;
    const Basis basis = cameraBasis (s.heading, s.pitch);
    Mat4 V {};
    {
        const float cx = camFrac[0], cy = (float) s.camY, cz = camFrac[1];
        const auto* r = basis.r; const auto* u = basis.u; const auto* f = basis.f;
        V[0] = r[0]; V[4] = r[1]; V[8] = r[2];    V[12] = -(r[0] * cx + r[1] * cy + r[2] * cz);
        V[1] = u[0]; V[5] = u[1]; V[9] = u[2];    V[13] = -(u[0] * cx + u[1] * cy + u[2] * cz);
        V[2] = -f[0]; V[6] = -f[1]; V[10] = -f[2]; V[14] = (f[0] * cx + f[1] * cy + f[2] * cz);
        V[15] = 1;
    }
    const Mat4 VP = multiply (P, V);
    const float tex0[2] { (float) fract (ccx / 7000), (float) fract (ccz / 7000) };
    const float tex1[2] { (float) (fract (ccx / 1250) + 0.37), (float) (fract (ccz / 1250) + 0.61) };
    const float tex2[2] { (float) (fract (ccx / 190) + 0.71), (float) (fract (ccz / 190) + 0.13) };
    const float tex3[2] { (float) (fract (ccx / 50) + 0.23), (float) (fract (ccz / 50) + 0.87) };
    const float det0[2] { (float) fract (ccx / 3.1), (float) fract (ccz / 3.1) };
    const float det1[2] { (float) fract (ccx / 0.45), (float) fract (ccz / 0.45) };
    const float det2[2] { (float) fract (ccx / 0.13), (float) fract (ccz / 0.13) };
    // The lineae's coordinates of the cell, wrapped to their period and wavelength, and the line's index.
    struct LinOff { float s, t; int base; };
    const auto linOff = [&] (double dx, double dz, double px, double pz, double period, double wavelength)
    {
        const double sv = ccx * dx + ccz * dz, tvv = ccx * px + ccz * pz;
        return LinOff { (float) (sv - std::floor (sv / period) * period), (float) (tvv - std::floor (tvv / wavelength) * wavelength), (int) std::floor (sv / period) };
    };
    const LinOff linA = linOff (0.904752, 0.425939, -0.425939, 0.904752, 1100.0, 1600.0);
    const LinOff linB = linOff (-0.370181, 0.928960, -0.928960, -0.370181, 1700.0, 2300.0);
    const float chaosOff[2] { (float) fract (ccx / IceWorld::chaosCell), (float) fract (ccz / IceWorld::chaosCell) };
    const int chaosBase[2] { (int) std::floor (ccx / IceWorld::chaosCell), (int) std::floor (ccz / IceWorld::chaosCell) };
    const float structOff[2] { (float) fract (ccx / IceWorld::structCell), (float) fract (ccz / IceWorld::structCell) };
    const int structBase[2] { (int) std::floor (ccx / IceWorld::structCell), (int) std::floor (ccz / IceWorld::structCell) };

    const auto setCommon = [&] (const Program& p)
    {
        glUniform1i (p.uniform ("uHeight"), 0);
        glUniform2f (p.uniform ("uCamCell"), (float) ccx, (float) ccz);
        glUniform2f (p.uniform ("uCamFrac"), camFrac[0], camFrac[1]);
        glUniform2f (p.uniform ("uTexOff0"), tex0[0], tex0[1]);
        glUniform2f (p.uniform ("uTexOff1"), tex1[0], tex1[1]);
        glUniform2f (p.uniform ("uTexOff2"), tex2[0], tex2[1]);
        glUniform2f (p.uniform ("uTexOff3"), tex3[0], tex3[1]);
        glUniform4f (p.uniform ("uLinOff"), linA.s, linA.t, linB.s, linB.t);
        glUniform2i (p.uniform ("uLinBase"), linA.base, linB.base);
        glUniform2f (p.uniform ("uChaosOff"), chaosOff[0], chaosOff[1]); glUniform2i (p.uniform ("uChaosBase"), chaosBase[0], chaosBase[1]);
        glUniform2f (p.uniform ("uStructOff"), structOff[0], structOff[1]); glUniform2i (p.uniform ("uStructBase"), structBase[0], structBase[1]);
    };
    const auto setLight = [&] (const Program& p)
    {
        glUniform3fv (p.uniform ("uSun"), 1, skyF.sun);
        glUniform3fv (p.uniform ("uJup"), 1, skyF.jup);
        glUniform3fv (p.uniform ("uHorizon"), 1, horizon.data());
        glUniform3fv (p.uniform ("uMist"), 1, mist.data());
        glUniform1f (p.uniform ("uHazeK"), hazeK);
        glUniform1f (p.uniform ("uSnow"), snowAmount);
        glUniform1f (p.uniform ("uSunInt"), sunInt);
    };

    glViewport (0, 0, rw, rh);
    glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST); glDepthFunc (GL_LEQUAL); glDisable (GL_CULL_FACE);
    glDepthMask (GL_TRUE);
    glClear (GL_DEPTH_BUFFER_BIT);
    glActiveTexture (GL_TEXTURE0); glBindTexture (GL_TEXTURE_2D, texHeight);
    glActiveTexture (GL_TEXTURE1); glBindTexture (GL_TEXTURE_2D, texDetail);
    glActiveTexture (GL_TEXTURE2); glBindTexture (GL_TEXTURE_2D, texGrad1);
    glActiveTexture (GL_TEXTURE3); glBindTexture (GL_TEXTURE_2D, texGrad2);

    // The terrain: eight clipmap levels, finest first.
    glUseProgram (terrain.id); glBindVertexArray (vaoTerrain);
    setCommon (terrain); setLight (terrain);
    glUniform1i (terrain.uniform ("uDetail"), 1); glUniform1i (terrain.uniform ("uGrad1"), 2); glUniform1i (terrain.uniform ("uGrad2"), 3);
    glUniform2f (terrain.uniform ("uGS1"), world.gradientScale1()[0], world.gradientScale1()[1]);
    glUniform2f (terrain.uniform ("uGS2"), world.gradientScale2()[0], world.gradientScale2()[1]);
    glUniformMatrix4fv (terrain.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
    glUniform3f (terrain.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
    glUniform2f (terrain.uniform ("uDetOff0"), det0[0], det0[1]); glUniform2f (terrain.uniform ("uDetOff1"), det1[0], det1[1]); glUniform2f (terrain.uniform ("uDetOff2"), det2[0], det2[1]);
    const int uLvlOrigin = terrain.uniform ("uLvlOrigin"), uCellSize = terrain.uniform ("uCellSize"), uMorphOn = terrain.uniform ("uMorphOn");
    for (int k = 0; k < levels; ++k)
    {
        auto& L = level[(size_t) k];
        const double cell = L.cell, snap = 2 * cell, half = N / 2 * cell;
        const double cx = s.x + fx * fwdShift * half, cz = s.z + fz * fwdShift * half;
        L.ox = std::floor (cx / snap) * snap - half; L.oz = std::floor (cz / snap) * snap - half;   // the absolute world origin of vertex (0, 0)
        if (k > 0)
        {
            const auto& F = level[(size_t) k - 1];
            const int hx = std::clamp ((int) std::lround ((F.ox - L.ox) / cell), 0, N / 2), hy = std::clamp ((int) std::lround ((F.oz - L.oz) / cell), 0, N / 2);
            if (hx != L.holeX || hy != L.holeY) rebuildRing (L, hx, hy);
        }
        glUniform2f (uLvlOrigin, (float) (L.ox - ccx), (float) (L.oz - ccz));
        glUniform1f (uCellSize, (float) cell);
        glUniform1f (uMorphOn, k < levels - 1 ? 1.0f : 0.0f);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, L.indexBuffer);
        glDrawElements (GL_TRIANGLES, L.count, GL_UNSIGNED_SHORT, nullptr);
    }

    // The steel: four instanced draws from the world's lists, remade when the window moves.
    if (instancing)
    {
        glUseProgram (steel.id); glBindVertexArray (vaoSteel);
        setCommon (steel); setLight (steel);
        glUniformMatrix4fv (steel.uniform ("uViewProj"), 1, GL_FALSE, VP.data());
        glUniform3f (steel.uniform ("uCamRel"), camFrac[0], (float) s.camY, camFrac[1]);
        glUniform1i (steel.uniform ("uDetail"), 1); glUniform1f (steel.uniform ("uJupCos"), skyF.jupCos);
        glUniform1f (steel.uniform ("uPxExpand"), (float) (2 * tv / rh));
        const double shift = IceWorld::structWindow * IceWorld::structCell * 0.3;
        const int bx = (int) std::floor ((s.x + fx * shift) / IceWorld::structCell) - IceWorld::structWindow / 2;
        const int bz = (int) std::floor ((s.z + fz * shift) / IceWorld::structCell) - IceWorld::structWindow / 2;
        if (bx != instanceBx || bz != instanceBz)
        {
            world.structuresInWindow (bx, bz, instanceCells);
            for (int v = 0; v < IceWorld::numVariants; ++v)
            {
                const auto& cells = instanceCells[(size_t) v];
                instanceCount[(size_t) v] = (int) (cells.size() / 2);
                glBindBuffer (GL_ARRAY_BUFFER, instanceBuffer[(size_t) v]);
                if (! cells.empty()) glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) (cells.size() * sizeof (float)), cells.data());
            }
            instanceBx = bx; instanceBz = bz;
        }
        const int uVariant = steel.uniform ("uVariant");
        for (int v = 0; v < IceWorld::numVariants; ++v)
        {
            if (instanceCount[(size_t) v] == 0) continue;
            glUniform1i (uVariant, v);
            glBindBuffer (GL_ARRAY_BUFFER, instanceBuffer[(size_t) v]);
            glEnableVertexAttribArray ((GLuint) locCell);
            glVertexAttribPointer ((GLuint) locCell, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            glVertexAttribDivisor ((GLuint) locCell, 1);
            glDrawArraysInstanced (GL_TRIANGLES, steelRange[(size_t) v].first, steelRange[(size_t) v].count, instanceCount[(size_t) v]);
        }
        glBindBuffer (GL_ARRAY_BUFFER, 0);
    }

    // The sky, where nothing was drawn (depth == 1).
    glUseProgram (sky.id); glBindVertexArray (vaoEmpty);
    setLight (sky);
    glUniform1i (sky.uniform ("uDetail"), 1);
    glUniform3fv (sky.uniform ("uCamFwd"), 1, basis.f); glUniform3fv (sky.uniform ("uCamRight"), 1, basis.r); glUniform3fv (sky.uniform ("uCamUp"), 1, basis.u);
    glUniform2f (sky.uniform ("uTanHalf"), (float) th, (float) tv); glUniform1f (sky.uniform ("uShear"), shear);
    glUniform1f (sky.uniform ("uTime"), (float) skyTime); glUniform1f (sky.uniform ("uPxAng"), (float) (2 * tv / rh));
    glUniform3fv (sky.uniform ("uJupR"), 1, skyF.jupR); glUniform3fv (sky.uniform ("uJupU"), 1, skyF.jupU); glUniform3fv (sky.uniform ("uJupAxis"), 1, skyF.jupAxis);
    glUniform3fv (sky.uniform ("uIo"), 1, io);
    glUniform1f (sky.uniform ("uJupSin"), skyF.jupSin); glUniform1f (sky.uniform ("uJupCos"), skyF.jupCos); glUniform1f (sky.uniform ("uIoSin"), skyF.ioSin);
    glDrawArrays (GL_TRIANGLES, 0, 3);

    // The snow: the flakes blended over everything, as many as the snow asks for.
    if (snowAmount > 0.005f)
    {
        glUseProgram (snow.id);
        glEnable (GL_BLEND); glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable (GL_DEPTH_TEST);
        glUniformMatrix4fv (snow.uniform ("uProj"), 1, GL_FALSE, P.data()); glUniform2f (snow.uniform ("uResolution"), (float) rw, (float) rh);
        glUniform1f (snow.uniform ("uTravel"), (float) std::fmod (s.travel, snowClock)); glUniform1f (snow.uniform ("uTime"), (float) skyTime);
        glUniform1f (snow.uniform ("uSpeed"), s.speed); glUniform1f (snow.uniform ("uSnow"), snowAmount); glUniform1f (snow.uniform ("uCount"), (float) IceWorld::snowCount);
        const int count = std::min (IceWorld::snowCount, (int) std::ceil (IceWorld::snowCount * std::min (1.0f, snowAmount + 0.12f)));
        glDrawArrays (GL_TRIANGLES, 0, count * 6);
        glDisable (GL_BLEND); glEnable (GL_DEPTH_TEST);
    }
    glBindVertexArray (0);
    glUseProgram (0);
}

} // namespace orbitamp::frostpaw
