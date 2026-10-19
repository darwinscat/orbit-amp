#include "plains/light.glsl"

in vec3 vN; in vec3 vRel; in float vTint;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 alb = mix (vec3 (0.42, 0.27, 0.19), vec3 (0.58, 0.40, 0.28), vTint);
  vec3 n = normalize (vN);
  vec3 col = shade (alb, n) + alb * mix (uHorizon, uZenith, 0.5) * 0.25;   // a little sky fill: rocks are not holes
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
