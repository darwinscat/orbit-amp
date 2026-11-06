#include "walker/common.glsl"
#include "walker/light.glsl"

in vec3 vN; in vec3 vRel; in vec2 vUv;
uniform float uRing;                // where along the piece an orange ring sits (uv.x), <0 for none
out vec4 fragColor;
void main () {
  vec3 toP = vRel - vec3 (0.0, uCamY, 0.0); float dist = length (toP); vec3 V = toP / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;
  vec3 lm = lightAt (vRel.xz);
  float brush = 0.88 + 0.12 * sin (vUv.y * 6.2831853 * 48.0 + vUv.x * 9.0);
  float panel = 0.85 + 0.15 * step (0.5, fract (vUv.x * 3.0 + 0.25));     // plates along the piece
  vec3 alb = vec3 (0.30, 0.30, 0.34) * brush * panel;
  float fres = pow (1.0 - max (dot (n, -V), 0.0), 3.0);
  // Lit from below by the pools (the caps' light climbs the shins), from the sky above, the sun's red at a grazing
  // angle; a rim that takes the pools' colour.
  vec3 col = alb * (ambient (n) * 3.0 + sunLight (n) * 1.5 + lm * (2.2 + 2.0 * max (-n.y, 0.0))) + (lm * 0.8 + ambient (n) * 1.2) * fres;
  float ring = (uRing >= 0.0) ? smoothstep (0.035, 0.02, abs (vUv.x - uRing)) : 0.0;
  col = mix (col, vec3 (1.0, 0.45, 0.08) * (0.35 + 0.3 * max (n.y, 0.0)), ring);
  col = applyFog (col, dist, V, lm);
  fragColor = vec4 (finish (col, gl_FragCoord.xy), 1.0);
}
