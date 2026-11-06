#include "walker/common.glsl"

in vec3 aPos; in vec3 aNrm; in vec2 aUv;
uniform mat4 uViewProj, uModel;
uniform mat3 uNormal;
out vec3 vN; out vec3 vRel; out vec2 vUv;
void main () {
  vec4 p = uModel * vec4 (aPos, 1.0);
  vRel = p.xyz; vN = uNormal * aNrm; vUv = aUv;
  gl_Position = uViewProj * p;
}
