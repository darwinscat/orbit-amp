#include "walker/common.glsl"

in vec4 aI0; in vec4 aI1;
uniform mat4 uView, uProj;
out vec2 vQ; out vec3 vCol; out float vDist;
void main () {
  int ci = gl_VertexID;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : -1.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : -1.0);
  float size = aI0.z;
  vec2 foot = aI0.xy - uCamXZ;
  float glow = glowOf (aI1.z, aI1.y, length (foot));
  float gh = groundAt0 (foot).r;
  vec3 c = vec3 (foot.x, gh + size * 0.72, foot.y);
  float R = size * (0.55 + 0.30 * uBreath + 0.25 * uStorm) + 0.25;
  vec4 vc = uView * vec4 (c, 1.0);
  vc.z += min (R * 0.7, -vc.z * 0.5);
  vc.xy += corner * R;
  gl_Position = uProj * vc;
  vQ = corner; vDist = length (c - vec3 (0.0, uCamY, 0.0));
  vCol = palette (aI1.x) * glow * 0.32;
}
