#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"
#include "underwater/light.glsl"
#include "underwater/jelly.glsl"

in vec2 aUV;
out vec3 vN; out vec3 vRel; out vec3 vCol; out vec3 vGlow; out vec2 vUV; out float vPulse; out float vAlpha;
void main() {
  float c = contraction (aPhase);
  float u = aUV.x * 6.2831853, v = aUV.y;
  vec3 p = bell (u, v, c);
  float du = 0.02, dv = 0.01;
  float vn = max (v, 0.03);
  vec3 pu = bell (u + du, vn, c) - bell (u - du, vn, c);
  vec3 pv = bell (u, min (vn + dv, 1.0), c) - bell (u, vn - dv, c);
  vec3 n = normalize (cross (pv, pu));
  if (dot (n, p - vec3 (0.0, 0.15, 0.0)) < 0.0) n = -n;
  mat3 F = bellFrame (aAxis);
  vec3 wp = aPos + F * (p * aScale);
  vN = F * n; vRel = wp - uCamRel; vCol = aCol; vGlow = aGlow; vUV = aUV; vPulse = c; vAlpha = aAlpha;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
