// From the prototype's metal-shaders.js (VS_PROP), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec3 aPos; in vec3 aNrm; in float aMat;
in vec4 aI0;     // x, y, z (rel), yaw
in vec4 aI1;     // scale, phase, lean x, lean z (per metre up)
uniform mat4 uViewProj;
out vec3 vRel; out vec3 vN; out float vMat; out vec3 vLoc; out float vPh;
void main() {
  vec3 p = aPos * aI1.x, n = aNrm;
  float c = cos (aI0.w), s = sin (aI0.w);
  p.xz = vec2 (c * p.x - s * p.z, s * p.x + c * p.z);
  n.xz = vec2 (c * n.x - s * n.z, s * n.x + c * n.z);
  p.xz += aI1.zw * max (p.y, 0.0);
  vec3 wp = aI0.xyz + p;
  vRel = wp - uCamRel; vN = n; vMat = aMat; vLoc = aPos; vPh = aI1.y;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
