// From the prototype's metal-shaders.js (VS_LETTERS), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec4 aL0;     // the letters' centre (rel), the yaw (its front, +z)
in vec4 aL1;     // half width, half height (m), a seed, the flames' room above the letters (in half heights)
in vec4 aL2;     // its rect in the atlas (u0, v0 top, u1, v1 bottom)
in vec4 aL3;     // its own colours' rect, if it has them (u0 < 0: none; it burns white-hot)
uniform mat4 uViewProj;
out vec2 vQ; out vec4 vRect; out float vSeed; out vec3 vRel; out vec4 vColRect; out float vHH;
void main() {
  int k = gl_VertexID;
  vec2 c = vec2 ((k == 1 || k == 2 || k == 4) ? 1.0 : -1.0, (k == 2 || k == 4 || k == 5) ? 1.0 : -1.0);
  vec2 q = vec2 (c.x * 1.06, c.y < 0.0 ? -1.12 : 1.0 + aL1.w);
  vec3 lp = vec3 (q.x * aL1.x, q.y * aL1.y, 0.0);
  float cy = cos (aL0.w), sy = sin (aL0.w);
  vec3 wp = aL0.xyz + vec3 (cy * lp.x, lp.y, -sy * lp.x);
  vQ = q; vRect = aL2; vSeed = aL1.z; vRel = wp - uCamRel; vColRect = aL3; vHH = aL1.y;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
