#include "plains/height.glsl"
#include "plains/grad.glsl"
#include "plains/light.glsl"

uniform sampler2D uDetail;
uniform vec3 uCamRel;
uniform vec2 uDetOff0, uDetOff1;
in vec3 vPos; in vec4 vA, vB, vC;
out vec4 fragColor;
void main() {
  vec3 rel = vPos - uCamRel; float dist = length (rel); vec3 V = rel / dist;
  vec2 g = terrainGrad (vPos.xz, vA, vB, vC);
  vec3 n = normalize (vec3 (-g.x, 1.0, -g.y));
  vec3 d1 = texture (uDetail, uDetOff0 + vPos.xz / 1.75).rgb * 2.0 - 1.0;
  vec3 d2 = texture (uDetail, uDetOff1 + vPos.xz / 0.35).rgb * 2.0 - 1.0;
  float df1 = exp (-dist / 140.0), df2 = exp (-dist / 30.0);
  n = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.55 * df1 + vec3 (d2.x, 0.0, d2.y) * 0.25 * df2);
  float slope = 1.0 - n.y;
  vec4 pat = vB;
  vec3 dust = mix (vec3 (0.72, 0.45, 0.27), vec3 (0.80, 0.55, 0.34), pat.a);
  vec3 rock = vec3 (0.38, 0.22, 0.14);
  vec3 alb = mix (dust, rock, smoothstep (0.12, 0.45, slope));
  alb *= 1.0 + 0.16 * d1.z * df1 + 0.10 * d2.z * df2;
  alb *= 0.9 + 0.2 * pat.g;
  vec3 col = shade (alb, n);
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
