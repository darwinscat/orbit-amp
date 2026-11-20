// From the prototype's io-shaders.js (FS_ROCK), its body as it is.
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 vPos; in vec3 vN; in float vTint;
out vec4 fragColor;
void main() {
  vec3 n = normalize (vN);
  vec3 V = normalize (vPos - uCamRel);
  if (dot (n, V) > 0.0) n = -n;
  // dark silicate under a crust of sulphur on the faces turned up; black glass on a fresh flow
  float t = fract (vTint);
  vec3 alb = vTint >= 2.0 ? vec3 (0.05) : mix (vec3 (0.17, 0.16, 0.13), vec3 (0.30, 0.26, 0.19), t);
  alb = mix (alb, vec3 (0.70, 0.62, 0.36), smoothstep (0.55, 0.95, n.y) * 0.35 * step (vTint, 1.5));
  float sh = sunShadow (vPos, n);
  fragColor = vec4 (lightAll (alb, n, vPos, sh), 1.0);
}
