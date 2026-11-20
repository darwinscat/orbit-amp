// From the prototype's io-shaders.js (FS_STRUCT), its body as it is.
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 vPos; in vec3 vN; in float vMat; in float vFlag; in float vSeed; in vec3 vLoc;
out vec4 fragColor;
void main() {
  vec3 n = normalize (vN);
  vec3 V = normalize (vPos - uCamRel);
  if (dot (n, V) > 0.0) n = -n;
  int m = int (vMat + 0.5);
  if (m == 5) {                                     // the beacon: a slow red blink (an RTG outlives its mission)
    float on = step (0.86, fract (uTime / 2.4 + vSeed * 7.0));
    fragColor = vec4 (vec3 (1.0, 0.12, 0.05) * (0.08 + 3.0 * on), 1.0);
    return;
  }
  if (m == 6) { fragColor = vec4 (vec3 (1.0, 0.86, 0.62) * 3.2, 1.0); return; }      // a work light, steady
  vec3 alb = m == 0 ? vec3 (0.50, 0.50, 0.48) : m == 1 ? vec3 (0.72, 0.71, 0.66) : m == 2 ? vec3 (0.70, 0.52, 0.20) : m == 3 ? vec3 (0.07, 0.07, 0.075) : m == 7 ? vec3 (0.82, 0.76, 0.58) : vec3 (0.74, 0.73, 0.68);
  float spec = m == 0 ? 0.35 : m == 2 ? 0.9 : m == 3 ? 0.25 : m == 7 ? 0.0 : 0.08;
  float pw = m == 2 ? 18.0 : m == 3 ? 40.0 : 24.0;
  // years of fallout: a sulphur crust on what faces up, the rest dulled and yellowed
  float crust = smoothstep (0.25, 0.85, n.y) * (0.55 + 0.4 * fract (sin (dot (floor (vLoc * 1.7), vec3 (12.9898, 78.233, 37.719)) + vSeed * 91.0) * 43758.5453));
  alb = mix (alb * vec3 (0.95, 0.90, 0.78), vec3 (0.80, 0.70, 0.34), crust * 0.8);
  spec *= 1.0 - crust;
  float sh = sunShadow (vPos, n);
  vec3 col = lightAll (alb, n, vPos, sh);
  vec3 Hh = normalize (uSun - V);
  col += uSunCol * uSunK * spec * pow (max (dot (n, Hh), 0.0), pw) * sh * (m == 2 ? vec3 (1.0, 0.75, 0.35) : vec3 (1.0));
  fragColor = vec4 (col, 1.0);
}
