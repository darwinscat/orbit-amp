#include "walker/common.glsl"

in vec4 aI0; in vec4 aI1;
uniform float uLmSize;
out vec2 vQ; out vec3 vCol;
void main () {
  int ci = gl_VertexID;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : -1.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : -1.0);
  float size = aI0.z;
  float R = size * 1.5 + 1.0;
  vec2 c = (aI0.xy - uCamXZ) / uLmSize * 2.0;
  gl_Position = vec4 (c + corner * R / uLmSize * 2.0, 0.0, 1.0);
  vQ = corner;
  vCol = palette (aI1.x) * glowOf (aI1.z, aI1.y, length (aI0.xy - uCamXZ)) * (0.8 + 0.3 * clamp (size / 6.0, 0.0, 1.0));
}
