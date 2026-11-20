// From the prototype's io-shaders.js (VS_PVOL), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec4 aV0;     // the vent (rel xyz), the plume's height (m)
in vec4 aV1;     // the ring's radius (m), the dust's tint, the history row, a seed
in vec4 aV2;     // the lava's glow at its foot, its flight time (s), -, -
uniform mat4 uViewProj;
out vec3 vWorld; flat out vec4 vA; flat out vec4 vB; flat out vec4 vC;
const int CI[36] = int[36] (0, 2, 1, 1, 2, 3,  4, 5, 6, 5, 7, 6,  0, 1, 4, 1, 5, 4,  2, 6, 3, 3, 6, 7,  0, 4, 2, 2, 4, 6,  1, 3, 5, 3, 7, 5);
void main() {
  int c = CI[gl_VertexID];
  vec3 u = vec3 (float (c & 1), float ((c >> 1) & 1), float ((c >> 2) & 1));
  float R = aV1.x * 1.04, H = aV0.w * 1.06;
  vWorld = aV0.xyz + vec3 ((u.x * 2.0 - 1.0) * R, u.y * (H + 8.0) - 8.0, (u.z * 2.0 - 1.0) * R);
  vA = aV0; vB = aV1; vC = aV2;
  gl_Position = project (uViewProj, vWorld);
}
