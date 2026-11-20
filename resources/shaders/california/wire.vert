#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec2 aTS;                   // t along the span 0..1, the ribbon's side 0/1
in vec3 aA; in vec3 aB; in float aSag;
uniform mat4 uViewProj; uniform float uPxAngle;
out vec3 vRel; out float vSide; out float vCov;
void main() {
  float t = aTS.x, t2 = t + 0.02;
  vec3 p = mix (aA, aB, t); p.y -= aSag * 4.0 * t * (1.0 - t);
  vec3 q = mix (aA, aB, t2); q.y -= aSag * 4.0 * t2 * (1.0 - t2);
  vec3 rel = p - uCamRel; float dist = length (rel);
  vec3 along = normalize (q - p);
  vec3 perp = normalize (cross (along, rel / dist));
  float w = 0.016, ww = max (w, 1.3 * dist * uPxAngle);
  vCov = w / ww;
  vec3 wp = p + perp * (aTS.y - 0.5) * ww;
  vRel = wp - uCamRel; vSide = aTS.y;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
