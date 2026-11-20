// From the prototype's io-shaders.js (FS_PVOL), its body as it is.
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

uniform sampler2D uNoise, uHist;
uniform mat4 uViewProj;
in vec3 vWorld; flat in vec4 vA; flat in vec4 vB; flat in vec4 vC;
out vec4 fragColor;
const float ZN = 0.5, ZF = 20000.0;
float histAt (float launch, float row) { int sl = int (floor (launch / 0.25)) & 255; return texelFetch (uHist, ivec2 (sl, int (row)), 0).r; }
// the azimuth round the axis (a cheap atan: the filaments need no more)
float azOf (vec2 q) {
  float ax = abs (q.x), az0 = abs (q.y), mn = min (ax, az0) / max (max (ax, az0), 1e-3);
  float at = mn * (0.7854 + 0.273 * (1.0 - mn));
  at = az0 > ax ? 1.5708 - at : at; at = q.x < 0.0 ? 3.1416 - at : at; return q.y < 0.0 ? -at : at;
}
// the crossings (perf pass 20.11: in five fixed slots — the shell's near and far, the column, the curtain's near and far —
// sorted by a fixed network, not in dynamically indexed arrays): an empty slot (t 1e30, tau 0) sorts last and composites
// as nothing, so the answer is the arrays' own (but for two crossings at exactly the same t, which may swap: no pixel shows it)
void put (float t, float tau, vec3 L, inout float st, inout float su, inout vec3 sl) { if (tau > 1e-4) { st = t; su = tau; sl = L; } }
void ce (inout float ta, inout float ua, inout vec3 la, inout float tb, inout float ub, inout vec3 lb) {
  if (tb < ta) { float x = ta; ta = tb; tb = x; x = ua; ua = ub; ub = x; vec3 y = la; la = lb; lb = y; }
}
void comp (float u, vec3 l, inout vec3 Cc, inout float T) { float al = 1.0 - exp (-u); Cc += T * al * l; T *= 1.0 - al; }
void main() {
  float sT0 = 1e30, sT1 = 1e30, sT2 = 1e30, sT3 = 1e30, sT4 = 1e30, sU0 = 0.0, sU1 = 0.0, sU2 = 0.0, sU3 = 0.0, sU4 = 0.0;
  vec3 sL0 = vec3 (0.0), sL1 = vec3 (0.0), sL2 = vec3 (0.0), sL3 = vec3 (0.0), sL4 = vec3 (0.0);
  vec3 ro = uCamRel, rd = normalize (vWorld - uCamRel);
  vec3 vent = vA.xyz; float H = vA.w, R = vB.x, row = vB.z, seed = vB.w, Tf = vC.y;
  float tScene = 1e9;          // the scene's depth tests the nearest crossing (gl_FragDepth); what lies under the ground is dropped
  vec3 o = ro - vent;
  float a = dot (rd.xz, rd.xz), b = dot (o.xz, rd.xz), c = dot (o.xz, o.xz);
  float fwd = max (dot (rd, uSun), 0.0);
  vec3 dust = mix (vec3 (0.98, 0.90, 0.76), vec3 (0.95, 0.95, 0.96), vB.y);
  vec3 sunS = uSunCol * uSunK * (0.80 + 0.6 * fwd + 1.6 * pow (fwd, 6.0)), jupS = uJupShine * 0.9;   // the Sun per crossing: Io's own shadow
  vec3 glowC = vec3 (0.42, 0.52, 1.0) * (0.06 + 0.42 * uDark);
  vec3 hotC = vec3 (1.0, 0.36, 0.08) * vC.x;
  float k = H / (R * R);
  // THE SHELL
  float A = k * a, B = 2.0 * k * b + rd.y, C = k * c + o.y - H;
  float disc = B * B - 4.0 * A * C;
  float th0 = 0.016 * H;
  if (A > 1e-12) {
    float t1 = 0.0, t2 = -1.0, w2 = 1.0;
    if (disc > 0.0) { float sd = sqrt (disc); t1 = (-B - sd) / (2.0 * A); t2 = (-B + sd) / (2.0 * A); w2 = 1.0; }
    else {                                          // a miss: near the limb it still grazes the shell's thickness
      t1 = -B / (2.0 * A); float Fm = -disc / (4.0 * A);
      w2 = 2.0 * exp (-sq (Fm / (th0 * 1.4)));
      t2 = -1.0;
      if (w2 < 0.01) t1 = -1.0;
    }
    for (int h = 0; h < 2; ++h) {
      float t = h == 0 ? t1 : t2;
      if (t <= 0.0 || t >= tScene) continue;
      vec3 q = o + rd * t;
      if (q.y < 0.0) continue;
      float rn = min (length (q.xz) / R, 1.0), yn = q.y / H;
      vec3 nn = normalize (vec3 (2.0 * k * q.x, 1.0, 2.0 * k * q.z));
      float cs = disc > 0.0 ? abs (dot (rd, nn)) : 0.0;
      float th = th0 * (0.55 + 0.9 * rn);
      float path = th / max (cs, 0.05) * (disc > 0.0 ? 1.0 : w2);
      float az = azOf (q.xz);
      vec4 nz = textureLod (uNoise, vec2 (az * 1.9099 + seed, rn * 0.10 - uTime * 0.0008), 0.0);
      float fil = mix (1.0, 0.25 + 1.7 * nz.b * nz.b, smoothstep (0.10, 0.45, rn));      // the filaments, gone over the round crown
      float dens = fil * (0.65 + 0.35 * nz.g) * (1.0 - smoothstep (0.86, 1.0, rn));
      dens *= histAt (uTime - Tf * (0.5 * (1.0 - sqrt (max (1.0 - yn, 0.0))) + 0.5 * rn), row);
      float side = 0.65 + 0.35 * max (dot (nn, uSun), 0.0);
      vec3 Lq = dust * (sunS * ioTerm (vent + q) + jupS) * side + glowC * (0.7 + 0.6 * fil) + hotC * exp (-q.y / 24.0);
      if (h == 0) put (t, dens * path / H, Lq, sT0, sU0, sL0); else put (t, dens * path / H, Lq, sT1, sU1, sL1);
    }
  }
  // THE COLUMN: its Gaussian's line integral at the ray's nearest pass to the axis (narrow and dense low, wider and
  // fainter up, gone into the canopy)
  if (a > 1e-6) {
    float tc = -b / a, d2 = max (c - b * b / a, 0.0), yc = o.y + rd.y * tc;
    float w0 = 0.020 * H, wc = w0 + 0.05 * max (yc, 0.0);
    if (tc > 0.0 && tc < tScene && yc > -2.0 && yc < 0.95 * H && d2 < 16.0 * wc * wc) {
      float act = histAt (uTime - Tf * 0.5 * (1.0 - sqrt (max (1.0 - yc / H, 0.0))), row);
      float tau = 1.1 * 1.7725 * w0 * w0 / wc / max (sqrt (a), 0.2) * exp (-d2 / (wc * wc)) * (1.0 - smoothstep (0.35 * H, 0.9 * H, yc)) * act / (0.05 * H);
      put (tc, tau, dust * (sunS * ioTerm (vent + vec3 (0.0, yc, 0.0)) + jupS) * 0.85 + glowC * 1.4 + hotC * 1.6 * exp (-max (yc, 0.0) / 30.0), sT2, sU2, sL2);
    }
  }
  // THE CURTAIN: the fallout raining onto the ring, a low sheet at 0.93 R
  {
    float Rc = 0.93 * R, hc = H * (1.0 - 0.93 * 0.93) * 1.15, tcu = 0.035 * R;
    float d2 = b * b - a * (c - Rc * Rc);
    if (d2 > 0.0 && a > 1e-8) {
      float sd = sqrt (d2);
      for (int h = 0; h < 2; ++h) {
        float t = (-b + (h == 0 ? -sd : sd)) / a;
        if (t <= 0.0 || t >= tScene) continue;
        vec3 q = o + rd * t;
        if (q.y < 0.0 || q.y > hc) continue;
        vec3 nr = vec3 (q.x / Rc, 0.0, q.z / Rc);
        float cs = abs (dot (rd, nr));
        float az = azOf (q.xz);
        float fil = 0.35 + 0.65 * (0.5 + 0.5 * sin (az * 23.0 + seed * 40.0)) * (0.5 + 0.5 * sin (az * 7.0 + seed * 13.0)) * 2.0;
        float dens = fil * (1.0 - q.y / hc) * histAt (uTime - Tf * 0.95, row);
        vec3 Lc = dust * (sunS * ioTerm (vent + q) + jupS) * (0.65 + 0.35 * max (dot (nr, uSun), 0.0)) + glowC * 0.8;
        if (h == 0) put (t, 0.9 * dens * tcu / max (cs, 0.08) / R, Lc, sT3, sU3, sL3); else put (t, 0.9 * dens * tcu / max (cs, 0.08) / R, Lc, sT4, sU4, sL4);
      }
    }
  }
  if (sU0 + sU1 + sU2 + sU3 + sU4 <= 0.0) discard;
  // front to back (a sorting network on the five)
  ce (sT0, sU0, sL0, sT1, sU1, sL1);
  ce (sT3, sU3, sL3, sT4, sU4, sL4);
  ce (sT2, sU2, sL2, sT4, sU4, sL4);
  ce (sT2, sU2, sL2, sT3, sU3, sL3);
  ce (sT1, sU1, sL1, sT4, sU4, sL4);
  ce (sT0, sU0, sL0, sT3, sU3, sL3);
  ce (sT0, sU0, sL0, sT2, sU2, sL2);
  ce (sT1, sU1, sL1, sT3, sU3, sL3);
  ce (sT1, sU1, sL1, sT2, sU2, sL2);
  vec3 Cc = vec3 (0.0); float T = 1.0;
  comp (sU0, sL0, Cc, T);
  comp (sU1, sL1, Cc, T);
  comp (sU2, sL2, Cc, T);
  comp (sU3, sL3, Cc, T);
  comp (sU4, sL4, Cc, T);
  fragColor = vec4 (Cc, 1.0 - T);
}
