#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

in vec3 vN; in vec3 vRel; in vec3 vPos; in float vT; in float vSeed; in vec3 vTipCol;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 alb = mix (vec3 (0.42, 0.22, 0.46), vec3 (0.70, 0.48, 0.62), vT);
  vec3 col = shadeGround (alb, n, vPos, vRel, dist) + alb * uFogLevel * 0.5;
  float rim = pow (max (1.0 - abs (dot (n, V)), 0.0), 2.0);
  col += vTipCol * rim * 0.25 + alb * rim * 0.4;
  float pulse = 0.6 + 0.4 * sin (uTime * 1.9 + vSeed * 40.0);
  float tip = smoothstep (0.78, 0.9, vT) * exp (-dist / 30.0) * (vSeed > 0.3 ? 1.0 : 0.15);
  col += vTipCol * tip * (0.9 + 0.8 * pulse) * (0.6 + 0.4 * rim) * hitK (dist);
  col = applyFog (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
