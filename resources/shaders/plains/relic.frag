#include "plains/light.glsl"

in vec3 vN; in vec3 vRel; in vec3 vCol; in float vDust;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 alb = mix (vCol, vec3 (0.76, 0.50, 0.30), vDust * smoothstep (0.2, 0.9, n.y));   // the dust settled on what faces up
  vec3 col = shade (alb, n) + alb * mix (uHorizon, uZenith, 0.5) * 0.25;
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
