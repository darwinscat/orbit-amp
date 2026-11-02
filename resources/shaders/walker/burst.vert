#include "walker/common.glsl"

uniform mat4 uView, uProj;
uniform vec4 uB0[24];               // x, y, z (xz absolute), t0
uniform vec4 uB1[24];               // kind, strength, hue, radius
uniform int uPer;                   // particles a burst
out float vA; out vec2 vQ; out vec3 vCol;
void main () {
  int id = gl_VertexID; int q = id / 6; int ci = id - q * 6;
  int b = q / uPer; uint pi = uint (q - b * uPer);
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : -1.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : -1.0);
  vec4 B0 = uB0[b], B1 = uB1[b];
  float age = uTime - B0.w;
  bool spark = B1.x > 0.5;
  float life = spark ? 1.6 : 3.2;
  if (age < 0.0 || age > life || B1.y <= 0.0) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vQ = corner; vCol = vec3 (0.0); return; }
  uint seed = uint (b) * 131u + 7u;
  float r1 = hs (pi, seed), r2 = hs (pi, seed + 1u), r3 = hs (pi, seed + 2u), r4 = hs (pi, seed + 3u), r5 = hs (pi, seed + 4u);
  float an = r1 * 6.2831853;
  vec3 dir;
  vec3 origin = vec3 (B0.x - uCamXZ.x, B0.y, B0.z - uCamXZ.y);
  vec3 P;
  float size, bright;
  if (spark) {
    // From the rim, outward and up, a quick flight then a drift.
    float up = 0.35 + 0.9 * r2;
    dir = normalize (vec3 (cos (an), up, sin (an)));
    float v0 = (5.0 + 8.0 * r3) * (0.6 + 0.4 * B1.y);
    float flight = (1.0 - exp (-age / 0.28)) * 0.28 * v0;
    P = origin + vec3 (cos (an), 0.0, sin (an)) * B1.w * (0.85 + 0.15 * r4) + dir * flight + vec3 (0.0, 0.5 * age, 0.0);
    P.xz += vec2 (sin (age * 3.0 + r5 * 20.0), cos (age * 2.6 + r4 * 17.0)) * 0.12 * age;
    size = (0.10 + 0.12 * r4) * (1.0 + 0.3 * age);
    bright = 2.2 * B1.y * (1.6 - age / life) * (1.0 - smoothstep (life * 0.6, life, age)) * smoothstep (0.0, 0.05, age);
  } else {
    // The puff: out of the moss, a hemisphere, slow, the spores climbing as they thin.
    float up = 0.3 + 0.7 * r2;
    dir = normalize (vec3 (cos (an) * (1.0 - up * 0.6), up, sin (an) * (1.0 - up * 0.6)));
    float st = sqrt (B1.y);
    float v0 = (2.5 + 4.0 * r3) * (0.6 + 0.5 * st);
    float flight = (1.0 - exp (-age / 0.55)) * 0.55 * v0;
    P = origin + vec3 (cos (an), 0.0, sin (an)) * B1.w * r4 * 0.8 + dir * flight + vec3 (0.0, 0.9 * age, 0.0);
    P.xz += vec2 (sin (age * 1.3 + r5 * 20.0), cos (age * 1.1 + r4 * 17.0)) * 0.25 * age;
    size = (0.05 + 0.11 * r4 * r4) * (0.8 + 0.4 * st) * (1.0 + 0.35 * age);
    bright = 0.7 * min (B1.y, 1.4) * smoothstep (0.0, 0.12, age) * (1.0 - smoothstep (life * 0.3, life, age));
  }
  vec4 vc = uView * vec4 (P, 1.0);
  if (vc.z > -0.4) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vQ = corner; vCol = vec3 (0.0); return; }
  vc.xy += corner * size;
  gl_Position = uProj * vc;
  vQ = corner;
  float dist = length (P - vec3 (0.0, uCamY, 0.0));
  vA = bright * (0.5 + 0.5 * r5) * exp (-dist / 90.0);
  vec3 c = palette (B1.z);
  vCol = spark ? mix (c, vec3 (1.0), 0.35) : mix (c, vec3 (0.85, 0.8, 1.0), 0.25 + 0.3 * r2);
}
