// From the prototype's metal-shaders.js (FS_PROP), its body as it is.
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform float uStageK;           // the stage's lights: their pulse with the playing
uniform float uHumK;             // the cabinets' pilot lamps' hum
uniform vec4 uFlood;             // a floodlight at the backline's foot (rel), its reach
vec3 flood (vec3 p, vec3 n) {
  vec3 L = uFlood.xyz - p; float d2 = dot (L, L);
  return vec3 (1.0, 0.62, 0.32) * 1.6 * max (dot (n, L * inversesqrt (d2 + 1e-3)), 0.0) / (1.0 + d2 / (uFlood.w * uFlood.w));
}
in vec3 vRel; in vec3 vN; in float vMat; in vec3 vLoc; in float vPh;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;
  vec3 wp = vRel + uCamRel;
  int m = int (vMat + 0.5);
  vec3 col;
  float glow = hitGlow ();
  if (m == 0) col = steel (n, V, wp, 1.0);
  else if (m == 1) col = steel (n, V, wp, 0.55) + lightMatte (vec3 (0.10), n, wp);
  else if (m == 2) col = lightMatte (vec3 (0.035), n, wp) + vec3 (0.035) * flood (wp, n) + envAt (reflect (V, n)) * 0.05;   // tolex
  else if (m == 3) {                                                                                         // grille cloth
    vec2 g = vLoc.xy * 22.0;
    float weave = 0.5 + 0.5 * sin (g.x * 3.14159) * sin (g.y * 3.14159);
    vec3 alb = mix (vec3 (0.05, 0.05, 0.055), vec3 (0.12, 0.12, 0.13), weave);
    col = lightMatte (alb, n, wp) + alb * flood (wp, n) + vec3 (0.30, 0.05, 0.02) * uHumK * (0.75 + 0.25 * sin (uTime * 2.0 + vPh * 20.0)) * (1.0 + 2.5 * glow) * (0.6 + 0.4 * weave);
  }
  else if (m == 4) col = vec3 (1.0, 0.18, 0.06) * (2.0 + 0.5 * sin (uTime * 3.0 + vPh * 30.0)) * uHumK * (1.0 + 2.0 * glow);   // a pilot lamp
  else if (m == 5) col = mix (vec3 (1.0, 0.24, 0.03), vec3 (1.0, 0.55, 0.15), 0.5 + 0.5 * sin (uTime * 1.3 + vPh * 6.0)) * (1.15 + 0.5 * uStorm + 2.0 * glow);   // lava
  else if (m == 6) {                                                                                         // basalt, its fissures glowing
    vec3 q = wp;
    float wq = sin (q.x * 0.07 + q.z * 0.05) * 3.0 + sin (q.y * 0.09 - q.z * 0.06) * 2.0;
    float f = abs (sin (q.x * 0.31 + wq + 2.0 * sin (q.y * 0.21 + q.z * 0.08)) * sin (q.z * 0.27 - wq + 2.0 * sin (q.y * 0.17 - q.x * 0.07)) * sin (q.y * 0.23 + wq * 0.5));
    float aw = max (fwidth (f), 1e-4);
    float vein = (1.0 - smoothstep (0.0, 0.02 + aw, f)) * min (1.0, 0.02 / aw) * smoothstep (0.2, 0.6, fract (sin (dot (floor (q.xz / 9.0), vec2 (12.9898, 78.233))) * 43758.5453) + 0.3);
    col = lightMatte (vec3 (0.15, 0.15, 0.17), n, wp) + vec3 (0.15) * vec3 (1.0, 0.36, 0.08) * 0.25 * max (-n.y + 0.4, 0.0)
        + vec3 (1.0, 0.32, 0.05) * vein * 0.6 * (1.0 + uStorm + 2.0 * glow);
  }
  else if (m == 7) col = lightMatte (vec3 (0.02), n, wp) + envAt (reflect (V, n)) * 0.03;                      // matte black
  else if (m == 14) {                                                                                        // the stage's LED wall
    vec2 q = vLoc.xy / vec2 (26.0, 17.0) - vec2 (0.0, 1.18);
    float r = length (q * vec2 (1.0, 1.6)), a = atan (q.y, q.x);
    float burst = 0.5 + 0.5 * sin (a * 9.0 + uTime * 0.7) * sin (r * 9.0 - uTime * 4.0);
    vec3 c2 = mix (vec3 (1.0, 0.25, 0.05), vec3 (1.0, 0.75, 0.3), burst) * smoothstep (1.2, 0.0, r);
    float px = step (0.18, fract (vLoc.x * 1.5)) * step (0.18, fract (vLoc.y * 1.5));
    col = c2 * (0.35 + 1.6 * uStageK) * (0.6 + 0.4 * px);
  }
  else if (m == 8) {                                                                                         // the stage's lights
    vec3 hue = 0.55 + 0.45 * cos (6.2831853 * (vec3 (0.0, 0.33, 0.67) + floor (vLoc.x / 6.0) * 0.17 + uTime * 0.05));
    col = hue * (0.6 + 2.6 * uStageK);
  }
  else if (m == 9) col = lightMatte (vec3 (0.02), n, wp) + envAt (reflect (V, n)) * (0.12 + 0.6 * pow (1.0 - max (-dot (V, n), 0.0), 4.0));   // gloss black
  else if (m == 10) col = envAt (reflect (V, n)) * 0.22 + vec3 (0.01);                                         // tinted glass
  else if (m == 11) col = lightMatte (vec3 (0.75, 0.70, 0.58), n, wp) + vec3 (0.04);                           // a label
  else if (m == 12) col = lightMatte (vec3 (0.50, 0.04, 0.03), n, wp) + envAt (reflect (V, n)) * 0.25;         // red paint
  else col = lightMatteK (vec3 (0.12, 0.12, 0.13), n, wp, 0.06) + vec3 (0.012, 0.005, 0.002);               // the tunnel's walls: dark
  col = applyFar (col, dist);
  fragColor = vec4 (col, 0.0);
}
