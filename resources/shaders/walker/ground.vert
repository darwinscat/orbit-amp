#include "walker/common.glsl"

in vec2 aOff;
uniform mat4 uViewProj;
out vec3 vRel;
void main () {
  float h = groundAt0 (aOff).r;
  vRel = vec3 (aOff.x, h, aOff.y);
  gl_Position = uViewProj * vec4 (vRel, 1.0);
}
