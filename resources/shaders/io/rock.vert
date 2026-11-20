// From the prototype's io-shaders.js (VS_ROCK), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 aPos; in vec3 aNrm;
in vec4 aR0;     // x, y, z (rel), yaw
in vec4 aR1;     // the scale along x, y, z; its tint (2 and more: black glass, on a fresh flow)
uniform mat4 uViewProj;
uniform float uShadowPass;
out vec3 vPos; out vec3 vN; out float vTint;
void main() {
  float c = cos (aR0.w), s = sin (aR0.w);
  vec3 p = aPos * aR1.xyz, n = aNrm / aR1.xyz;
  p.xz = vec2 (c * p.x - s * p.z, s * p.x + c * p.z); n.xz = vec2 (c * n.x - s * n.z, s * n.x + c * n.z);
  vPos = aR0.xyz + p; vN = n; vTint = aR1.w;
  gl_Position = uShadowPass > 0.5 ? uViewProj * vec4 (vPos, 1.0) : project (uViewProj, vPos);
}
