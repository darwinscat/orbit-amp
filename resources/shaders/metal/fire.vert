// From the prototype's metal-shaders.js (VS_FIRE), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec4 aE0;     // the source (rel xyz), its kind
in vec4 aE1;     // its way (xyz), its size
in vec4 aE2;     // its age (s since the burst), a seed, its strength, how long it lasts
uniform mat4 uViewProj; uniform vec3 uCamR, uCamU;
uniform float uThinF;            // the ladder's foot leaves half the particles out
out vec2 vUV; out vec4 vCol; out float vSoft;
vec3 flame (float u) { return mix (mix (vec3 (1.0, 0.95, 0.7), vec3 (1.0, 0.55, 0.12), smoothstep (0.0, 0.35, u)), vec3 (0.7, 0.12, 0.02), smoothstep (0.35, 0.9, u)); }
void main() {
  int pi = gl_VertexID / 6, ci = gl_VertexID - pi * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  vUV = corner; vSoft = 1.0;
  int kind = int (aE0.w + 0.5);
  uint sd = uint (aE2.y * 997.0) * 64u + uint (pi);
  float r1 = hs (sd, 1u), r2 = hs (sd, 2u), r3 = hs (sd, 3u), r4 = hs (sd, 4u), r5 = hs (sd, 5u);
  float age = aE2.x, str = aE2.z, size = 0.0;
  vec3 P = aE0.xyz, dir = aE1.xyz;
  vec4 col = vec4 (0.0);
  bool on = (pi & 1) == 0 || uThinF < 0.5;
  if (kind == 0) {
    if (pi < 10) {                          // the pilot: a small steady flame
      float life = 0.45 + 0.3 * r1, a = fract (uTime / life + r2);
      P += vec3 ((r3 - 0.5) * 0.35, a * 1.3 * (0.6 + 0.4 * r4), (r5 - 0.5) * 0.35);
      size = 0.32 * (1.0 - 0.5 * a);
      col = vec4 (flame (a * 0.8) * (1.0 - a) * 0.9, 0.0);
    } else {                                // the column: fed for 0.9 s after the hit, each puff rising 15-20 m
      float t = age - r1 * 1.0, life = 0.55 + 0.4 * r2;
      if (t > 0.0 && t < life) {
        float u = t / life, v0 = 21.0 + 7.0 * r3;
        float h = v0 * t - 7.0 * t * t;
        float spread = 0.4 + 2.4 * u;
        float wig = sin (uTime * 9.0 + r4 * 20.0 + h * 0.4);
        P += vec3 ((r4 - 0.5) * spread + 0.6 * wig * u, max (h, 0.0) + 0.6, (r5 - 0.5) * spread);
        size = (1.1 + 2.8 * u) * aE1.w;
        col = vec4 (flame (0.1 + 0.9 * u) * pow (1.0 - u, 1.5) * 0.75 * str * (0.7 + 0.6 * r5), 0.0);
      }
    }
  } else if (kind == 1 || kind == 4) {      // a jet of flame along dir: from the mouth, while it lasts (aE2.w s)
    float life = kind == 1 ? 0.85 : 0.4;
    float cyc = fract (uTime / life + r1), born = age - cyc * life;
    float dur = aE2.w;
    if (born > 0.0 && born < dur && (kind == 1 || pi < 18)) {
      float t = cyc * life, u = cyc;
      float sp = kind == 1 ? 52.0 + 14.0 * r2 : 9.0 + 4.0 * r2;
      vec3 side = normalize (cross (dir, vec3 (0.31, 0.95, 0.12)));
      vec3 up2 = cross (dir, side);
      float cone = kind == 1 ? 0.22 : 0.35;
      vec3 d = normalize (dir + (side * (r3 - 0.5) + up2 * (r4 - 0.5)) * cone * 2.0);
      P += d * sp * t * (1.0 - 0.35 * u) + vec3 (0.0, 3.0 * t * t, 0.0);
      size = (kind == 1 ? 1.2 + 9.0 * u : 0.3 + 1.1 * u) * aE1.w;
      col = vec4 (flame (0.1 + 0.9 * u) * pow (1.0 - u, 1.2) * (kind == 1 ? 1.0 : 0.9) * (0.7 + 0.6 * r5) * str * smoothstep (0.0, 0.08, born) * (1.0 - smoothstep (dur - 0.2, dur, born)), 0.0);
    }
    if (kind == 4 && pi >= 18 && pi < 30) {  // a hiccup's puff of smoke after it
      float t = age - 0.15 - r1 * 0.2;
      if (t > 0.0 && t < 2.5) {
        P += dir * (4.0 + 2.0 * r2) * (1.0 - exp (-t)) + vec3 ((r3 - 0.5) * 1.5, 0.8 * t, (r4 - 0.5) * 1.5);
        size = (0.6 + 1.0 * t) * aE1.w;
        float a = 0.35 * (1.0 - t / 2.5);
        col = vec4 (vec3 (0.25, 0.24, 0.24) * a, a);
      }
    }
  } else if (kind == 2) {                   // a pathetic smoke ring: a little torus of puffs drifting toward the van
    if (age < 3.5 && pi < 32) {
      float a = float (pi) / 32.0 * 6.2831853;
      vec3 d = normalize (dir);
      vec3 s1 = normalize (cross (d, vec3 (0.0, 1.0, 0.0) + vec3 (0.01))), s2 = cross (d, s1);
      float R = (0.25 + 0.75 * (1.0 - exp (-age / 0.9))) * aE1.w;
      P += d * (1.6 * (1.0 - exp (-age / 1.2)) + 0.25 * age) + (s1 * cos (a) + s2 * sin (a)) * R + vec3 (0.0, 0.15 * age, 0.0);
      size = (0.16 + 0.14 * age) * aE1.w;
      float al = 0.55 * smoothstep (0.0, 0.15, age) * (1.0 - smoothstep (1.8, 3.5, age));
      col = vec4 (vec3 (0.42, 0.40, 0.40) * al, al);
    }
  } else if (kind == 3) {                   // a lava fountain: bombs thrown in arcs, each with a trail of three
    int b = pi / 3, tr = pi - b * 3;
    uint bs = uint (aE2.y * 997.0) * 64u + uint (b);
    float q1 = hs (bs, 11u), q2 = hs (bs, 12u), q3 = hs (bs, 13u), q4 = hs (bs, 14u);
    float t = age - q1 * 2.2 - float (tr) * 0.05;
    vec3 v0 = vec3 ((q2 - 0.5) * 16.0, 16.0 + 14.0 * q3, (q4 - 0.5) * 16.0) * aE1.w;
    float tl = (v0.y + sqrt (v0.y * v0.y + 4.0 * 9.81 * 0.5)) / 9.81;
    if (t > 0.0 && t < tl) {
      P += v0 * t + vec3 (0.0, -4.9 * t * t, 0.0);
      size = (tr == 0 ? 0.55 : 0.4 - 0.1 * float (tr)) * aE1.w;
      col = vec4 (mix (vec3 (1.0, 0.85, 0.5), vec3 (1.0, 0.3, 0.05), float (tr) * 0.4 + t / tl * 0.4) * (tr == 0 ? 2.2 : 1.0) * str, 0.0);
    }
    if (pi >= 45) {                         // the spout itself, glowing up from the pool while it erupts
      float t2 = age; float u = fract (uTime * 1.7 + r1);
      if (t2 < 3.0) { P += vec3 ((r2 - 0.5) * 2.0, u * 7.0 * (1.0 - t2 / 3.0), (r3 - 0.5) * 2.0); size = 2.2 * aE1.w; col = vec4 (flame (u) * (1.0 - u) * 1.6, 0.0); }
    }
  } else if (kind == 5) {                   // a ball lightning: a core, a halo, flickering arcs round it
    if (pi < 3) {
      float fl = 0.8 + 0.2 * sin (uTime * 37.0 + r1 * 9.0) * sin (uTime * 23.0);
      size = (pi == 0 ? 0.9 : pi == 1 ? 2.6 : 6.0) * aE1.w;
      col = vec4 (vec3 (0.65, 0.75, 1.0) * (pi == 0 ? 3.0 : pi == 1 ? 0.9 : 0.25) * fl * str, 0.0);
    } else if (pi < 15) {
      float a = uTime * (3.0 + 4.0 * r1) + r2 * 6.28;
      P += vec3 (cos (a), sin (a * 1.3) * 0.8, sin (a)) * (1.0 + 0.6 * r3) * aE1.w;
      size = 0.25 * aE1.w;
      col = vec4 (vec3 (0.8, 0.85, 1.0) * 1.5 * step (0.4, fract (uTime * 5.0 + r4)) * str, 0.0);
    }
  } else if (kind == 6) {                   // a strike's sparks: thrown off the steel, falling
    float t = age - r1 * 0.08;
    if (t > 0.0 && t < 1.1) {
      vec3 v0 = vec3 ((r2 - 0.5) * 22.0, 4.0 + 12.0 * r3, (r4 - 0.5) * 22.0);
      P += v0 * t + vec3 (0.0, -9.0 * t * t, 0.0);
      size = (0.18 + 0.12 * r5) * aE1.w;
      col = vec4 (mix (vec3 (0.9, 0.95, 1.0), vec3 (1.0, 0.6, 0.2), t) * 2.5 * (1.0 - t / 1.1) * str, 0.0);
    }
  }
  else if (kind == 7) {                   // embers drifting off burning letters: along the text (aE1 its half width), rising
    float life = 2.0 + 1.4 * r1, a = fract (uTime / life + r2);
    P += aE1.xyz * (r3 * 2.0 - 1.0) + vec3 (0.0, aE2.w * (r4 * 1.6 - 0.8) + a * 7.0, 0.0)
       + vec3 (sin (uTime * 1.3 + r5 * 20.0), 0.0, cos (uTime * 1.1 + r1 * 20.0)) * a * 1.6;
    size = 0.14 + 0.1 * r5;
    col = vec4 (mix (vec3 (1.0, 0.8, 0.4), vec3 (1.0, 0.3, 0.05), a) * 2.0 * (1.0 - a) * smoothstep (0.0, 0.08, a) * str, 0.0);
  }
  if (size <= 0.0 || ! on) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
  vec3 wp = P + uCamR * ((corner.x - 0.5) * 2.0 * size) + uCamU * ((corner.y - 0.5) * 2.0 * size);
  vCol = col;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
