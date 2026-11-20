// From the prototype's io-shaders.js (FS_MESH), its body as it is.
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 vPos; in vec3 vN; in vec3 vCol; flat in int vKind;
uniform float uMeshSeed;
out vec4 fragColor;
void main() {
  vec3 n = normalize (vN);
  vec3 V = normalize (vPos - uCamRel);
  if (dot (n, V) > 0.0) n = -n;
  if (vKind == 3) {          // a lamp: its own colour; a red one is a beacon, blinking slowly
    bool beacon = vCol.r > 2.0 * vCol.g;
    float on = beacon ? 0.08 + 3.0 * step (0.86, fract (uTime / 2.4 + uMeshSeed)) : 2.6 * mix (1.0, 3.5, uDark);   // brighter in the dark: they read from afar
    fragColor = vec4 ((beacon ? vec3 (1.0, 0.12, 0.05) : max (vCol, vec3 (0.25))) * on, 1.0); return;
  }
  vec3 alb = mix (vCol, vec3 (0.80, 0.70, 0.34), smoothstep (0.3, 0.9, n.y) * 0.25);   // the models carry their own fallout
  float sh = sunShadow (vPos, n);
  vec3 col = lightAll (alb, n, vPos, sh);
  if (vKind == 1 || vKind == 2) col += uSunCol * uSunK * 0.4 * pow (max (dot (n, normalize (uSun - V)), 0.0), 30.0) * sh;
  fragColor = vec4 (col, 1.0);
}
