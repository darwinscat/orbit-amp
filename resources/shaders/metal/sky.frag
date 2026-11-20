// From the prototype's metal-shaders.js (FS_SKY), its body as it is but for the curtains' terms that vary only with the
// aurora's clock: worked out once a frame by MetalGl.cpp (uCurtA, uCurtB, curtain k's), not once a pixel; and in two passes
// (SKY_SMOOTH 1, then 0): the smooth parts, the galaxy's band and the aurora, at half the view's pixels each way, the rest at
// all of them, the smooth parts read scaled up.
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform sampler2D uNoise;
uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uPxAng, uAurT, uFlat;
uniform vec3 uBandH, uBandT, uBandN;
#include <skypass>
uniform sampler2D uSmooth; uniform vec4 uSmoothMap;      // the smooth pass's picture, and where the view's uv falls in it
uniform float uSmoothSq;    // 1 where that picture is 8 bits a channel (no half floats to draw into): sqrt (v / 4) in it,
                            // so what reaches 4 fits and what is dim keeps small steps
uniform vec4 uCurtA[4], uCurtB[4];   // per curtain: its sway, its fine waver's phase, its ripple, the ripples' phase; its breath
in vec2 vNdc;
out vec4 fragColor;
// the noise texture, periodic: x in turns of the azimuth times a whole number, so it wraps round the turn
float nz (float u, float v, int ch) { vec4 t = textureLod (uNoise, vec2 (u, v), 0.0); return ch == 0 ? t.r : ch == 1 ? t.g : ch == 2 ? t.b : t.a; }
vec3 curtain (float az, float el, float T, float azc, float azw, float elb, float elh, float wgt, int k) {
  float da = az - azc; da -= floor (da + 0.5);
  float win = 1.0 - smoothstep (azw * 0.4, azw, abs (da));
  if (win <= 0.0) return vec3 (0.0);
  float row = 0.13 + 0.29 * float (k);
  float u = az + 0.37 * float (k);
  // The arc stays put (the owner, 19.11: no steady drift): its edge only sways a little; the folds ripple back and forth
  // along it, the ripples' way turning slowly (both directions); the brightness breathes. The aurora's clock T runs
  // faster in the storm, so the ripples do.
  float sway = uCurtA[k].x;
  vec4 e = textureLod (uNoise, vec2 (u + sway, row), 0.0);                // one tap: the broad waver (R) and the fine (G)
  float lower = elb + 0.060 * (e.r - 0.5) + 0.022 * (e.g - 0.5) + 0.004 * sin (u * TAU * 23.0 - uCurtA[k].y);
  float y = (el - lower) / elh;
  float prof = smoothstep (-0.28, 0.08, y) * exp (-max (y, 0.0) * 0.9);
  if (prof <= 0.002) return vec3 (0.0);
  float rip = uCurtA[k].z + 0.005 * sin (u * TAU * 7.0 + uCurtA[k].w);
  float fold = smoothstep (0.22, 0.68, nz ((u + rip) * 2.0, y * 0.06 + row, 1));
  vec4 rr = textureLod (uNoise, vec2 ((u + rip * 0.6 + 0.002 * sin (T * 0.37 + u * 90.0)) * 11.0, (y * 0.4 - T * 0.02) / 16.0 + row), 0.0);   // the rays (B), finer (A)
  float ray = (0.35 + 0.65 * smoothstep (0.2, 0.8, rr.b)) * (0.7 + 0.3 * mix (rr.a, 0.5, uFlat));
  float surge = 1.0 + uStorm * (0.9 + 0.6 * sin (T * 1.7 + float (k) * 2.0 + u * 30.0));
  float I = win * prof * fold * ray * wgt * surge * uCurtB[k].x;
  vec3 c = mix (vec3 (0.12, 0.95, 0.40), vec3 (0.50, 0.18, 0.95), smoothstep (0.25, 1.7, y));
  c = mix (c, vec3 (0.9, 0.2, 0.75), uStorm * 0.35 * smoothstep (0.6, 1.8, y));
  c = mix (vec3 (0.95, 0.15, 0.50), c, smoothstep (-0.28, 0.10, y));
  return c * I;
}
// THE SMOOTH PARTS: the galaxy's band and the aurora, as the sky adds them (each already weighted by the elevation).
vec3 smoothSky (vec3 dir, float az, float el) {
  vec3 col = vec3 (0.0);
  // THE SMOOTH PASS (MetalGl.cpp): the galaxy's band and the aurora, at half the view's pixels each way, which the full
  // pass reads scaled up — added to what it adds them to there, so the sum is the same.
  // the galaxy's band
  float blat = asin (clamp (dot (dir, uBandN), -1.0, 1.0));
  float blon = atan (dot (dir, uBandT), dot (dir, uBandH)) / TAU;
  float core = exp (-sq (blat / 0.075)), halo = exp (-sq (blat / 0.20));
  if (halo > 0.01) {
    float cloud = nz (blon * 6.0, blat * 2.3 + 0.5, 0);
    float cloud2 = nz (blon * 11.0, blat * 3.7 + 0.25, 2);
    float lane = smoothstep (0.45, 0.7, nz (blon * 5.0 + 0.3, blat * 2.5 + 0.7, 1)) * exp (-sq ((blat + 0.015) / 0.045));
    float mw = (core * 0.9 + halo * 0.3) * (0.35 + 1.1 * cloud) * (1.0 - 0.8 * lane);
    col += mix (vec3 (0.56, 0.62, 0.84), vec3 (0.80, 0.74, 0.66), smoothstep (0.35, 0.75, cloud2)) * mw * 0.36;
  }
  col *= 0.35 + 0.65 * smoothstep (-0.01, 0.07, el);
  // the aurora
  float T = uAurT, wgt = 1.0 + 1.1 * uStorm;
  vec3 au = curtain (az, el, T, 0.02, 0.30, 0.080, 0.15, 1.40 * wgt, 0)
          + curtain (az, el, T, 0.27, 0.22, 0.060, 0.11, 1.00 * wgt, 1)
          + curtain (az, el, T, 0.52, 0.28, 0.075, 0.13, 1.20 * wgt, 2)
          + curtain (az, el, T, 0.78, 0.25, 0.050, 0.10, 0.90 * wgt, 3);
  col += au * smoothstep (-0.02, 0.02, el);
  return col;
}
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  float az = atan (dir.x, -dir.z) / TAU;
  float el = asin (clamp (dir.y, -1.0, 1.0));
  vec3 col = vec3 (0.0);
#if SKY_SMOOTH
  vec3 sm = smoothSky (dir, az, el);
  fragColor = vec4 (uSmoothSq > 0.5 ? 0.5 * sqrt (max (sm, vec3 (0.0))) : sm, 1.0);
#else
  // the far ranges: two layers of silhouettes round the horizon, black, a faint glow at their foot; under them nothing
  // of the sky shows
  for (int k = 0; k < 2; ++k) {
    float m = k == 0 ? 3.0 : 2.0, D = k == 0 ? 2600.0 : 5200.0;
    float hN = nz (az * m, 0.81 + 0.07 * float (k), 0) * 0.75 + nz (az * m * 4.0, 0.33 + 0.07 * float (k), 1) * 0.25;
    float H = k == 0 ? 20.0 + 260.0 * hN * hN : 60.0 + 620.0 * hN * hN;
    if (el < atan ((H - uCamRel.y) / D) && dir.y > -0.006) {
      vec3 ridge = mix (vec3 (0.010, 0.012, 0.018), vec3 (0.004, 0.005, 0.008), float (k));
      ridge += vec3 (1.0, 0.32, 0.06) * 0.05 * exp (-(el + 0.004) / 0.006) * (1.0 + uStorm) + uFlashCol * uFlash * 0.03;
      fragColor = vec4 (mix (ridge, uFar, smoothstep (0.003, -0.006, dir.y)), 1.0);
      return;
    }
  }
  // the galaxy's band: its halo thickens the stars (its light comes from the smooth pass)
  float blat = asin (clamp (dot (dir, uBandN), -1.0, 1.0));
  float halo = exp (-sq (blat / 0.20));
  // the stars: three hashed layers of cells in (azimuth, elevation), wrapping with the azimuth
  for (int l = 0; l < 3; ++l) {
    float N = (l == 0) ? 420.0 : (l == 1) ? 900.0 : 1700.0;
    float prob = (l == 0) ? 0.07 : (l == 1) ? 0.09 : 0.10;
    vec2 ae = vec2 (az + 0.5, el / TAU + 0.5) * N;
    vec2 cf = floor (ae); vec2 f = ae - cf;
    ivec2 ci = ivec2 (int (cf.x) % int (N), int (cf.y));
    uint ls = uint (l) * 8u;
    float cellPx = (TAU / N) / uPxAng;                         // a cell's size on the screen: where pixels are big
    float keep = smoothstep (2.5, 6.0, cellPx);                 // (the small window) the finer layers thin out
    if (hcell (ci, 3u + ls) < prob * (1.0 + 1.3 * halo) * keep) {
      vec2 sc = vec2 (hcell (ci, 4u + ls), hcell (ci, 5u + ls));
      float dpx = length (f - sc) * (TAU / N) / uPxAng;
      float mag = hcell (ci, 6u + ls);
      float br = (l == 0 ? 0.35 + 1.6 * mag * mag * mag : l == 1 ? 0.15 + 0.5 * mag * mag : 0.05 + 0.2 * mag * mag);
      float tw = 1.0 + 0.3 * smoothstep (0.6, 1.0, mag) * sin (uTime * (3.0 + 5.0 * mag) + mag * 60.0);
      float st = exp (-dpx * dpx * 0.75) * br * tw;
      if (l == 0) st += br * 0.06 * exp (-dpx * 0.35) * smoothstep (0.75, 1.0, mag);
      col += mix (vec3 (0.70, 0.82, 1.0), vec3 (1.0, 0.86, 0.68), hcell (ci, 7u + ls)) * st;
    }
  }
  col *= 0.35 + 0.65 * smoothstep (-0.01, 0.07, el);
  col += vec3 (0.05, 0.11, 0.07) * exp (-max (el, 0.0) / 0.018) * 0.5;
  vec3 sm = texture (uSmooth, (vNdc * 0.5 + 0.5) * uSmoothMap.xy + uSmoothMap.zw).rgb;
  col += uSmoothSq > 0.5 ? 4.0 * sm * sm : sm;              // the galaxy and the aurora
  // the lightning's flash in the air: a violet glow, more toward the horizon
  if (uFlash > 0.002) col += uFlashCol * uFlash * (0.10 + 0.20 * exp (-max (el, 0.0) / 0.15)) * (0.6 + 0.8 * nz (az * 3.0, el * 0.4 + 0.1, 0));
  col = mix (col, uFar, smoothstep (0.003, -0.006, dir.y));
  fragColor = vec4 (col, 1.0);
#endif
}
