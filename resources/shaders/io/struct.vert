// From the prototype's io-shaders.js (VS_STRUCT), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 aPos; in vec3 aNrm; in vec3 aAcross; in float aMat; in float aFlag;
in vec4 aI0;     // x, y, z (rel), yaw
in vec4 aI1;     // scale, lean x, lean z, seed
uniform mat4 uViewProj;
uniform float uPxAng, uShadowPass;
out vec3 vPos; out vec3 vN; out float vMat; out float vFlag; out float vSeed; out vec3 vLoc;
void main() {
  float c = cos (aI0.w), s = sin (aI0.w);
  vec3 p = aPos * aI1.x, n = aNrm;
  // widen the bars far off: never thinner than ~1.2 px
  vec3 wp0 = aI0.xyz + vec3 (c * p.x - s * p.z, p.y, s * p.x + c * p.z);
  float dist = length (wp0 - uCamRel);
  float grow = max (0.0, 1.2 * uPxAng * dist - 0.12) * step (0.5, length (aAcross)) * (1.0 - uShadowPass);
  p += aAcross * grow * 0.5;
  p.xz = vec2 (c * p.x - s * p.z, s * p.x + c * p.z);
  n.xz = vec2 (c * n.x - s * n.z, s * n.x + c * n.z);
  p.xz += aI1.yz * max (p.y, 0.0); p.y -= 0.5 * dot (aI1.yz, aI1.yz) * max (p.y, 0.0);
  vPos = aI0.xyz + p; vN = n; vMat = aMat; vFlag = aFlag; vSeed = aI1.w; vLoc = aPos;
  gl_Position = uShadowPass > 0.5 ? uViewProj * vec4 (vPos, 1.0) : project (uViewProj, vPos);
}
