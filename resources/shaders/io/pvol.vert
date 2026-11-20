// From the prototype's io-shaders.js (VS_PVOL), its body as it is but for the hull it draws (below).
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec4 aV0;     // the vent (rel xyz), the plume's height (m)
in vec4 aV1;     // the ring's radius (m), the dust's tint, the history row, a seed
in vec4 aV2;     // the lava's glow at its foot, its flight time (s), -, -
uniform mat4 uViewProj;
out vec3 vWorld; flat out vec4 vA; flat out vec4 vB; flat out vec4 vC;
// THE HULL (IoGl.cpp, the prototype's own named lever, 20.11): the box round the plume tapered above a quarter of its
// height — 1.04 R to 0.25 H, then in to 0.554 R at its top — still holding the canopy's shell lifted by its grazing
// thickness (r <= R sqrt ((1.08 H - y) / H)), the column and the curtain (0.93 R, under 0.16 H), each by 0.1 R or more;
// convex, so a pixel is shaded once. Its pixels are the box's that can hold anything of the plume.
const int CI[60] = int[60] (0, 1, 3, 0, 3, 2, 8, 11, 9, 8, 10, 11, 0, 2, 6, 0, 6, 4, 1, 7, 3, 1, 5, 7, 0, 5, 1, 0, 4, 5, 2, 3, 7, 2, 7, 6, 4, 6, 10, 4, 10, 8, 5, 11, 7, 5, 9, 11, 4, 9, 5, 4, 8, 9, 6, 7, 11, 6, 11, 10);
void main() {
  int c = CI[gl_VertexID], lv = c / 4;
  float R = aV1.x * (lv < 2 ? 1.04 : 0.554), H = aV0.w;
  float y = lv == 0 ? -8.0 : (lv == 1 ? 0.25 * H : 1.06 * H);
  vWorld = aV0.xyz + vec3 (((c & 1) != 0 ? 1.0 : -1.0) * R, y, ((c & 2) != 0 ? 1.0 : -1.0) * R);
  vA = aV0; vB = aV1; vC = aV2;
  gl_Position = project (uViewProj, vWorld);
}
