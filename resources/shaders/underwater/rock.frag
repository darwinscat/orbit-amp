#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

in vec3 vN; in vec3 vRel; in vec3 vPos; in float vTint; in float vLocalY;
uniform float uTall;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 alb = mix (vec3 (0.14, 0.12, 0.19), vec3 (0.24, 0.20, 0.26), vTint);
  vec3 col = shadeGround (alb, n, vPos, vRel, dist) + alb * uFogLevel * 0.35;
  // The crust: teal on the upward faces; on the spires, rings of a slow bioluminescent pulse.
  col += vec3 (0.04, 0.20, 0.17) * smoothstep (0.2, 0.9, n.y) * 0.5;
  if (uTall > 0.5) {
    float ring = pow (0.5 + 0.5 * sin (vLocalY * 9.0 - uTime * 1.3 + vTint * 20.0), 12.0);
    col += mix (vec3 (0.2, 1.0, 0.8), vec3 (1.0, 0.5, 0.9), vTint) * ring * 0.45 * smoothstep (-0.3, 0.3, vLocalY) * exp (-dist / 50.0);
  }
  col = applyFog (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
