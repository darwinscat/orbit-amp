// From the prototype's metal-shaders.js (FS_GROUND), its body as it is but for its two discards, each under a switch the
// C++ writes (MetalGl.cpp): the hole's under HOLE, the far cut's under FARCUT — a level that can have neither fragment draws
// with neither (a shader that can discard is shaded the slow way on a tiling GPU).
#include <hole>
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform sampler2D uDetail, uCells;
uniform vec2 uDetOff0, uDetOff1;
uniform vec2 uCrackOff;                      // uCamCell modulo the veins' period: their domain's origin
uniform float uBreath, uStuds, uFarCut, uThin;
uniform vec4 uHole; uniform float uHoleOn;
in vec3 vPos; in vec4 vA; in vec4 vB; in float vD;
out vec4 fragColor;
// Cellular noise from the cells' texture: F2 - F1 (zero on the borders: the crack network) and a hash the two cells
// either side share, so a vein is one thing along its length.
float worley (vec2 p, ivec2 shift, out float id) {
  vec2 n = floor (p), f = p - n;
  ivec2 ni = ivec2 (n) + shift;
  float d1 = 8.0, d2 = 8.0, h1 = 0.0, h2 = 0.0;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      vec4 t = texelFetch (uCells, (ni + ivec2 (i, j)) & 255, 0);
      vec2 r = vec2 (float (i), float (j)) + t.rg - f;
      float d = dot (r, r);
      if (d < d1) { d2 = d1; h2 = h1; d1 = d; h1 = t.b; } else if (d < d2) { d2 = d; h2 = t.b; }
    }
  id = fract (h1 + h2);
  return sqrt (d2) - sqrt (d1);
}
float crack (float e, float w, float aa, out float centre) {
  float hw = max (w, 0.6 * aa);
  float cov = min (1.0, w / hw);
  float x = e / hw;
  centre = 1.0 - smoothstep (0.0, 0.55, x);
  return (1.0 - smoothstep (0.65, 1.0, x)) * cov;
}
void main() {
#if HOLE
  if (uHoleOn > 0.5 && vPos.x > uHole.x && vPos.z > uHole.y && vPos.x < uHole.z && vPos.z < uHole.w) discard;
#endif
  vec3 rel = vPos - uCamRel; float dist = length (rel);
#if FARCUT
  if (dist > uFarCut) discard;
#endif
  vec3 n = normalize (cross (dFdx (vPos), dFdy (vPos)));
  if (n.y < 0.0) n = -n;
  n = normalize (mix (n, vec3 (0.0, 1.0, 0.0), 0.4));
  float df1 = exp (-dist / 50.0);
  vec3 d1 = texture (uDetail, uDetOff0 + vPos.xz / 1.5).rgb * 2.0 - 1.0;
  n = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.6 * df1);
  float ad = abs (vD);
  float roadK = 1.0 - smoothstep (ROAD_HALF - 0.15, ROAD_HALF + 0.35, ad);
  float tun = inTunnel (vPos.xz, ad);

  // The veins: a 6 m cell network warped by the slow texture, a 1.5 m one near; not on the road.
  float hot = (0.12 + 0.88 * smoothstep (0.30, 0.50, vA.a * 0.5 + vB.a * 0.5)) * smoothstep (0.30, 0.40, vA.a);
  hot *= smoothstep (ROAD_HALF + 0.6, ROAD_HALF + 3.0, ad);
  float glow = hitGlow ();
  float flare = 1.0 + 1.0 * uStorm + 2.2 * glow;
  vec3 emis = vec3 (0.0); float bleed = 0.0;
  float farK = 1.0 - smoothstep (250.0, 1100.0, dist);
  vec2 q = vPos.xz + uCrackOff;
  if (hot > 0.003 && farK > 0.0) {
    vec2 warp = (vB.rg - 0.5) * 4.0;
    float idA; float eA = worley ((q + warp) / 6.0, ivec2 (0), idA);
    float aaA = max (fwidth (eA), 1e-5);
    float alive = smoothstep (0.28, 0.42, idA);
    float pulse = 0.72 + 0.28 * sin (uTime * (0.35 + 0.5 * idA) * (1.0 + 2.5 * uStorm) + idA * 40.0);
    float heat = hot * alive * pulse * flare;
    float cA; float covA = crack (eA, 0.038, aaA, cA);
    float crust = smoothstep (0.50, 0.62, texture (uDetail, uDetOff1 + vPos.xz / 0.9).b) * (1.0 - cA);
    vec3 lava = mix (vec3 (1.0, 0.26, 0.03), vec3 (1.0, 0.88, 0.55), cA * (0.6 + 0.4 * pulse)) * (1.0 - 0.75 * crust);
    emis = lava * covA * 2.4 * heat;
    bleed = exp (-eA * 6.0) * heat;
    if (dist < 60.0) {
      float idB; float eB = worley ((q + warp * 0.3) / 1.5, ivec2 (97, 131), idB);
      float aaB = max (fwidth (eB), 1e-5);
      float cB; float covB = crack (eB, 0.045, aaB, cB) * smoothstep (0.55, 0.7, idB) * exp (-dist / 40.0);
      float heatB = hot * pulse * flare * 0.7;
      emis += lava * covB * 1.2 * heatB;
      bleed += exp (-eB * 5.0) * heatB * 0.35 * exp (-dist / 40.0);
    }
    emis *= farK; bleed *= farK;
  }
  emis += vec3 (1.0, 0.30, 0.05) * hot * 0.05 * flare * (1.0 - farK);       // far off: the veins' haze

  // The rock: dark, grainy; the road a smoother, darker bed with the rover's tracks worn paler.
  vec3 alb = vec3 (0.16, 0.17, 0.19) * (0.65 + 0.7 * vB.b) * (1.0 + 0.25 * d1.z * df1);
  if (roadK > 0.0) {
    float wear = smoothstep (0.45, 0.0, abs (abs (ad - 1.9) - 0.8)) * 0.05;
    vec3 bed = vec3 (0.085, 0.085, 0.095) * (0.9 + 0.25 * d1.z) + wear;
    alb = mix (alb, bed, roadK);
    n = normalize (mix (n, vec3 (0.0, 1.0, 0.0), roadK * 0.8));
  }
  float tilt = clamp (1.0 - n.y, 0.0, 1.0);
  vec3 veinLight = vec3 (1.0, 0.36, 0.08) * bleed * (1.6 + 1.5 * tilt);
  vec3 col = lightMatteK (alb, n, vPos, 1.0 - 0.92 * tun) + alb * veinLight + emis;
  // the studs: amber cat's eyes along both edges of the roadbed, every 10 m round the loop
  if (ad < ROAD_HALF + 0.5 && ad > ROAD_HALF - 0.8) {
    vec2 lq = vPos.xz - uLoopC;
    float along = fract (atan (lq.y, lq.x) * (1.0 / 6.2831853) * uStuds) - 0.5;
    float mAlong = along * 10.0, aw = max (fwidth (mAlong), 1e-4), cw = max (fwidth (ad), 1e-4);
    float stud = (1.0 - smoothstep (0.12, 0.12 + aw, abs (mAlong))) * (1.0 - smoothstep (0.12, 0.12 + cw, abs (ad - (ROAD_HALF - 0.35))));
    float k = min (1.0, 0.24 / max (aw, 0.24)) * min (1.0, 0.24 / max (cw, 0.24));          // sub-pixel: their mean
    col += vec3 (1.0, 0.55, 0.12) * (2.2 + 2.0 * glow) * max (stud, k * 0.024 * step (abs (ad - (ROAD_HALF - 0.35)), 0.5));
  }
  // in the tunnel: a lava lamp in the wall every 15 m, its warm light on the road
  if (tun > 0.0) {
    vec2 lq = vPos.xz - uLoopC;
    float along = fract (atan (lq.y, lq.x) * (1.0 / 6.2831853) * uStuds / 1.5) - 0.5;
    col += alb * vec3 (1.0, 0.4, 0.1) * tun * 1.6 * exp (-sq (along * 15.0 / 5.0)) * (0.5 + 0.5 * smoothstep (2.0, ROAD_HALF + 2.0, ad));
  }
  // the vent's mouth: a disc of lava that breathes, its glow on the rim
  if (ad > 12.0) {
    vec2 p = vPos.xz + uCamCell, ctr; float ph;
    if (ventAt (ivec2 (floor (p / VENT_CELL)), ctr, ph)) {
      float d = length (p - ctr);
      float br = breath (uTime, ph) * (0.35 + 0.65 * uBreath) + 0.15;
      float mouth = 1.0 - smoothstep (0.7, 0.95, d);
      float halo = exp (-d / 1.6) * (1.0 - mouth);
      col += vec3 (1.0, 0.55, 0.18) * mouth * (0.6 + 1.6 * br) * flare + alb * vec3 (1.0, 0.4, 0.1) * halo * 6.0 * br;
    }
  }
  col = applyFar (col, dist);
  fragColor = vec4 (col, 0.0);
}
