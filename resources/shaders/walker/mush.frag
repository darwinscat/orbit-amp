#include "walker/common.glsl"
#include "walker/light.glsl"

in vec3 vN; in vec3 vRel; in vec2 vUv; in float vPart; in vec3 vGlowCol; in float vGlow; in float vSize;
out vec4 fragColor;
void main () {
  vec3 toP = vRel - vec3 (0.0, uCamY, 0.0); float dist = length (toP); vec3 V = toP / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;
  vec3 lm = lightAt (vRel.xz);
  vec3 col;
  if (vPart < 0.5) {
    float t = vUv.x;
    float fib = 0.88 + 0.12 * sin (vUv.y * 6.2831853 * 11.0 + t * 3.0);
    vec3 alb = vec3 (0.70, 0.64, 0.70) * fib;
    vec3 own = vGlowCol * vGlow * (0.06 + 0.9 * t * t * t) * (0.6 + 0.4 * max (n.y, 0.0) + 0.3);
    float fres = pow (1.0 - max (dot (n, -V), 0.0), 2.5);
    col = alb * (lm * 1.3 + own + ambient (n) + sunLight (n)) + vGlowCol * vGlow * fres * 0.15 * t;
  } else if (vPart < 1.5) {
    float u = vUv.x;
    vec2 sc = vec2 (u * 4.0, vUv.y * 22.0);
    vec2 cell = floor (sc);
    float sp = hcell (ivec2 (cell), 7u);
    vec2 ctr = cell + vec2 (0.3 + 0.4 * hcell (ivec2 (cell), 8u), 0.3 + 0.4 * hcell (ivec2 (cell), 9u));
    float d = length ((sc - ctr) * vec2 (1.0, 0.55));
    float spot = smoothstep (0.30, 0.16, d) * step (sp, 0.45) * smoothstep (0.95, 0.8, u);
    float vein = 0.92 + 0.08 * sin (vUv.y * 6.2831853 * 26.0);
    float thin = 1.0 - 0.62 * u;
    vec3 alb = vGlowCol * 0.22 + vec3 (0.04, 0.03, 0.05);
    float fres = pow (1.0 - max (dot (n, -V), 0.0), 3.0);
    vec3 emis = vGlowCol * vGlow * (thin * vein * 0.55 + spot * 0.5 + fres * 0.45);
    col = alb * (ambient (n) + sunLight (n) * 1.5 + lm * 0.4) + emis + spot * vec3 (0.12);
  } else {
    float ng = 24.0 + 20.0 * clamp (vSize / 8.0, 0.0, 1.0);
    float lam = abs (fract (vUv.y * ng) * 2.0 - 1.0);
    float gill = 0.55 + 0.45 * smoothstep (0.25, 0.75, lam);
    float rim = 0.7 + 0.3 * (1.0 - vUv.x);
    col = vGlowCol * vGlow * (1.1 + 0.9 * gill) * rim + ambient (n) * 0.2;
  }
  col = applyFog (col, dist, V, lm);
  fragColor = vec4 (finish (col, gl_FragCoord.xy), 1.0);
}
